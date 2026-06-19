#include "bg_editor.h"
#include "Core/app_diagnostics.h"
#include "Core/bdd_core.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/image_processing.h"
#include "Core/mk2_analysis.h"
#include "Core/path_utils.h"
#include "Core/project_header.h"
#include "Core/stage_paths.h"
#include "Core/world_module_utils.h"
#include "UI/tools/mk2_budget_relief_suggestions.h"
#include "UI/tools/mk2_runtime_actor_tool.h"
#include "UI/tools/mk2_stage_config.h"
#include "UI/tools/mk2_stage_fx_builder_tool.h"
#include "UI/actions/object_actions.h"
#include "UI/actions/selection_helpers.h"
#include "UI/view/toast_notifications.h"
#include "UI/view/world_view_helpers.h"
#include "undo_manager.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
static void backup_base_dir(char *out, size_t outsz, const char *fallback_path)
{
    if (!out || outsz == 0) return;
    const char *root = g_bdd_path[0] ? g_bdd_path : fallback_path;
    snprintf(out, outsz, "%s", root ? root : "");
    char *sep = strrchr(out, '\\');
    char *slash = strrchr(out, '/');
    if (!sep || (slash && slash > sep)) sep = slash;
    if (sep) sep[1] = '\0';
    else out[0] = '\0';
}

/* ---- MK2 authoring diagnostics ------------------------------------ */

static int load2_estimated_block_bytes(const Img *im, int *out_bpp)
{
    if (!im || im->w <= 0 || im->h <= 0) {
        if (out_bpp) *out_bpp = 0;
        return 0;
    }
    size_t bytes = bdd_core_load2_estimated_block_bytes(im->pix, im->w, im->h, out_bpp);
    return bytes > (size_t)INT_MAX ? INT_MAX : (int)bytes;
}

static void mk2_clear_selection_flags(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap > 0)
        editor_project_clear_selection();
}

static int mk2_object_visible_at_camera(int obj_idx, int camera_x, int camera_y,
                                        int view_w, int view_h,
                                        int min_layer, int max_layer)
{
    if (obj_idx < 0 || obj_idx >= g_no || g_obj_hidden[obj_idx]) return 0;
    Obj *o = &g_obj[obj_idx];
    int layer = (o->wx >> 8) & 0xFF;
    if (layer < min_layer || layer > max_layer) return 0;
    Img *im = img_find(o->ii);
    if (!im || im->w <= 0 || im->h <= 0) return 0;
    if (runtime_actor_image_is_preview_import(im)) return 0;

    int ox = o->depth;
    int oy = o->sy;
    gv_object_origin(obj_idx, &ox, &oy);
    BddCoreObject core_obj;
    memset(&core_obj, 0, sizeof core_obj);
    core_obj.wx = o->wx;
    core_obj.depth = o->depth;
    core_obj.sy = o->sy;
    core_obj.ii = o->ii;
    core_obj.fl = o->fl;
    core_obj.order = o->order;
    return bdd_core_object_visible_at_camera(&core_obj,
                                             im->w, im->h,
                                             ox, oy,
                                             camera_x, camera_y,
                                             view_w, view_h,
                                             min_layer, max_layer);
}

static int mk2_visible_object_count_at_camera(int camera_x, int camera_y,
                                              int view_w, int view_h,
                                              int min_layer, int max_layer)
{
    int count = 0;
    for (int i = 0; i < g_no; i++)
        count += mk2_object_visible_at_camera(i, camera_x, camera_y,
                                              view_w, view_h, min_layer, max_layer);
    return count;
}

static int mk2_estimate_max_visible_objects(int *out_x)
{
    if (!g_have_bdb || g_no <= 0) return 0;

    int wx_min = 0, wx_max = 400, wy_min = 0, wy_max = 254;
    bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    int scroll_max = wx_max - 400;
    if (scroll_max < wx_min) scroll_max = wx_min;

    int best = 0;
    int best_x = wx_min;
    int span = scroll_max - wx_min;
    int step = span > 1024 ? 32 : 16;
    if (step < 1) step = 1;

    for (int cam = wx_min; cam <= scroll_max; cam += step) {
        int count = mk2_visible_object_count_at_camera(cam, g_game_view_y,
                                                       400, 254, 0x00, 0xFF);
        if (count > best) {
            best = count;
            best_x = cam;
        }
        if (cam == scroll_max) break;
        if (cam + step > scroll_max) cam = scroll_max - step;
    }
    if (out_x) *out_x = best_x;
    return best;
}

int mk2_diag_hard_issues(const Mk2Diag *d)
{
    if (!d) return 0;
    return d->missing_images + d->bad_palettes + d->unassigned_objects +
           d->module_bound_issues + d->load2_oversize_images +
           d->load2_palette_overflow + d->load2_module_overflow +
           d->load2_image_header_overflow + d->load2_block_table_overflow +
           d->display_object_overflow;
}

int mk2_diag_cautions(const Mk2Diag *d)
{
    if (!d) return 0;
    return d->old_style_bounds + d->high_color_images + d->order_issues +
           d->runtime_palette_pressure + d->runtime_palette16_pressure +
           d->load2_narrow_padded_images + d->load2_zero_compress_disabled +
           d->display_object_pressure;
}

