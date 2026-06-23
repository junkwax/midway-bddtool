#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/actions/object_position_undo.h"
#include "undo_manager.h"
#include "libs/stb_image_write.h"

#include <imgui.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
int active_object_index(void)
{
    if (g_hl_obj >= 0 && g_hl_obj < g_no)
        return g_hl_obj;
    for (int i = 0; i < g_no; i++)
        if (g_sel_flags[i])
            return i;
    return -1;
}

void open_object_properties(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    g_hl_obj = idx;
    if (selected_count() == 0)
        g_sel_flags[idx] = 1;
    g_show_obj_properties = true;
    g_focus_obj_properties_next = true;
}

bool obj_properties_take_focus_request(void)
{
    if (!g_focus_obj_properties_next)
        return false;
    g_focus_obj_properties_next = false;
    return true;
}

int active_menu_image_index(void)
{
    int oi = active_object_index();
    if (oi >= 0) {
        Img *im = img_find(g_obj[oi].ii);
        if (im)
            return (int)(im - g_img);
    }
    if (g_place_tool_img >= 0 && g_place_tool_img < g_ni)
        return g_place_tool_img;
    return -1;
}

static void normalize_object_order_to_array(void)
{
    for (int i = 0; i < g_no; i++)
        g_obj[i].order = i;
}

void move_object_to_index(int src, int dst)
{
    if (src < 0 || src >= g_no || dst < 0 || dst >= g_no || src == dst)
        return;

    if (!editor_project_move_object_slot(src, dst))
        return;
    g_hl_obj = dst;
    normalize_object_order_to_array();
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
}

static bool object_menu_targeted(int idx, int active, int sel_count)
{
    if (idx < 0 || idx >= g_no) return false;
    return (sel_count > 0) ? (g_sel_flags[idx] != 0) : (idx == active);
}

static bool object_action_runtime_locked(int idx)
{
    if (idx < 0 || idx >= g_no) return false;
    Img *im = img_find(g_obj[idx].ii);
    return runtime_actor_image_is_preview_import(im);
}

static void object_actions_clear_selection(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap > 0)
        editor_project_clear_selection();
}

int reorder_object_menu_targets(int active, bool to_front)
{
    if (g_no <= 0) return 0;

    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return 0;
    int sel_count = selected_count();
    std::vector<unsigned char> target((size_t)object_cap, 0);
    int target_count = 0;
    for (int i = 0; i < g_no; i++) {
        target[i] = object_menu_targeted(i, active, sel_count) ? 1 : 0;
        if (target[i]) target_count++;
    }
    if (target_count <= 0 || target_count >= g_no) return 0;

    undo_save_ex(to_front ? "Bring to Front" : "Send to Back");

    std::vector<Obj> new_obj((size_t)g_no);
    std::vector<int> new_sel((size_t)g_no);
    std::vector<int> new_lock((size_t)g_no);
    std::vector<int> new_hidden((size_t)g_no);
    std::vector<int> old_index((size_t)g_no);
    int n = 0;

    auto append_obj = [&](int i) {
        new_obj[n] = g_obj[i];
        new_sel[n] = g_sel_flags[i];
        new_lock[n] = g_obj_lock[i];
        new_hidden[n] = g_obj_hidden[i];
        old_index[n] = i;
        n++;
    };

    if (!to_front) {
        for (int i = 0; i < g_no; i++) if (target[i]) append_obj(i);
        for (int i = 0; i < g_no; i++) if (!target[i]) append_obj(i);
    } else {
        for (int i = 0; i < g_no; i++) if (!target[i]) append_obj(i);
        for (int i = 0; i < g_no; i++) if (target[i]) append_obj(i);
    }

    int new_hl = -1;
    for (int i = 0; i < g_no; i++) {
        if (old_index[i] == active) new_hl = i;
    }
    editor_project_replace_object_rows(new_obj.data(), g_no, g_no,
                                       new_lock.data(), new_hidden.data(), new_sel.data());
    if (new_hl < 0) {
        for (int i = 0; i < g_no; i++) {
            if (g_sel_flags[i]) {
                new_hl = i;
                break;
            }
        }
    }
    g_hl_obj = new_hl;
    normalize_object_order_to_array();
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
    return target_count;
}

