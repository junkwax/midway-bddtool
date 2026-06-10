#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "UI/tools/mk2_runtime_actor_tool.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define mk2_runtime_backend_strcasecmp _stricmp
#else
#include <strings.h>
#define mk2_runtime_backend_strcasecmp strcasecmp
#endif
#define strcasecmp mk2_runtime_backend_strcasecmp
static int runtime_guide_palette(void)
{
    for (int p = 0; p < g_n_pals; p++)
        if (strcasecmp(g_pal_name[p], "RTGUIDE") == 0) return p;
    Uint32 colors[256] = {};
    colors[0] = 0;
    colors[1] = 0xFFD27D46;
    colors[2] = 0xFFFF5A28;
    colors[3] = 0xFF8CB0E0;
    colors[4] = 0xFF70D27D;
    colors[5] = 0xFFB987E6;
    colors[6] = 0xFF11131A;
    colors[7] = 0xFFFFFFFF;
    int pi = editor_project_append_palette_slot("RTGUIDE", 8, colors);
    return pi >= 0 ? pi : ((g_n_pals > 0) ? 0 : -1);
}

void runtime_guide_visual_rect(const RuntimeExtraGuide *e, const Img *im,
                                      int *x, int *y, int *w, int *h)
{
    int iw = (im && im->w > 0) ? im->w : (e ? e->w : 0);
    int ih = (im && im->h > 0) ? im->h : (e ? e->h : 0);
    int ix = e ? e->x : 0;
    int iy = e ? e->y : 0;
    if (im) {
        ix -= img_anim_offset_x(im, e ? e->hfl : 0);
        iy -= img_anim_offset_y(im, 0);
    }
    if (x) *x = ix;
    if (y) *y = iy;
    if (w) *w = iw;
    if (h) *h = ih;
}

static int make_runtime_guide_image(const RuntimeExtraGuide *e)
{
    if (!e) return -1;
    int pi = runtime_guide_palette();
    if (pi < 0) return -1;
    int max_idx = 0;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;
    Img *im = editor_project_append_image_slot();
    if (!im) return -1;
    im->idx = max_idx + 1;
    im->w = e->w > 1 ? e->w : 1;
    im->h = e->h > 1 ? e->h : 1;
    im->pal_idx = pi;
    snprintf(im->label, sizeof im->label, "%.31s_GUIDE", e->asset);
    snprintf(im->source, sizeof im->source, "%.31s", e->source);
    im->pix = (Uint8 *)malloc((size_t)im->w * (size_t)im->h);
    if (!im->pix) {
        editor_project_delete_image_slot(g_ni - 1);
        return -1;
    }
    int fill = 1;
    if (strstr(e->asset, "Flame")) fill = 2;
    else if (strstr(e->asset, "cloud")) fill = 3;
    else if (strstr(e->asset, "FL_")) fill = 4;
    else if (strstr(e->asset, "monk")) fill = 5;
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            int edge = x < 2 || y < 2 || x >= im->w - 2 || y >= im->h - 2;
            int grid = (x % 24 == 0) || (y % 24 == 0);
            im->pix[y * im->w + x] = (Uint8)((edge || grid) ? 7 : fill);
        }
    }
    return g_ni - 1;
}

static int runtime_max_image_idx(void)
{
    int max_idx = 0;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;
    return max_idx;
}

