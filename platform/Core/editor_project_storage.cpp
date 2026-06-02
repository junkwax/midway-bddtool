#include "Core/editor_project_storage.h"
#include "bg_editor_globals.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct EditorProjectStorage {
    int image_capacity;
    int object_capacity;
    int module_capacity;
    int palette_capacity;
    Img *img;
    Obj *obj;
    char (*bdb_modules)[256];
    Uint32 (*pals)[256];
    int *pal_count;
    char (*pal_name)[64];
    Uint16 (*pal_rgb555)[256];
    Uint32 (*pal_argb_snapshot)[256];
    int *pal_rgb555_valid;
    int *pal_rgb555_count;
    char (*pal_rgb555_name)[64];
    int *obj_lock;
    int *obj_hidden;
    int *sel_flags;
} EditorProjectStorage;

static EditorProjectStorage *g_project_storage = NULL;

static Uint16 (*g_pal_rgb555)[256] = NULL;
static Uint32 (*g_pal_argb_snapshot)[256] = NULL;
static int    *g_pal_rgb555_valid = NULL;
static int    *g_pal_rgb555_count = NULL;
static char   (*g_pal_rgb555_name)[64] = NULL;

static void editor_project_storage_refresh_globals(EditorProjectStorage *s)
{
    if (!s) return;
    g_img = s->img;
    g_obj = s->obj;
    g_bdb_modules = s->bdb_modules;
    g_pals = s->pals;
    g_pal_count = s->pal_count;
    g_pal_name = s->pal_name;
    g_pal_rgb555 = s->pal_rgb555;
    g_pal_argb_snapshot = s->pal_argb_snapshot;
    g_pal_rgb555_valid = s->pal_rgb555_valid;
    g_pal_rgb555_count = s->pal_rgb555_count;
    g_pal_rgb555_name = s->pal_rgb555_name;
    g_obj_lock = s->obj_lock;
    g_obj_hidden = s->obj_hidden;
    g_sel_flags = s->sel_flags;
}

static int editor_project_next_capacity(int current, int minimum)
{
    int next = current > 0 ? current : 1;
    if (minimum <= next) return next;
    while (next < minimum) {
        if (next > INT_MAX / 2) {
            next = minimum;
            break;
        }
        next *= 2;
    }
    return next;
}

int editor_project_storage_init(void)
{
    EditorProjectStorage *s;
    if (g_project_storage) return 1;
    s = (EditorProjectStorage *)calloc(1, sizeof(*s));
    if (!s) {
        fprintf(stderr, "fatal: could not allocate editor project storage\n");
        return 0;
    }
    s->image_capacity = MAX_IMAGES;
    s->object_capacity = MAX_OBJECTS;
    s->module_capacity = MAX_MODULES;
    s->palette_capacity = MAX_PALS;
    s->img = (Img *)calloc((size_t)s->image_capacity, sizeof s->img[0]);
    s->obj = (Obj *)calloc((size_t)s->object_capacity, sizeof s->obj[0]);
    s->bdb_modules = (char (*)[256])calloc((size_t)s->module_capacity, sizeof s->bdb_modules[0]);
    s->pals = (Uint32 (*)[256])calloc((size_t)s->palette_capacity, sizeof s->pals[0]);
    s->pal_count = (int *)calloc((size_t)s->palette_capacity, sizeof s->pal_count[0]);
    s->pal_name = (char (*)[64])calloc((size_t)s->palette_capacity, sizeof s->pal_name[0]);
    s->pal_rgb555 = (Uint16 (*)[256])calloc((size_t)s->palette_capacity, sizeof s->pal_rgb555[0]);
    s->pal_argb_snapshot = (Uint32 (*)[256])calloc((size_t)s->palette_capacity, sizeof s->pal_argb_snapshot[0]);
    s->pal_rgb555_valid = (int *)calloc((size_t)s->palette_capacity, sizeof s->pal_rgb555_valid[0]);
    s->pal_rgb555_count = (int *)calloc((size_t)s->palette_capacity, sizeof s->pal_rgb555_count[0]);
    s->pal_rgb555_name = (char (*)[64])calloc((size_t)s->palette_capacity, sizeof s->pal_rgb555_name[0]);
    s->obj_lock = (int *)calloc((size_t)s->object_capacity, sizeof s->obj_lock[0]);
    s->obj_hidden = (int *)calloc((size_t)s->object_capacity, sizeof s->obj_hidden[0]);
    s->sel_flags = (int *)calloc((size_t)s->object_capacity, sizeof s->sel_flags[0]);
    if (!s->img || !s->obj || !s->bdb_modules || !s->pals || !s->pal_count ||
        !s->pal_name || !s->pal_rgb555 || !s->pal_argb_snapshot ||
        !s->pal_rgb555_valid || !s->pal_rgb555_count || !s->pal_rgb555_name ||
        !s->obj_lock || !s->obj_hidden || !s->sel_flags) {
        fprintf(stderr, "fatal: could not allocate editor project arrays\n");
        free(s->img);
        free(s->obj);
        free(s->bdb_modules);
        free(s->pals);
        free(s->pal_count);
        free(s->pal_name);
        free(s->pal_rgb555);
        free(s->pal_argb_snapshot);
        free(s->pal_rgb555_valid);
        free(s->pal_rgb555_count);
        free(s->pal_rgb555_name);
        free(s->obj_lock);
        free(s->obj_hidden);
        free(s->sel_flags);
        free(s);
        return 0;
    }
    g_project_storage = s;
    editor_project_storage_refresh_globals(s);
    return 1;
}