void mk2_collect_diag(Mk2Diag *d)
{
    memset(d, 0, sizeof(*d));
    int world_w = 0, world_h = 0;
    int have_world = get_world_size(&world_w, &world_h);
    int exportable_images = 0;
    for (int i = 0; i < g_ni; i++) {
        if (!runtime_actor_image_is_preview_import(&g_img[i]))
            exportable_images++;
    }

    if (g_n_pals > MK2_LOAD2_MAX_STAGE_PALETTES)
        d->load2_palette_overflow = g_n_pals - MK2_LOAD2_MAX_STAGE_PALETTES;
    if (g_bdb_num_modules > MK2_LOAD2_MAX_MODULES)
        d->load2_module_overflow = g_bdb_num_modules - MK2_LOAD2_MAX_MODULES;
    if (exportable_images > MK2_LOAD2_MAX_IMAGE_HEADERS)
        d->load2_image_header_overflow = exportable_images - MK2_LOAD2_MAX_IMAGE_HEADERS;
    if (exportable_images > MK2_LOAD2_MAX_BLOCKS)
        d->load2_block_table_overflow = exportable_images - MK2_LOAD2_MAX_BLOCKS;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1, x2, y1, y2;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2) || x2 < x1 || y2 < y1) {
            d->module_bound_issues++;
            continue;
        }
        if (have_world && ((world_w > 0 && x2 == world_w) || (world_h > 0 && y2 == world_h)))
            d->old_style_bounds++;
    }

    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (runtime_actor_image_is_preview_import(im))
            continue;
        int bpp = 0;
        int block_bytes = load2_estimated_block_bytes(im, &bpp);
        if (block_bytes > MK2_LOAD2_MAX_DATA_BYTES) d->load2_oversize_images++;
        if (block_bytes > d->max_load2_block_bytes) {
            d->max_load2_block_bytes = block_bytes;
            d->max_load2_block_bpp = bpp;
        }
        if (image_max_pixel(im) >= 64) d->high_color_images++;
        if (im->w > 0 && im->w < 3) d->load2_narrow_padded_images++;
        if (im->w > 0 && im->w < 10) d->load2_zero_compress_disabled++;
    }

    const int module_cap = editor_project_module_capacity();
    const int palette_cap = editor_project_palette_capacity();
    std::vector<int> last_x((size_t)(module_cap > 0 ? module_cap : 0), INT_MIN);

    std::vector<int> obj_order((size_t)g_no);
    for (int i = 0; i < g_no; i++) obj_order[(size_t)i] = i;
    for (int i = 1; i < g_no; i++) {
        int tmp = obj_order[(size_t)i];
        int j = i - 1;
        while (j >= 0 && g_obj[obj_order[(size_t)j]].order > g_obj[tmp].order) {
            obj_order[(size_t)(j + 1)] = obj_order[(size_t)j];
            j--;
        }
        obj_order[(size_t)(j + 1)] = tmp;
    }

    std::vector<unsigned char> used_palette((size_t)(palette_cap > 0 ? palette_cap : 0), 0);
    std::vector<unsigned char> module_palette((size_t)(module_cap > 0 && palette_cap > 0 ? module_cap * palette_cap : 0), 0);

    for (int pos = 0; pos < g_no; pos++) {
        Obj *o = &g_obj[obj_order[(size_t)pos]];
        Img *im = img_find(o->ii);
        if (!im) {
            d->missing_images++;
            continue;
        }
        if (runtime_actor_image_is_preview_import(im))
            continue;
        if (o->fl < 0 || o->fl >= g_n_pals) d->bad_palettes++;
        if (o->fl >= 16) d->palette_high_nibble++;
        if (o->fl >= 0 && o->fl < palette_cap)
            used_palette[(size_t)o->fl] = 1;
        int m = assign_module(o->depth, o->sy, im->w, im->h);
        if (m < 0) {
            d->unassigned_objects++;
            continue;
        }
        if (m >= 0 && m < module_cap && o->fl >= 0 && o->fl < palette_cap)
            module_palette[(size_t)m * (size_t)palette_cap + (size_t)o->fl] = 1;
        int x1 = 0;
        parse_module_bounds(m, NULL, &x1, NULL, NULL, NULL);
        int local_x = o->depth - x1;
        if (m >= 0 && m < module_cap) {
            if (local_x < last_x[(size_t)m]) d->order_issues++;
            if (local_x > last_x[(size_t)m]) last_x[(size_t)m] = local_x;
        }
    }

    for (int p = 0; p < palette_cap; p++)
        if (used_palette[(size_t)p]) d->runtime_palette_count++;
    if (d->runtime_palette_count > MK2_BG_DYNAMIC_PALETTE_SLOTS)
        d->runtime_palette_pressure++;
    if (d->runtime_palette_count > MK2_RUNTIME_PALETTE_SLOTS)
        d->runtime_palette16_pressure++;

    for (int m = 0; m < g_bdb_num_modules && m < module_cap; m++) {
        int count = 0;
        for (int p = 0; p < palette_cap; p++)
            if (module_palette[(size_t)m * (size_t)palette_cap + (size_t)p]) count++;
        if (count > d->max_module_palettes) {
            d->max_module_palettes = count;
            parse_module_bounds(m, d->max_module_palette_name, NULL, NULL, NULL, NULL);
        }
        if (count > MK2_BG_DYNAMIC_PALETTE_SLOTS)
            d->runtime_palette_pressure++;
        if (count > MK2_RUNTIME_PALETTE_SLOTS)
            d->runtime_palette16_pressure++;
    }

    d->max_visible_objects = mk2_estimate_max_visible_objects(&d->max_visible_objects_x);
    /* Background blocks share the getobj display-object pool (nobj=358) with the
     * runtime sprites that are not in the BDD (fighters + stage actors such as
     * the Dead Pool hangers). Charge that reserve against the on-screen block
     * peak so we flag a stage that fits in the editor but overflows at runtime
     * and makes disp_add drop blocks at scene init. */
    int runtime_budget = d->max_visible_objects + MK2_DISPLAY_OBJECT_RUNTIME_RESERVE;
    if (runtime_budget > MK2_DISPLAY_OBJECT_CAP)
        d->display_object_overflow = runtime_budget - MK2_DISPLAY_OBJECT_CAP;
    else if (runtime_budget > MK2_DISPLAY_OBJECT_WARN)
        d->display_object_pressure = 1;
}

int mk2_create_default_module(void)
{
    int ww = 0, wh = 0;
    int x1 = 0, y1 = 0, x2 = -1, y2 = -1;
    bool have_bounds = false;
    if (get_world_size(&ww, &wh) && ww > 0 && wh > 0) {
        x1 = 0;
        y1 = 0;
        x2 = ww - 1;
        y2 = wh - 1;
        have_bounds = true;
    }

    int wx_min, wx_max, wy_min, wy_max;
    bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    if (wx_min != INT_MAX && wx_max > wx_min && wy_max > wy_min) {
        if (!have_bounds || wx_min < x1) x1 = wx_min;
        if (!have_bounds || wx_max - 1 > x2) x2 = wx_max - 1;
        if (!have_bounds || wy_min < y1) y1 = wy_min;
        if (!have_bounds || wy_max - 1 > y2) y2 = wy_max - 1;
        have_bounds = true;
    }

    if (!have_bounds || x2 < x1 || y2 < y1 || !editor_project_reserve_modules(1)) return 0;
    undo_save_ex("Create Module");
    char line[256];
    snprintf(line, sizeof line, "TSTMOD %d %d %d %d", x1, x2, y1, y2);
    editor_project_set_single_module_line(line);
    sync_bdb_header_counts();
    g_dirty = 1;
    g_show_module_bounds = true;
    return 1;
}