static const char *const *runtime_floor_piece_labels(const char *asset, int *out_count)
{
    static const char *const tower[] = {
        "CASFLOOR1", "CASFLOOR2", "CASFLOOR3",
        "CASFLOOR4", "CASFLOOR5", "CASFLOOR6"
    };
    static const char *const forest[] = {
        "FORFLOR1", "FORFLOR2", "FORFLOR3",
        "FORFLOR4", "FORFLOR5", "FORFLOR6"
    };
    static const char *const battle[] = {
        "BATGRND1", "BATGRND2", "BATGRND3",
        "BATGRND4", "BATGRND5", "BATGRND6"
    };
    if (out_count) *out_count = 0;
    if (!asset) return NULL;
    if (strcasecmp(asset, "FL_TOW") == 0) {
        if (out_count) *out_count = (int)(sizeof tower / sizeof tower[0]);
        return tower;
    }
    if (strcasecmp(asset, "FL_FORST") == 0) {
        if (out_count) *out_count = (int)(sizeof forest / sizeof forest[0]);
        return forest;
    }
    if (strcasecmp(asset, "FL_BATTL") == 0) {
        if (out_count) *out_count = (int)(sizeof battle / sizeof battle[0]);
        return battle;
    }
    return NULL;
}

static bool runtime_floor_write_composite_pixels(Uint8 *dst, int dst_w, int dst_h,
                                                 const int *piece_idx, int count)
{
    int dst_x = 0;
    if (!dst || dst_w <= 0 || dst_h <= 0 || !piece_idx || count <= 0)
        return false;

    memset(dst, 0, (size_t)dst_w * (size_t)dst_h);
    while (dst_x < dst_w) {
        bool wrote = false;
        for (int i = 0; i < count && dst_x < dst_w; i++) {
            Img *piece = &g_img[piece_idx[i]];
            int copy_w;
            if (!piece->pix || piece->w <= 0 || piece->h <= 0)
                return false;
            copy_w = piece->w;
            if (dst_x + copy_w > dst_w)
                copy_w = dst_w - dst_x;
            for (int y = 0; y < piece->h && y < dst_h; y++) {
                memcpy(dst + (size_t)y * (size_t)dst_w + (size_t)dst_x,
                       piece->pix + (size_t)y * (size_t)piece->w,
                       (size_t)copy_w);
            }
            dst_x += copy_w;
            wrote = true;
        }
        if (!wrote)
            return false;
    }
    return true;
}

static int runtime_floor_desired_width(const RuntimeExtraGuide *e, int source_w)
{
    int desired = source_w;
    if (e && e->w > desired)
        desired = e->w;
    return desired;
}

static bool runtime_floor_replace_preview_image(int img_i, const RuntimeExtraGuide *e,
                                                int out_w, int out_h,
                                                int pal_idx,
                                                const int *piece_idx, int count)
{
    Uint8 *pix;
    Img *im;

    if (img_i < 0 || img_i >= g_ni || out_w <= 0 || out_h <= 0)
        return false;
    im = &g_img[img_i];
    if (!runtime_actor_image_is_preview_import(im))
        return false;

    pix = (Uint8 *)malloc((size_t)out_w * (size_t)out_h);
    if (!pix)
        return false;
    if (!runtime_floor_write_composite_pixels(pix, out_w, out_h, piece_idx, count)) {
        free(pix);
        return false;
    }

    free(im->pix);
    im->pix = pix;
    im->w = out_w;
    im->h = out_h;
    im->pal_idx = pal_idx;
    im->lod_ref = 1;
    snprintf(im->label, sizeof im->label, "%.63s", e->asset);
    runtime_actor_mark_preview_import_range(img_i, -1, img_i, img_i + 1, "MK7MIL.LOD");
    g_need_rebuild = 1;
    g_view_changed = 1;
    return true;
}

