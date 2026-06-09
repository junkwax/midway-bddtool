#include "undo_manager.h"
#include "Core/editor_project_storage.h"
#include "Core/project_snapshot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {
    extern Img *g_img;
    extern int  g_ni;
    extern Obj *g_obj;
    extern int  g_no;
    extern char g_name[64];
    extern int  g_have_bdb;
    extern char g_bdb_path[512];
    extern char g_bdd_path[512];
    extern char g_bdb_header[256];
    extern char (*g_bdb_modules)[256];
    extern int  g_bdb_num_modules;
    extern Uint32 (*g_pals)[256];
    extern int *g_pal_count;
    extern int  g_n_pals;
    extern char (*g_pal_name)[64];

    extern int  g_dirty;
    extern int  g_need_rebuild;
    extern int  g_hl_obj;
    extern int *g_sel_flags;
}

enum UndoKind {
    UNDO_KIND_EMPTY = 0,
    UNDO_KIND_SNAPSHOT,
    UNDO_KIND_OBJECT_POSITIONS,
    UNDO_KIND_OBJECT_RECORDS,
    UNDO_KIND_MODULE_LINES,
    UNDO_KIND_PALETTE_SLOT,
    UNDO_KIND_IMAGE_INDEX,
    UNDO_KIND_IMAGE_PIXELS
};

struct ObjectPositionDelta {
    int count;
    int *indices;
    int *before_depth;
    int *before_sy;
    int *after_depth;
    int *after_sy;
};

struct ObjectRecordDelta {
    int count;
    int *indices;
    Obj *before_objects;
    Obj *after_objects;
};

struct ModuleLineDelta {
    int count;
    int *indices;
    char (*before_lines)[256];
    char (*after_lines)[256];
};

struct PaletteSlotDelta {
    int pal_i;
    int before_count;
    int after_count;
    char before_name[64];
    char after_name[64];
    Uint32 before_colors[256];
    Uint32 after_colors[256];
    int before_rgb555_valid;
    int after_rgb555_valid;
    int before_rgb555_count;
    int after_rgb555_count;
    Uint16 before_rgb555[256];
    Uint16 after_rgb555[256];
};

struct ImageIndexDelta {
    int img_i;
    int before_idx;
    int after_idx;
};

struct ImagePixelDelta {
    int img_i;
    int image_idx;   /* stable BDD image id; survives slot reorder/delete */
    int width;
    int height;
    int count;
    int *offsets;
    Uint8 *before_pixels;
    Uint8 *after_pixels;
};

struct Undo {
    int kind;
    char label[48];
    ProjectSnapshot snapshot;
    ObjectPositionDelta object_pos;
    ObjectRecordDelta object_records;
    ModuleLineDelta module_lines;
    PaletteSlotDelta palette_slot;
    ImageIndexDelta image_index;
    ImagePixelDelta image_pixels;
};

#define UNDO_DEPTH 32
static Undo g_undo_ring[UNDO_DEPTH] = {};
static int  g_undo_base = 0;   /* index of oldest entry in ring */
static int  g_undo_n    = 0;   /* number of valid entries (0..UNDO_DEPTH) */
static Undo g_redo_state = {};
static int  g_redo_avail = 0;

static char g_action_label[48] = "";             /* consumed by next undo_save() */

struct Checkpoint { Undo state; char label[64]; bool used; };
static Checkpoint g_checkpoints[CHECKPOINT_N] = {};

static void undo_free_object_positions(Undo *u)
{
    if (!u) return;
    free(u->object_pos.indices);
    free(u->object_pos.before_depth);
    free(u->object_pos.before_sy);
    free(u->object_pos.after_depth);
    free(u->object_pos.after_sy);
    memset(&u->object_pos, 0, sizeof(u->object_pos));
}

static void undo_free_object_records(Undo *u)
{
    if (!u) return;
    free(u->object_records.indices);
    free(u->object_records.before_objects);
    free(u->object_records.after_objects);
    memset(&u->object_records, 0, sizeof(u->object_records));
}

static void undo_free_module_lines(Undo *u)
{
    if (!u) return;
    free(u->module_lines.indices);
    free(u->module_lines.before_lines);
    free(u->module_lines.after_lines);
    memset(&u->module_lines, 0, sizeof(u->module_lines));
}

static void undo_free_image_pixels(Undo *u)
{
    if (!u) return;
    free(u->image_pixels.offsets);
    free(u->image_pixels.before_pixels);
    free(u->image_pixels.after_pixels);
    memset(&u->image_pixels, 0, sizeof(u->image_pixels));
}

static void undo_release_storage(Undo *u)
{
    if (!u) return;
    project_snapshot_release(&u->snapshot);
    undo_free_object_positions(u);
    undo_free_object_records(u);
    undo_free_module_lines(u);
    undo_free_image_pixels(u);
    memset(u, 0, sizeof(*u));
}