int mk2_first_unassigned_object(void)
{
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) < 0)
            return i;
    }
    return -1;
}

int mk2_select_unassigned_objects(void)
{
    int count = 0;
    mk2_clear_selection_flags();
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) >= 0) continue;
        g_sel_flags[i] = 1;
        g_hl_obj = i;
        count++;
    }
    if (count > 0) {
        center_view_on_object(g_hl_obj);
        g_show_obj_properties = true;
        g_focus_obj_properties_next = true;
    }
    return count;
}

static int mk2_expand_nearest_module_to_object(int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= g_no || g_bdb_num_modules <= 0) return 0;
    Obj *o = &g_obj[obj_idx];
    Img *im = img_find(o->ii);
    if (!im) return 0;

    int ox1 = o->depth;
    int oy1 = o->sy;
    int ox2 = ox1 + im->w - 1;
    int oy2 = oy1 + im->h - 1;
    int best = -1;
    long long best_dist = LLONG_MAX;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        long long cx = ((long long)x1 + (long long)x2) / 2;
        long long cy = ((long long)y1 + (long long)y2) / 2;
        long long ocx = ((long long)ox1 + (long long)ox2) / 2;
        long long ocy = ((long long)oy1 + (long long)oy2) / 2;
        long long dx = ocx - cx;
        long long dy = ocy - cy;
        long long dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best = m;
        }
    }
    if (best < 0) return 0;

    char name[64] = "";
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (!parse_module_bounds(best, name, &x1, &x2, &y1, &y2)) return 0;
    int nx1 = x1, nx2 = x2, ny1 = y1, ny2 = y2;
    if (ox1 < nx1) nx1 = ox1;
    if (ox2 > nx2) nx2 = ox2;
    if (oy1 < ny1) ny1 = oy1;
    if (oy2 > ny2) ny2 = oy2;
    if (nx1 == x1 && nx2 == x2 && ny1 == y1 && ny2 == y2) return 0;

    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d", name, nx1, nx2, ny1, ny2);
    return editor_project_set_module_line(best, line) ? 1 : 0;
}

int mk2_include_unassigned_objects_in_modules(void)
{
    if (g_bdb_num_modules <= 0) return 0;
    int changed = 0;
    bool undo_done = false;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) >= 0) continue;
        if (!undo_done) {
            undo_save_ex("Include Objects in Modules");
            undo_done = true;
        }
        changed += mk2_expand_nearest_module_to_object(i);
    }
    if (changed) {
        sync_bdb_header_counts();
        g_dirty = 1;
        g_view_changed = 1;
    }
    return changed;
}

int mk2_include_object_in_nearest_module(int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= g_no || g_bdb_num_modules <= 0) return 0;
    Img *im = img_find(g_obj[obj_idx].ii);
    if (!im) return 0;
    if (assign_module(g_obj[obj_idx].depth, g_obj[obj_idx].sy, im->w, im->h) >= 0)
        return 0;

    undo_save_ex("Include Object in Module");
    int changed = mk2_expand_nearest_module_to_object(obj_idx);
    if (changed) {
        sync_bdb_header_counts();
        g_dirty = 1;
        g_view_changed = 1;
    }
    return changed;
}

int mk2_fit_module_bounds_to_objects(void)
{
    if (g_bdb_num_modules <= 0) return 0;

    struct ModuleFitScratch {
        int min_x;
        int max_x;
        int min_y;
        int max_y;
        int count;
        char name[64];
    };
    const int module_cap = editor_project_module_capacity();
    int module_n = g_bdb_num_modules;
    if (module_n > module_cap) module_n = module_cap;
    std::vector<ModuleFitScratch> modules((size_t)(module_n > 0 ? module_n : 0));
    for (int m = 0; m < module_n; m++) {
        modules[(size_t)m].min_x = INT_MAX;
        modules[(size_t)m].min_y = INT_MAX;
        modules[(size_t)m].max_x = INT_MIN;
        modules[(size_t)m].max_y = INT_MIN;
        modules[(size_t)m].count = 0;
        modules[(size_t)m].name[0] = '\0';
        parse_module_bounds(m, modules[(size_t)m].name, NULL, NULL, NULL, NULL);
    }

    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        int m = assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h);
        if (m < 0 || m >= module_n) continue;
        int x1 = g_obj[i].depth;
        int y1 = g_obj[i].sy;
        int x2 = x1 + im->w - 1;
        int y2 = y1 + im->h - 1;
        ModuleFitScratch &scratch = modules[(size_t)m];
        if (x1 < scratch.min_x) scratch.min_x = x1;
        if (y1 < scratch.min_y) scratch.min_y = y1;
        if (x2 > scratch.max_x) scratch.max_x = x2;
        if (y2 > scratch.max_y) scratch.max_y = y2;
        scratch.count++;
    }

    int changed = 0;
    for (int m = 0; m < module_n; m++) {
        ModuleFitScratch &scratch = modules[(size_t)m];
        if (!scratch.count || !scratch.name[0]) continue;
        int old_x1, old_x2, old_y1, old_y2;
        if (!parse_module_bounds(m, NULL, &old_x1, &old_x2, &old_y1, &old_y2)) continue;
        if (old_x1 != scratch.min_x || old_x2 != scratch.max_x || old_y1 != scratch.min_y || old_y2 != scratch.max_y)
            changed++;
    }
    if (!changed) return 0;

    undo_save_ex("Fit Module Bounds");
    for (int m = 0; m < module_n; m++) {
        ModuleFitScratch &scratch = modules[(size_t)m];
        if (!scratch.count || !scratch.name[0]) continue;
        char line[256];
        snprintf(line, sizeof line, "%s %d %d %d %d",
                 scratch.name, scratch.min_x, scratch.max_x, scratch.min_y, scratch.max_y);
        editor_project_set_module_line(m, line);
    }
    sync_bdb_header_counts();
    g_dirty = 1;
    return changed;
}