int delete_object_targets_preserve_order(int active, const char *undo_label)
{
    if (g_no <= 0) return 0;

    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return 0;
    std::vector<unsigned char> remove((size_t)object_cap, 0);
    int sel_count = selected_count();
    if (sel_count > 0) {
        for (int i = 0; i < g_no; i++)
            remove[i] = g_sel_flags[i] != 0 ? 1 : 0;
    } else if (active >= 0 && active < g_no) {
        remove[active] = 1;
    } else {
        return 0;
    }

    int editable_targets = 0;
    for (int i = 0; i < g_no; i++) {
        if (!remove[i]) continue;
        if (object_action_runtime_locked(i)) {
            remove[i] = 0;
            continue;
        }
        editable_targets++;
    }
    if (editable_targets <= 0) {
        stage_set_toast("Runtime placements are move-only");
        return 0;
    }

    undo_save_ex(undo_label ? undo_label : "Delete");
    int removed = 0;
    for (int i = g_no - 1; i >= 0; i--) {
        if (!remove[i]) continue;
        mk2_delete_object_preserve_order(i);
        removed++;
    }
    if (removed <= 0) {
        stage_set_toast("Runtime placements are move-only");
        return 0;
    }

    object_actions_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    return removed;
}

void delete_object_menu_targets(int active)
{
    int removed = delete_object_targets_preserve_order(active, "Delete");
    if (!removed) return;
}

static void delete_selected_objects_preserve_order(void)
{
    int sel_count = selected_count();
    if (sel_count > 0) {
        delete_object_targets_preserve_order(-1, "Delete");
    }
}

void duplicate_object_menu_targets(int active)
{
    if (active < 0 || active >= g_no) return;
    int sel_count = selected_count();
    int needed = g_no + (sel_count > 0 ? sel_count : 1);
    if (!editor_project_reserve_objects(needed)) return;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0 || g_no >= object_cap) return;
    bool has_editable_target = false;
    for (int i = 0; i < g_no; i++) {
        if (object_menu_targeted(i, active, sel_count) && !object_action_runtime_locked(i)) {
            has_editable_target = true;
            break;
        }
    }
    if (!has_editable_target) {
        stage_set_toast("Runtime placements are move-only");
        return;
    }
    undo_save_ex("Duplicate");
    int max_order = mk2_max_object_order();
    int original_no = g_no;
    int added = 0;
    for (int i = 0; i < original_no && g_no < object_cap; i++) {
        if (!object_menu_targeted(i, active, sel_count)) continue;
        if (object_action_runtime_locked(i)) continue;
        Obj *dst = editor_project_append_object_slot();
        if (!dst) break;
        int dst_i = g_no - 1;
        *dst = g_obj[i];
        dst->depth += 16;
        dst->sy += 8;
        dst->order = ++max_order;
        g_hl_obj = dst_i;
        added++;
    }
    if (added <= 0) {
        stage_set_toast("Runtime placements are move-only");
        return;
    }
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
}