static bool undo_capture_snapshot(Undo *u)
{
    if (!u) return false;
    undo_release_storage(u);
    if (!project_snapshot_capture_current(&u->snapshot, false))
        return false;
    u->kind = UNDO_KIND_SNAPSHOT;
    return true;
}

static bool undo_copy_object_positions(Undo *dst, const Undo *src)
{
    int count;
    size_t bytes;

    if (!dst || !src || src->kind != UNDO_KIND_OBJECT_POSITIONS)
        return false;

    undo_release_storage(dst);
    count = src->object_pos.count;
    if (count <= 0)
        return false;
    bytes = (size_t)count * sizeof(int);
    dst->object_pos.indices = (int *)malloc(bytes);
    dst->object_pos.before_depth = (int *)malloc(bytes);
    dst->object_pos.before_sy = (int *)malloc(bytes);
    dst->object_pos.after_depth = (int *)malloc(bytes);
    dst->object_pos.after_sy = (int *)malloc(bytes);
    if (!dst->object_pos.indices || !dst->object_pos.before_depth ||
        !dst->object_pos.before_sy || !dst->object_pos.after_depth ||
        !dst->object_pos.after_sy) {
        undo_release_storage(dst);
        return false;
    }
    dst->object_pos.count = count;
    memcpy(dst->object_pos.indices, src->object_pos.indices, bytes);
    memcpy(dst->object_pos.before_depth, src->object_pos.before_depth, bytes);
    memcpy(dst->object_pos.before_sy, src->object_pos.before_sy, bytes);
    memcpy(dst->object_pos.after_depth, src->object_pos.after_depth, bytes);
    memcpy(dst->object_pos.after_sy, src->object_pos.after_sy, bytes);
    dst->kind = UNDO_KIND_OBJECT_POSITIONS;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Move");
    return true;
}

static bool undo_copy_object_records(Undo *dst, const Undo *src)
{
    int count;
    size_t index_bytes;
    size_t object_bytes;

    if (!dst || !src || src->kind != UNDO_KIND_OBJECT_RECORDS)
        return false;

    undo_release_storage(dst);
    count = src->object_records.count;
    if (count <= 0)
        return false;
    index_bytes = (size_t)count * sizeof(int);
    object_bytes = (size_t)count * sizeof(Obj);
    dst->object_records.indices = (int *)malloc(index_bytes);
    dst->object_records.before_objects = (Obj *)malloc(object_bytes);
    dst->object_records.after_objects = (Obj *)malloc(object_bytes);
    if (!dst->object_records.indices || !dst->object_records.before_objects ||
        !dst->object_records.after_objects) {
        undo_release_storage(dst);
        return false;
    }
    dst->object_records.count = count;
    memcpy(dst->object_records.indices, src->object_records.indices, index_bytes);
    memcpy(dst->object_records.before_objects, src->object_records.before_objects, object_bytes);
    memcpy(dst->object_records.after_objects, src->object_records.after_objects, object_bytes);
    dst->kind = UNDO_KIND_OBJECT_RECORDS;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Edit Objects");
    return true;
}

static bool undo_copy_module_lines(Undo *dst, const Undo *src)
{
    int count;
    size_t index_bytes;
    size_t line_bytes;

    if (!dst || !src || src->kind != UNDO_KIND_MODULE_LINES)
        return false;

    undo_release_storage(dst);
    count = src->module_lines.count;
    if (count <= 0)
        return false;
    index_bytes = (size_t)count * sizeof(int);
    line_bytes = (size_t)count * 256u;
    dst->module_lines.indices = (int *)malloc(index_bytes);
    dst->module_lines.before_lines = (char (*)[256])malloc(line_bytes);
    dst->module_lines.after_lines = (char (*)[256])malloc(line_bytes);
    if (!dst->module_lines.indices || !dst->module_lines.before_lines ||
        !dst->module_lines.after_lines) {
        undo_release_storage(dst);
        return false;
    }
    dst->module_lines.count = count;
    memcpy(dst->module_lines.indices, src->module_lines.indices, index_bytes);
    memcpy(dst->module_lines.before_lines, src->module_lines.before_lines, line_bytes);
    memcpy(dst->module_lines.after_lines, src->module_lines.after_lines, line_bytes);
    dst->kind = UNDO_KIND_MODULE_LINES;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Edit Module");
    return true;
}

static bool undo_copy_palette_slot(Undo *dst, const Undo *src)
{
    if (!dst || !src || src->kind != UNDO_KIND_PALETTE_SLOT)
        return false;

    undo_release_storage(dst);
    dst->palette_slot = src->palette_slot;
    dst->kind = UNDO_KIND_PALETTE_SLOT;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Edit Palette");
    return true;
}