int mk2_sort_objects_x_major(void)
{
    if (g_no <= 1) return 0;
    const int module_cap = editor_project_module_capacity();
    std::vector<int> obj_order((size_t)g_no);
    for (int i = 0; i < g_no; i++) obj_order[(size_t)i] = i;

    for (int i = 1; i < g_no; i++) {
        int tmp = obj_order[(size_t)i];
        Img *tmp_im = img_find(g_obj[tmp].ii);
        int tmp_mod = tmp_im ? assign_module(g_obj[tmp].depth, g_obj[tmp].sy, tmp_im->w, tmp_im->h) : module_cap;
        int j = i - 1;
        while (j >= 0) {
            int cur = obj_order[(size_t)j];
            Img *cur_im = img_find(g_obj[cur].ii);
            int cur_mod = cur_im ? assign_module(g_obj[cur].depth, g_obj[cur].sy, cur_im->w, cur_im->h) : module_cap;
            bool after = false;
            if (cur_mod > tmp_mod) after = true;
            else if (cur_mod == tmp_mod && g_obj[cur].depth > g_obj[tmp].depth) after = true;
            else if (cur_mod == tmp_mod && g_obj[cur].depth == g_obj[tmp].depth && g_obj[cur].sy > g_obj[tmp].sy) after = true;
            else if (cur_mod == tmp_mod && g_obj[cur].depth == g_obj[tmp].depth && g_obj[cur].sy == g_obj[tmp].sy && g_obj[cur].order > g_obj[tmp].order) after = true;
            if (!after) break;
            obj_order[(size_t)(j + 1)] = obj_order[(size_t)j];
            j--;
        }
        obj_order[(size_t)(j + 1)] = tmp;
    }

    int changed = 0;
    for (int pos = 0; pos < g_no; pos++)
        if (g_obj[obj_order[(size_t)pos]].order != pos) changed++;
    if (!changed) return 0;

    undo_save_ex("Reorder Objects");
    for (int pos = 0; pos < g_no; pos++)
        g_obj[obj_order[(size_t)pos]].order = pos;
    sync_bdb_header_counts();
    g_dirty = 1;
    return changed;
}

int image_use_count(int ii)
{
    int count = 0;
    for (int oi = 0; oi < g_no; oi++)
        if (g_obj[oi].ii == ii) count++;
    return count;
}

int first_unused_image_index(void)
{
    for (int i = 0; i < g_ni; i++)
        if (image_use_count(g_img[i].idx) == 0) return i;
    return -1;
}

int mk2_max_object_order(void)
{
    int max_order = -1;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;
    return max_order;
}

int mk2_find_first_fit_for_image(const Img *im, int *out_x, int *out_y)
{
    if (!im || im->w <= 0 || im->h <= 0) return 0;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        if (x2 - x1 + 1 < im->w || y2 - y1 + 1 < im->h) continue;
        if (out_x) *out_x = x1;
        if (out_y) *out_y = y1;
        return 1;
    }
    return 0;
}

int mk2_find_center_fit_for_image(const Img *im, int *out_x, int *out_y)
{
    if (!im || im->w <= 0 || im->h <= 0) return 0;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        int mw = x2 - x1 + 1;
        int mh = y2 - y1 + 1;
        if (mw < im->w || mh < im->h) continue;
        if (out_x) *out_x = x1 + (mw - im->w) / 2;
        if (out_y) *out_y = y1 + (mh - im->h) / 2;
        return 1;
    }
    return 0;
}

void mk2_delete_object_preserve_order(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    editor_project_delete_object_slot(idx);
}

static bool mk2_copy_file_for_backup(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) {
        int err = errno;
        bdd_save_logf("outside-delete backup failed: cannot open source=\"%s\" dest=\"%s\" errno=%d (%s)",
                      src ? src : "", dst ? dst : "", err, strerror(err));
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        int err = errno;
        bdd_save_logf("outside-delete backup failed: cannot open dest=\"%s\" source=\"%s\" errno=%d (%s)",
                      dst ? dst : "", src ? src : "", err, strerror(err));
        fclose(in);
        return false;
    }
    char buf[16384];
    bool ok = true;
    int err = 0;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            err = errno;
            break;
        }
    }
    if (ferror(in) && ok) {
        ok = false;
        err = errno;
    }
    if (fclose(in) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (fclose(out) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (!ok) {
        bdd_save_logf("outside-delete backup failed while copying: source=\"%s\" dest=\"%s\" errno=%d (%s)",
                      src ? src : "", dst ? dst : "", err, err ? strerror(err) : "unknown");
        remove(dst);
    }
    return ok;
}

static void mk2_backup_timestamp(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    strftime(out, outsz, "outside-delete-%Y%m%d-%H%M%S", &tmv);
}

static bool mk2_backup_outside_delete(int outside_count)
{
    g_outside_delete_backup_status[0] = '\0';
    char stamp[64];
    mk2_backup_timestamp(stamp, sizeof stamp);
    if (!stamp[0]) snprintf(stamp, sizeof stamp, "outside-delete");

    const char *base = g_bdd_path[0] ? g_bdd_path : (g_bdb_path[0] ? g_bdb_path : "outside_objects");
    char bakdir[520];
    backup_base_dir(bakdir, sizeof bakdir, base);
    char manifest[768];
    char manifest_name[256];
    snprintf(manifest_name, sizeof manifest_name, "%s.%s.removed.txt", path_basename_ptr(base), stamp);
    path_join(manifest, sizeof manifest, bakdir, manifest_name);

    char bdb_bak[768] = "";
    char bdd_bak[768] = "";
    bool ok = true;
    int copied = 0;
    if (g_bdb_path[0] && stage_file_exists(g_bdb_path)) {
        char bak_name[256];
        snprintf(bak_name, sizeof bak_name, "%s.%s.bak", path_basename_ptr(g_bdb_path), stamp);
        path_join(bdb_bak, sizeof bdb_bak, bakdir, bak_name);
        if (mk2_copy_file_for_backup(g_bdb_path, bdb_bak)) copied++;
        else ok = false;
    }
    if (g_bdd_path[0] && stage_file_exists(g_bdd_path)) {
        char bak_name[256];
        snprintf(bak_name, sizeof bak_name, "%s.%s.bak", path_basename_ptr(g_bdd_path), stamp);
        path_join(bdd_bak, sizeof bdd_bak, bakdir, bak_name);
        if (mk2_copy_file_for_backup(g_bdd_path, bdd_bak)) copied++;
        else ok = false;
    }

    FILE *f = fopen(manifest, "wb");
    if (!f) {
        int err = errno;
        snprintf(g_outside_delete_backup_status, sizeof g_outside_delete_backup_status,
                 "Backup failed: could not write %s", manifest);
        bdd_save_logf("outside-delete backup failed: cannot write manifest=\"%s\" errno=%d (%s)",
                      manifest, err, strerror(err));
        return false;
    }
    fprintf(f, "Outside-module object delete backup\n");
    fprintf(f, "BDB: %s\n", g_bdb_path[0] ? g_bdb_path : "(none)");
    fprintf(f, "BDD: %s\n", g_bdd_path[0] ? g_bdd_path : "(none)");
    if (bdb_bak[0]) fprintf(f, "BDB backup: %s\n", bdb_bak);
    if (bdd_bak[0]) fprintf(f, "BDD backup: %s\n", bdd_bak);
    fprintf(f, "Outside objects: %d\n\n", outside_count);
    fprintf(f, "# obj ii wx depth sy fl hfl vfl order image_w image_h image_label image_source\n");
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) >= 0) continue;
        fprintf(f, "%d 0x%04X 0x%04X %d %d %d %d %d %d %d %d \"%s\" \"%s\"\n",
                i, g_obj[i].ii, g_obj[i].wx, g_obj[i].depth, g_obj[i].sy,
                g_obj[i].fl, g_obj[i].hfl, g_obj[i].vfl, g_obj[i].order,
                im->w, im->h, im->label, im->source);
    }
    if (fclose(f) != 0) {
        int err = errno;
        bdd_save_logf("outside-delete backup failed: cannot close manifest=\"%s\" errno=%d (%s)",
                      manifest, err, strerror(err));
        ok = false;
    }

    if (!ok) {
        snprintf(g_outside_delete_backup_status, sizeof g_outside_delete_backup_status,
                 "Backup incomplete; delete was not run. Manifest: %s", manifest);
        bdd_save_logf("outside-delete backup incomplete; delete not run: manifest=\"%s\" bdb=\"%s\" bdd=\"%s\"",
                      manifest, g_bdb_path, g_bdd_path);
        return false;
    }

    snprintf(g_outside_delete_backup_status, sizeof g_outside_delete_backup_status,
             "Backup written: %s (%d file copy/copies)", manifest, copied);
    return true;
}