void editor_project_storage_shutdown(void)
{
    EditorProjectStorage *s = g_project_storage;
    if (!s) return;
    free(s->img);
    free(s->obj);
    free(s->bdb_modules);
    free(s->pals);
    free(s->pal_count);
    free(s->pal_name);
    free(s->pal_rgb555);
    free(s->pal_argb_snapshot);
    free(s->pal_rgb555_valid);
    free(s->pal_rgb555_count);
    free(s->pal_rgb555_name);
    free(s->obj_lock);
    free(s->obj_hidden);
    free(s->sel_flags);
    free(s);
    g_project_storage = NULL;
    g_img = NULL;
    g_obj = NULL;
    g_bdb_modules = NULL;
    g_pals = NULL;
    g_pal_count = NULL;
    g_pal_name = NULL;
    g_pal_rgb555 = NULL;
    g_pal_argb_snapshot = NULL;
    g_pal_rgb555_valid = NULL;
    g_pal_rgb555_count = NULL;
    g_pal_rgb555_name = NULL;
    g_obj_lock = NULL;
    g_obj_hidden = NULL;
    g_sel_flags = NULL;
}

int editor_project_image_capacity(void)
{
    return editor_project_storage_init() ? g_project_storage->image_capacity : 0;
}

int editor_project_object_capacity(void)
{
    return editor_project_storage_init() ? g_project_storage->object_capacity : 0;
}

int editor_project_module_capacity(void)
{
    return editor_project_storage_init() ? g_project_storage->module_capacity : 0;
}

int editor_project_palette_capacity(void)
{
    return editor_project_storage_init() ? g_project_storage->palette_capacity : 0;
}

int editor_project_reserve_images(int min_capacity);

void editor_project_clear_selection(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0 || !g_sel_flags) return;
    memset(g_sel_flags, 0, (size_t)object_cap * sizeof g_sel_flags[0]);
}

void editor_project_clear_images(void)
{
    int image_cap = editor_project_image_capacity();
    if (image_cap <= 0 || !g_img) {
        g_ni = 0;
        return;
    }
    int used = g_ni;
    if (used < 0) used = 0;
    if (used > image_cap) used = image_cap;
    for (int i = 0; i < used; i++) {
        free(g_img[i].pix);
        g_img[i].pix = NULL;
    }
    memset(g_img, 0, (size_t)image_cap * sizeof g_img[0]);
    g_ni = 0;
}

void editor_project_clear_objects(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap > 0 && g_obj)
        memset(g_obj, 0, (size_t)object_cap * sizeof g_obj[0]);
    if (object_cap > 0 && g_obj_lock)
        memset(g_obj_lock, 0, (size_t)object_cap * sizeof g_obj_lock[0]);
    if (object_cap > 0 && g_obj_hidden)
        memset(g_obj_hidden, 0, (size_t)object_cap * sizeof g_obj_hidden[0]);
    editor_project_clear_selection();
    g_no = 0;
    g_hl_obj = -1;
    g_ctx_obj = -1;
}

void editor_project_clear_modules(void)
{
    int module_cap = editor_project_module_capacity();
    if (module_cap > 0 && g_bdb_modules)
        memset(g_bdb_modules, 0, (size_t)module_cap * sizeof g_bdb_modules[0]);
    g_bdb_num_modules = 0;
}

void editor_project_clear_palettes(void)
{
    int pal_cap = editor_project_palette_capacity();
    if (pal_cap > 0 && g_pals)
        memset(g_pals, 0, (size_t)pal_cap * sizeof g_pals[0]);
    if (pal_cap > 0 && g_pal_count)
        memset(g_pal_count, 0, (size_t)pal_cap * sizeof g_pal_count[0]);
    if (pal_cap > 0 && g_pal_name)
        memset(g_pal_name, 0, (size_t)pal_cap * sizeof g_pal_name[0]);
    if (pal_cap > 0 && g_pal_rgb555)
        memset(g_pal_rgb555, 0, (size_t)pal_cap * sizeof g_pal_rgb555[0]);
    if (pal_cap > 0 && g_pal_argb_snapshot)
        memset(g_pal_argb_snapshot, 0, (size_t)pal_cap * sizeof g_pal_argb_snapshot[0]);
    if (pal_cap > 0 && g_pal_rgb555_valid)
        memset(g_pal_rgb555_valid, 0, (size_t)pal_cap * sizeof g_pal_rgb555_valid[0]);
    if (pal_cap > 0 && g_pal_rgb555_count)
        memset(g_pal_rgb555_count, 0, (size_t)pal_cap * sizeof g_pal_rgb555_count[0]);
    if (pal_cap > 0 && g_pal_rgb555_name)
        memset(g_pal_rgb555_name, 0, (size_t)pal_cap * sizeof g_pal_rgb555_name[0]);
    g_n_pals = 0;
}

void editor_project_clear_content(void)
{
    editor_project_clear_images();
    editor_project_clear_objects();
    editor_project_clear_modules();
    editor_project_clear_palettes();
}

void editor_project_reset_loaded_stage(void)
{
    editor_project_clear_content();
    g_have_bdb = 0;
    g_name[0] = '\0';
    g_bdb_path[0] = '\0';
    g_bdd_path[0] = '\0';
    g_bdb_header[0] = '\0';
    g_hl_obj = -1;
    g_ctx_obj = -1;
}

int editor_project_delete_image_slot(int img_i)
{
    if (!editor_project_storage_init() || !g_img) return 0;
    if (img_i < 0 || img_i >= g_ni) return 0;
    free(g_img[img_i].pix);
    for (int j = img_i; j < g_ni - 1; j++)
        g_img[j] = g_img[j + 1];
    if (g_ni > 0)
        memset(&g_img[g_ni - 1], 0, sizeof g_img[g_ni - 1]);
    g_ni--;
    if (g_ni < 0) g_ni = 0;
    return 1;
}

int editor_project_delete_marked_images(const unsigned char *delete_flags, int delete_flags_len)
{
    int original_ni;
    int new_ni = 0;
    int removed = 0;
    if (!editor_project_storage_init() || !g_img || !delete_flags || delete_flags_len <= 0)
        return 0;
    original_ni = g_ni;
    if (original_ni < 0) original_ni = 0;
    if (original_ni > editor_project_image_capacity())
        original_ni = editor_project_image_capacity();
    for (int i = 0; i < original_ni; i++) {
        int del = (i < delete_flags_len) && delete_flags[i];
        if (del) {
            free(g_img[i].pix);
            g_img[i].pix = NULL;
            removed++;
            continue;
        }
        if (new_ni != i)
            g_img[new_ni] = g_img[i];
        new_ni++;
    }
    for (int i = new_ni; i < original_ni; i++)
        memset(&g_img[i], 0, sizeof g_img[i]);
    g_ni = new_ni;
    return removed;
}