static bool undo_copy_image_index(Undo *dst, const Undo *src)
{
    if (!dst || !src || src->kind != UNDO_KIND_IMAGE_INDEX)
        return false;

    undo_release_storage(dst);
    dst->image_index = src->image_index;
    dst->kind = UNDO_KIND_IMAGE_INDEX;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Edit Image Index");
    return true;
}

static bool undo_copy_image_pixels(Undo *dst, const Undo *src)
{
    int count;
    size_t index_bytes;
    size_t pixel_bytes;

    if (!dst || !src || src->kind != UNDO_KIND_IMAGE_PIXELS)
        return false;

    undo_release_storage(dst);
    count = src->image_pixels.count;
    if (count <= 0)
        return false;
    index_bytes = (size_t)count * sizeof(int);
    pixel_bytes = (size_t)count * sizeof(Uint8);
    dst->image_pixels.offsets = (int *)malloc(index_bytes);
    dst->image_pixels.before_pixels = (Uint8 *)malloc(pixel_bytes);
    dst->image_pixels.after_pixels = (Uint8 *)malloc(pixel_bytes);
    if (!dst->image_pixels.offsets || !dst->image_pixels.before_pixels ||
        !dst->image_pixels.after_pixels) {
        undo_release_storage(dst);
        return false;
    }
    dst->image_pixels.img_i = src->image_pixels.img_i;
    dst->image_pixels.image_idx = src->image_pixels.image_idx;
    dst->image_pixels.width = src->image_pixels.width;
    dst->image_pixels.height = src->image_pixels.height;
    dst->image_pixels.count = count;
    memcpy(dst->image_pixels.offsets, src->image_pixels.offsets, index_bytes);
    memcpy(dst->image_pixels.before_pixels, src->image_pixels.before_pixels, pixel_bytes);
    memcpy(dst->image_pixels.after_pixels, src->image_pixels.after_pixels, pixel_bytes);
    dst->kind = UNDO_KIND_IMAGE_PIXELS;
    snprintf(dst->label, sizeof(dst->label), "%s",
             src->label[0] ? src->label : "Edit Pixels");
    return true;
}

/* Image undo deltas record the slot index they were captured at, but slots can
   be reordered, deleted, or deduped afterward. Resolve the real slot by the
   stable BDD image id, preferring the recorded slot only while it still holds
   the expected id, so an undo can never write into the wrong image. */
static int undo_find_image_slot(int recorded_slot, int image_idx)
{
    if (recorded_slot >= 0 && recorded_slot < g_ni &&
        g_img[recorded_slot].idx == image_idx)
        return recorded_slot;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx == image_idx)
            return i;
    return -1;
}

static void undo_apply_object_positions(const Undo *u, bool redo)
{
    if (!u || u->kind != UNDO_KIND_OBJECT_POSITIONS)
        return;

    for (int i = 0; i < u->object_pos.count; i++) {
        int obj_i = u->object_pos.indices[i];
        if (obj_i < 0 || obj_i >= g_no) continue;
        g_obj[obj_i].depth = redo ? u->object_pos.after_depth[i]
                                  : u->object_pos.before_depth[i];
        g_obj[obj_i].sy = redo ? u->object_pos.after_sy[i]
                               : u->object_pos.before_sy[i];
    }
    g_need_rebuild = 1;
}

static void undo_apply_object_records(const Undo *u, bool redo)
{
    if (!u || u->kind != UNDO_KIND_OBJECT_RECORDS)
        return;

    for (int i = 0; i < u->object_records.count; i++) {
        int obj_i = u->object_records.indices[i];
        if (obj_i < 0 || obj_i >= g_no) continue;
        g_obj[obj_i] = redo ? u->object_records.after_objects[i]
                            : u->object_records.before_objects[i];
    }
    g_need_rebuild = 1;
}

static void undo_apply_module_lines(const Undo *u, bool redo)
{
    if (!u || u->kind != UNDO_KIND_MODULE_LINES)
        return;

    for (int i = 0; i < u->module_lines.count; i++) {
        int mod_i = u->module_lines.indices[i];
        const char *line;
        if (mod_i < 0 || mod_i >= g_bdb_num_modules) continue;
        line = redo ? u->module_lines.after_lines[i]
                    : u->module_lines.before_lines[i];
        snprintf(g_bdb_modules[mod_i], 256, "%s", line);
    }
    g_need_rebuild = 1;
}

static void undo_apply_palette_slot(const Undo *u, bool redo)
{
    const PaletteSlotDelta *p;
    const Uint32 *colors;
    const Uint16 *rgb555;
    const char *name;
    int count;
    int rgb555_valid;
    int rgb555_count;

    if (!u || u->kind != UNDO_KIND_PALETTE_SLOT)
        return;

    p = &u->palette_slot;
    if (p->pal_i < 0 || p->pal_i >= g_n_pals)
        return;

    colors = redo ? p->after_colors : p->before_colors;
    rgb555 = redo ? p->after_rgb555 : p->before_rgb555;
    name = redo ? p->after_name : p->before_name;
    count = redo ? p->after_count : p->before_count;
    rgb555_valid = redo ? p->after_rgb555_valid : p->before_rgb555_valid;
    rgb555_count = redo ? p->after_rgb555_count : p->before_rgb555_count;

    if (!editor_project_set_palette_slot(p->pal_i, name, count, colors))
        return;
    if (rgb555_valid)
        editor_project_set_palette_rgb555_cache(p->pal_i, rgb555, rgb555_count);
    g_need_rebuild = 1;
}