int flip_object_targets_mirrored(int active, bool horizontal, const char *undo_label)
{
    int sel_count = selected_count();
    if (sel_count <= 0 && (active < 0 || active >= g_no)) return 0;

    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return 0;
    std::vector<unsigned char> target((size_t)object_cap, 0);
    int target_count = 0;
    int bx0 = INT_MAX, by0 = INT_MAX, bx1 = INT_MIN, by1 = INT_MIN;
    for (int i = 0; i < g_no; i++) {
        if (!object_menu_targeted(i, active, sel_count) || g_obj_lock[i] ||
            object_action_runtime_locked(i)) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = (im && im->w > 0) ? im->w : 1;
        int oh = (im && im->h > 0) ? im->h : 1;
        target[i] = 1;
        target_count++;
        if (g_obj[i].depth < bx0) bx0 = g_obj[i].depth;
        if (g_obj[i].sy    < by0) by0 = g_obj[i].sy;
        if (g_obj[i].depth + ow > bx1) bx1 = g_obj[i].depth + ow;
        if (g_obj[i].sy    + oh > by1) by1 = g_obj[i].sy    + oh;
    }
    if (target_count <= 0) return 0;

    ObjectRecordUndoCapture undo;
    if (!object_record_undo_capture_mask(&undo, target.data())) return 0;
    bool mirror_positions = target_count > 1;
    for (int i = 0; i < g_no; i++) {
        if (!target[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = (im && im->w > 0) ? im->w : 1;
        int oh = (im && im->h > 0) ? im->h : 1;
        if (mirror_positions) {
            if (horizontal)
                g_obj[i].depth = bx0 + bx1 - (g_obj[i].depth + ow);
            else
                g_obj[i].sy = by0 + by1 - (g_obj[i].sy + oh);
        }
        if (horizontal) g_obj[i].hfl ^= 1;
        else            g_obj[i].vfl ^= 1;
        g_obj[i].wx = (g_obj[i].wx & ~0x30) |
                      (g_obj[i].hfl ? 0x10 : 0) |
                      (g_obj[i].vfl ? 0x20 : 0);
    }
    if (object_record_undo_commit(&undo, undo_label ? undo_label : (horizontal ? "H-Flip" : "V-Flip")) > 0) {
        g_need_rebuild = 1;
        g_view_changed = 1;
    }
    return target_count;
}

void flip_object_menu_targets(int active, bool horizontal)
{
    flip_object_targets_mirrored(active, horizontal, horizontal ? "H-Flip" : "V-Flip");
}

void center_view_on_object(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    Img *im = img_find(g_obj[idx].ii);
    if (!im) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int center_x = 0, center_y = 0;
    bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                        0, 0, g_zoom, &center_x, &center_y);
    g_view_x = g_obj[idx].depth + im->w / 2 - center_x;
    g_view_y = g_obj[idx].sy + im->h / 2 - center_y;
    g_view_changed = 1;
}

void edit_block_for_object(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    Img *im = img_find(g_obj[idx].ii);
    if (!im) return;
    if (runtime_actor_image_is_preview_import(im)) {
        stage_set_toast("Runtime source art is read-only");
        return;
    }
    g_block_edit_img = (int)(im - g_img);
    g_block_edit_zoom = 8;
    g_block_edit_col = 0;
    g_block_edit_open = true;
}

void export_object_image_png_dialog(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    Obj *o = &g_obj[idx];
    Img *im = img_find(o->ii);
    if (!im || !im->pix) return;
    int pi = (o->fl >= 0 && o->fl < g_n_pals) ? o->fl
           : (im->pal_idx >= 0 ? im->pal_idx : 0);
    const Uint32 *pal = (pi >= 0 && pi < g_n_pals) ? g_pals[pi] : NULL;
    if (!pal) return;

    char out[512] = "";
    if (!file_dialog_save("Export Image as PNG", "PNG Files\0*.png\0All Files\0*.*\0",
                          out, sizeof out))
        return;
    size_t len = strlen(out);
    if (len < 4 || strcasecmp(out + len - 4, ".png") != 0)
        strncat(out, ".png", sizeof out - len - 1);

    unsigned char *rgba = (unsigned char *)malloc((size_t)im->w * im->h * 4);
    if (!rgba) return;
    for (int py = 0; py < im->h; py++) {
        for (int px = 0; px < im->w; px++) {
            Uint8 v = im->pix[py * im->w + px];
            Uint32 c = (v == 0) ? 0u : pal[v];
            int op = (py * im->w + px) * 4;
            rgba[op + 0] = (c >> 16) & 0xFF;
            rgba[op + 1] = (c >>  8) & 0xFF;
            rgba[op + 2] =  c        & 0xFF;
            rgba[op + 3] = (v == 0) ? 0 : 0xFF;
        }
    }
    stbi_write_png(out, im->w, im->h, 4, rgba, im->w * 4);
    free(rgba);
}

void select_all_with_image_ii(int image_ii)
{
    if (image_ii < 0) return;
    object_actions_clear_selection();
    g_hl_obj = -1;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].ii == image_ii) {
            g_sel_flags[i] = 1;
            if (g_hl_obj < 0)
                g_hl_obj = i;
        }
}

void select_all_in_layer_byte(int layer)
{
    if (layer < 0) return;
    object_actions_clear_selection();
    g_hl_obj = -1;
    for (int i = 0; i < g_no; i++)
        if (((g_obj[i].wx >> 8) & 0xFF) == layer) {
            g_sel_flags[i] = 1;
            if (g_hl_obj < 0)
                g_hl_obj = i;
        }
}

void assign_layer_to_object_targets(int active, int layer)
{
    if (active < 0 || active >= g_no) return;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return;
    int sel_count = selected_count();
    std::vector<unsigned char> target((size_t)object_cap, 0);
    for (int i = 0; i < g_no && i < object_cap; i++)
        if (object_menu_targeted(i, active, sel_count) && !g_obj_lock[i])
            target[i] = 1;
    ObjectRecordUndoCapture undo;
    if (!object_record_undo_capture_mask(&undo, target.data())) return;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        if (!target[(size_t)i]) continue;
        g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (layer << 8);
    }
    if (object_record_undo_commit(&undo, "Assign Layer") > 0)
        g_view_changed = 1;
}

