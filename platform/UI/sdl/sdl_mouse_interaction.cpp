#include "UI/sdl/sdl_mouse_interaction.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/world_module_utils.h"
#include "UI/sdl/sdl_object_drag_autopan.h"
#include "UI/sdl/sdl_object_picker.h"
#include "UI/sdl/sdl_selection_rect.h"
#include "UI/sdl/sdl_tooltip.h"
#include "undo_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static std::vector<int> s_module_drag_x1;
static std::vector<int> s_module_drag_x2;
static std::vector<int> s_module_drag_y1;
static std::vector<int> s_module_drag_y2;
static std::vector<unsigned char> s_module_drag_mask;
static std::vector<unsigned char> s_module_drag_obj_mask;

static int first_selected_object(void)
{
    for (int i = 0; i < g_no; i++) {
        if (g_sel_flags[i])
            return i;
    }
    return -1;
}

static void toggle_object_selection_local(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    if (g_sel_flags[idx]) {
        g_sel_flags[idx] = 0;
        if (g_hl_obj == idx)
            g_hl_obj = first_selected_object();
    } else {
        g_sel_flags[idx] = 1;
        g_hl_obj = idx;
    }
}

static int ensure_drag_capacity(BddSdlMouseState *state)
{
    int want;
    int old_cap;
    int *new_depth;
    int *new_sy;

    if (!state)
        return 0;

    want = editor_project_object_capacity();
    if (want < g_no) want = g_no;
    if (want < 1) want = 1;
    if (state->obj_drag_depth_a && state->obj_drag_sy_a &&
        state->obj_drag_capacity >= want)
        return 1;

    old_cap = state->obj_drag_capacity;
    new_depth = (int *)std::realloc(state->obj_drag_depth_a,
                                    (size_t)want * sizeof state->obj_drag_depth_a[0]);
    if (!new_depth)
        return 0;
    state->obj_drag_depth_a = new_depth;

    new_sy = (int *)std::realloc(state->obj_drag_sy_a,
                                 (size_t)want * sizeof state->obj_drag_sy_a[0]);
    if (!new_sy)
        return 0;
    state->obj_drag_sy_a = new_sy;

    if (want > old_cap) {
        std::memset(state->obj_drag_depth_a + old_cap, 0,
                    (size_t)(want - old_cap) * sizeof state->obj_drag_depth_a[0]);
        std::memset(state->obj_drag_sy_a + old_cap, 0,
                    (size_t)(want - old_cap) * sizeof state->obj_drag_sy_a[0]);
    }
    state->obj_drag_capacity = want;
    return 1;
}

static int ensure_module_drag_capacity(BddSdlMouseState *state)
{
    if (!ensure_drag_capacity(state))
        return 0;

    int module_cap = editor_project_module_capacity();
    if (module_cap < g_bdb_num_modules) module_cap = g_bdb_num_modules;
    if (module_cap < 1) module_cap = 1;
    if ((int)s_module_drag_x1.size() < module_cap) {
        s_module_drag_x1.resize((size_t)module_cap, 0);
        s_module_drag_x2.resize((size_t)module_cap, 0);
        s_module_drag_y1.resize((size_t)module_cap, 0);
        s_module_drag_y2.resize((size_t)module_cap, 0);
        s_module_drag_mask.resize((size_t)module_cap, 0);
    }
    if ((int)s_module_drag_obj_mask.size() < state->obj_drag_capacity)
        s_module_drag_obj_mask.resize((size_t)state->obj_drag_capacity, 0);
    return 1;
}

static int hit_object_at(int wx, int wy, int skip_hidden, int skip_locked)
{
    for (int i = g_no - 1; i >= 0; i--) {
        int ox, oy;
        Obj *o;
        Img *im;

        if (skip_hidden && g_obj_hidden[i]) continue;
        if (skip_locked && g_obj_lock[i]) continue;
        o = &g_obj[i];
        im = img_find(o->ii);
        if (!im) continue;

        ox = o->depth;
        oy = o->sy;
        bdd_object_editor_origin(i, &ox, &oy);
        if (bg_editor_object_hit_test_at(i, ox, oy, wx, wy))
            return i;
    }
    return -1;
}