static void undo_apply_image_index(const Undo *u, bool redo)
{
    if (!u || u->kind != UNDO_KIND_IMAGE_INDEX)
        return;

    /* The id itself is what changes, so locate the image by the id it currently
       holds (after_idx when undoing, before_idx when redoing). */
    int source_idx = redo ? u->image_index.before_idx : u->image_index.after_idx;
    int slot = undo_find_image_slot(u->image_index.img_i, source_idx);
    if (slot < 0)
        return;

    g_img[slot].idx = redo ? u->image_index.after_idx
                           : u->image_index.before_idx;
    g_need_rebuild = 1;
}

static void undo_apply_image_pixels(const Undo *u, bool redo)
{
    int img_i;
    int pixel_count;
    Img *im;

    if (!u || u->kind != UNDO_KIND_IMAGE_PIXELS)
        return;

    img_i = undo_find_image_slot(u->image_pixels.img_i, u->image_pixels.image_idx);
    if (img_i < 0 || img_i >= g_ni)
        return;
    im = &g_img[img_i];
    if (!im->pix || im->w != u->image_pixels.width || im->h != u->image_pixels.height)
        return;
    if (im->w <= 0 || im->h <= 0 || im->w > 0x3fffffff / im->h)
        return;
    pixel_count = im->w * im->h;

    for (int i = 0; i < u->image_pixels.count; i++) {
        int offset = u->image_pixels.offsets[i];
        if (offset < 0 || offset >= pixel_count) continue;
        im->pix[offset] = redo ? u->image_pixels.after_pixels[i]
                               : u->image_pixels.before_pixels[i];
    }
    g_need_rebuild = 1;
}

static void undo_apply(const Undo *u, bool redo)
{
    if (!u) return;
    if (u->kind == UNDO_KIND_OBJECT_POSITIONS) {
        undo_apply_object_positions(u, redo);
    } else if (u->kind == UNDO_KIND_OBJECT_RECORDS) {
        undo_apply_object_records(u, redo);
    } else if (u->kind == UNDO_KIND_MODULE_LINES) {
        undo_apply_module_lines(u, redo);
    } else if (u->kind == UNDO_KIND_PALETTE_SLOT) {
        undo_apply_palette_slot(u, redo);
    } else if (u->kind == UNDO_KIND_IMAGE_INDEX) {
        undo_apply_image_index(u, redo);
    } else if (u->kind == UNDO_KIND_IMAGE_PIXELS) {
        undo_apply_image_pixels(u, redo);
    } else if (u->kind == UNDO_KIND_SNAPSHOT) {
        project_snapshot_restore_current(&u->snapshot, false);
        g_need_rebuild = 1;
        g_hl_obj = -1;
    }
}

void undo_manager_init(void) {
    g_undo_base = 0;
    g_undo_n = 0;
    g_redo_avail = 0;
    memset(g_action_label, 0, sizeof(g_action_label));
    for (int i = 0; i < CHECKPOINT_N; i++) {
        g_checkpoints[i].used = false;
        memset(g_checkpoints[i].label, 0, sizeof(g_checkpoints[i].label));
    }
}

void undo_manager_shutdown(void) {
    for (int i = 0; i < UNDO_DEPTH; i++) {
        undo_release_storage(&g_undo_ring[i]);
    }
    undo_release_storage(&g_redo_state);
    for (int i = 0; i < CHECKPOINT_N; i++) {
        undo_release_storage(&g_checkpoints[i].state);
    }
}

void undo_save(void)
{
    /* new edit clears redo */
    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    /* evict oldest if ring full */
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }
    int slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    if (!undo_capture_snapshot(&g_undo_ring[slot])) return;
    snprintf(g_undo_ring[slot].label, sizeof(g_undo_ring[slot].label),
             "%s", g_action_label[0] ? g_action_label : "Edit");
    g_action_label[0] = '\0';
    g_undo_n++;
    g_dirty = 1;
}