Img *editor_project_append_image_slot(void)
{
    Img *slot;
    if (!editor_project_reserve_images(g_ni + 1) || !g_img) return NULL;
    if (g_ni < 0 || g_ni >= editor_project_image_capacity()) return NULL;
    slot = &g_img[g_ni++];
    memset(slot, 0, sizeof *slot);
    return slot;
}

int editor_project_swap_image_slots(int img_a, int img_b)
{
    Img tmp;
    if (!editor_project_storage_init() || !g_img) return 0;
    if (img_a < 0 || img_b < 0 || img_a >= g_ni || img_b >= g_ni) return 0;
    if (img_a == img_b) return 1;
    tmp = g_img[img_a];
    g_img[img_a] = g_img[img_b];
    g_img[img_b] = tmp;
    return 1;
}

int editor_project_reorder_images(const int *image_order, int image_order_len)
{
    Img *sorted;
    unsigned char *seen;
    if (!editor_project_storage_init() || !g_img) return 0;
    if (image_order_len != g_ni) return 0;
    if (g_ni <= 0) return 1;
    if (!image_order) return 0;
    sorted = (Img *)malloc((size_t)g_ni * sizeof(sorted[0]));
    seen = (unsigned char *)calloc((size_t)g_ni, sizeof(seen[0]));
    if (!sorted || !seen) {
        free(sorted);
        free(seen);
        return 0;
    }
    for (int pos = 0; pos < g_ni; pos++) {
        int old_pos = image_order[pos];
        if (old_pos < 0 || old_pos >= g_ni || seen[old_pos]) {
            free(sorted);
            free(seen);
            return 0;
        }
        seen[old_pos] = 1;
        sorted[pos] = g_img[old_pos];
    }
    memcpy(g_img, sorted, (size_t)g_ni * sizeof(g_img[0]));
    free(sorted);
    free(seen);
    return 1;
}

int editor_project_truncate_images(int image_count)
{
    int image_cap;
    if (!editor_project_storage_init() || !g_img) return 0;
    image_cap = editor_project_image_capacity();
    if (image_count < 0) image_count = 0;
    if (image_count > image_cap) image_count = image_cap;
    if (g_ni < 0) g_ni = 0;
    if (g_ni > image_cap) g_ni = image_cap;
    for (int i = image_count; i < g_ni; i++) {
        free(g_img[i].pix);
        g_img[i].pix = NULL;
        memset(&g_img[i], 0, sizeof g_img[i]);
    }
    g_ni = image_count;
    return 1;
}

int editor_project_reserve_images(int min_capacity)
{
    EditorProjectStorage *s;
    Img *next_img;
    int new_capacity;
    if (!editor_project_storage_init()) return 0;
    s = g_project_storage;
    if (min_capacity <= s->image_capacity) return 1;
    new_capacity = editor_project_next_capacity(s->image_capacity, min_capacity);
    next_img = (Img *)calloc((size_t)new_capacity, sizeof(next_img[0]));
    if (!next_img) return 0;
    memcpy(next_img, s->img, (size_t)s->image_capacity * sizeof(next_img[0]));
    free(s->img);
    s->img = next_img;
    s->image_capacity = new_capacity;
    editor_project_storage_refresh_globals(s);
    return 1;
}

int editor_project_reserve_objects(int min_capacity)
{
    EditorProjectStorage *s;
    Obj *next_obj;
    int *next_lock;
    int *next_hidden;
    int *next_sel;
    int new_capacity;
    if (!editor_project_storage_init()) return 0;
    s = g_project_storage;
    if (min_capacity <= s->object_capacity) return 1;
    new_capacity = editor_project_next_capacity(s->object_capacity, min_capacity);
    next_obj = (Obj *)calloc((size_t)new_capacity, sizeof(next_obj[0]));
    next_lock = (int *)calloc((size_t)new_capacity, sizeof(next_lock[0]));
    next_hidden = (int *)calloc((size_t)new_capacity, sizeof(next_hidden[0]));
    next_sel = (int *)calloc((size_t)new_capacity, sizeof(next_sel[0]));
    if (!next_obj || !next_lock || !next_hidden || !next_sel) {
        free(next_obj);
        free(next_lock);
        free(next_hidden);
        free(next_sel);
        return 0;
    }
    memcpy(next_obj, s->obj, (size_t)s->object_capacity * sizeof(next_obj[0]));
    memcpy(next_lock, s->obj_lock, (size_t)s->object_capacity * sizeof(next_lock[0]));
    memcpy(next_hidden, s->obj_hidden, (size_t)s->object_capacity * sizeof(next_hidden[0]));
    memcpy(next_sel, s->sel_flags, (size_t)s->object_capacity * sizeof(next_sel[0]));
    free(s->obj);
    free(s->obj_lock);
    free(s->obj_hidden);
    free(s->sel_flags);
    s->obj = next_obj;
    s->obj_lock = next_lock;
    s->obj_hidden = next_hidden;
    s->sel_flags = next_sel;
    s->object_capacity = new_capacity;
    editor_project_storage_refresh_globals(s);
    return 1;
}

Obj *editor_project_append_object_slot(void)
{
    Obj *slot;
    if (!editor_project_reserve_objects(g_no + 1) || !g_obj) return NULL;
    if (g_no < 0 || g_no >= editor_project_object_capacity()) return NULL;
    slot = &g_obj[g_no++];
    memset(slot, 0, sizeof *slot);
    if (g_obj_lock) g_obj_lock[g_no - 1] = 0;
    if (g_obj_hidden) g_obj_hidden[g_no - 1] = 0;
    if (g_sel_flags) g_sel_flags[g_no - 1] = 0;
    return slot;
}