int mk2_delete_unassigned_objects(void)
{
    int count = 0;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) < 0)
            count++;
    }
    if (count <= 0) return 0;
    if (!mk2_backup_outside_delete(count)) return -1;

    undo_save_ex("Delete Outside Objects");
    int removed = 0;
    for (int i = g_no - 1; i >= 0; i--) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) >= 0) continue;
        mk2_delete_object_preserve_order(i);
        removed++;
    }
    mk2_clear_selection_flags();
    if (g_hl_obj >= g_no) g_hl_obj = g_no - 1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_view_changed = 1;
    return removed;
}

void mk2_toast_outside_delete_result(int removed)
{
    if (removed < 0) {
        stage_set_toast("Delete outside refused: backup failed");
        return;
    }
    char msg[128];
    snprintf(msg, sizeof msg,
             removed ? "Backed up and deleted %d outside-module object(s)" : "No outside-module objects",
             removed);
    stage_set_toast(msg);
}

int mk2_add_object_for_image(int img_i, int x, int y, int layer, int pal, int hfl, int vfl,
                             bool save_undo)
{
    if (img_i < 0 || img_i >= g_ni || !editor_project_reserve_objects(g_no + 1)) return 0;
    Img *im = &g_img[img_i];
    if (pal < 0 || pal >= g_n_pals)
        pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? im->pal_idx : 0;

    if (save_undo) undo_save();
    Obj *o = editor_project_append_object_slot();
    if (!o) return 0;
    int obj_i = g_no - 1;
    o->wx = (layer << 8) | (hfl ? 0x10 : 0) | (vfl ? 0x20 : 0);
    o->depth = x;
    o->sy = y;
    o->ii = im->idx;
    o->fl = pal;
    o->hfl = hfl ? 1 : 0;
    o->vfl = vfl ? 1 : 0;
    o->order = mk2_max_object_order() + 1;

    mk2_clear_selection_flags();
    g_sel_flags[obj_i] = 1;
    g_obj_lock[obj_i] = 0;
    g_obj_hidden[obj_i] = 0;
    g_hl_obj = obj_i;

    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    return 1;
}

static void mk2_expand_header_to_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (!g_bdb_header[0]) return;
    char nm[64] = "";
    int ww = 0, wh = 0, md = 255, old_nm = 0, old_np = 0, old_no = 0;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &ww, &wh, &md, &old_nm, &old_np, &old_no) < 7)
        return;
    int x2 = x + w;
    int y2 = y + h;
    if (x2 > ww) ww = x2;
    if (y2 > wh) wh = y2;
    if (ww < 400) ww = 400;
    if (wh < 254) wh = 254;
    snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
             nm, ww, wh, md, g_bdb_num_modules, g_n_pals, g_no);
}

static void mk2_ensure_bdb_for_static_background(const Img *im)
{
    if (!im) return;
    if (!g_have_bdb || !g_bdb_header[0]) {
        char nm[64] = "";
        if (g_name[0])
            snprintf(nm, sizeof nm, "%s", g_name);
        else
            snprintf(nm, sizeof nm, "BACKGROUND");
        nm[8] = '\0';
        int ww = im->w > 400 ? im->w : 400;
        int wh = im->h > 254 ? im->h : 254;
        snprintf(g_name, sizeof g_name, "%s", nm);
        snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d 255 %d %d %d",
                 nm, ww, wh, g_bdb_num_modules, g_n_pals, g_no);
        g_have_bdb = 1;
    }
    if (g_bdb_num_modules <= 0) {
        char line[256];
        snprintf(line, sizeof line, "BGMOD 0 %d 0 %d", im->w - 1, im->h - 1);
        editor_project_set_single_module_line(line);
    }
}