int undo_save_object_position_delta_for_mask(const int *before_depth,
                                             const int *before_sy,
                                             const unsigned char *object_mask,
                                             int object_capacity,
                                             const char *lbl)
{
    int changed = 0;
    int slot;
    Undo *u;
    size_t bytes;

    if (!before_depth || !before_sy || object_capacity <= 0)
        return 0;

    for (int i = 0; i < g_no && i < object_capacity; i++) {
        if (object_mask) {
            if (!object_mask[i]) continue;
        } else if (!g_sel_flags[i]) {
            continue;
        }
        if (before_depth[i] != g_obj[i].depth || before_sy[i] != g_obj[i].sy)
            changed++;
    }
    if (changed <= 0)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    bytes = (size_t)changed * sizeof(int);
    u->object_pos.indices = (int *)malloc(bytes);
    u->object_pos.before_depth = (int *)malloc(bytes);
    u->object_pos.before_sy = (int *)malloc(bytes);
    u->object_pos.after_depth = (int *)malloc(bytes);
    u->object_pos.after_sy = (int *)malloc(bytes);
    if (!u->object_pos.indices || !u->object_pos.before_depth ||
        !u->object_pos.before_sy || !u->object_pos.after_depth ||
        !u->object_pos.after_sy) {
        undo_release_storage(u);
        return 0;
    }

    u->kind = UNDO_KIND_OBJECT_POSITIONS;
    u->object_pos.count = changed;
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Move");
    for (int i = 0, j = 0; i < g_no && i < object_capacity; i++) {
        if (object_mask) {
            if (!object_mask[i]) continue;
        } else if (!g_sel_flags[i]) {
            continue;
        }
        if (before_depth[i] == g_obj[i].depth && before_sy[i] == g_obj[i].sy)
            continue;
        u->object_pos.indices[j] = i;
        u->object_pos.before_depth[j] = before_depth[i];
        u->object_pos.before_sy[j] = before_sy[i];
        u->object_pos.after_depth[j] = g_obj[i].depth;
        u->object_pos.after_sy[j] = g_obj[i].sy;
        j++;
    }

    g_undo_n++;
    g_dirty = 1;
    return changed;
}

int undo_save_object_position_delta_for_selection(const int *before_depth,
                                                  const int *before_sy,
                                                  int object_capacity,
                                                  const char *lbl)
{
    return undo_save_object_position_delta_for_mask(before_depth, before_sy,
                                                   NULL, object_capacity, lbl);
}

int undo_save_object_record_delta_for_mask(const Obj *before_objects,
                                           const unsigned char *object_mask,
                                           int object_capacity,
                                           const char *lbl)
{
    int changed = 0;
    int slot;
    Undo *u;
    size_t index_bytes;
    size_t object_bytes;

    if (!before_objects || !object_mask || object_capacity <= 0)
        return 0;

    for (int i = 0; i < g_no && i < object_capacity; i++) {
        if (!object_mask[i]) continue;
        if (memcmp(&before_objects[i], &g_obj[i], sizeof(Obj)) != 0)
            changed++;
    }
    if (changed <= 0)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    index_bytes = (size_t)changed * sizeof(int);
    object_bytes = (size_t)changed * sizeof(Obj);
    u->object_records.indices = (int *)malloc(index_bytes);
    u->object_records.before_objects = (Obj *)malloc(object_bytes);
    u->object_records.after_objects = (Obj *)malloc(object_bytes);
    if (!u->object_records.indices || !u->object_records.before_objects ||
        !u->object_records.after_objects) {
        undo_release_storage(u);
        return 0;
    }

    u->kind = UNDO_KIND_OBJECT_RECORDS;
    u->object_records.count = changed;
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Edit Objects");
    for (int i = 0, j = 0; i < g_no && i < object_capacity; i++) {
        if (!object_mask[i]) continue;
        if (memcmp(&before_objects[i], &g_obj[i], sizeof(Obj)) == 0)
            continue;
        u->object_records.indices[j] = i;
        u->object_records.before_objects[j] = before_objects[i];
        u->object_records.after_objects[j] = g_obj[i];
        j++;
    }

    g_undo_n++;
    g_dirty = 1;
    return changed;
}

int undo_save_module_line_delta_for_mask(const char *before_lines,
                                         const unsigned char *module_mask,
                                         int module_capacity,
                                         const char *lbl)
{
    int changed = 0;
    int slot;
    Undo *u;
    size_t index_bytes;
    size_t line_bytes;

    if (!before_lines || !module_mask || module_capacity <= 0)
        return 0;

    for (int i = 0; i < g_bdb_num_modules && i < module_capacity; i++) {
        const char *before = before_lines + ((size_t)i * 256u);
        if (!module_mask[i]) continue;
        if (strncmp(before, g_bdb_modules[i], 256) != 0)
            changed++;
    }
    if (changed <= 0)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    index_bytes = (size_t)changed * sizeof(int);
    line_bytes = (size_t)changed * 256u;
    u->module_lines.indices = (int *)malloc(index_bytes);
    u->module_lines.before_lines = (char (*)[256])malloc(line_bytes);
    u->module_lines.after_lines = (char (*)[256])malloc(line_bytes);
    if (!u->module_lines.indices || !u->module_lines.before_lines ||
        !u->module_lines.after_lines) {
        undo_release_storage(u);
        return 0;
    }

    u->kind = UNDO_KIND_MODULE_LINES;
    u->module_lines.count = changed;
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Edit Module");
    for (int i = 0, j = 0; i < g_bdb_num_modules && i < module_capacity; i++) {
        const char *before = before_lines + ((size_t)i * 256u);
        if (!module_mask[i]) continue;
        if (strncmp(before, g_bdb_modules[i], 256) == 0)
            continue;
        u->module_lines.indices[j] = i;
        snprintf(u->module_lines.before_lines[j], 256, "%s", before);
        snprintf(u->module_lines.after_lines[j], 256, "%s", g_bdb_modules[i]);
        j++;
    }

    g_undo_n++;
    g_dirty = 1;
    return changed;
}