static int hit_selected_object_at(int wx, int wy)
{
    for (int i = g_no - 1; i >= 0; i--) {
        int ox, oy;
        Obj *o;
        Img *im;

        if (!g_sel_flags[i]) continue;
        if (g_obj_hidden[i]) continue;
        if (g_obj_lock[i]) continue;
        if (object_in_locked_module(i)) continue;
        o = &g_obj[i];
        im = img_find(o->ii);
        if (!im) continue;

        ox = o->depth;
        oy = o->sy;
        bdd_object_editor_origin(i, &ox, &oy);
        if (bg_editor_object_hit_test_at(i, ox, oy, wx, wy))
            return i;
    }
    return -1;
}

/* Find the module rectangle under a world point. When rectangles overlap, the
   smallest-area one wins so nested modules stay grabbable. */
static int hit_module_at(int wx, int wy, int *ox1, int *ox2, int *oy1, int *oy2)
{
    int best = -1;
    long best_area = 0;

    if (!g_show_module_bounds) return -1;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        if (x2 < x1 || y2 < y1) continue;
        if (wx < x1 || wx > x2 || wy < y1 || wy > y2) continue;
        long area = (long)(x2 - x1 + 1) * (long)(y2 - y1 + 1);
        if (best < 0 || area < best_area) {
            best = m;
            best_area = area;
            if (ox1) *ox1 = x1;
            if (ox2) *ox2 = x2;
            if (oy1) *oy1 = y1;
            if (oy2) *oy2 = y2;
        }
    }
    return best;
}

/* Module rectangles are often large enough to cover most of a stage, so using
   the full filled rect as a drag target makes object editing feel sticky.
   Keep selection/context hits broad, but only start a module move from an edge
   grab. */
static int hit_module_drag_edge_at(int wx, int wy, int zoom,
                                   int *ox1, int *ox2, int *oy1, int *oy2)
{
    int best = -1;
    long best_area = 0;
    int tol = 8;

    if (!g_show_module_bounds) return -1;
    if (zoom > 1)
        tol = (tol + zoom - 1) / zoom;
    if (tol < 1)
        tol = 1;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        if (x2 < x1 || y2 < y1) continue;
        if (wx < x1 - tol || wx > x2 + tol ||
            wy < y1 - tol || wy > y2 + tol)
            continue;
        bool on_edge = (std::abs(wx - x1) <= tol || std::abs(wx - x2) <= tol ||
                        std::abs(wy - y1) <= tol || std::abs(wy - y2) <= tol);
        if (!on_edge) continue;

        long area = (long)(x2 - x1 + 1) * (long)(y2 - y1 + 1);
        if (best < 0 || area < best_area) {
            best = m;
            best_area = area;
            if (ox1) *ox1 = x1;
            if (ox2) *ox2 = x2;
            if (oy1) *oy1 = y1;
            if (oy2) *oy2 = y2;
        }
    }
    return best;
}

static int begin_module_drag(BddSdlMouseState *state, int module_idx,
                             int mouse_x, int mouse_y,
                             int mx1, int mx2, int my1, int my2)
{
    if (!state || module_idx < 0 || module_idx >= g_bdb_num_modules)
        return 0;
    if (!ensure_module_drag_capacity(state))
        return 0;

    if (!module_selection_get(module_idx))
        module_selection_select_only(module_idx);

    std::fill(s_module_drag_mask.begin(), s_module_drag_mask.end(), 0);
    int drag_count = 0;
    for (int m = 0; m < g_bdb_num_modules && m < (int)s_module_drag_mask.size(); m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!module_selection_get(m)) continue;
        if (module_is_locked_by_index(m)) continue;
        if (!parse_module_bounds(m, NULL, &x1, &x2, &y1, &y2)) continue;
        s_module_drag_mask[(size_t)m] = 1;
        s_module_drag_x1[(size_t)m] = x1;
        s_module_drag_x2[(size_t)m] = x2;
        s_module_drag_y1[(size_t)m] = y1;
        s_module_drag_y2[(size_t)m] = y2;
        drag_count++;
    }
    if (drag_count <= 0)
        return 0;

    state->module_drag_idx = module_idx;
    state->module_drag_ox = mouse_x;
    state->module_drag_oy = mouse_y;
    state->module_drag_x1 = mx1;
    state->module_drag_x2 = mx2;
    state->module_drag_y1 = my1;
    state->module_drag_y2 = my2;
    state->module_drag_undo_saved = 0;
    return 1;
}