static int ensure_runtime_floor_composite(const RuntimeExtraGuide *e)
{
    int count = 0;
    const char *const *labels = runtime_floor_piece_labels(e ? e->asset : NULL, &count);
    if (!e || !labels || count <= 0) return -1;

    int piece_idx[8] = {};
    int total_w = 0;
    int max_h = 0;
    int pal_idx = -1;
    for (int i = 0; i < count; i++) {
        piece_idx[i] = find_img_by_label_casefold(labels[i]);
        if (piece_idx[i] < 0 || piece_idx[i] >= g_ni)
            return -1;
        Img *piece = &g_img[piece_idx[i]];
        if (!piece->pix || piece->w <= 0 || piece->h <= 0)
            return -1;
        total_w += piece->w;
        if (piece->h > max_h) max_h = piece->h;
        if (pal_idx < 0) pal_idx = piece->pal_idx;
    }
    if (total_w <= 0 || max_h <= 0 || pal_idx < 0)
        return -1;

    int out_w = runtime_floor_desired_width(e, total_w);
    int out_h = max_h;
    int existing = find_img_by_label_casefold(e->asset);
    if (existing >= 0) {
        Img *im = &g_img[existing];
        if (im->pix && im->w == out_w && im->h == out_h)
            return existing;
        if (runtime_floor_replace_preview_image(existing, e, out_w, out_h,
                                                pal_idx, piece_idx, count))
            return existing;
        return existing;
    }

    Img *im = editor_project_append_image_slot();
    if (!im) return -1;
    im->idx = runtime_max_image_idx() + 1;
    im->w = out_w;
    im->h = out_h;
    im->pal_idx = pal_idx;
    im->lod_ref = 1;
    snprintf(im->label, sizeof im->label, "%.63s", e->asset);
    snprintf(im->source, sizeof im->source, "MK7MIL.LOD");
    im->pix = (Uint8 *)malloc((size_t)im->w * (size_t)im->h);
    if (!im->pix) {
        editor_project_delete_image_slot(g_ni - 1);
        return -1;
    }
    if (!runtime_floor_write_composite_pixels(im->pix, im->w, im->h,
                                              piece_idx, count)) {
        editor_project_delete_image_slot(g_ni - 1);
        return -1;
    }
    runtime_actor_mark_preview_import_range(g_ni - 1, -1, g_ni - 1, g_ni, "MK7MIL.LOD");
    return g_ni - 1;
}

static bool runtime_guide_same_key(const RuntimeExtraGuide *a, const RuntimeExtraGuide *b)
{
    if (!a || !b) return false;
    return strcasecmp(a->asset, b->asset) == 0 &&
           a->layer == b->layer &&
           a->hfl == b->hfl;
}

bool object_matches_runtime_guide(const Obj *o, const RuntimeExtraGuide *e)
{
    if (!o || !e) return false;
    if (((o->wx >> 8) & 0xFF) != e->layer) return false;
    if ((o->hfl ? 1 : 0) != (e->hfl ? 1 : 0)) return false;
    Img *im = img_find(o->ii);
    return im && im->label[0] && strcasecmp(im->label, e->asset) == 0;
}

static int runtime_guide_occurrence_wanted(int guide_index)
{
    const RuntimeExtraGuide *cur = &g_tower_runtime_guides[guide_index];
    int wanted = 0;
    for (int i = 0; i <= guide_index; i++) {
        if (runtime_guide_same_key(&g_tower_runtime_guides[i], cur))
            wanted++;
    }
    return wanted;
}

int runtime_guide_existing_object_count(const RuntimeExtraGuide *e)
{
    int count = 0;
    for (int i = 0; i < g_no; i++) {
        if (object_matches_runtime_guide(&g_obj[i], e))
            count++;
    }
    return count;
}

int runtime_guide_existing_object_for_index(int guide_idx)
{
    if (guide_idx < 0 || guide_idx >= tower_runtime_guide_count()) return -1;
    const RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    int wanted = runtime_guide_occurrence_wanted(guide_idx);
    int seen = 0;
    for (int i = 0; i < g_no; i++) {
        if (!object_matches_runtime_guide(&g_obj[i], e)) continue;
        seen++;
        if (seen == wanted) return i;
    }
    return -1;
}