int editor_project_delete_object_slot(int obj_i)
{
    if (!editor_project_storage_init() || !g_obj) return 0;
    if (obj_i < 0 || obj_i >= g_no) return 0;
    for (int j = obj_i; j < g_no - 1; j++) {
        g_obj[j] = g_obj[j + 1];
        if (g_sel_flags) g_sel_flags[j] = g_sel_flags[j + 1];
        if (g_obj_lock) g_obj_lock[j] = g_obj_lock[j + 1];
        if (g_obj_hidden) g_obj_hidden[j] = g_obj_hidden[j + 1];
    }
    if (g_no > 0) {
        int tail = g_no - 1;
        memset(&g_obj[tail], 0, sizeof g_obj[tail]);
        if (g_sel_flags) g_sel_flags[tail] = 0;
        if (g_obj_lock) g_obj_lock[tail] = 0;
        if (g_obj_hidden) g_obj_hidden[tail] = 0;
        g_no--;
    }
    if (g_no < 0) g_no = 0;
    return 1;
}

int editor_project_move_object_slot(int src, int dst)
{
    Obj tmp;
    int tmp_sel = 0;
    int tmp_lock = 0;
    int tmp_hidden = 0;
    if (!editor_project_storage_init() || !g_obj) return 0;
    if (src < 0 || src >= g_no || dst < 0 || dst >= g_no) return 0;
    if (src == dst) return 1;
    tmp = g_obj[src];
    if (g_sel_flags) tmp_sel = g_sel_flags[src];
    if (g_obj_lock) tmp_lock = g_obj_lock[src];
    if (g_obj_hidden) tmp_hidden = g_obj_hidden[src];

    if (src < dst) {
        for (int k = src; k < dst; k++) {
            g_obj[k] = g_obj[k + 1];
            if (g_sel_flags) g_sel_flags[k] = g_sel_flags[k + 1];
            if (g_obj_lock) g_obj_lock[k] = g_obj_lock[k + 1];
            if (g_obj_hidden) g_obj_hidden[k] = g_obj_hidden[k + 1];
        }
    } else {
        for (int k = src; k > dst; k--) {
            g_obj[k] = g_obj[k - 1];
            if (g_sel_flags) g_sel_flags[k] = g_sel_flags[k - 1];
            if (g_obj_lock) g_obj_lock[k] = g_obj_lock[k - 1];
            if (g_obj_hidden) g_obj_hidden[k] = g_obj_hidden[k - 1];
        }
    }

    g_obj[dst] = tmp;
    if (g_sel_flags) g_sel_flags[dst] = tmp_sel;
    if (g_obj_lock) g_obj_lock[dst] = tmp_lock;
    if (g_obj_hidden) g_obj_hidden[dst] = tmp_hidden;
    return 1;
}

int editor_project_sort_objects_by_layer_order(void)
{
    if (!editor_project_storage_init() || !g_obj) return 0;
    for (int i = 1; i < g_no; i++) {
        Obj tmp = g_obj[i];
        int tmp_sel = g_sel_flags ? g_sel_flags[i] : 0;
        int tmp_lock = g_obj_lock ? g_obj_lock[i] : 0;
        int tmp_hidden = g_obj_hidden ? g_obj_hidden[i] : 0;
        int key = (tmp.wx >> 8) & 0xFF;
        int j = i - 1;
        while (j >= 0 && (((g_obj[j].wx >> 8) & 0xFF) > key ||
                          (((g_obj[j].wx >> 8) & 0xFF) == key &&
                           g_obj[j].order > tmp.order))) {
            g_obj[j + 1] = g_obj[j];
            if (g_sel_flags) g_sel_flags[j + 1] = g_sel_flags[j];
            if (g_obj_lock) g_obj_lock[j + 1] = g_obj_lock[j];
            if (g_obj_hidden) g_obj_hidden[j + 1] = g_obj_hidden[j];
            j--;
        }
        g_obj[j + 1] = tmp;
        if (g_sel_flags) g_sel_flags[j + 1] = tmp_sel;
        if (g_obj_lock) g_obj_lock[j + 1] = tmp_lock;
        if (g_obj_hidden) g_obj_hidden[j + 1] = tmp_hidden;
    }
    return 1;
}

int editor_project_reserve_modules(int min_capacity)
{
    EditorProjectStorage *s;
    char (*next_modules)[256];
    int new_capacity;
    if (!editor_project_storage_init()) return 0;
    s = g_project_storage;
    if (min_capacity <= s->module_capacity) return 1;
    new_capacity = editor_project_next_capacity(s->module_capacity, min_capacity);
    next_modules = (char (*)[256])calloc((size_t)new_capacity, sizeof(next_modules[0]));
    if (!next_modules) return 0;
    memcpy(next_modules, s->bdb_modules, (size_t)s->module_capacity * sizeof(next_modules[0]));
    free(s->bdb_modules);
    s->bdb_modules = next_modules;
    s->module_capacity = new_capacity;
    editor_project_storage_refresh_globals(s);
    return 1;
}

int editor_project_append_module_line(const char *line)
{
    if (!line || !editor_project_reserve_modules(g_bdb_num_modules + 1) || !g_bdb_modules)
        return 0;
    if (g_bdb_num_modules < 0 || g_bdb_num_modules >= editor_project_module_capacity())
        return 0;
    snprintf(g_bdb_modules[g_bdb_num_modules], sizeof g_bdb_modules[g_bdb_num_modules], "%s", line);
    g_bdb_num_modules++;
    return 1;
}