/* Move selected module rectangles, their member objects, and any separately
   selected loose objects by the same world delta. The undo snapshot and target
   capture happen lazily on the first real movement so a plain click on a
   module has no side effects. */
static void module_drag_apply(BddSdlMouseState *state, int mouse_x, int mouse_y, int zoom)
{
    int m = state->module_drag_idx;
    if (m < 0 || m >= g_bdb_num_modules) return;
    if (zoom <= 0) zoom = 1;

    int dwx = (mouse_x - state->module_drag_ox) / zoom;
    int dwy = (mouse_y - state->module_drag_oy) / zoom;
    if (g_grid_snap && g_grid_sx > 0 && g_grid_sy > 0) {
        dwx = (dwx / g_grid_sx) * g_grid_sx;
        dwy = (dwy / g_grid_sy) * g_grid_sy;
    }

    if (!state->module_drag_undo_saved) {
        if (dwx == 0 && dwy == 0)
            return;
        if (!ensure_module_drag_capacity(state))
            return;

        std::fill(s_module_drag_obj_mask.begin(), s_module_drag_obj_mask.end(), 0);
        int first_member = -1;
        int target_objects = 0;

        for (int i = 0; i < g_no && i < state->obj_drag_capacity; i++) {
            if (!g_sel_flags[i] || g_obj_lock[i])
                continue;
            Img *lim = img_find(g_obj[i].ii);
            if (lim) {
                int lm = module_smallest_containing(g_obj[i].depth, g_obj[i].sy, lim->w, lim->h);
                if (lm >= 0 && module_is_locked_by_index(lm))
                    continue;
            }
            s_module_drag_obj_mask[(size_t)i] = 1;
        }

        for (int i = 0; i < g_no; i++) {
            Img *im = img_find(g_obj[i].ii);
            if (!im) continue;
            int assigned = assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h);
            if (assigned < 0 || assigned >= (int)s_module_drag_mask.size() ||
                !s_module_drag_mask[(size_t)assigned])
                continue;
            if (i < (int)s_module_drag_obj_mask.size())
                s_module_drag_obj_mask[(size_t)i] = 1;
        }

        for (int i = 0; i < g_no && i < state->obj_drag_capacity; i++) {
            if (!s_module_drag_obj_mask[(size_t)i]) continue;
            if (first_member < 0) first_member = i;
            state->obj_drag_depth_a[i] = g_obj[i].depth;
            state->obj_drag_sy_a[i] = g_obj[i].sy;
            target_objects++;
        }

        undo_save_ex((module_selection_count() > 1 || target_objects > 0)
                     ? "Move Modules/Assets"
                     : "Move Module");
        if (first_member >= 0)
            g_hl_obj = first_member;
        state->module_drag_undo_saved = 1;
    }

    for (int mi = 0; mi < g_bdb_num_modules && mi < (int)s_module_drag_mask.size(); mi++) {
        char name[64] = "";
        if (!s_module_drag_mask[(size_t)mi]) continue;
        if (!parse_module_bounds(mi, name, NULL, NULL, NULL, NULL)) continue;
        char line[256];
        snprintf(line, sizeof line, "%s %d %d %d %d", name,
                 s_module_drag_x1[(size_t)mi] + dwx,
                 s_module_drag_x2[(size_t)mi] + dwx,
                 s_module_drag_y1[(size_t)mi] + dwy,
                 s_module_drag_y2[(size_t)mi] + dwy);
        editor_project_set_module_line(mi, line);
    }
    for (int i = 0; i < g_no; i++) {
        if (i >= state->obj_drag_capacity ||
            i >= (int)s_module_drag_obj_mask.size() ||
            !s_module_drag_obj_mask[(size_t)i])
            continue;
        g_obj[i].depth = state->obj_drag_depth_a[i] + dwx;
        g_obj[i].sy = state->obj_drag_sy_a[i] + dwy;
    }
    g_dirty = 1;
}

static void begin_pan_drag(BddSdlMouseState *state, int x, int y,
                           int view_x, int view_y)
{
    state->dragging = 1;
    state->drag_ox = x;
    state->drag_oy = y;
    state->drag_vx = view_x;
    state->drag_vy = view_y;
}