int sync_runtime_guide_object_placement(int guide_idx)
{
    int obj_i = runtime_guide_existing_object_for_index(guide_idx);
    if (obj_i < 0) return 0;
    RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    Img *im = img_find(g_obj[obj_i].ii);
    int obj_x = e->x;
    int obj_y = e->y;
    runtime_guide_visual_rect(e, im, &obj_x, &obj_y, NULL, NULL);
    bool changed = false;
    if (g_obj[obj_i].depth != obj_x) {
        g_obj[obj_i].depth = obj_x;
        changed = true;
    }
    if (g_obj[obj_i].sy != obj_y) {
        g_obj[obj_i].sy = obj_y;
        changed = true;
    }
    int flags = (g_obj[obj_i].wx & 0x00FF & ~0x10) | (e->hfl ? 0x10 : 0);
    int wx = ((e->layer & 0xFF) << 8) | flags;
    if (g_obj[obj_i].wx != wx) {
        g_obj[obj_i].wx = wx;
        g_obj[obj_i].hfl = e->hfl ? 1 : 0;
        changed = true;
    }
    if (!changed) return 0;
    g_sel_flags[obj_i] = 1;
    g_hl_obj = obj_i;
    g_dirty = 1;
    g_view_changed = 1;
    return 1;
}

static void runtime_clear_selection(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap > 0)
        editor_project_clear_selection();
}

int select_runtime_guide_objects(int guide_idx)
{
    if (guide_idx < 0 || guide_idx >= tower_runtime_guide_count()) return 0;
    const RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    runtime_clear_selection();
    g_hl_obj = -1;
    int selected = 0;
    for (int i = 0; i < g_no; i++) {
        if (!object_matches_runtime_guide(&g_obj[i], e)) continue;
        g_sel_flags[i] = 1;
        if (g_hl_obj < 0) g_hl_obj = i;
        selected++;
    }
    if (selected > 0) {
        g_show_objects = true;
        g_tower_runtime_selected = guide_idx;
    }
    return selected;
}

int delete_runtime_guide_objects(int guide_idx)
{
    if (guide_idx < 0 || guide_idx >= tower_runtime_guide_count()) return 0;
    const RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    std::vector<unsigned char> remove((size_t)editor_project_object_capacity(), 0);
    int removed = 0;
    for (int i = 0; i < g_no; i++) {
        if (!object_matches_runtime_guide(&g_obj[i], e)) continue;
        remove[(size_t)i] = 1;
        removed++;
    }
    if (removed <= 0) return 0;

    undo_save_ex("Delete Runtime LOD Placement");
    for (int i = g_no - 1; i >= 0; i--) {
        if (!remove[(size_t)i]) continue;
        mk2_delete_object_preserve_order(i);
    }
    runtime_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    return removed;
}

static bool runtime_guide_image_matches(const Img *im)
{
    if (!im || !im->label[0]) return false;
    int count = tower_runtime_guide_count();
    for (int gi = 0; gi < count; gi++) {
        if (strcasecmp(im->label, g_tower_runtime_guides[gi].asset) == 0)
            return true;
    }
    return false;
}

int delete_all_runtime_guide_objects(bool save_undo)
{
    if (!mk2_current_stage_has_known_runtime_extras()) return 0;
    tower_runtime_guides_init_once();

    std::vector<unsigned char> remove((size_t)editor_project_object_capacity(), 0);
    int removed = 0;
    for (int i = 0; i < g_no; i++) {
        for (int gi = 0; gi < tower_runtime_guide_count(); gi++) {
            if (!object_matches_runtime_guide(&g_obj[i], &g_tower_runtime_guides[gi]))
                continue;
            remove[(size_t)i] = 1;
            removed++;
            break;
        }
    }
    if (removed <= 0) return 0;

    if (save_undo) undo_save_ex("Delete Runtime LOD Placements");
    for (int i = g_no - 1; i >= 0; i--) {
        if (!remove[(size_t)i]) continue;
        mk2_delete_object_preserve_order(i);
    }
    runtime_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    g_view_changed = 1;
    return removed;
}