int editor_project_delete_module_line(int module_i)
{
    if (!editor_project_storage_init() || !g_bdb_modules) return 0;
    if (module_i < 0 || module_i >= g_bdb_num_modules) return 0;
    for (int j = module_i; j < g_bdb_num_modules - 1; j++)
        snprintf(g_bdb_modules[j], sizeof g_bdb_modules[j], "%s", g_bdb_modules[j + 1]);
    if (g_bdb_num_modules > 0) {
        g_bdb_num_modules--;
        memset(g_bdb_modules[g_bdb_num_modules], 0, sizeof g_bdb_modules[g_bdb_num_modules]);
    }
    if (g_bdb_num_modules < 0) g_bdb_num_modules = 0;
    return 1;
}

int editor_project_set_module_line(int module_i, const char *line)
{
    if (!line || !editor_project_storage_init() || !g_bdb_modules) return 0;
    if (module_i < 0 || module_i >= g_bdb_num_modules) return 0;
    snprintf(g_bdb_modules[module_i], sizeof g_bdb_modules[module_i], "%s", line);
    return 1;
}

int editor_project_set_single_module_line(const char *line)
{
    if (!line || !editor_project_reserve_modules(1) || !g_bdb_modules) return 0;
    memset(g_bdb_modules, 0, (size_t)editor_project_module_capacity() * sizeof g_bdb_modules[0]);
    snprintf(g_bdb_modules[0], sizeof g_bdb_modules[0], "%s", line);
    g_bdb_num_modules = 1;
    return 1;
}

int editor_project_reserve_palettes(int min_capacity)
{
    EditorProjectStorage *s;
    Uint32 (*next_pals)[256];
    int *next_count;
    char (*next_name)[64];
    Uint16 (*next_rgb555)[256];
    Uint32 (*next_argb_snapshot)[256];
    int *next_rgb555_valid;
    int *next_rgb555_count;
    char (*next_rgb555_name)[64];
    int new_capacity;
    if (!editor_project_storage_init()) return 0;
    s = g_project_storage;
    if (min_capacity <= s->palette_capacity) return 1;
    new_capacity = editor_project_next_capacity(s->palette_capacity, min_capacity);
    next_pals = (Uint32 (*)[256])calloc((size_t)new_capacity, sizeof(next_pals[0]));
    next_count = (int *)calloc((size_t)new_capacity, sizeof(next_count[0]));
    next_name = (char (*)[64])calloc((size_t)new_capacity, sizeof(next_name[0]));
    next_rgb555 = (Uint16 (*)[256])calloc((size_t)new_capacity, sizeof(next_rgb555[0]));
    next_argb_snapshot = (Uint32 (*)[256])calloc((size_t)new_capacity, sizeof(next_argb_snapshot[0]));
    next_rgb555_valid = (int *)calloc((size_t)new_capacity, sizeof(next_rgb555_valid[0]));
    next_rgb555_count = (int *)calloc((size_t)new_capacity, sizeof(next_rgb555_count[0]));
    next_rgb555_name = (char (*)[64])calloc((size_t)new_capacity, sizeof(next_rgb555_name[0]));
    if (!next_pals || !next_count || !next_name || !next_rgb555 || !next_argb_snapshot ||
        !next_rgb555_valid || !next_rgb555_count || !next_rgb555_name) {
        free(next_pals);
        free(next_count);
        free(next_name);
        free(next_rgb555);
        free(next_argb_snapshot);
        free(next_rgb555_valid);
        free(next_rgb555_count);
        free(next_rgb555_name);
        return 0;
    }
    memcpy(next_pals, s->pals, (size_t)s->palette_capacity * sizeof(next_pals[0]));
    memcpy(next_count, s->pal_count, (size_t)s->palette_capacity * sizeof(next_count[0]));
    memcpy(next_name, s->pal_name, (size_t)s->palette_capacity * sizeof(next_name[0]));
    memcpy(next_rgb555, s->pal_rgb555, (size_t)s->palette_capacity * sizeof(next_rgb555[0]));
    memcpy(next_argb_snapshot, s->pal_argb_snapshot, (size_t)s->palette_capacity * sizeof(next_argb_snapshot[0]));
    memcpy(next_rgb555_valid, s->pal_rgb555_valid, (size_t)s->palette_capacity * sizeof(next_rgb555_valid[0]));
    memcpy(next_rgb555_count, s->pal_rgb555_count, (size_t)s->palette_capacity * sizeof(next_rgb555_count[0]));
    memcpy(next_rgb555_name, s->pal_rgb555_name, (size_t)s->palette_capacity * sizeof(next_rgb555_name[0]));
    free(s->pals);
    free(s->pal_count);
    free(s->pal_name);
    free(s->pal_rgb555);
    free(s->pal_argb_snapshot);
    free(s->pal_rgb555_valid);
    free(s->pal_rgb555_count);
    free(s->pal_rgb555_name);
    s->pals = next_pals;
    s->pal_count = next_count;
    s->pal_name = next_name;
    s->pal_rgb555 = next_rgb555;
    s->pal_argb_snapshot = next_argb_snapshot;
    s->pal_rgb555_valid = next_rgb555_valid;
    s->pal_rgb555_count = next_rgb555_count;
    s->pal_rgb555_name = next_rgb555_name;
    s->palette_capacity = new_capacity;
    editor_project_storage_refresh_globals(s);
    return 1;
}

int editor_project_append_palette_slot(const char *name, int count, const Uint32 *colors)
{
    int pi;
    if (!editor_project_reserve_palettes(g_n_pals + 1)) return -1;
    if (!g_pals || !g_pal_count || !g_pal_name) return -1;
    if (g_n_pals < 0 || g_n_pals >= editor_project_palette_capacity()) return -1;
    if (count < 0) count = 0;
    if (count > 256) count = 256;
    pi = g_n_pals++;
    memset(g_pals[pi], 0, sizeof g_pals[pi]);
    if (colors) memcpy(g_pals[pi], colors, sizeof g_pals[pi]);
    g_pal_count[pi] = count;
    snprintf(g_pal_name[pi], sizeof g_pal_name[pi], "%s",
             (name && name[0]) ? name : "PAL");
    if (g_pal_rgb555) memset(g_pal_rgb555[pi], 0, sizeof g_pal_rgb555[pi]);
    if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[pi], 0, sizeof g_pal_argb_snapshot[pi]);
    if (g_pal_rgb555_valid) g_pal_rgb555_valid[pi] = 0;
    if (g_pal_rgb555_count) g_pal_rgb555_count[pi] = 0;
    if (g_pal_rgb555_name) memset(g_pal_rgb555_name[pi], 0, sizeof g_pal_rgb555_name[pi]);
    return pi;
}