int undo_save_palette_slot_delta(int pal_i,
                                 const Uint32 *before_colors,
                                 int before_count,
                                 const char *before_name,
                                 const Uint16 *before_rgb555,
                                 int before_rgb555_valid,
                                 int before_rgb555_count,
                                 const char *lbl)
{
    Uint16 after_rgb555[256];
    int after_rgb555_valid;
    int after_rgb555_count = 0;
    int colors_changed;
    int cache_changed;
    int slot;
    Undo *u;

    if (!before_colors || pal_i < 0 || pal_i >= g_n_pals)
        return 0;
    if (before_count < 0) before_count = 0;
    if (before_count > 256) before_count = 256;
    if (before_rgb555_count < 0) before_rgb555_count = 0;
    if (before_rgb555_count > 256) before_rgb555_count = 256;
    before_rgb555_valid = before_rgb555_valid && before_rgb555 != NULL;

    memset(after_rgb555, 0, sizeof(after_rgb555));
    after_rgb555_valid = editor_project_get_palette_rgb555_cache(pal_i, after_rgb555, 256);
    if (after_rgb555_valid)
        after_rgb555_count = g_pal_count[pal_i];

    colors_changed =
        before_count != g_pal_count[pal_i] ||
        strncmp(before_name ? before_name : "", g_pal_name[pal_i], 64) != 0 ||
        memcmp(before_colors, g_pals[pal_i], sizeof g_pals[pal_i]) != 0;
    cache_changed =
        before_rgb555_valid != after_rgb555_valid ||
        (before_rgb555_valid && after_rgb555_valid &&
         (before_rgb555_count != after_rgb555_count ||
          memcmp(before_rgb555, after_rgb555,
                 (size_t)before_rgb555_count * sizeof(before_rgb555[0])) != 0));
    if (!colors_changed && !cache_changed)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    u->kind = UNDO_KIND_PALETTE_SLOT;
    u->palette_slot.pal_i = pal_i;
    u->palette_slot.before_count = before_count;
    u->palette_slot.after_count = g_pal_count[pal_i];
    snprintf(u->palette_slot.before_name, sizeof(u->palette_slot.before_name),
             "%s", before_name && before_name[0] ? before_name : "PAL");
    snprintf(u->palette_slot.after_name, sizeof(u->palette_slot.after_name),
             "%s", g_pal_name[pal_i][0] ? g_pal_name[pal_i] : "PAL");
    memcpy(u->palette_slot.before_colors, before_colors, sizeof u->palette_slot.before_colors);
    memcpy(u->palette_slot.after_colors, g_pals[pal_i], sizeof u->palette_slot.after_colors);
    u->palette_slot.before_rgb555_valid = before_rgb555_valid;
    u->palette_slot.after_rgb555_valid = after_rgb555_valid;
    u->palette_slot.before_rgb555_count = before_rgb555_count;
    u->palette_slot.after_rgb555_count = after_rgb555_count;
    if (before_rgb555_valid)
        memcpy(u->palette_slot.before_rgb555, before_rgb555,
               sizeof u->palette_slot.before_rgb555);
    if (after_rgb555_valid)
        memcpy(u->palette_slot.after_rgb555, after_rgb555,
               sizeof u->palette_slot.after_rgb555);
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Edit Palette");

    g_undo_n++;
    g_dirty = 1;
    return 1;
}

int undo_save_image_index_delta(int img_i, int before_idx, const char *lbl)
{
    int slot;
    Undo *u;

    if (img_i < 0 || img_i >= g_ni)
        return 0;
    if (before_idx == g_img[img_i].idx)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    u->kind = UNDO_KIND_IMAGE_INDEX;
    u->image_index.img_i = img_i;
    u->image_index.before_idx = before_idx;
    u->image_index.after_idx = g_img[img_i].idx;
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Edit Image Index");

    g_undo_n++;
    g_dirty = 1;
    return 1;
}