int delete_runtime_guide_images_and_objects(int *out_objects, int *out_images)
{
    if (out_objects) *out_objects = 0;
    if (out_images) *out_images = 0;
    if (!mk2_current_stage_has_known_runtime_extras()) return 0;
    tower_runtime_guides_init_once();

    std::vector<unsigned char> del_img((size_t)editor_project_image_capacity(), 0);
    int delete_images = 0;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!runtime_guide_image_matches(im)) continue;
        if (!im->lod_ref && !im->source[0]) continue;
        del_img[(size_t)i] = 1;
        delete_images++;
    }

    std::vector<unsigned char> remove_obj((size_t)editor_project_object_capacity(), 0);
    int removed_objects = 0;
    for (int oi = 0; oi < g_no; oi++) {
        bool remove = false;
        for (int gi = 0; gi < tower_runtime_guide_count(); gi++) {
            if (object_matches_runtime_guide(&g_obj[oi], &g_tower_runtime_guides[gi])) {
                remove = true;
                break;
            }
        }
        if (!remove) {
            for (int ii = 0; ii < g_ni; ii++) {
                if (!del_img[(size_t)ii] || g_obj[oi].ii != g_img[ii].idx) continue;
                remove = true;
                break;
            }
        }
        if (!remove) continue;
        remove_obj[(size_t)oi] = 1;
        removed_objects++;
    }

    if (delete_images <= 0 && removed_objects <= 0) return 0;

    undo_save_ex("Strip Runtime Extras");
    for (int oi = g_no - 1; oi >= 0; oi--) {
        if (!remove_obj[(size_t)oi]) continue;
        mk2_delete_object_preserve_order(oi);
    }

    if (delete_images > 0) {
        editor_project_delete_marked_images(del_img.data(), editor_project_image_capacity());
        if (g_place_tool_img >= g_ni) g_place_tool_img = g_ni - 1;
        if (g_tile_img >= g_ni) g_tile_img = g_ni - 1;
        if (g_last_import_img >= g_ni) g_last_import_img = g_ni - 1;
        if (g_block_edit_img >= g_ni) {
            g_block_edit_img = -1;
            g_block_edit_open = false;
        }
    }

    runtime_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_view_changed = 1;
    if (out_objects) *out_objects = removed_objects;
    if (out_images) *out_images = delete_images;
    return removed_objects + delete_images;
}

int hide_runtime_guide_for_session(int guide_idx)
{
    int count = tower_runtime_guide_count();
    if (guide_idx < 0 || guide_idx >= count) return 0;
    for (int i = guide_idx; i < count - 1; i++)
        g_tower_runtime_guides[i] = g_tower_runtime_guides[i + 1];
    memset(&g_tower_runtime_guides[count - 1], 0, sizeof(g_tower_runtime_guides[count - 1]));
    g_tower_runtime_guide_n--;
    if (g_tower_runtime_selected >= g_tower_runtime_guide_n)
        g_tower_runtime_selected = g_tower_runtime_guide_n - 1;
    if (g_tower_runtime_selected < 0 && g_tower_runtime_guide_n > 0)
        g_tower_runtime_selected = 0;
    g_tower_runtime_guides_dirty = true;
    g_view_changed = 1;
    return 1;
}

int hide_all_runtime_guides_for_session(void)
{
    tower_runtime_guides_init_once();
    int hidden = g_tower_runtime_guide_n;
    memset(g_tower_runtime_guides, 0, sizeof(g_tower_runtime_guides));
    g_tower_runtime_guide_n = 0;
    g_tower_runtime_selected = -1;
    g_tower_runtime_guides_dirty = true;
    g_runtime_extras_overlay = false;
    g_runtime_recipe_kind = 0;
    g_tower_runtime_stage_kind = 0;
    g_view_changed = 1;
    return hidden;
}