int editor_project_set_palette_slot(int pal_i, const char *name, int count, const Uint32 *colors)
{
    if (!editor_project_storage_init()) return 0;
    if (pal_i < 0 || pal_i >= g_n_pals || pal_i >= editor_project_palette_capacity()) return 0;
    if (!g_pals || !g_pal_count || !g_pal_name) return 0;
    if (count < 0) count = 0;
    if (count > 256) count = 256;
    if (colors) {
        memset(g_pals[pal_i], 0, sizeof g_pals[pal_i]);
        memcpy(g_pals[pal_i], colors, sizeof g_pals[pal_i]);
    }
    g_pal_count[pal_i] = count;
    if (name)
        snprintf(g_pal_name[pal_i], sizeof g_pal_name[pal_i], "%s", name[0] ? name : "PAL");
    if (g_pal_rgb555) memset(g_pal_rgb555[pal_i], 0, sizeof g_pal_rgb555[pal_i]);
    if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[pal_i], 0, sizeof g_pal_argb_snapshot[pal_i]);
    if (g_pal_rgb555_valid) g_pal_rgb555_valid[pal_i] = 0;
    if (g_pal_rgb555_count) g_pal_rgb555_count[pal_i] = 0;
    if (g_pal_rgb555_name) memset(g_pal_rgb555_name[pal_i], 0, sizeof g_pal_rgb555_name[pal_i]);
    return 1;
}

int editor_project_set_palette_color(int pal_i, int color_i, Uint32 color)
{
    if (!editor_project_storage_init()) return 0;
    if (pal_i < 0 || pal_i >= g_n_pals || pal_i >= editor_project_palette_capacity()) return 0;
    if (color_i < 0 || color_i >= 256) return 0;
    if (!g_pals) return 0;
    g_pals[pal_i][color_i] = color;
    if (g_pal_rgb555) memset(g_pal_rgb555[pal_i], 0, sizeof g_pal_rgb555[pal_i]);
    if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[pal_i], 0, sizeof g_pal_argb_snapshot[pal_i]);
    if (g_pal_rgb555_valid) g_pal_rgb555_valid[pal_i] = 0;
    if (g_pal_rgb555_count) g_pal_rgb555_count[pal_i] = 0;
    if (g_pal_rgb555_name) memset(g_pal_rgb555_name[pal_i], 0, sizeof g_pal_rgb555_name[pal_i]);
    return 1;
}

int editor_project_rotate_palette_range(int pal_i, int lo, int hi)
{
    Uint32 tmp;
    if (!editor_project_storage_init()) return 0;
    if (pal_i < 0 || pal_i >= g_n_pals || pal_i >= editor_project_palette_capacity()) return 0;
    if (lo < 0) lo = 0;
    if (hi > 255) hi = 255;
    if (hi <= lo) return 0;
    if (!g_pals) return 0;
    tmp = g_pals[pal_i][hi];
    memmove(&g_pals[pal_i][lo + 1], &g_pals[pal_i][lo],
            (size_t)(hi - lo) * sizeof g_pals[pal_i][0]);
    g_pals[pal_i][lo] = tmp;
    if (g_pal_rgb555) memset(g_pal_rgb555[pal_i], 0, sizeof g_pal_rgb555[pal_i]);
    if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[pal_i], 0, sizeof g_pal_argb_snapshot[pal_i]);
    if (g_pal_rgb555_valid) g_pal_rgb555_valid[pal_i] = 0;
    if (g_pal_rgb555_count) g_pal_rgb555_count[pal_i] = 0;
    if (g_pal_rgb555_name) memset(g_pal_rgb555_name[pal_i], 0, sizeof g_pal_rgb555_name[pal_i]);
    return 1;
}

int editor_project_truncate_palettes(int palette_count)
{
    int old_count;
    int pal_cap;
    if (!editor_project_storage_init()) return 0;
    pal_cap = editor_project_palette_capacity();
    if (palette_count < 0) palette_count = 0;
    if (palette_count > pal_cap) palette_count = pal_cap;
    old_count = g_n_pals;
    if (old_count < 0) old_count = 0;
    if (old_count > pal_cap) old_count = pal_cap;
    for (int p = palette_count; p < old_count; p++) {
        memset(g_pals[p], 0, sizeof g_pals[p]);
        g_pal_count[p] = 0;
        memset(g_pal_name[p], 0, sizeof g_pal_name[p]);
        if (g_pal_rgb555) memset(g_pal_rgb555[p], 0, sizeof g_pal_rgb555[p]);
        if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[p], 0, sizeof g_pal_argb_snapshot[p]);
        if (g_pal_rgb555_valid) g_pal_rgb555_valid[p] = 0;
        if (g_pal_rgb555_count) g_pal_rgb555_count[p] = 0;
        if (g_pal_rgb555_name) memset(g_pal_rgb555_name[p], 0, sizeof g_pal_rgb555_name[p]);
    }
    g_n_pals = palette_count;
    return 1;
}