int undo_save_image_pixels_delta(int img_i,
                                 int width,
                                 int height,
                                 const Uint8 *before_pixels,
                                 const char *lbl)
{
    Img *im;
    int pixel_count;
    int changed = 0;
    int slot;
    Undo *u;
    size_t index_bytes;
    size_t pixel_bytes;

    if (!before_pixels || img_i < 0 || img_i >= g_ni)
        return 0;
    im = &g_img[img_i];
    if (!im->pix || width <= 0 || height <= 0)
        return 0;
    if (im->w != width || im->h != height)
        return 0;
    if (width > 0x3fffffff / height)
        return 0;
    pixel_count = width * height;

    for (int i = 0; i < pixel_count; i++) {
        if (before_pixels[i] != im->pix[i])
            changed++;
    }
    if (changed <= 0)
        return 0;

    if (g_redo_avail) { undo_release_storage(&g_redo_state); g_redo_avail = 0; }
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }

    slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    u = &g_undo_ring[slot];
    undo_release_storage(u);
    index_bytes = (size_t)changed * sizeof(int);
    pixel_bytes = (size_t)changed * sizeof(Uint8);
    u->image_pixels.offsets = (int *)malloc(index_bytes);
    u->image_pixels.before_pixels = (Uint8 *)malloc(pixel_bytes);
    u->image_pixels.after_pixels = (Uint8 *)malloc(pixel_bytes);
    if (!u->image_pixels.offsets || !u->image_pixels.before_pixels ||
        !u->image_pixels.after_pixels) {
        undo_release_storage(u);
        return 0;
    }

    u->kind = UNDO_KIND_IMAGE_PIXELS;
    u->image_pixels.img_i = img_i;
    u->image_pixels.image_idx = im->idx;
    u->image_pixels.width = width;
    u->image_pixels.height = height;
    u->image_pixels.count = changed;
    snprintf(u->label, sizeof(u->label), "%s", lbl && lbl[0] ? lbl : "Edit Pixels");
    for (int i = 0, j = 0; i < pixel_count; i++) {
        if (before_pixels[i] == im->pix[i])
            continue;
        u->image_pixels.offsets[j] = i;
        u->image_pixels.before_pixels[j] = before_pixels[i];
        u->image_pixels.after_pixels[j] = im->pix[i];
        j++;
    }

    g_undo_n++;
    g_dirty = 1;
    return changed;
}

void undo_save_ex(const char *lbl)
{
    snprintf(g_action_label, 48, "%s", lbl ? lbl : "");
    undo_save();
}

void undo_set_action_label(const char *lbl)
{
    snprintf(g_action_label, 48, "%s", lbl ? lbl : "");
}