void assign_palette_to_object_targets(int active, int pal)
{
    if (active < 0 || active >= g_no || pal < 0 || pal >= g_n_pals) return;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return;
    int sel_count = selected_count();
    std::vector<unsigned char> target((size_t)object_cap, 0);
    for (int i = 0; i < g_no && i < object_cap; i++)
        if (object_menu_targeted(i, active, sel_count))
            target[i] = 1;
    ObjectRecordUndoCapture undo;
    if (!object_record_undo_capture_mask(&undo, target.data())) return;
    for (int i = 0; i < g_no && i < object_cap; i++)
        if (target[(size_t)i])
            g_obj[i].fl = pal;
    object_record_undo_commit(&undo, "Assign Palette");
}

void assign_module_to_object_targets(int active, int module_idx)
{
    if (active < 0 || active >= g_no || module_idx < 0 || module_idx >= g_bdb_num_modules) return;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return;
    char mn[64] = "";
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (sscanf(g_bdb_modules[module_idx], "%63s %d %d %d %d", mn, &x1, &x2, &y1, &y2) < 5)
        return;
    int sel_count = selected_count();
    std::vector<unsigned char> target((size_t)object_cap, 0);
    for (int i = 0; i < g_no && i < object_cap; i++)
        if (object_menu_targeted(i, active, sel_count))
            target[i] = 1;
    ObjectPositionUndoCapture undo;
    if (!object_position_undo_capture_mask(&undo, target.data())) return;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        if (!target[(size_t)i]) continue;
        g_obj[i].depth = x1 + (g_obj[i].depth % 64);
        g_obj[i].sy = y1 + (g_obj[i].sy % 32);
    }
    if (object_position_undo_commit(&undo, "Assign Module") > 0)
        g_view_changed = 1;
    (void)x2; (void)y2;
}

/* A BDB module is a plain rectangle, so a sparse selection (e.g. two
 * ctrl-clicked objects far apart) unavoidably sweeps in anything else
 * sitting between them once a module spanning both is created. Counts
 * *visible*, non-selected objects inside [bx0,bx1]x[by0,by1] -- hidden
 * objects are excluded, since hiding them first (see hide_unselected_objects)
 * is how the user explicitly accepts they'll be swept in. */
static int count_visible_unselected_in_bounds(int bx0, int bx1, int by0, int by1)
{
    int n = 0;
    for (int i = 0; i < g_no; i++) {
        if (g_sel_flags[i] || g_obj_hidden[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        int ox2 = g_obj[i].depth + ow - 1;
        int oy2 = g_obj[i].sy + oh - 1;
        if (g_obj[i].depth > bx1 || ox2 < bx0 || g_obj[i].sy > by1 || oy2 < by0) continue;
        n++;
    }
    return n;
}

bool wrap_selected_objects_in_region(void)
{
    int sel_count = selected_count();
    if (!g_simple_mode || sel_count < 1 || !editor_project_reserve_modules(g_bdb_num_modules + 1))
        return false;
    int module_cap = editor_project_module_capacity();
    if (module_cap <= 0 || g_bdb_num_modules >= module_cap)
        return false;

    int bx0 = INT_MAX, bx1 = INT_MIN, by0 = INT_MAX, by1 = INT_MIN;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        if (g_obj[i].depth < bx0) bx0 = g_obj[i].depth;
        if (g_obj[i].depth + ow - 1 > bx1) bx1 = g_obj[i].depth + ow - 1;
        if (g_obj[i].sy < by0) by0 = g_obj[i].sy;
        if (g_obj[i].sy + oh - 1 > by1) by1 = g_obj[i].sy + oh - 1;
    }
    if (bx0 >= bx1 || by0 >= by1)
        return false;

    int swept = count_visible_unselected_in_bounds(bx0, bx1, by0, by1);
    if (swept > 0) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "%d other visible object(s) are inside that area too -- hide them first "
                 "(right-click > Hide Unselected) if that's intended, or adjust your selection.",
                 swept);
        stage_set_toast(msg);
        return false;
    }

    undo_save();
    char name[64];
    module_generate_unique_name(name, sizeof name, "MOD");
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d", name, bx0, bx1, by0, by1);
    if (editor_project_insert_module_line_before_enclosing(line, bx0, bx1, by0, by1) < 0)
        return false;
    sync_bdb_header_counts();
    g_dirty = 1;
    return true;
}