static void begin_selection_rect(BddSdlMouseState *state, int x, int y, int additive)
{
    state->sel_rx1 = state->sel_rx2 = x;
    state->sel_ry1 = state->sel_ry2 = y;
    state->sel_rect_active = 1;
    state->sel_rect_additive = additive;
}

static void start_drag_for_selected(BddSdlMouseState *state, int obj_idx, int x, int y)
{
    if (!ensure_drag_capacity(state))
        return;

    state->obj_drag_idx = obj_idx;
    state->obj_drag_ox = x;
    state->obj_drag_oy = y;
    SDL_CaptureMouse(SDL_TRUE);

    for (int si = 0; si < g_no; si++) {
        if (g_sel_flags[si]) {
            state->obj_drag_depth_a[si] = g_obj[si].depth;
            state->obj_drag_sy_a[si] = g_obj[si].sy;
        }
    }
}

int bdd_sdl_mouse_state_init(BddSdlMouseState *state)
{
    if (!state)
        return 0;

    std::memset(state, 0, sizeof *state);
    state->obj_drag_idx = -1;
    state->module_drag_idx = -1;
    return ensure_drag_capacity(state);
}

void bdd_sdl_mouse_state_shutdown(BddSdlMouseState *state)
{
    if (!state)
        return;

    std::free(state->obj_drag_depth_a);
    std::free(state->obj_drag_sy_a);
    state->obj_drag_depth_a = NULL;
    state->obj_drag_sy_a = NULL;
    state->obj_drag_capacity = 0;
    state->obj_drag_idx = -1;
}