void undo_restore(void)
{
    if (g_undo_n == 0) return;
    /* pop newest undo entry */
    g_undo_n--;
    int slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    Undo *entry = &g_undo_ring[slot];
    undo_release_storage(&g_redo_state);
    if (entry->kind == UNDO_KIND_OBJECT_POSITIONS) {
        if (!undo_copy_object_positions(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else if (entry->kind == UNDO_KIND_OBJECT_RECORDS) {
        if (!undo_copy_object_records(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else if (entry->kind == UNDO_KIND_MODULE_LINES) {
        if (!undo_copy_module_lines(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else if (entry->kind == UNDO_KIND_PALETTE_SLOT) {
        if (!undo_copy_palette_slot(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else if (entry->kind == UNDO_KIND_IMAGE_INDEX) {
        if (!undo_copy_image_index(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else if (entry->kind == UNDO_KIND_IMAGE_PIXELS) {
        if (!undo_copy_image_pixels(&g_redo_state, entry)) {
            g_undo_n++;
            return;
        }
    } else {
        if (!undo_capture_snapshot(&g_redo_state)) {
            g_undo_n++;
            return;
        }
        snprintf(g_redo_state.label, sizeof(g_redo_state.label), "%s",
                 entry->label[0] ? entry->label : "Edit");
    }
    g_redo_avail = 1;
    undo_apply(entry, false);
    g_dirty = 1;
}

void redo_restore(void)
{
    if (!g_redo_avail) return;

    /* Build the entry that will be pushed back onto the undo ring into a
       temporary first. This way a copy failure (OOM) leaves the ring and its
       bookkeeping untouched instead of silently dropping the oldest entry. */
    Undo restored = {};
    bool ok;
    if (g_redo_state.kind == UNDO_KIND_OBJECT_POSITIONS) {
        ok = undo_copy_object_positions(&restored, &g_redo_state);
    } else if (g_redo_state.kind == UNDO_KIND_OBJECT_RECORDS) {
        ok = undo_copy_object_records(&restored, &g_redo_state);
    } else if (g_redo_state.kind == UNDO_KIND_MODULE_LINES) {
        ok = undo_copy_module_lines(&restored, &g_redo_state);
    } else if (g_redo_state.kind == UNDO_KIND_PALETTE_SLOT) {
        ok = undo_copy_palette_slot(&restored, &g_redo_state);
    } else if (g_redo_state.kind == UNDO_KIND_IMAGE_INDEX) {
        ok = undo_copy_image_index(&restored, &g_redo_state);
    } else if (g_redo_state.kind == UNDO_KIND_IMAGE_PIXELS) {
        ok = undo_copy_image_pixels(&restored, &g_redo_state);
    } else {
        ok = undo_capture_snapshot(&restored);
        if (ok)
            snprintf(restored.label, sizeof(restored.label), "%s",
                     g_redo_state.label[0] ? g_redo_state.label : "Edit");
    }
    if (!ok) {
        undo_release_storage(&restored);
        return;
    }

    /* Commit: only now that we hold a valid entry do we evict the oldest and
       push current → undo ring (without clearing redo). */
    if (g_undo_n == UNDO_DEPTH) {
        undo_release_storage(&g_undo_ring[g_undo_base]);
        g_undo_base = (g_undo_base + 1) % UNDO_DEPTH;
        g_undo_n--;
    }
    int slot = (g_undo_base + g_undo_n) % UNDO_DEPTH;
    undo_release_storage(&g_undo_ring[slot]);
    g_undo_ring[slot] = restored;   /* transfers ownership of malloc'd buffers */
    g_undo_n++;

    /* restore redo state */
    undo_apply(&g_redo_state, true);
    undo_release_storage(&g_redo_state);
    g_redo_avail = 0;
    g_dirty = 1;
}

bool undo_is_available(void) { return g_undo_n > 0; }
bool redo_is_available(void) { return g_redo_avail != 0; }
int undo_get_count(void) { return g_undo_n; }

const char* undo_get_history_label(int depth) {
    if (depth < 0 || depth >= g_undo_n) return "Edit";
    int ring_i = g_undo_n - 1 - depth;
    int slot = (g_undo_base + ring_i) % UNDO_DEPTH;
    return g_undo_ring[slot].label[0] ? g_undo_ring[slot].label : "Edit";
}

int undo_get_history_objects_count(int depth) {
    if (depth < 0 || depth >= g_undo_n) return 0;
    int ring_i = g_undo_n - 1 - depth;
    int slot = (g_undo_base + ring_i) % UNDO_DEPTH;
    if (g_undo_ring[slot].kind == UNDO_KIND_OBJECT_POSITIONS ||
        g_undo_ring[slot].kind == UNDO_KIND_OBJECT_RECORDS ||
        g_undo_ring[slot].kind == UNDO_KIND_MODULE_LINES ||
        g_undo_ring[slot].kind == UNDO_KIND_PALETTE_SLOT ||
        g_undo_ring[slot].kind == UNDO_KIND_IMAGE_INDEX ||
        g_undo_ring[slot].kind == UNDO_KIND_IMAGE_PIXELS)
        return g_no;
    return g_undo_ring[slot].snapshot.no;
}

int undo_get_history_images_count(int depth) {
    if (depth < 0 || depth >= g_undo_n) return 0;
    int ring_i = g_undo_n - 1 - depth;
    int slot = (g_undo_base + ring_i) % UNDO_DEPTH;
    if (g_undo_ring[slot].kind == UNDO_KIND_OBJECT_POSITIONS ||
        g_undo_ring[slot].kind == UNDO_KIND_OBJECT_RECORDS ||
        g_undo_ring[slot].kind == UNDO_KIND_MODULE_LINES ||
        g_undo_ring[slot].kind == UNDO_KIND_PALETTE_SLOT ||
        g_undo_ring[slot].kind == UNDO_KIND_IMAGE_INDEX ||
        g_undo_ring[slot].kind == UNDO_KIND_IMAGE_PIXELS)
        return g_ni;
    return g_undo_ring[slot].snapshot.ni;
}

bool checkpoint_is_used(int index) {
    if (index < 0 || index >= CHECKPOINT_N) return false;
    return g_checkpoints[index].used;
}

const char* checkpoint_get_label(int index) {
    if (index < 0 || index >= CHECKPOINT_N) return "";
    return g_checkpoints[index].label;
}

void checkpoint_save(int index, const char* label) {
    if (index < 0 || index >= CHECKPOINT_N) return;
    Checkpoint *cp = &g_checkpoints[index];
    undo_release_storage(&cp->state);
    if (!undo_capture_snapshot(&cp->state)) return;
    snprintf(cp->label, sizeof(cp->label), "%s", label && label[0] ? label : "Checkpoint");
    cp->used = true;
}

void checkpoint_restore(int index) {
    if (index < 0 || index >= CHECKPOINT_N) return;
    Checkpoint *cp = &g_checkpoints[index];
    if (!cp->used) return;
    undo_save_ex("Restore Checkpoint");
    undo_apply(&cp->state, false);
}

void checkpoint_delete(int index) {
    if (index < 0 || index >= CHECKPOINT_N) return;
    Checkpoint *cp = &g_checkpoints[index];
    if (cp->used) {
        undo_release_storage(&cp->state);
        cp->used = false;
    }
}