int mk2_set_image_as_static_background(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    Img *im = &g_img[img_i];
    if (!im || im->w <= 0 || im->h <= 0) return 0;

    int obj_i = -1;
    for (int i = 0; i < g_no; i++) {
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        if (g_obj[i].ii == im->idx && layer == 0x32) {
            obj_i = i;
            break;
        }
    }
    if (obj_i < 0 && !editor_project_reserve_objects(g_no + 1)) return 0;

    undo_save_ex("Set Static Background");
    mk2_ensure_bdb_for_static_background(im);

    if (obj_i < 0) {
        Obj *created = editor_project_append_object_slot();
        if (!created) return 0;
        obj_i = g_no - 1;
    }

    int pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? im->pal_idx : 0;
    Obj *o = &g_obj[obj_i];
    o->wx = 0x3200;
    o->depth = 0;
    o->sy = 0;
    o->ii = im->idx;
    o->fl = pal;
    o->hfl = 0;
    o->vfl = 0;
    for (int i = 0; i < g_no; i++) {
        if (i != obj_i && g_obj[i].order >= 0)
            g_obj[i].order++;
    }
    o->order = 0;
    g_obj_lock[obj_i] = 0;
    g_obj_hidden[obj_i] = 0;

    if (assign_module(o->depth, o->sy, im->w, im->h) < 0) {
        if (g_bdb_num_modules <= 0) {
            char line[256];
            snprintf(line, sizeof line, "BGMOD 0 %d 0 %d", im->w - 1, im->h - 1);
            editor_project_set_single_module_line(line);
        } else {
            mk2_expand_nearest_module_to_object(obj_i);
        }
    }
    mk2_expand_header_to_rect(o->depth, o->sy, im->w, im->h);

    mk2_clear_selection_flags();
    g_sel_flags[obj_i] = 1;
    g_hl_obj = obj_i;
    g_place_tool_img = img_i;
    g_cur_tool = 0;
    g_runtime_layout_view = 1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_view_changed = 1;
    center_view_on_object(obj_i);
    return 1;
}

int mk2_enable_unused_asset(int img_i)
{
    if (img_i < 0 || img_i >= g_ni || !editor_project_reserve_objects(g_no + 1)) return 0;
    Img *im = &g_img[img_i];
    int place_x = g_unused_enable_x;
    int place_y = g_unused_enable_y;
    if (g_unused_auto_fit)
        mk2_find_first_fit_for_image(im, &place_x, &place_y);
    return mk2_add_object_for_image(img_i, place_x, place_y, g_unused_enable_layer,
                                    g_unused_enable_pal, 0, 0);
}

int mk2_disable_selected_assets_keep_images(void)
{
    int marked = selected_count();
    if (marked == 0 && g_hl_obj >= 0 && g_hl_obj < g_no) {
        g_sel_flags[g_hl_obj] = 1;
        marked = 1;
    }
    if (marked == 0) return 0;

    undo_save_ex("Delete");
    int removed = 0;
    for (int i = g_no - 1; i >= 0; i--) {
        if (!g_sel_flags[i]) continue;
        mk2_delete_object_preserve_order(i);
        removed++;
    }
    if (g_hl_obj >= g_no) g_hl_obj = g_no - 1;
    if (removed > 0 && g_hl_obj >= 0 && g_hl_obj < g_no)
        g_sel_flags[g_hl_obj] = 1;

    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    return removed;
}

int object_module_index(const Obj *o, const Img *im)
{
    if (!o || !im) return -1;
    return assign_module(o->depth, o->sy, im->w, im->h);
}

int object_pixel_at_world(const Obj *o, const Img *im, int wx, int wy)
{
    if (!o || !im || !im->pix) return -1;
    int lx = wx - o->depth;
    int ly = wy - o->sy;
    if (lx < 0 || ly < 0 || lx >= im->w || ly >= im->h) return -1;
    if (o->hfl) lx = im->w - 1 - lx;
    if (o->vfl) ly = im->h - 1 - ly;
    return im->pix[ly * im->w + lx];
}

int mk2_select_objects_by_image(int ii)
{
    mk2_clear_selection_flags();
    int count = 0;
    g_hl_obj = -1;
    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != ii) continue;
        g_sel_flags[i] = 1;
        if (g_hl_obj < 0) g_hl_obj = i;
        count++;
    }
    if (count > 0) {
        snprintf(g_obj_filter, sizeof g_obj_filter, "%04X", ii);
    }
    return count;
}

int mk2_bpp_for_image(const Img *im)
{
    return bdd_core_load2_bpp_for_max_pixel(image_max_pixel(im));
}

int mk2_bpp_for_max_index(int max_px)
{
    return bdd_core_load2_bpp_for_max_pixel(max_px);
}

size_t mk2_estimate_image_bytes_for_bpp(const Img *im, int bpp)
{
    if (!im || im->w <= 0 || im->h <= 0 || bpp <= 0) return 0;
    return ((size_t)im->w * (size_t)im->h * (size_t)bpp + 7u) / 8u;
}

size_t mk2_estimate_image_bytes(const Img *im)
{
    return mk2_estimate_image_bytes_for_bpp(im, mk2_bpp_for_image(im));
}

Mk2Budget mk2_collect_budget(void)
{
    Mk2Budget b;
    memset(&b, 0, sizeof b);
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        int pixels = im->w * im->h;
        size_t raw = mk2_estimate_image_bytes(im);
        b.raw_image_bytes += raw;
        if (pixels > b.max_image_pixels) b.max_image_pixels = pixels;
        if (raw > (size_t)MK2_LOAD2_MAX_DATA_BYTES) b.oversized_images++;
        if (image_max_pixel(im) >= 64) b.high_color_images++;
        if (image_use_count(im->idx) == 0) b.unused_images++;
    }
    for (int p = 0; p < g_n_pals; p++)
        b.palette_entries += g_pal_count[p];
    b.estimated_payload = b.raw_image_bytes
                        + (size_t)g_no * 8u
                        + (size_t)g_ni * 12u
                        + (size_t)b.palette_entries * 2u
                        + (size_t)g_bdb_num_modules * 16u;
    return b;
}

static int mk2_visible_pixel_at_screen(int camera_x, int screen_x, int screen_y, int min_layer, int max_layer)
{
    for (int i = g_no - 1; i >= 0; i--) {
        if (g_obj_hidden[i]) continue;
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        if (layer < min_layer || layer > max_layer) continue;
        Img *im = img_find(g_obj[i].ii);
        int px = object_pixel_at_screen(&g_obj[i], im, camera_x, screen_x, screen_y);
        if (px > 0) return 1;
    }
    return 0;
}

float mk2_screen_band_coverage(int camera_x, int y0, int y1)
{
    int stride = g_pan_scan_stride < 1 ? 1 : g_pan_scan_stride;
    int samples = 0, covered = 0;
    if (y0 < 0) y0 = 0;
    if (y1 > g_pan_scan_view_h) y1 = g_pan_scan_view_h;
    if (y1 <= y0) return 100.0f;
    for (int y = y0; y < y1; y += stride) {
        for (int x = 0; x < g_pan_scan_view_w; x += stride) {
            samples++;
            covered += mk2_visible_pixel_at_screen(camera_x, x, y, g_pan_scan_min_layer, g_pan_scan_max_layer);
        }
    }
    return samples > 0 ? (covered * 100.0f) / (float)samples : 100.0f;
}