void bdd_sdl_mouse_button_down(BddSdlMouseState *state,
                               const SDL_MouseButtonEvent *button,
                               int window_w,
                               int *view_x, int *view_y, int *zoom,
                               int *last_obj)
{
    int bx, by;

    if (!state || !button || !view_x || !view_y || !zoom || !last_obj)
        return;

    if (button->button == SDL_BUTTON_MIDDLE) {
        state->mmb_drag = 1;
        state->drag_ox = button->x;
        state->drag_oy = button->y;
        state->drag_vx = *view_x;
        state->drag_vy = *view_y;
    }

    if (button->button == SDL_BUTTON_LEFT) {
        int select_double_click = 0;

        bx = button->x;
        by = button->y;

        if (g_cur_tool == 0)
            select_double_click = (button->clicks >= 2);

        if (g_cur_tool == 0 && select_double_click) {
            g_cur_tool = 2;
            state->dragging = 0;
            state->obj_drag_idx = -1;
            state->obj_drag_use_position_delta = 0;
            state->sel_rect_active = 0;
            state->sel_rect_additive = 0;
            return;
        }

        if (g_cur_tool == 2) {
            if (g_have_bdb) {
                int wx2 = 0, wy2 = 0;
                bdd_screen_to_world(bx, by, *view_x, *view_y, *zoom, &wx2, &wy2);
                int hit_obj = hit_object_at(wx2, wy2, 1, 0);
                if (hit_obj >= 0) {
                    g_cur_tool = 0;
                    editor_project_clear_selection();
                    g_sel_flags[hit_obj] = 1;
                    g_hl_obj = hit_obj;
                    *last_obj = hit_obj;
                    bdd_tooltip_free();
                    return;
                }
            }
            begin_pan_drag(state, bx, by, *view_x, *view_y);
            return;
        }

        if (g_cur_tool == 3) {
            int old_z = *zoom;
            int wx2 = 0, wy2 = 0;
            bdd_screen_to_world(bx, by, *view_x, *view_y, old_z, &wx2, &wy2);
            if (SDL_GetModState() & KMOD_CTRL) {
                if (*zoom > 1) (*zoom)--;
            } else if (*zoom < 8) {
                (*zoom)++;
            }
            if (*zoom != old_z) {
                *view_x = wx2 - bx / *zoom;
                *view_y = wy2 - by / *zoom;
            }
            return;
        }

        if (g_cur_tool == 1 || g_cur_tool == 4) {
            int tool_before_place = g_cur_tool;
            if (g_place_tool_img >= 0 && g_place_tool_img < g_ni) {
                int pi = g_place_tool_img;
                Obj *po;

                bg_editor_set_action_label("Place");
                bg_editor_undo_save();
                po = editor_project_append_object_slot();
                if (po && ensure_drag_capacity(state)) {
                    int obj_i = g_no - 1;
                    int wx2 = 0, wy2 = 0;
                    bdd_screen_to_world(bx, by, *view_x, *view_y, *zoom, &wx2, &wy2);
                    po->wx = 0x4100;
                    po->depth = wx2;
                    po->sy = wy2;
                    po->ii = g_img[pi].idx;
                    po->fl = (g_img[pi].pal_idx >= 0) ? g_img[pi].pal_idx : 0;
                    po->hfl = 0;
                    po->vfl = 0;
                    po->order = obj_i;
                    g_hl_obj = obj_i;
                    editor_project_clear_selection();
                    g_sel_flags[obj_i] = 1;
                    state->obj_drag_idx = obj_i;
                    state->obj_drag_use_position_delta = 0;
                    state->obj_drag_ox = bx;
                    state->obj_drag_oy = by;
                    state->obj_drag_depth_a[obj_i] = po->depth;
                    state->obj_drag_sy_a[obj_i] = po->sy;
                    SDL_CaptureMouse(SDL_TRUE);
                }
            }
            if (tool_before_place == 1) {
                g_cur_tool = 0;
                return;
            }
        }

        if (bdd_object_picker_is_open() && bx >= window_w - bdd_object_picker_width()) {
            bdd_object_picker_select_at_y(by);
            return;
        }

        if (g_place_img >= 0 && g_place_img < g_ni) {
            Obj *o;
            Img *im;

            bg_editor_set_action_label("Place");
            bg_editor_undo_save();
            o = editor_project_append_object_slot();
            im = &g_img[g_place_img];
            if (o) {
                int saved_order = g_no - 1;
                int wx2 = 0, wy2 = 0;
                bdd_screen_to_world(bx, by, *view_x, *view_y, *zoom, &wx2, &wy2);
                o->wx = 0x4100;
                o->depth = wx2;
                o->sy = wy2;
                o->ii = im->idx;
                o->fl = (im->pal_idx >= 0) ? im->pal_idx : 0;
                o->hfl = 0;
                o->vfl = 0;
                o->order = saved_order;
                editor_project_sort_objects_by_layer_order();
                for (int i = 0; i < g_no; i++) {
                    if (g_obj[i].order == saved_order) {
                        *last_obj = i;
                        break;
                    }
                }
            }
            bdd_object_picker_cancel_place();
            return;
        }

        if ((SDL_GetModState() & KMOD_SHIFT) && g_have_bdb) {
            begin_selection_rect(state, bx, by, (SDL_GetModState() & KMOD_CTRL) != 0);
            return;
        }

        if (g_have_bdb) {
            int wx2 = 0, wy2 = 0;
            int ctrl_down = (SDL_GetModState() & KMOD_CTRL) != 0;
            int alt_down = (SDL_GetModState() & KMOD_ALT) != 0;
            int found_obj = 0;

            bdd_screen_to_world(bx, by, *view_x, *view_y, *zoom, &wx2, &wy2);
            state->obj_drag_idx = -1;
            state->obj_drag_use_position_delta = 0;

            if (!ctrl_down && !alt_down) {
                int selected_hit = hit_selected_object_at(wx2, wy2);
                if (selected_hit >= 0) {
                    state->obj_drag_use_position_delta = 1;
                    start_drag_for_selected(state, selected_hit, bx, by);
                    module_selection_clear();
                    bdd_tooltip_free();
                    return;
                }
            }

            if (g_show_modules && g_show_module_bounds && !alt_down) {
                int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
                int mhit = hit_module_at(wx2, wy2, &mx1, &mx2, &my1, &my2);
                if (mhit >= 0 && ctrl_down) {
                    module_selection_toggle(mhit);
                    bdd_tooltip_free();
                    return;
                }
                if (mhit >= 0 && module_is_locked_by_index(mhit)) {
                    bdd_tooltip_free();
                    return;
                }
                if (mhit >= 0) {
                    if (!module_selection_get(mhit))
                        module_selection_select_only(mhit);
                    if (begin_module_drag(state, mhit, bx, by, mx1, mx2, my1, my2)) {
                        SDL_CaptureMouse(SDL_TRUE);
                        bdd_tooltip_free();
                        return;
                    }
                }
            }

            for (int i = g_no - 1; i >= 0; i--) {
                Obj *o;
                Img *im;
                int ox, oy;

                if (g_obj_lock[i]) continue;
                if (object_in_locked_module(i)) continue;
                o = &g_obj[i];
                im = img_find(o->ii);
                if (!im) continue;

                ox = o->depth;
                oy = o->sy;
                bdd_object_editor_origin(i, &ox, &oy);
                if (!bg_editor_object_hit_test_at(i, ox, oy, wx2, wy2)) continue;

                found_obj = 1;
                if (ctrl_down && !alt_down) {
                    toggle_object_selection_local(i);
                    bdd_tooltip_free();
                    return;
                }

                if (alt_down) {
                    Obj src = *o;
                    bg_editor_set_action_label("Clone");
                    bg_editor_undo_save();
                    state->obj_drag_use_position_delta = 0;
                    Obj *clone = editor_project_append_object_slot();
                    if (clone) {
                        i = g_no - 1;
                        *clone = src;
                        clone->order = i;
                        clone->depth += 16;
                        clone->sy += 8;
                        editor_project_clear_selection();
                        g_sel_flags[i] = 1;
                        g_hl_obj = i;
                        g_dirty = 1;
                    }
                } else {
                    state->obj_drag_use_position_delta = 1;
                }

                if (!ensure_drag_capacity(state))
                    return;

                state->obj_drag_idx = i;
                state->obj_drag_ox = bx;
                state->obj_drag_oy = by;
                SDL_CaptureMouse(SDL_TRUE);
                if (!g_sel_flags[i]) {
                    editor_project_clear_selection();
                    g_sel_flags[i] = 1;
                }
                if (!(SDL_GetModState() & KMOD_ALT))
                    module_selection_clear();
                for (int si = 0; si < g_no; si++) {
                    if (g_sel_flags[si]) {
                        state->obj_drag_depth_a[si] = g_obj[si].depth;
                        state->obj_drag_sy_a[si] = g_obj[si].sy;
                    }
                }
                bdd_tooltip_free();
                return;
            }

            if (!found_obj) {
                int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
                int mhit = hit_module_at(wx2, wy2, &mx1, &mx2, &my1, &my2);
                if (mhit >= 0 && ctrl_down) {
                    module_selection_toggle(mhit);
                    bdd_tooltip_free();
                    return;
                }
                bool module_mode = g_show_modules && g_show_module_bounds;
                if (!module_mode)
                    mhit = hit_module_drag_edge_at(wx2, wy2, *zoom, &mx1, &mx2, &my1, &my2);
                if (mhit >= 0 && module_is_locked_by_index(mhit)) {
                    bdd_tooltip_free();
                    return;
                }
                if (mhit >= 0) {
                    if (!module_selection_get(mhit))
                        module_selection_select_only(mhit);
                    if (begin_module_drag(state, mhit, bx, by, mx1, mx2, my1, my2)) {
                        SDL_CaptureMouse(SDL_TRUE);
                        bdd_tooltip_free();
                        return;
                    }
                }
                if (!bdd_object_picker_is_open())
                    begin_selection_rect(state, bx, by, ctrl_down);
            }
            return;
        }

        if (!bdd_object_picker_is_open())
            begin_pan_drag(state, bx, by, *view_x, *view_y);
    }

    if (button->button == SDL_BUTTON_RIGHT) {
        if (g_place_img >= 0) {
            bdd_object_picker_cancel_place();
        } else if (g_have_bdb) {
            int wx2 = 0, wy2 = 0;
            bdd_screen_to_world(button->x, button->y,
                                *view_x, *view_y, *zoom, &wx2, &wy2);
            g_ctx_obj = hit_object_at(wx2, wy2, 0, 0);
            if (g_ctx_obj < 0) {
                g_ctx_module = hit_module_at(wx2, wy2, NULL, NULL, NULL, NULL);
                g_ctx_module_wx = wx2;
                g_ctx_module_wy = wy2;
            }
        }
    }
}