int editor_project_delete_palette_slot(int pal_i)
{
    int pal_cap;
    if (!editor_project_storage_init()) return 0;
    pal_cap = editor_project_palette_capacity();
    if (pal_i < 0 || pal_i >= g_n_pals || pal_i >= pal_cap) return 0;
    for (int j = pal_i; j < g_n_pals - 1 && j + 1 < pal_cap; j++) {
        memcpy(g_pals[j], g_pals[j + 1], sizeof g_pals[j]);
        g_pal_count[j] = g_pal_count[j + 1];
        snprintf(g_pal_name[j], sizeof g_pal_name[j], "%s", g_pal_name[j + 1]);
        if (g_pal_rgb555) memcpy(g_pal_rgb555[j], g_pal_rgb555[j + 1], sizeof g_pal_rgb555[j]);
        if (g_pal_argb_snapshot) memcpy(g_pal_argb_snapshot[j], g_pal_argb_snapshot[j + 1], sizeof g_pal_argb_snapshot[j]);
        if (g_pal_rgb555_valid) g_pal_rgb555_valid[j] = g_pal_rgb555_valid[j + 1];
        if (g_pal_rgb555_count) g_pal_rgb555_count[j] = g_pal_rgb555_count[j + 1];
        if (g_pal_rgb555_name)
            snprintf(g_pal_rgb555_name[j], sizeof g_pal_rgb555_name[j], "%s", g_pal_rgb555_name[j + 1]);
    }
    if (g_n_pals > 0) {
        g_n_pals--;
        memset(g_pals[g_n_pals], 0, sizeof g_pals[g_n_pals]);
        g_pal_count[g_n_pals] = 0;
        memset(g_pal_name[g_n_pals], 0, sizeof g_pal_name[g_n_pals]);
        if (g_pal_rgb555) memset(g_pal_rgb555[g_n_pals], 0, sizeof g_pal_rgb555[g_n_pals]);
        if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot[g_n_pals], 0, sizeof g_pal_argb_snapshot[g_n_pals]);
        if (g_pal_rgb555_valid) g_pal_rgb555_valid[g_n_pals] = 0;
        if (g_pal_rgb555_count) g_pal_rgb555_count[g_n_pals] = 0;
        if (g_pal_rgb555_name) memset(g_pal_rgb555_name[g_n_pals], 0, sizeof g_pal_rgb555_name[g_n_pals]);
    }
    if (g_n_pals < 0) g_n_pals = 0;
    return 1;
}

int editor_project_replace_objects(const Obj *objects, int object_count, int source_capacity,
                                   const int *locks, const int *hidden, int include_flags)
{
    int object_cap;
    int copy_objects;
    int copy_flags;
    if (!objects || !editor_project_reserve_objects(object_count)) return 0;
    object_cap = editor_project_object_capacity();
    if (object_cap <= 0 || !g_obj) return 0;
    if (source_capacity < 0) source_capacity = 0;
    copy_objects = object_count;
    if (copy_objects < 0) copy_objects = 0;
    if (copy_objects > source_capacity) copy_objects = source_capacity;
    if (copy_objects > object_cap) copy_objects = object_cap;
    memset(g_obj, 0, (size_t)object_cap * sizeof g_obj[0]);
    memcpy(g_obj, objects, (size_t)copy_objects * sizeof g_obj[0]);
    g_no = copy_objects;

    if (include_flags && locks && hidden && g_obj_lock && g_obj_hidden) {
        copy_flags = source_capacity;
        if (copy_flags < 0) copy_flags = 0;
        if (copy_flags > object_cap) copy_flags = object_cap;
        memset(g_obj_lock, 0, (size_t)object_cap * sizeof g_obj_lock[0]);
        memset(g_obj_hidden, 0, (size_t)object_cap * sizeof g_obj_hidden[0]);
        memcpy(g_obj_lock, locks, (size_t)copy_flags * sizeof g_obj_lock[0]);
        memcpy(g_obj_hidden, hidden, (size_t)copy_flags * sizeof g_obj_hidden[0]);
    }
    return 1;
}

int editor_project_replace_object_rows(const Obj *objects, int object_count, int source_capacity,
                                       const int *locks, const int *hidden, const int *selection)
{
    int object_cap;
    int copy_objects;
    if (!objects || !editor_project_reserve_objects(object_count)) return 0;
    object_cap = editor_project_object_capacity();
    if (object_cap <= 0 || !g_obj) return 0;
    if (source_capacity < 0) source_capacity = 0;
    copy_objects = object_count;
    if (copy_objects < 0) copy_objects = 0;
    if (copy_objects > source_capacity) copy_objects = source_capacity;
    if (copy_objects > object_cap) copy_objects = object_cap;
    memset(g_obj, 0, (size_t)object_cap * sizeof g_obj[0]);
    memcpy(g_obj, objects, (size_t)copy_objects * sizeof g_obj[0]);
    g_no = copy_objects;
    if (g_obj_lock) {
        memset(g_obj_lock, 0, (size_t)object_cap * sizeof g_obj_lock[0]);
        if (locks) memcpy(g_obj_lock, locks, (size_t)copy_objects * sizeof g_obj_lock[0]);
    }
    if (g_obj_hidden) {
        memset(g_obj_hidden, 0, (size_t)object_cap * sizeof g_obj_hidden[0]);
        if (hidden) memcpy(g_obj_hidden, hidden, (size_t)copy_objects * sizeof g_obj_hidden[0]);
    }
    if (g_sel_flags) {
        memset(g_sel_flags, 0, (size_t)object_cap * sizeof g_sel_flags[0]);
        if (selection) memcpy(g_sel_flags, selection, (size_t)copy_objects * sizeof g_sel_flags[0]);
    }
    return 1;
}