int mk2_bake_runtime_guides_to_bdb(bool save_undo, bool allow_guide_images)
{
    int object_cap = editor_project_object_capacity();
    if (!mk2_current_stage_has_known_runtime_extras() || g_no >= object_cap) return 0;
    tower_runtime_guides_init_once();
    if (save_undo) undo_save_ex("Bake Runtime Extras");
    if (!g_have_bdb) {
        g_have_bdb = 1;
        snprintf(g_name, sizeof g_name, "RTEXTRA");
        snprintf(g_bdb_header, sizeof g_bdb_header, "RTEXTRA 1203 320 255 1 0 0");
        editor_project_set_single_module_line("RTEXTRA 0 1202 -64 320");
    }

    int added = 0;
    int count = tower_runtime_guide_count();
    int max_order = 0;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;
    runtime_clear_selection();
    bool tried_lod_import = false;
    for (int i = 0; i < count && g_no < object_cap; i++) {
        const RuntimeExtraGuide *e = &g_tower_runtime_guides[i];
        if (runtime_guide_existing_object_count(e) >= runtime_guide_occurrence_wanted(i))
            continue;
        int img_i = find_img_by_label_casefold(e->asset);
        if (img_i < 0)
            img_i = ensure_runtime_floor_composite(e);
        if (img_i < 0 && !tried_lod_import && !runtime_actor_preview_imports_loaded()) {
            import_runtime_lod_sources_for_active_guides(false);
            tried_lod_import = true;
            img_i = find_img_by_label_casefold(e->asset);
            if (img_i < 0)
                img_i = ensure_runtime_floor_composite(e);
        }
        if (img_i < 0 && allow_guide_images) img_i = make_runtime_guide_image(e);
        if (img_i < 0 || img_i >= g_ni) continue;
        Img *im = &g_img[img_i];
        int obj_x = e->x;
        int obj_y = e->y;
        runtime_guide_visual_rect(e, im, &obj_x, &obj_y, NULL, NULL);
        Obj *o = editor_project_append_object_slot();
        if (!o) continue;
        int obj_i = g_no - 1;
        o->wx = (e->layer << 8) | (e->hfl ? 0x10 : 0);
        o->depth = obj_x;
        o->sy = obj_y;
        o->ii = im->idx;
        o->fl = (im->pal_idx >= 0) ? im->pal_idx : 0;
        o->hfl = e->hfl;
        o->vfl = 0;
        o->order = max_order + 1 + added;
        g_sel_flags[obj_i] = 1;
        g_hl_obj = obj_i;
        added++;
    }
    if (added) {
        sync_bdb_header_counts();
        g_dirty = 1;
        g_need_rebuild = 1;
        g_show_images = true;
    }
    return added;
}

static void append_json_string_buf(char *out, size_t outsz, const char *s)
{
    stage_append(out, outsz, "\"");
    for (const char *p = s ? s : ""; *p; p++) {
        if (*p == '"' || *p == '\\') {
            char esc[3] = { '\\', *p, 0 };
            stage_append(out, outsz, esc);
        } else {
            char one[2] = { *p, 0 };
            stage_append(out, outsz, one);
        }
    }
    stage_append(out, outsz, "\"");
}

void mk2_copy_selected_runtime_recipe(void)
{
    char out[8192];
    char line[256];
    out[0] = '\0';
    stage_append(out, sizeof out, "{\n  \"runtime_extras\": [\n");
    int n = 0;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (n > 0) stage_append(out, sizeof out, ",\n");
        stage_append(out, sizeof out, "    { \"name\": ");
        append_json_string_buf(out, sizeof out, im->label[0] ? im->label : "BDD_SPRITE");
        snprintf(line, sizeof line,
                 ", \"ii\": %d, \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"layer\": \"0x%02X\", \"hflip\": %s, \"palette\": %d }",
                 g_obj[i].ii, g_obj[i].depth, g_obj[i].sy, im->w, im->h,
                 (g_obj[i].wx >> 8) & 0xFF, g_obj[i].hfl ? "true" : "false", g_obj[i].fl);
        stage_append(out, sizeof out, line);
        n++;
    }
    stage_append(out, sizeof out, "\n  ]\n}\n");
    ImGui::SetClipboardText(out);
    snprintf(g_toast_msg, sizeof g_toast_msg, n ? "Copied %d selected runtime recipe item(s)" : "No selected objects to export");
    g_toast_timer = 3.0f;
}