bool create_module_from_selection(void)
{
    if (selected_count() < 1)
        return false;
    int module_cap = editor_project_module_capacity();
    if (module_cap <= 0 || g_bdb_num_modules >= module_cap ||
        !editor_project_reserve_modules(g_bdb_num_modules + 1))
        return false;

    int bx0 = INT_MAX, bx1 = INT_MIN, by0 = INT_MAX, by1 = INT_MIN;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        if (g_obj[i].depth < bx0) bx0 = g_obj[i].depth;
        if (g_obj[i].depth + ow - 1 > bx1) bx1 = g_obj[i].depth + ow - 1;
        if (g_obj[i].sy < by0) by0 = g_obj[i].sy;
        if (g_obj[i].sy + oh - 1 > by1) by1 = g_obj[i].sy + oh - 1;
    }
    if (bx0 > bx1 || by0 > by1)
        return false;

    int swept = count_visible_unselected_in_bounds(bx0, bx1, by0, by1);
    if (swept > 0) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "%d other visible object(s) are inside that area too -- hide them first "
                 "(right-click > Hide Unselected) if that's intended, or adjust your selection.",
                 swept);
        stage_set_toast(msg);
        return false;
    }

    /* Stage-prefixed and guaranteed unique, so the new anchor can't collide
       with another module here or, once promoted, with another stage's. */
    char name[64];
    module_generate_unique_name(name, sizeof name, "MOD");

    undo_save_ex("Create Module from Selection");
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d", name, bx0, bx1, by0, by1);
    if (editor_project_insert_module_line_before_enclosing(line, bx0, bx1, by0, by1) < 0)
        return false;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_show_module_bounds = true;
    return true;
}

int module_collect_stats(int module_idx, int *palette_count,
                         int *layer_count, int *first_obj)
{
    int palette_cap = editor_project_palette_capacity();
    if (palette_cap <= 0) return 0;
    std::vector<unsigned char> pal_used((size_t)palette_cap, 0);
    bool layer_used[256];
    memset(layer_used, 0, sizeof layer_used);
    if (palette_count) *palette_count = 0;
    if (layer_count) *layer_count = 0;
    if (first_obj) *first_obj = -1;
    if (module_idx < 0 || module_idx >= g_bdb_num_modules) return 0;

    int objects = 0;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) != module_idx)
            continue;
        if (first_obj && *first_obj < 0) *first_obj = i;
        objects++;
        if (g_obj[i].fl >= 0 && g_obj[i].fl < palette_cap)
            pal_used[g_obj[i].fl] = 1;
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        layer_used[layer] = true;
    }

    int pc = 0, lc = 0;
    for (int p = 0; p < g_n_pals && p < palette_cap; p++) if (pal_used[p]) pc++;
    for (int l = 0; l < 256; l++) if (layer_used[l]) lc++;
    if (palette_count) *palette_count = pc;
    if (layer_count) *layer_count = lc;
    return objects;
}

int module_select_objects(int module_idx)
{
    if (module_idx < 0 || module_idx >= g_bdb_num_modules) return 0;
    object_actions_clear_selection();
    int count = 0;
    int first = -1;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) != module_idx)
            continue;
        g_sel_flags[i] = 1;
        if (first < 0) first = i;
        count++;
    }
    if (first >= 0) {
        g_hl_obj = first;
        center_view_on_object(first);
    }
    return count;
}

void module_center_view(int module_idx)
{
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (!parse_module_bounds(module_idx, NULL, &x1, &x2, &y1, &y2)) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int cx = (x1 + x2) / 2;
    int cy = (y1 + y2) / 2;
    int center_x = 0, center_y = 0;
    bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                        0, 0, g_zoom, &center_x, &center_y);
    g_view_x = cx - center_x;
    g_view_y = cy - center_y;
    g_view_changed = 1;
}

void add_image_to_view_center(int img_i)
{
    int object_cap = editor_project_object_capacity();
    if (img_i < 0 || img_i >= g_ni || object_cap <= 0 || g_no >= object_cap) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int x = 0, y = 0;
    bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                        g_view_x, g_view_y, g_zoom, &x, &y);
    int pal = (g_img[img_i].pal_idx >= 0) ? g_img[img_i].pal_idx : 0;
    if (mk2_add_object_for_image(img_i, x, y, 0x41, pal, 0, 0, true))
        simple_ensure_module(g_hl_obj);
}