void bdd_sdl_mouse_button_up(BddSdlMouseState *state,
                             const SDL_MouseButtonEvent *button,
                             int view_x, int view_y, int zoom,
                             int window_w, int window_h,
                             int *last_obj)
{
    if (!state || !button)
        return;

    if (button->button == SDL_BUTTON_MIDDLE)
        state->mmb_drag = 0;

    if (button->button != SDL_BUTTON_LEFT)
        return;

    if (state->module_drag_idx >= 0) {
        if (state->module_drag_undo_saved) {
            g_dirty = 1;
            if (last_obj && g_hl_obj >= 0) *last_obj = g_hl_obj;
        }
        state->module_drag_idx = -1;
        SDL_CaptureMouse(SDL_FALSE);
        state->dragging = 0;
        return;
    }

    if (state->sel_rect_active) {
        state->sel_rect_active = 0;
        bdd_selection_rect_apply(state->sel_rx1, state->sel_ry1,
                                 state->sel_rx2, state->sel_ry2,
                                 view_x, view_y, zoom,
                                 window_w, window_h,
                                 state->sel_rect_additive);
        state->sel_rect_additive = 0;
    } else if (state->obj_drag_idx >= 0) {
        if (state->obj_drag_use_position_delta) {
            undo_save_object_position_delta_for_selection(
                state->obj_drag_depth_a,
                state->obj_drag_sy_a,
                state->obj_drag_capacity,
                "Move");
        } else {
            g_dirty = 1;
        }
        if (last_obj) *last_obj = state->obj_drag_idx;
        state->obj_drag_idx = -1;
        state->obj_drag_use_position_delta = 0;
        state->guide_vn = 0;
        state->guide_hn = 0;
        SDL_CaptureMouse(SDL_FALSE);
    }
    state->dragging = 0;
}