int editor_project_replace_palettes(const Uint32 (*pals)[256], const int *counts,
                                    const char (*names)[64], int palette_count, int source_capacity)
{
    int pal_cap;
    int copy_pals;
    if (!pals || !counts || !names || !editor_project_reserve_palettes(palette_count)) return 0;
    pal_cap = editor_project_palette_capacity();
    if (pal_cap <= 0 || !g_pals || !g_pal_count || !g_pal_name) return 0;
    if (source_capacity < 0) source_capacity = 0;
    copy_pals = source_capacity;
    if (copy_pals > pal_cap) copy_pals = pal_cap;
    if (palette_count < 0) palette_count = 0;
    if (palette_count > copy_pals) palette_count = copy_pals;
    memset(g_pals, 0, (size_t)pal_cap * sizeof g_pals[0]);
    memset(g_pal_count, 0, (size_t)pal_cap * sizeof g_pal_count[0]);
    memset(g_pal_name, 0, (size_t)pal_cap * sizeof g_pal_name[0]);
    if (g_pal_rgb555) memset(g_pal_rgb555, 0, (size_t)pal_cap * sizeof g_pal_rgb555[0]);
    if (g_pal_argb_snapshot) memset(g_pal_argb_snapshot, 0, (size_t)pal_cap * sizeof g_pal_argb_snapshot[0]);
    if (g_pal_rgb555_valid) memset(g_pal_rgb555_valid, 0, (size_t)pal_cap * sizeof g_pal_rgb555_valid[0]);
    if (g_pal_rgb555_count) memset(g_pal_rgb555_count, 0, (size_t)pal_cap * sizeof g_pal_rgb555_count[0]);
    if (g_pal_rgb555_name) memset(g_pal_rgb555_name, 0, (size_t)pal_cap * sizeof g_pal_rgb555_name[0]);
    memcpy(g_pals, pals, (size_t)copy_pals * sizeof g_pals[0]);
    memcpy(g_pal_count, counts, (size_t)copy_pals * sizeof g_pal_count[0]);
    memcpy(g_pal_name, names, (size_t)copy_pals * sizeof g_pal_name[0]);
    g_n_pals = palette_count;
    return 1;
}

int editor_project_replace_modules(const char (*modules)[256], int module_count, int source_capacity)
{
    int module_cap;
    int copy_modules;
    if (!modules || !editor_project_reserve_modules(module_count)) return 0;
    module_cap = editor_project_module_capacity();
    if (module_cap <= 0 || !g_bdb_modules) return 0;
    if (source_capacity < 0) source_capacity = 0;
    copy_modules = source_capacity;
    if (copy_modules > module_cap) copy_modules = module_cap;
    if (module_count < 0) module_count = 0;
    if (module_count > copy_modules) module_count = copy_modules;
    memset(g_bdb_modules, 0, (size_t)module_cap * sizeof g_bdb_modules[0]);
    memcpy(g_bdb_modules, modules, (size_t)copy_modules * sizeof g_bdb_modules[0]);
    g_bdb_num_modules = module_count;
    return 1;
}

void editor_project_clear_palette_rgb555_cache(void)
{
    int pal_cap;
    if (!editor_project_storage_init()) return;
    pal_cap = editor_project_palette_capacity();
    if (pal_cap <= 0) return;
    memset(g_pal_rgb555, 0, (size_t)pal_cap * sizeof g_pal_rgb555[0]);
    memset(g_pal_argb_snapshot, 0, (size_t)pal_cap * sizeof g_pal_argb_snapshot[0]);
    memset(g_pal_rgb555_valid, 0, (size_t)pal_cap * sizeof g_pal_rgb555_valid[0]);
    memset(g_pal_rgb555_count, 0, (size_t)pal_cap * sizeof g_pal_rgb555_count[0]);
    memset(g_pal_rgb555_name, 0, (size_t)pal_cap * sizeof g_pal_rgb555_name[0]);
}

static void palette_raw_cache_capture(int pal_idx)
{
    if (pal_idx < 0 || pal_idx >= editor_project_palette_capacity()) return;
    g_pal_rgb555_valid[pal_idx] = 1;
    g_pal_rgb555_count[pal_idx] = g_pal_count[pal_idx];
    snprintf(g_pal_rgb555_name[pal_idx], sizeof g_pal_rgb555_name[pal_idx],
             "%s", g_pal_name[pal_idx]);
    memcpy(g_pal_argb_snapshot[pal_idx], g_pals[pal_idx], sizeof g_pals[pal_idx]);
}

static int palette_raw_cache_matches_current(int pal_idx)
{
    if (pal_idx < 0 || pal_idx >= editor_project_palette_capacity()) return 0;
    if (!g_pal_rgb555_valid[pal_idx]) return 0;
    if (g_pal_rgb555_count[pal_idx] != g_pal_count[pal_idx]) return 0;
    if (strcmp(g_pal_rgb555_name[pal_idx], g_pal_name[pal_idx]) != 0) return 0;
    return memcmp(g_pal_argb_snapshot[pal_idx], g_pals[pal_idx],
                  sizeof g_pals[pal_idx]) == 0;
}

int editor_project_set_palette_rgb555_cache(int pal_idx, const Uint16 *rgb555, int count)
{
    if (!editor_project_storage_init()) return 0;
    if (!rgb555 || pal_idx < 0 || pal_idx >= g_n_pals ||
        pal_idx >= editor_project_palette_capacity()) return 0;
    if (!g_pal_rgb555 || !g_pal_rgb555_valid || !g_pal_rgb555_count ||
        !g_pal_rgb555_name || !g_pal_argb_snapshot) return 0;
    if (count < 0) count = 0;
    if (count > 256) count = 256;
    memset(g_pal_rgb555[pal_idx], 0, sizeof g_pal_rgb555[pal_idx]);
    memcpy(g_pal_rgb555[pal_idx], rgb555, (size_t)count * sizeof g_pal_rgb555[pal_idx][0]);
    palette_raw_cache_capture(pal_idx);
    return 1;
}

int editor_project_get_palette_rgb555_cache(int pal_idx, Uint16 *out_rgb555, int max_count)
{
    int copy_count;
    if (!out_rgb555 || max_count <= 0) return 0;
    if (!editor_project_storage_init()) return 0;
    if (pal_idx < 0 || pal_idx >= g_n_pals ||
        pal_idx >= editor_project_palette_capacity()) return 0;
    if (!g_pal_rgb555 || !palette_raw_cache_matches_current(pal_idx)) return 0;
    copy_count = g_pal_count[pal_idx];
    if (copy_count < 0) copy_count = 0;
    if (copy_count > max_count) copy_count = max_count;
    if (copy_count > 256) copy_count = 256;
    memcpy(out_rgb555, g_pal_rgb555[pal_idx], (size_t)copy_count * sizeof out_rgb555[0]);
    return 1;
}