DisplayObjectSummary mk2_compute_display_object_summary(void)
{
    DisplayObjectSummary s = {};
    s.worst_x = g_pan_scan_start_x;
    int step = g_pan_scan_step < 1 ? 1 : g_pan_scan_step;
    int end_x = g_pan_scan_end_x < g_pan_scan_start_x ? g_pan_scan_start_x : g_pan_scan_end_x;
    int view_w = g_pan_scan_view_w < 16 ? 16 : g_pan_scan_view_w;
    int view_h = g_pan_scan_view_h < 16 ? 16 : g_pan_scan_view_h;
    for (int cam = g_pan_scan_start_x; cam <= end_x; cam += step) {
        int count = mk2_visible_object_count_at_camera(cam, g_game_view_y,
                                                       view_w, view_h,
                                                       g_pan_scan_min_layer, g_pan_scan_max_layer);
        if (count > s.max_count) {
            s.max_count = count;
            s.worst_x = cam;
        }
        s.points++;
    }
    return s;
}

int mk2_select_visible_objects_at_camera(int camera_x)
{
    mk2_clear_selection_flags();
    g_hl_obj = -1;
    int count = 0;
    int view_w = g_pan_scan_view_w < 16 ? 16 : g_pan_scan_view_w;
    int view_h = g_pan_scan_view_h < 16 ? 16 : g_pan_scan_view_h;
    for (int i = 0; i < g_no; i++) {
        if (!mk2_object_visible_at_camera(i, camera_x, g_game_view_y,
                                          view_w, view_h,
                                          g_pan_scan_min_layer, g_pan_scan_max_layer))
            continue;
        g_sel_flags[i] = 1;
        if (g_hl_obj < 0) g_hl_obj = i;
        count++;
    }
    return count;
}

int mk2_select_visible_objects_at_camera_layer(int camera_x, int layer)
{
    mk2_clear_selection_flags();
    g_hl_obj = -1;
    int count = 0;
    int view_w = g_pan_scan_view_w < 16 ? 16 : g_pan_scan_view_w;
    int view_h = g_pan_scan_view_h < 16 ? 16 : g_pan_scan_view_h;
    for (int i = 0; i < g_no; i++) {
        if (((g_obj[i].wx >> 8) & 0xFF) != layer)
            continue;
        if (!mk2_object_visible_at_camera(i, camera_x, g_game_view_y,
                                          view_w, view_h,
                                          g_pan_scan_min_layer, g_pan_scan_max_layer))
            continue;
        g_sel_flags[i] = 1;
        if (g_hl_obj < 0) g_hl_obj = i;
        count++;
    }
    return count;
}

int mk2_visible_object_counts_by_layer_at_camera(int camera_x,
                                                 int *layers,
                                                 int *counts,
                                                 int max_layers)
{
    if (!layers || !counts || max_layers <= 0) return 0;
    int n = 0;
    int view_w = g_pan_scan_view_w < 16 ? 16 : g_pan_scan_view_w;
    int view_h = g_pan_scan_view_h < 16 ? 16 : g_pan_scan_view_h;
    for (int i = 0; i < g_no; i++) {
        if (!mk2_object_visible_at_camera(i, camera_x, g_game_view_y,
                                          view_w, view_h,
                                          g_pan_scan_min_layer, g_pan_scan_max_layer))
            continue;
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        int slot = -1;
        for (int j = 0; j < n; j++) {
            if (layers[j] == layer) { slot = j; break; }
        }
        if (slot < 0) {
            if (n >= max_layers) continue;
            slot = n++;
            layers[slot] = layer;
            counts[slot] = 0;
        }
        counts[slot]++;
    }
    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            if (counts[a] > counts[b]) continue;
            if (counts[a] == counts[b] && layers[a] <= layers[b]) continue;
            int tl = layers[a]; layers[a] = layers[b]; layers[b] = tl;
            int tc = counts[a]; counts[a] = counts[b]; counts[b] = tc;
        }
    }
    return n;
}

Uint32 image_pixel_hash(const Img *im, bool hflip)
{
    if (!im || !im->pix) return 0;
    Uint32 h = 2166136261u;
    h ^= (Uint32)im->w; h *= 16777619u;
    h ^= (Uint32)im->h; h *= 16777619u;
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            int sx = hflip ? (im->w - 1 - x) : x;
            h ^= im->pix[y * im->w + sx];
            h *= 16777619u;
        }
    }
    return h;
}

bool image_pixels_match(const Img *a, const Img *b, bool mirror)
{
    if (!a || !b || !a->pix || !b->pix) return false;
    if (a->w != b->w || a->h != b->h) return false;
    for (int y = 0; y < a->h; y++) {
        for (int x = 0; x < a->w; x++) {
            int bx = mirror ? (b->w - 1 - x) : x;
            if (a->pix[y * a->w + x] != b->pix[y * b->w + bx]) return false;
        }
    }
    return true;
}

bool mk2_has_drawable_stage(void)
{
    return g_have_bdb && g_no > 0 && g_ni > 0;
}

PanCoverageSummary mk2_compute_pan_summary(void)
{
    PanCoverageSummary s;
    s.full = 100.0f;
    s.top = 100.0f;
    s.floor = 100.0f;
    s.worst_x = g_pan_scan_start_x;
    s.points = 0;
    int step = g_pan_scan_step < 1 ? 1 : g_pan_scan_step;
    int end_x = g_pan_scan_end_x < g_pan_scan_start_x ? g_pan_scan_start_x : g_pan_scan_end_x;
    for (int cam = g_pan_scan_start_x; cam <= end_x; cam += step) {
        float full = mk2_screen_band_coverage(cam, 0, g_pan_scan_view_h);
        float top = mk2_screen_band_coverage(cam, 0, g_pan_scan_view_h / 5);
        float floor = mk2_screen_band_coverage(cam, (g_pan_scan_view_h * 3) / 4, g_pan_scan_view_h);
        if (full < s.full) { s.full = full; s.worst_x = cam; }
        if (top < s.top) s.top = top;
        if (floor < s.floor) s.floor = floor;
        s.points++;
    }
    return s;
}