void bdd_sdl_mouse_motion(BddSdlMouseState *state,
                          const SDL_MouseMotionEvent *motion,
                          int *view_x, int *view_y, int zoom,
                          int *hover_x, int *hover_y,
                          Uint32 *hover_since,
                          int *hover_printed)
{
    if (!state || !motion || !view_x || !view_y)
        return;

    if (state->module_drag_idx >= 0) {
        module_drag_apply(state, motion->x, motion->y, zoom);
    } else if (state->sel_rect_active) {
        state->sel_rx2 = motion->x;
        state->sel_ry2 = motion->y;
    } else if (state->obj_drag_idx >= 0) {
        bdd_sdl_object_drag_update(state->obj_drag_idx,
                                   motion->x, motion->y,
                                   state->obj_drag_ox, state->obj_drag_oy,
                                   zoom,
                                   state->obj_drag_depth_a,
                                   state->obj_drag_sy_a,
                                   state->guide_vx, &state->guide_vn,
                                   state->guide_vy, &state->guide_hn);
    } else if (state->mmb_drag) {
        *view_x = state->drag_vx - (motion->x - state->drag_ox) / zoom;
        *view_y = state->drag_vy - (motion->y - state->drag_oy) / zoom;
    } else if (state->dragging) {
        *view_x = state->drag_vx - (motion->x - state->drag_ox) / zoom;
        *view_y = state->drag_vy - (motion->y - state->drag_oy) / zoom;
    }

    if (hover_x && hover_y && hover_since && hover_printed &&
        (motion->x != *hover_x || motion->y != *hover_y))
    {
        *hover_x = motion->x;
        *hover_y = motion->y;
        *hover_since = SDL_GetTicks();
        *hover_printed = 0;
        bdd_tooltip_free();
    }
}

void bdd_sdl_mouse_tick(BddSdlMouseState *state,
                        int *view_x, int *view_y,
                        int zoom, int window_w, int window_h,
                        int *hover_x, int *hover_y,
                        Uint32 *hover_since,
                        int *hover_printed)
{
    if (!state)
        return;

    bdd_sdl_object_drag_auto_pan(&state->obj_drag_idx,
                                 &state->guide_vn, &state->guide_hn,
                                 view_x, view_y,
                                 zoom, window_w, window_h,
                                 state->obj_drag_depth_a,
                                 state->obj_drag_sy_a,
                                 hover_x, hover_y,
                                 hover_since, hover_printed);
}