size_t mk2_estimate_duplicate_savings(void)
{
    std::vector<Uint32> hash((size_t)g_ni, 0);
    std::vector<Uint32> mirror_hash((size_t)g_ni, 0);
    for (int i = 0; i < g_ni; i++) {
        hash[(size_t)i] = image_pixel_hash(&g_img[i], false);
        mirror_hash[(size_t)i] = image_pixel_hash(&g_img[i], true);
    }
    size_t savings = 0;
    for (int a = 0; a < g_ni; a++) {
        Img *ia = &g_img[a];
        if (!ia->pix || ia->w * ia->h < g_dup_min_pixels) continue;
        for (int b = a + 1; b < g_ni; b++) {
            Img *ib = &g_img[b];
            if (!ib->pix || ia->w != ib->w || ia->h != ib->h) continue;
            if (hash[(size_t)a] == hash[(size_t)b] && image_pixels_match(ia, ib, false)) {
                savings += (size_t)ia->w * (size_t)ia->h;
            } else if (g_dup_include_mirrors && hash[(size_t)a] == mirror_hash[(size_t)b] && image_pixels_match(ia, ib, true)) {
                savings += (size_t)ia->w * (size_t)ia->h;
            }
        }
    }
    return savings;
}

void mk2_readiness_report(char *out, size_t outsz)
{
    out[0] = '\0';
    char line[256];
    snprintf(line, sizeof line, "MK2 Stage Readiness Report: %s / %s\n", g_stage_internal_name, g_stage_display_name);
    stage_append(out, outsz, line);

    if (!mk2_has_drawable_stage()) {
        stage_append(out, outsz, "No drawable BDB/BDD is loaded; package readiness is not applicable yet.\n");
        stage_append(out, outsz, "Open or generate a stage with objects and images before using pan/package gates.\n");
        return;
    }

    Mk2Diag d;
    mk2_collect_diag(&d);
    Mk2Budget b = mk2_collect_budget();
    PanCoverageSummary ps = mk2_compute_pan_summary();
    int hard = mk2_diag_hard_issues(&d);
    size_t dup = mk2_estimate_duplicate_savings();

    snprintf(line, sizeof line, "LOAD2 hard issues: %d\n", hard);
    stage_append(out, outsz, line);
    if (d.unassigned_objects > 0) {
        snprintf(line, sizeof line,
                 "LOAD2 detail: %d object(s) are outside every module; full sprite rectangles must fit inside module bounds.\n",
                 d.unassigned_objects);
        stage_append(out, outsz, line);
    }
    snprintf(line, sizeof line,
             "LOAD2 caps: %d/%d palettes, %d/%d modules, %d/%d image headers, max block %d/%d bytes (%dbpp)\n",
             g_n_pals, MK2_LOAD2_MAX_STAGE_PALETTES,
             g_bdb_num_modules, MK2_LOAD2_MAX_MODULES,
             g_ni, MK2_LOAD2_MAX_IMAGE_HEADERS,
             d.max_load2_block_bytes, MK2_LOAD2_MAX_DATA_BYTES,
             d.max_load2_block_bpp);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "Runtime pressure: %d/%d visible background objects at X %d, %d palettes used (max module %d)\n",
             d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP, d.max_visible_objects_x,
             d.runtime_palette_count, d.max_module_palettes);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "X-order cautions: %d\n", d.order_issues);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "Payload estimate: 0x%zX / limit 0x%X\n", b.estimated_payload, g_gate_payload_limit);
    stage_append(out, outsz, line);
    if (g_gate_payload_limit > 0 && b.estimated_payload > (size_t)g_gate_payload_limit)
        mk2_append_budget_relief_report(out, outsz, b.estimated_payload - (size_t)g_gate_payload_limit);
    snprintf(line, sizeof line, "High-color images: %d\n", b.high_color_images);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "Pan coverage: full %.1f%%, top %.1f%%, floor %.1f%%, worst X %d\n",
             ps.full, ps.top, ps.floor, ps.worst_x);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "Duplicate/mirror raw savings estimate: 0x%zX\n", dup);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "Stage FX: %s, triggers: %s\n",
             g_stage_red_enabled ? "enabled" : "disabled", stage_fx_trigger_summary());
    stage_append(out, outsz, line);
}

bool find_first_duplicate_pair(int *keep_i, int *replace_i, bool *mirror)
{
    std::vector<Uint32> hash((size_t)g_ni, 0);
    std::vector<Uint32> mirror_hash((size_t)g_ni, 0);
    for (int i = 0; i < g_ni; i++) {
        hash[(size_t)i] = image_pixel_hash(&g_img[i], false);
        mirror_hash[(size_t)i] = image_pixel_hash(&g_img[i], true);
    }
    for (int a = 0; a < g_ni; a++) {
        Img *ia = &g_img[a];
        if (!ia->pix || ia->w * ia->h < g_dup_min_pixels) continue;
        for (int b = a + 1; b < g_ni; b++) {
            Img *ib = &g_img[b];
            if (!ib->pix || ia->w != ib->w || ia->h != ib->h) continue;
            if (hash[(size_t)a] == hash[(size_t)b] && image_pixels_match(ia, ib, false)) {
                if (keep_i) *keep_i = a;
                if (replace_i) *replace_i = b;
                if (mirror) *mirror = false;
                return true;
            }
            if (g_dup_include_mirrors && hash[(size_t)a] == mirror_hash[(size_t)b] && image_pixels_match(ia, ib, true)) {
                if (keep_i) *keep_i = a;
                if (replace_i) *replace_i = b;
                if (mirror) *mirror = true;
                return true;
            }
        }
    }
    return false;
}

int apply_safe_dedup(int keep_i, int replace_i, bool mirror)
{
    if (keep_i < 0 || keep_i >= g_ni || replace_i < 0 || replace_i >= g_ni || keep_i == replace_i)
        return -1;
    Img *keep = &g_img[keep_i];
    Img *rep = &g_img[replace_i];
    if (!keep->pix || !rep->pix || keep->w != rep->w || keep->h != rep->h)
        return -1;
    if (!image_pixels_match(keep, rep, mirror))
        return -1;

    undo_save();
    int changed = 0;
    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != rep->idx) continue;
        g_obj[i].ii = keep->idx;
        if (mirror) {
            g_obj[i].hfl = !g_obj[i].hfl;
            g_obj[i].wx = (g_obj[i].wx & ~0x10) | (g_obj[i].hfl ? 0x10 : 0);
        }
        changed++;
    }
    if (changed) {
        g_dirty = 1;
        g_need_rebuild = 1;
    }
    return changed;
}
