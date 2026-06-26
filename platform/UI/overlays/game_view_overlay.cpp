#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/project_header.h"
#include "Core/world_module_utils.h"
#include "undo_manager.h"
#include "libs/stb_image_write.h"
#include <imgui.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct RectBounds {
    int valid;
    int x1, y1, x2, y2;
};

static void bounds_reset(RectBounds *b)
{
    if (!b) return;
    b->valid = 0;
    b->x1 = b->y1 = INT_MAX;
    b->x2 = b->y2 = INT_MIN;
}

static void bounds_add_rect(RectBounds *b, int x1, int y1, int x2, int y2)
{
    if (!b || x2 <= x1 || y2 <= y1) return;
    if (!b->valid) {
        b->valid = 1;
        b->x1 = x1; b->y1 = y1; b->x2 = x2; b->y2 = y2;
        return;
    }
    if (x1 < b->x1) b->x1 = x1;
    if (y1 < b->y1) b->y1 = y1;
    if (x2 > b->x2) b->x2 = x2;
    if (y2 > b->y2) b->y2 = y2;
}

static int rounded_div_nearest(int value, int step)
{
    if (step <= 0) return 0;
    if (value >= 0) return (value + step / 2) / step;
    return -((-value + step / 2) / step);
}
static void runtime_consider_snap_pair(int moving, int target, int snap, int *best_adj, int *snap_world)
{
    if (!best_adj || !snap_world) return;
    int adj = target - moving;
    if (abs(adj) <= snap && abs(adj) < abs(*best_adj)) {
        *best_adj = adj;
        *snap_world = target;
    }
}

static void runtime_consider_snap_target(const RectBounds *moving, const RectBounds *target,
                                         int snap, int *best_ax, int *best_ay,
                                         int *snap_x, int *snap_y)
{
    if (!moving || !moving->valid || !target || !target->valid) return;
    int mcx = (moving->x1 + moving->x2) / 2;
    int mcy = (moving->y1 + moving->y2) / 2;
    int tcx = (target->x1 + target->x2) / 2;
    int tcy = (target->y1 + target->y2) / 2;
    int xpairs[7][2] = {
        { moving->x1, target->x1 }, { moving->x2, target->x2 },
        { moving->x1, target->x2 }, { moving->x2, target->x1 },
        { mcx, tcx },               { moving->x1, tcx },
        { moving->x2, tcx }
    };
    int ypairs[7][2] = {
        { moving->y1, target->y1 }, { moving->y2, target->y2 },
        { moving->y1, target->y2 }, { moving->y2, target->y1 },
        { mcy, tcy },               { moving->y1, tcy },
        { moving->y2, tcy }
    };
    for (int p = 0; p < 7; p++) {
        runtime_consider_snap_pair(xpairs[p][0], xpairs[p][1], snap, best_ax, snap_x);
        runtime_consider_snap_pair(ypairs[p][0], ypairs[p][1], snap, best_ay, snap_y);
    }
}
static int game_preview_object_bounds_at_source(int obj_idx, int source_x, int source_y,
                                                RectBounds *out)
{
    if (obj_idx < 0 || obj_idx >= g_no || !out) return 0;

    int cur_x = g_obj[obj_idx].depth;
    int cur_y = g_obj[obj_idx].sy;
    int ox = cur_x;
    int oy = cur_y;
    gv_object_origin(obj_idx, &ox, &oy);

    ox += source_x - cur_x;
    oy += source_y - cur_y;

    float scroll = bdd_object_game_scroll_factor(obj_idx);
    int px = ox - (int)(g_scroll_pos * scroll);
    int py = oy - g_game_view_y;

    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!bg_editor_object_snap_rect_at(obj_idx, px, py, &x1, &y1, &x2, &y2))
        return 0;

    bounds_reset(out);
    bounds_add_rect(out, x1, y1, x2, y2);
    return out->valid;
}

static void game_preview_snap_drag_delta(int primary_idx,
                                         const int *init_depth,
                                         const int *init_sy,
                                         int *dx, int *dy,
                                         int *snap_x, int *snap_y,
                                         bool *has_snap_x, bool *has_snap_y)
{
    if (snap_x) *snap_x = 0;
    if (snap_y) *snap_y = 0;
    if (has_snap_x) *has_snap_x = false;
    if (has_snap_y) *has_snap_y = false;
    if (!dx || !dy || !init_depth || !init_sy) return;
    if (primary_idx < 0 || primary_idx >= g_no || !g_sel_flags[primary_idx])
        return;
    if (!g_have_bdb || g_grid_snap) return;

    int snap = bg_editor_snap_dist();
    if (snap < 1) return;

    RectBounds moving;
    if (!game_preview_object_bounds_at_source(primary_idx,
            init_depth[primary_idx] + *dx,
            init_sy[primary_idx] + *dy,
            &moving))
        return;

    int best_ax = snap + 1;
    int best_ay = snap + 1;
    int tx = 0;
    int ty = 0;

    for (int i = 0; i < g_no; i++) {
        if (g_sel_flags[i] || g_obj_hidden[i]) continue;
        RectBounds target;
        if (game_preview_object_bounds_at_source(i, g_obj[i].depth, g_obj[i].sy, &target))
            runtime_consider_snap_target(&moving, &target, snap, &best_ax, &best_ay, &tx, &ty);
    }

    if (abs(best_ax) <= snap) {
        *dx += best_ax;
        if (snap_x) *snap_x = tx;
        if (has_snap_x) *has_snap_x = true;
    }
    if (abs(best_ay) <= snap) {
        *dy += best_ay;
        if (snap_y) *snap_y = ty;
        if (has_snap_y) *has_snap_y = true;
    }
}

static int game_view_max_object_order(void)
{
    int max_order = 0;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;
    return max_order;
}

static void game_view_clear_selection(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap > 0)
        editor_project_clear_selection();
}

static int clone_game_preview_drag_targets(int hit)
{
    int object_cap = editor_project_object_capacity();
    if (hit < 0 || hit >= g_no || object_cap <= 0 || g_no >= object_cap) return -1;

    std::vector<unsigned char> target((size_t)object_cap, 0);
    int sel_count = selected_count();
    if (sel_count > 0 && g_sel_flags[hit]) {
        for (int i = 0; i < g_no; i++)
            target[i] = g_sel_flags[i] != 0 ? 1 : 0;
    } else {
        target[hit] = 1;
    }

    undo_save_ex("Clone");
    int original_no = g_no;
    int max_order = game_view_max_object_order();
    int new_hit = -1;
    int first_new = -1;

    game_view_clear_selection();
    for (int i = 0; i < original_no && g_no < object_cap; i++) {
        if (!target[i]) continue;
        Obj *clone = editor_project_append_object_slot();
        if (!clone) break;
        int dst = g_no - 1;
        *clone = g_obj[i];
        clone->depth += 16;
        clone->sy += 8;
        clone->order = ++max_order;
        g_obj_lock[dst] = 0;
        g_obj_hidden[dst] = 0;
        g_sel_flags[dst] = 1;
        if (first_new < 0) first_new = dst;
        if (i == hit) new_hit = dst;
    }

    if (first_new < 0) return -1;
    if (new_hit < 0) new_hit = first_new;
    g_hl_obj = new_hit;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    g_view_changed = 1;
    return new_hit;
}

enum GameViewModuleResizeHandle {
    GV_MOD_HANDLE_NONE   = 0,
    GV_MOD_HANDLE_LEFT   = 1 << 0,
    GV_MOD_HANDLE_RIGHT  = 1 << 1,
    GV_MOD_HANDLE_TOP    = 1 << 2,
    GV_MOD_HANDLE_BOTTOM = 1 << 3
};

struct GameViewModuleRect {
    int module_idx;
    char name[64];
    int x1, x2, y1, y2;
    float sx1, sy1, sx2, sy2;
    bool runtime;
    bool locked;
};

static bool game_view_name_ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

static void game_view_strip_bmod_suffix(const char *name, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!name) return;
    snprintf(out, out_sz, "%s", name);
    size_t n = strlen(out);
    if (n >= 4 && game_view_name_ieq(out + n - 4, "BMOD"))
        out[n - 4] = '\0';
}

static int game_view_find_stage_plane(const char *module_name, int *ox, int *oy,
                                      float *scroll, int *plane_idx)
{
    char want[64] = "";
    game_view_strip_bmod_suffix(module_name, want, sizeof want);
    if (!want[0])
        return 0;

    int n = bdd_stage_plane_count();
    for (int p = 0; p < n; p++) {
        char pn[64] = "";
        int px = 0, py = 0;
        float ps = 1.0f;
        if (!bdd_stage_plane_info(p, pn, sizeof pn, &px, &py, &ps, NULL))
            continue;
        char plane_base[64] = "";
        game_view_strip_bmod_suffix(pn, plane_base, sizeof plane_base);
        if (!game_view_name_ieq(want, plane_base))
            continue;
        if (ox) *ox = px;
        if (oy) *oy = py;
        if (scroll) *scroll = ps;
        if (plane_idx) *plane_idx = p;
        return 1;
    }
    return 0;
}

static bool game_view_module_block_bounds(const char *module_name,
                                          int *x1, int *x2,
                                          int *y1, int *y2)
{
    static BddBgndBlock blocks[768];
    int n = bdd_stage_module_blocks(module_name, blocks,
                                    (int)(sizeof blocks / sizeof blocks[0]));
    if (n <= 0)
        return false;

    int bx1 = INT_MAX, bx2 = INT_MIN;
    int by1 = INT_MAX, by2 = INT_MIN;
    for (int i = 0; i < n; i++) {
        int hdr = blocks[i].hdr;
        if (hdr < 0 || hdr >= g_ni)
            continue;
        Img *im = &g_img[hdr];
        if (im->w <= 0 || im->h <= 0)
            continue;
        if (blocks[i].x < bx1) bx1 = blocks[i].x;
        if (blocks[i].y < by1) by1 = blocks[i].y;
        if (blocks[i].x + im->w > bx2) bx2 = blocks[i].x + im->w;
        if (blocks[i].y + im->h > by2) by2 = blocks[i].y + im->h;
    }
    if (bx1 == INT_MAX || bx2 == INT_MIN || by1 == INT_MAX || by2 == INT_MIN)
        return false;
    if (x1) *x1 = bx1;
    if (x2) *x2 = bx2;
    if (y1) *y1 = by1;
    if (y2) *y2 = by2;
    return true;
}

static bool game_view_project_module_rect(int module_idx,
                                          float gx, float gy, int zoom,
                                          GameViewModuleRect *out)
{
    if (!out || module_idx < 0 || module_idx >= g_bdb_num_modules)
        return false;

    memset(out, 0, sizeof *out);
    out->module_idx = module_idx;
    if (!parse_module_bounds(module_idx, out->name, &out->x1, &out->x2,
                             &out->y1, &out->y2))
        return false;
    if (out->x2 < out->x1 || out->y2 < out->y1)
        return false;

    int ox = 0;
    int oy = 0;
    int plane_idx = -1;
    float scroll = 1.0f;
    int local_w = out->x2 - out->x1 + 1;
    int local_h = out->y2 - out->y1 + 1;
    float z = (float)(zoom > 0 ? zoom : 1);
    out->locked = module_is_locked(out->name);

    out->runtime = game_view_find_stage_plane(out->name, &ox, &oy,
                                              &scroll, &plane_idx) != 0;
    if (out->runtime) {
        int bx1 = 0, bx2 = local_w, by1 = 0, by2 = local_h;
        int start_x = 0, start_y = 0;
        int scroll_origin_x = 0;
        bdd_get_stage_start_camera(&start_x, &start_y);
        scroll_origin_x = start_x;
        if (plane_idx >= 0)
            bdd_stage_plane_scroll_origin(plane_idx, &scroll_origin_x);
        if (!game_view_module_block_bounds(out->name, &bx1, &bx2, &by1, &by2)) {
            bx1 = 0;
            by1 = 0;
            bx2 = local_w;
            by2 = local_h;
        }
        int parallax = scroll_origin_x + (int)((float)(g_scroll_pos - start_x) * scroll);
        out->sx1 = gx + (float)(ox + bx1 - parallax) * z;
        out->sy1 = gy + (float)(oy + by1 - g_game_view_y) * z;
        out->sx2 = gx + (float)(ox + bx2 - parallax) * z;
        out->sy2 = gy + (float)(oy + by2 - g_game_view_y) * z;
        return true;
    }

    out->sx1 = gx + (float)(out->x1 - g_scroll_pos) * z;
    out->sy1 = gy + (float)(out->y1 - g_game_view_y) * z;
    out->sx2 = out->sx1 + (float)local_w * z;
    out->sy2 = out->sy1 + (float)local_h * z;
    return true;
}

static void game_view_rewrite_module_bounds(int module_idx, const char *name,
                                            int x1, int x2, int y1, int y2)
{
    if (module_idx < 0 || module_idx >= g_bdb_num_modules)
        return;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d",
             (name && name[0]) ? name : "MOD", x1, x2, y1, y2);
    if (!editor_project_set_module_line(module_idx, line))
        return;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
}

static int game_view_module_handle_at(const GameViewModuleRect *r, ImVec2 mouse,
                                      float tol)
{
    if (!r) return GV_MOD_HANDLE_NONE;
    if (mouse.x < r->sx1 - tol || mouse.x > r->sx2 + tol ||
        mouse.y < r->sy1 - tol || mouse.y > r->sy2 + tol)
        return GV_MOD_HANDLE_NONE;

    float dl = fabsf(mouse.x - r->sx1);
    float dr = fabsf(mouse.x - r->sx2);
    float dt = fabsf(mouse.y - r->sy1);
    float db = fabsf(mouse.y - r->sy2);
    bool near_l = dl <= tol;
    bool near_r = dr <= tol;
    bool near_t = dt <= tol;
    bool near_b = db <= tol;

    int h = GV_MOD_HANDLE_NONE;
    if (near_l && near_r)
        h |= dl <= dr ? GV_MOD_HANDLE_LEFT : GV_MOD_HANDLE_RIGHT;
    else if (near_l)
        h |= GV_MOD_HANDLE_LEFT;
    else if (near_r)
        h |= GV_MOD_HANDLE_RIGHT;

    if (near_t && near_b)
        h |= dt <= db ? GV_MOD_HANDLE_TOP : GV_MOD_HANDLE_BOTTOM;
    else if (near_t)
        h |= GV_MOD_HANDLE_TOP;
    else if (near_b)
        h |= GV_MOD_HANDLE_BOTTOM;
    return h;
}

static void game_view_set_resize_cursor(int handle)
{
    bool x = (handle & (GV_MOD_HANDLE_LEFT | GV_MOD_HANDLE_RIGHT)) != 0;
    bool y = (handle & (GV_MOD_HANDLE_TOP | GV_MOD_HANDLE_BOTTOM)) != 0;
    if (x && y) {
        bool nwse = ((handle & GV_MOD_HANDLE_LEFT) && (handle & GV_MOD_HANDLE_TOP)) ||
                    ((handle & GV_MOD_HANDLE_RIGHT) && (handle & GV_MOD_HANDLE_BOTTOM));
        ImGui::SetMouseCursor(nwse ? ImGuiMouseCursor_ResizeNWSE
                                   : ImGuiMouseCursor_ResizeNESW);
    } else if (x) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (y) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
}

static void game_view_draw_module_handles(ImDrawList *dl,
                                          const GameViewModuleRect *r,
                                          ImU32 col)
{
    if (!dl || !r) return;
    float cx = (r->sx1 + r->sx2) * 0.5f;
    float cy = (r->sy1 + r->sy2) * 0.5f;
    const float s = 4.0f;
    ImVec2 pts[8] = {
        ImVec2(r->sx1, r->sy1), ImVec2(cx, r->sy1), ImVec2(r->sx2, r->sy1),
        ImVec2(r->sx1, cy),                         ImVec2(r->sx2, cy),
        ImVec2(r->sx1, r->sy2), ImVec2(cx, r->sy2), ImVec2(r->sx2, r->sy2)
    };
    for (int i = 0; i < 8; i++) {
        dl->AddRectFilled(ImVec2(pts[i].x - s, pts[i].y - s),
                          ImVec2(pts[i].x + s, pts[i].y + s),
                          IM_COL32(6, 10, 16, 220), 1.0f);
        dl->AddRect(ImVec2(pts[i].x - s, pts[i].y - s),
                    ImVec2(pts[i].x + s, pts[i].y + s),
                    col, 1.0f, 0, 1.5f);
    }
}

static bool draw_game_view_module_resize_overlay(ImDrawList *dl,
                                                 float gx, float gy,
                                                 float gw, float gh,
                                                 int zoom,
                                                 ImVec2 mouse,
                                                 bool hov,
                                                 bool blocked)
{
    static bool s_resize = false;
    static int  s_resize_mod = -1;
    static int  s_resize_handle = GV_MOD_HANDLE_NONE;
    static float s_resize_mouse_x = 0.0f;
    static float s_resize_mouse_y = 0.0f;
    static int s_resize_x1 = 0, s_resize_x2 = 0;
    static int s_resize_y1 = 0, s_resize_y2 = 0;
    static char s_resize_name[64] = "";
    static bool s_resize_undo_saved = false;

    if (!g_show_module_bounds || g_bdb_num_modules <= 0 || !dl) {
        if (!ImGui::IsMouseDown(0)) {
            s_resize = false;
            s_resize_mod = -1;
            s_resize_handle = GV_MOD_HANDLE_NONE;
            s_resize_undo_saved = false;
        }
        return false;
    }

    std::vector<GameViewModuleRect> rects;
    rects.reserve((size_t)g_bdb_num_modules);
    int hot_mod = -1;
    int hot_handle = GV_MOD_HANDLE_NONE;
    long hot_area = 0;
    float tol = 7.0f;
    if (zoom > 2) tol = 8.0f;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        GameViewModuleRect r;
        if (!game_view_project_module_rect(m, gx, gy, zoom, &r))
            continue;
        if (r.sx2 < gx || r.sx1 > gx + gw || r.sy2 < gy || r.sy1 > gy + gh) {
            rects.push_back(r);
            continue;
        }

        int handle = (!blocked && hov && g_cur_tool == 0)
            ? game_view_module_handle_at(&r, mouse, tol)
            : GV_MOD_HANDLE_NONE;
        if (handle != GV_MOD_HANDLE_NONE) {
            long area = (long)(r.x2 - r.x1 + 1) * (long)(r.y2 - r.y1 + 1);
            if (hot_mod < 0 || area < hot_area) {
                hot_mod = m;
                hot_handle = handle;
                hot_area = area;
            }
        }
        rects.push_back(r);
    }

    if (!s_resize && hot_mod >= 0 && !blocked && hov &&
        g_cur_tool == 0 && ImGui::IsMouseClicked(0)) {
        GameViewModuleRect *hot_rect = NULL;
        for (size_t i = 0; i < rects.size(); i++) {
            if (rects[i].module_idx == hot_mod) {
                hot_rect = &rects[i];
                break;
            }
        }
        if (hot_rect && !hot_rect->locked) {
            module_selection_select_only(hot_mod);
            s_resize = true;
            s_resize_mod = hot_mod;
            s_resize_handle = hot_handle;
            s_resize_mouse_x = mouse.x;
            s_resize_mouse_y = mouse.y;
            s_resize_x1 = hot_rect->x1;
            s_resize_x2 = hot_rect->x2;
            s_resize_y1 = hot_rect->y1;
            s_resize_y2 = hot_rect->y2;
            snprintf(s_resize_name, sizeof s_resize_name, "%s", hot_rect->name);
            s_resize_undo_saved = false;
        } else if (hot_rect && hot_rect->locked) {
            stage_set_toast("Module is locked");
        }
    }

    if (s_resize && ImGui::IsMouseDown(0)) {
        int dx = (int)((mouse.x - s_resize_mouse_x) / (float)(zoom > 0 ? zoom : 1));
        int dy = (int)((mouse.y - s_resize_mouse_y) / (float)(zoom > 0 ? zoom : 1));
        if (g_grid_snap) {
            if (g_grid_sx > 1)
                dx = rounded_div_nearest(dx, g_grid_sx) * g_grid_sx;
            if (g_grid_sy > 1)
                dy = rounded_div_nearest(dy, g_grid_sy) * g_grid_sy;
        }

        int nx1 = s_resize_x1;
        int nx2 = s_resize_x2;
        int ny1 = s_resize_y1;
        int ny2 = s_resize_y2;
        if (s_resize_handle & GV_MOD_HANDLE_LEFT)   nx1 = s_resize_x1 + dx;
        if (s_resize_handle & GV_MOD_HANDLE_RIGHT)  nx2 = s_resize_x2 + dx;
        if (s_resize_handle & GV_MOD_HANDLE_TOP)    ny1 = s_resize_y1 + dy;
        if (s_resize_handle & GV_MOD_HANDLE_BOTTOM) ny2 = s_resize_y2 + dy;
        if (nx2 < nx1) {
            if (s_resize_handle & GV_MOD_HANDLE_LEFT) nx1 = nx2;
            else nx2 = nx1;
        }
        if (ny2 < ny1) {
            if (s_resize_handle & GV_MOD_HANDLE_TOP) ny1 = ny2;
            else ny2 = ny1;
        }

        if (nx1 != s_resize_x1 || nx2 != s_resize_x2 ||
            ny1 != s_resize_y1 || ny2 != s_resize_y2) {
            if (!s_resize_undo_saved) {
                undo_save_ex("Resize Module Frame");
                s_resize_undo_saved = true;
            }
            game_view_rewrite_module_bounds(s_resize_mod, s_resize_name,
                                            nx1, nx2, ny1, ny2);
        }
    }

    if (s_resize && !ImGui::IsMouseDown(0)) {
        if (s_resize_undo_saved) {
            char msg[128];
            int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            char name[64] = "";
            if (parse_module_bounds(s_resize_mod, name, &x1, &x2, &y1, &y2)) {
                snprintf(msg, sizeof msg, "Resized %s to %dx%d",
                         name, x2 - x1 + 1, y2 - y1 + 1);
                stage_set_toast(msg);
            }
        }
        s_resize = false;
        s_resize_mod = -1;
        s_resize_handle = GV_MOD_HANDLE_NONE;
        s_resize_undo_saved = false;
    }

    for (size_t i = 0; i < rects.size(); i++) {
        const GameViewModuleRect &r = rects[i];
        bool selected = module_selection_get(r.module_idx);
        bool hot = (r.module_idx == hot_mod);
        bool active = s_resize && r.module_idx == s_resize_mod;
        ImU32 fill = r.locked ? IM_COL32(255, 150, 30, 18) :
                    selected ? IM_COL32(255, 220, 60, 24) :
                               IM_COL32(80, 130, 255, 13);
        ImU32 line = r.locked ? IM_COL32(255, 165, 40, 225) :
                    (hot || active) ? IM_COL32(110, 240, 255, 245) :
                    selected ? IM_COL32(255, 230, 90, 235) :
                               IM_COL32(130, 170, 255, 160);
        float thick = (hot || active || selected) ? 2.2f : 1.3f;
        dl->AddRectFilled(ImVec2(r.sx1, r.sy1), ImVec2(r.sx2, r.sy2), fill);
        dl->AddRect(ImVec2(r.sx1, r.sy1), ImVec2(r.sx2, r.sy2), line, 0.0f, 0, thick);

        if (selected || hot || active)
            game_view_draw_module_handles(dl, &r, line);

        if (selected || hot || active) {
            char label[128];
            snprintf(label, sizeof label, "%s%d %s  %dx%d%s",
                     r.locked ? "[LOCKED] " : "", r.module_idx, r.name,
                     r.x2 - r.x1 + 1, r.y2 - r.y1 + 1,
                     r.runtime ? "" : " src");
            ImVec2 ts = ImGui::CalcTextSize(label);
            float lx = r.sx1 + 5.0f;
            float ly = r.sy1 + 4.0f;
            if (lx + ts.x + 8.0f > gx + gw)
                lx = gx + gw - ts.x - 8.0f;
            if (lx < gx + 3.0f) lx = gx + 3.0f;
            if (ly + ts.y + 5.0f > gy + gh)
                ly = gy + gh - ts.y - 5.0f;
            if (ly < gy + 3.0f) ly = gy + 3.0f;
            dl->AddRectFilled(ImVec2(lx - 3.0f, ly - 2.0f),
                              ImVec2(lx + ts.x + 4.0f, ly + ts.y + 3.0f),
                              IM_COL32(4, 7, 12, 195), 2.0f);
            dl->AddText(ImVec2(lx, ly), r.locked ? IM_COL32(255, 210, 150, 245)
                                                 : IM_COL32(230, 245, 255, 245),
                        label);
        }
    }

    if (s_resize) {
        game_view_set_resize_cursor(s_resize_handle);
        return true;
    }
    if (hot_mod >= 0 && !blocked && hov && g_cur_tool == 0) {
        game_view_set_resize_cursor(hot_handle);
        if (ImGui::IsMouseDown(0))
            return true;
        ImGui::SetTooltip("Drag module edge to resize; drag a corner to resize width and height.");
        return true;
    }
    return false;
}

/* ---- export one 400x254 game view frame as PNG -------------------- */

static void export_game_frame_png(int frame_n)
{
    const int GW = 400, GH = 254;
    unsigned char *buf = (unsigned char *)calloc(GW * GH, 4);
    if (!buf) return;
    for (int i = 0; i < g_no; i++) {
        if (g_obj_hidden[i]) continue;
        Obj *o  = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im || !im->pix) continue;
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                            ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;
        int base_x = o->depth, base_y = o->sy;
        float f  = bdd_object_game_scroll_factor(i);
        gv_object_origin(i, &base_x, &base_y);
        int   ox = base_x - (int)(g_scroll_pos * f);
        int   oy = base_y - g_game_view_y;
        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                int   sx2 = o->hfl ? (im->w - 1 - x) : x;
                int   sy2 = (o->vfl ? (im->h - 1 - y) : y) * im->w;
                Uint8 v   = im->pix[sy2 + sx2];
                if (!v) continue;
                int px = ox + x, py = oy + y;
                if (px < 0 || px >= GW || py < 0 || py >= GH) continue;
                Uint32 c = pal[v];
                size_t off = ((size_t)py * GW + px) * 4;
                buf[off+0] = (c>>16)&0xFF; buf[off+1] = (c>>8)&0xFF;
                buf[off+2] =  c    &0xFF;  buf[off+3] = 0xFF;
            }
        }
    }
    char path[640];
    snprintf(path, sizeof(path), "%s\\frame_%04d.png", g_record_dir, frame_n);
    stbi_write_png(path, GW, GH, 4, buf, GW * 4);
    free(buf);
}

void draw_game_view_overlay(void)
{
    if (!g_game_view || !g_have_bdb || g_no == 0) return;

    /* ---- current preview bounds (raw source or runtime-local layout) ---- */
    int wx_min = 0, wx_max = 400, wy_min = 0, wy_max = 254;
    bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    int scroll_max   = wx_max - 400;
    int scroll_y_max = wy_max - 254;

    /* ---- animation advance ---- */
    if (g_anim_playing) {
        float dt = ImGui::GetIO().DeltaTime;
        g_anim_fpos += g_anim_dir * g_anim_speed * dt;
        if (g_anim_bounce) {
            if (g_anim_fpos >= (float)scroll_max) {
                g_anim_fpos = (float)scroll_max; g_anim_dir = -1;
            }
            if (g_anim_fpos <= (float)wx_min) {
                g_anim_fpos = (float)wx_min; g_anim_dir = 1;
            }
        } else {
            if (g_anim_fpos >= (float)scroll_max) g_anim_fpos = (float)wx_min;
            if (g_anim_fpos < (float)wx_min) g_anim_fpos = (float)scroll_max;
        }
        g_scroll_pos = (int)g_anim_fpos;

        /* vertical sweep */
        if (g_anim_v_sweep && scroll_y_max > 0) {
            g_anim_vy_fpos += g_anim_vy_dir * g_anim_vy_speed * dt;
            if (g_anim_bounce) {
                if (g_anim_vy_fpos >= (float)scroll_y_max) {
                    g_anim_vy_fpos = (float)scroll_y_max; g_anim_vy_dir = -1;
                }
                if (g_anim_vy_fpos <= (float)wy_min) {
                    g_anim_vy_fpos = (float)wy_min; g_anim_vy_dir = 1;
                }
            } else {
                if (g_anim_vy_fpos >= (float)scroll_y_max) g_anim_vy_fpos = (float)wy_min;
                if (g_anim_vy_fpos < (float)wy_min) g_anim_vy_fpos = (float)scroll_y_max;
            }
            g_game_view_y = (int)g_anim_vy_fpos;
        }

        /* recording at 30 fps */
        if (g_recording && g_record_dir[0]) {
            g_record_accum += dt;
            const float frame_dt = 1.0f / 30.0f;
            while (g_record_accum >= frame_dt && g_record_n < 1800) {
                export_game_frame_png(g_record_n++);
                g_record_accum -= frame_dt;
            }
            if (g_record_n >= 1800) { g_recording = false; g_record_accum = 0.0f; }
        }

        pal_animation_step(dt);
    }

    ImVec2 ds     = ImGui::GetIO().DisplaySize;
    BddScreenRect viewport;
    bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);
    float  gw     = (float)viewport.w;
    float  gh     = (float)viewport.h;
    float  gx     = (float)viewport.x;
    float  gy     = (float)viewport.y;

    /* invisible window covering the game viewport rectangle */
    ImGui::SetNextWindowPos(ImVec2(gx, gy));
    ImGui::SetNextWindowSize(ImVec2(gw, gh));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin("##gv_overlay", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!open) { ImGui::End(); return; }

    ImDrawList *dl   = ImGui::GetWindowDrawList();
    ImVec2      mpos = ImGui::GetIO().MousePos;
    bool        hov  = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    /* hit-test: find topmost object under screen pos (mx, my) */
    auto hit_obj = [&](float mx, float my) -> int {
        for (int i = g_no - 1; i >= 0; i--) {
            if (g_obj_hidden[i]) continue;
            Obj *o  = &g_obj[i];
            Img *im = img_find(o->ii);
            if (!im) continue;
            int ox = o->depth, oy = o->sy;
            float f  = bdd_object_game_scroll_factor(i);
            gv_object_origin(i, &ox, &oy);
            int local_x = ox - (int)(g_scroll_pos * f);
            int local_y = oy - g_game_view_y;
            int px = (int)((mx - gx) / (float)g_zoom);
            int py = (int)((my - gy) / (float)g_zoom);
            if (bg_editor_object_hit_test_at(i, local_x, local_y, px, py))
                return i;
        }
        return -1;
    };

    static bool  s_drag       = false;
    static bool  s_cam_drag   = false;
    static bool  s_moved      = false;
    static bool  s_use_position_delta = false;
    static float s_start_mx, s_start_my;
    static std::vector<int> s_init_depth;
    static std::vector<int> s_init_sy;
    static int   s_cam_start  = 0;
    static int   s_drag_idx   = -1;
    static int   s_snap_x     = 0;
    static int   s_snap_y     = 0;
    static bool  s_has_snap_x = false;
    static bool  s_has_snap_y = false;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) {
        ImGui::End();
        return;
    }
    if ((int)s_init_depth.size() != object_cap) {
        s_init_depth.assign((size_t)object_cap, 0);
        s_init_sy.assign((size_t)object_cap, 0);
    }

    static bool  s_fighter_box_drag = false;
    static float s_fighter_box_grab_x = 0.0f;
    static float s_fighter_box_grab_y = 0.0f;
    bool fighter_box_hover = false;
    if (g_match_start_fighter_box_visible) {
        if (g_match_start_fighter_box_w < 8) g_match_start_fighter_box_w = 8;
        if (g_match_start_fighter_box_h < 16) g_match_start_fighter_box_h = 16;
        if (g_match_start_fighter_box_w > 160) g_match_start_fighter_box_w = 160;
        if (g_match_start_fighter_box_h > 240) g_match_start_fighter_box_h = 240;
        if (g_match_start_fighter_box_x < 0) g_match_start_fighter_box_x = 0;
        int max_box_x = 400 - g_match_start_fighter_box_w;
        if (max_box_x < 0) max_box_x = 0;
        if (g_match_start_fighter_box_x > max_box_x)
            g_match_start_fighter_box_x = max_box_x;
        int min_box_y = wy_min - g_match_start_fighter_box_h;
        int max_box_y = wy_max;
        if (max_box_y < min_box_y) max_box_y = min_box_y;
        if (g_match_start_fighter_box_y < min_box_y)
            g_match_start_fighter_box_y = min_box_y;
        if (g_match_start_fighter_box_y > max_box_y)
            g_match_start_fighter_box_y = max_box_y;

        float z = (float)(g_zoom > 0 ? g_zoom : 1);
        float bx1 = gx + (float)g_match_start_fighter_box_x * z;
        float by1 = gy + (float)(g_match_start_fighter_box_y - g_game_view_y) * z;
        float bx2 = bx1 + (float)g_match_start_fighter_box_w * z;
        float by2 = by1 + (float)g_match_start_fighter_box_h * z;
        fighter_box_hover = hov && mpos.x >= bx1 && mpos.x <= bx2 &&
                            mpos.y >= by1 && mpos.y <= by2;

        if (fighter_box_hover && ImGui::IsMouseClicked(0) && g_cur_tool == 0) {
            s_fighter_box_drag = true;
            s_fighter_box_grab_x = (mpos.x - bx1) / z;
            s_fighter_box_grab_y = (mpos.y - by1) / z;
        }
        if (s_fighter_box_drag && ImGui::IsMouseDown(0)) {
            int nx = (int)((mpos.x - gx) / z - s_fighter_box_grab_x);
            int ny = g_game_view_y + (int)((mpos.y - gy) / z - s_fighter_box_grab_y);
            if (nx < 0) nx = 0;
            if (nx > max_box_x) nx = max_box_x;
            if (ny < min_box_y) ny = min_box_y;
            if (ny > max_box_y) ny = max_box_y;
            g_match_start_fighter_box_x = nx;
            g_match_start_fighter_box_y = ny;
            g_stage_start_ground_y = g_match_start_fighter_box_y + g_match_start_fighter_box_h;
            g_stage_start_ground_enabled = true;
        }
        if (!ImGui::IsMouseDown(0))
            s_fighter_box_drag = false;

        bx1 = gx + (float)g_match_start_fighter_box_x * z;
        by1 = gy + (float)(g_match_start_fighter_box_y - g_game_view_y) * z;
        bx2 = bx1 + (float)g_match_start_fighter_box_w * z;
        by2 = by1 + (float)g_match_start_fighter_box_h * z;
        ImU32 box_line = s_fighter_box_drag || fighter_box_hover
                       ? IM_COL32(110, 240, 170, 245)
                       : IM_COL32(110, 240, 170, 190);
        dl->AddRectFilled(ImVec2(bx1, by1), ImVec2(bx2, by2),
                          IM_COL32(50, 180, 120, 28));
        dl->AddRect(ImVec2(bx1, by1), ImVec2(bx2, by2), box_line, 0.0f, 0, 2.0f);
        dl->AddLine(ImVec2(gx, by2), ImVec2(gx + gw, by2),
                    IM_COL32(110, 240, 170, 150), 1.0f);
        char flabel[48];
        snprintf(flabel, sizeof flabel, "Ground Y %d", g_stage_start_ground_y);
        ImVec2 fs = ImGui::CalcTextSize(flabel);
        float tx = bx2 + 5.0f;
        if (tx + fs.x + 6.0f > gx + gw) tx = bx1 - fs.x - 9.0f;
        if (tx < gx + 3.0f) tx = gx + 3.0f;
        float ty = by2 - fs.y - 3.0f;
        dl->AddRectFilled(ImVec2(tx - 3.0f, ty - 2.0f),
                          ImVec2(tx + fs.x + 3.0f, ty + fs.y + 2.0f),
                          IM_COL32(4, 14, 10, 185), 2.0f);
        dl->AddText(ImVec2(tx, ty), IM_COL32(190, 255, 220, 245), flabel);
        if (fighter_box_hover || s_fighter_box_drag)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (!ImGui::IsMouseDown(0)) {
        s_fighter_box_drag = false;
    }

    bool fighter_box_blocks_mouse = fighter_box_hover || s_fighter_box_drag;
    bool module_blocks_mouse =
        draw_game_view_module_resize_overlay(dl, gx, gy, gw, gh, g_zoom,
                                             mpos, hov, fighter_box_blocks_mouse);
    g_module_bounds_mouse_capture = module_blocks_mouse;
    bool mouse_clicked = hov && ImGui::IsMouseClicked(0) &&
                         !fighter_box_blocks_mouse && !module_blocks_mouse;
    bool select_double_to_pan = false;
    if (mouse_clicked && g_cur_tool == 0) {
        select_double_to_pan = ImGui::IsMouseDoubleClicked(0);
    }
    if (select_double_to_pan) {
        g_cur_tool = 2;
        s_drag = false;
        s_cam_drag = false;
        s_use_position_delta = false;
        s_drag_idx = -1;
        s_has_snap_x = s_has_snap_y = false;
    } else if (mouse_clicked) {
        bool shift = ImGui::GetIO().KeyShift;
        bool ctrl = ImGui::GetIO().KeyCtrl;
        bool alt = ImGui::GetIO().KeyAlt;
        int hit    = hit_obj(mpos.x, mpos.y);
        if (g_cur_tool == 2 && hit >= 0) {
            if (ctrl) {
                toggle_object_selection(hit);
            } else {
                if (!shift) game_view_clear_selection();
                g_sel_flags[hit] = 1;
                g_hl_obj = hit;
            }
            g_cur_tool = 0;
            s_drag = false;
            s_cam_drag = false;
            s_moved = false;
            s_use_position_delta = false;
            s_drag_idx = -1;
            s_has_snap_x = s_has_snap_y = false;
        } else if (hit >= 0) {
            if (ctrl) {
                toggle_object_selection(hit);
                s_drag = false;
                s_cam_drag = false;
                s_moved = false;
                s_use_position_delta = false;
                s_drag_idx = -1;
                s_has_snap_x = s_has_snap_y = false;
            } else {
                if (!shift) game_view_clear_selection();
                g_sel_flags[hit] = 1;
                g_hl_obj = hit;
                bool cloned_for_drag = false;
                if (alt) {
                    int clone_hit = clone_game_preview_drag_targets(hit);
                    if (clone_hit >= 0) {
                        hit = clone_hit;
                        cloned_for_drag = true;
                    }
                }
                s_drag     = true;
                s_cam_drag = false;
                s_moved    = cloned_for_drag;
                s_use_position_delta = !cloned_for_drag;
                s_start_mx = mpos.x;
                s_start_my = mpos.y;
                s_drag_idx = hit;
                s_has_snap_x = s_has_snap_y = false;
                for (int i = 0; i < g_no; i++) {
                    s_init_depth[i] = g_obj[i].depth;
                    s_init_sy[i]    = g_obj[i].sy;
                }
            }
        } else {
            if (ctrl) {
                s_drag = false;
                s_cam_drag = false;
                s_use_position_delta = false;
                s_drag_idx = -1;
                s_has_snap_x = s_has_snap_y = false;
            } else {
                if (!shift) game_view_clear_selection();
                s_drag      = true;
                s_cam_drag  = true;
                s_use_position_delta = false;
                s_start_mx  = mpos.x;
                s_cam_start = g_scroll_pos;
                s_drag_idx  = -1;
                s_has_snap_x = s_has_snap_y = false;
            }
        }
    }

    if (s_drag && ImGui::IsMouseDown(0)) {
        float dmx = mpos.x - s_start_mx;
        float dmy = mpos.y - s_start_my;
        if (s_cam_drag) {
            g_scroll_pos = s_cam_start - (int)(dmx / g_zoom);
        } else {
            int idx = (int)(dmx / g_zoom);
            int idy = (int)(dmy / g_zoom);
            if (idx || idy) {
                game_preview_snap_drag_delta(s_drag_idx, s_init_depth.data(), s_init_sy.data(),
                                             &idx, &idy,
                                             &s_snap_x, &s_snap_y,
                                             &s_has_snap_x, &s_has_snap_y);
            } else {
                s_has_snap_x = s_has_snap_y = false;
            }
            bool any = false;
            bool any_change = false;
            for (int i = 0; i < g_no; i++) {
                if (!g_sel_flags[i] || g_obj_lock[i]) continue;
                any = true;
                int nx = s_init_depth[i] + idx;
                int ny = s_init_sy[i] + idy;
                if (g_grid_snap) {
                    if (g_grid_sx > 1) nx = rounded_div_nearest(nx, g_grid_sx) * g_grid_sx;
                    if (g_grid_sy > 1) ny = rounded_div_nearest(ny, g_grid_sy) * g_grid_sy;
                }
                if (g_obj[i].depth != nx || g_obj[i].sy != ny)
                    any_change = true;
            }
            if (any && any_change) {
                if (!s_moved)
                    s_moved = true;
                for (int i = 0; i < g_no; i++) {
                    if (!g_sel_flags[i] || g_obj_lock[i]) continue;
                    int nx = s_init_depth[i] + idx;
                    int ny = s_init_sy[i] + idy;
                    if (g_grid_snap) {
                        if (g_grid_sx > 1) nx = rounded_div_nearest(nx, g_grid_sx) * g_grid_sx;
                        if (g_grid_sy > 1) ny = rounded_div_nearest(ny, g_grid_sy) * g_grid_sy;
                    }
                    g_obj[i].depth = nx;
                    g_obj[i].sy = ny;
                }
                g_dirty = 1; g_view_changed = 1;
            }
        }
    }

    if (!ImGui::IsMouseDown(0)) {
        if (s_drag && !s_cam_drag && s_moved && s_use_position_delta) {
            undo_save_object_position_delta_for_selection(s_init_depth.data(),
                                                          s_init_sy.data(),
                                                          object_cap,
                                                          "Move");
        }
        s_drag = false;
        s_drag_idx = -1;
        s_use_position_delta = false;
        s_has_snap_x = s_has_snap_y = false;
    }

    if (s_drag && !s_cam_drag) {
        if (s_has_snap_x) {
            float sx = gx + (float)s_snap_x * (float)g_zoom;
            dl->AddLine(ImVec2(sx, gy), ImVec2(sx, gy + gh),
                        IM_COL32(100, 210, 255, 190), 1.5f);
        }
        if (s_has_snap_y) {
            float sy = gy + (float)s_snap_y * (float)g_zoom;
            dl->AddLine(ImVec2(gx, sy), ImVec2(gx + gw, sy),
                        IM_COL32(100, 210, 255, 190), 1.5f);
        }
    }

    /* selection outlines in game-view parallax space */
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i] || g_obj_hidden[i]) continue;
        Obj *o  = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;
        RectBounds rb;
        if (!game_preview_object_bounds_at_source(i, o->depth, o->sy, &rb))
            continue;
        float sx = gx + rb.x1 * (float)g_zoom;
        float sy = gy + rb.y1 * (float)g_zoom;
        float sw = (rb.x2 - rb.x1) * (float)g_zoom;
        float sh = (rb.y2 - rb.y1) * (float)g_zoom;
        if (sx + sw < gx || sx > gx + gw) continue;
        dl->AddRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh),
                    IM_COL32(255, 220, 50, 220), 0.0f, 0, 2.0f);
        /* drag handle hint */
        if (s_drag && !s_cam_drag)
            dl->AddCircleFilled(ImVec2(sx + sw * 0.5f, sy + sh * 0.5f), 5.0f,
                                IM_COL32(255, 220, 50, 180));
    }

    /* hover highlight */
    if (hov && !s_drag && !fighter_box_blocks_mouse && !module_blocks_mouse) {
        int hi = hit_obj(mpos.x, mpos.y);
        if (hi >= 0 && !g_sel_flags[hi]) {
            Obj *o  = &g_obj[hi];
            Img *im = img_find(o->ii);
            if (im) {
                RectBounds rb;
                if (game_preview_object_bounds_at_source(hi, o->depth, o->sy, &rb)) {
                    float sx = gx + rb.x1 * (float)g_zoom;
                    float sy = gy + rb.y1 * (float)g_zoom;
                    float sw = (rb.x2 - rb.x1) * (float)g_zoom;
                    float sh = (rb.y2 - rb.y1) * (float)g_zoom;
                    dl->AddRect(ImVec2(sx, sy),
                                ImVec2(sx + sw, sy + sh),
                                IM_COL32(180, 200, 255, 130), 0.0f, 0, 1.0f);
                }
            }
        }
        ImGui::SetMouseCursor(hi >= 0 ? ImGuiMouseCursor_ResizeAll
                                       : ImGuiMouseCursor_Hand);
    }

    /* split view: yellow divider + A/B position labels */
    if (g_split_view) {
        float mid_x = gx + gw * 0.5f;
        dl->AddLine(ImVec2(mid_x, gy), ImVec2(mid_x, gy + gh),
                    IM_COL32(255, 220, 0, 220), 2.0f);
        char lbl_a[32]; snprintf(lbl_a, sizeof lbl_a, "A  %d px", g_split_scroll_a);
        ImVec2 a_sz = ImGui::CalcTextSize(lbl_a);
        dl->AddRectFilled(ImVec2(gx + 4, gy + 4),
                          ImVec2(gx + 8 + a_sz.x, gy + 6 + a_sz.y), IM_COL32(0,0,0,160), 2.0f);
        dl->AddText(ImVec2(gx + 6, gy + 5), IM_COL32(255, 220, 0, 230), lbl_a);
        char lbl_b[32]; snprintf(lbl_b, sizeof lbl_b, "B  %d px", g_scroll_pos);
        ImVec2 b_sz = ImGui::CalcTextSize(lbl_b);
        dl->AddRectFilled(ImVec2(mid_x + 4, gy + 4),
                          ImVec2(mid_x + 8 + b_sz.x, gy + 6 + b_sz.y), IM_COL32(0,0,0,160), 2.0f);
        dl->AddText(ImVec2(mid_x + 6, gy + 5), IM_COL32(100, 200, 255, 230), lbl_b);
    }

    /* right-click context menu in game view for layer assignment */
    if (hov && ImGui::IsMouseClicked(1) && !s_drag && !module_blocks_mouse) {
        int hit = hit_obj(mpos.x, mpos.y);
        if (hit >= 0) {
            if (!g_sel_flags[hit]) {
                game_view_clear_selection();
                g_sel_flags[hit] = 1;
            }
            g_hl_obj = hit;
            ImGui::OpenPopup("##gv_ctx");
        }
    }
    if (ImGui::BeginPopup("##gv_ctx")) {
        int active = active_object_index();
        bool has_obj = active >= 0 && active < g_no;
        int sel_count = 0;
        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_count++;
        ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f),
                           sel_count > 1 ? "Assign Layer (%d objects)" : "Assign Layer",
                           sel_count);
        ImGui::Separator();
        /* show current layer of first selected object */
        int cur_layer = -1;
        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) { cur_layer = (g_obj[i].wx >> 8) & 0xFF; break; }
        int preset_count = mk2_layer_preset_count();
        for (int li = 0; li < preset_count; li++) {
            int byte = mk2_layer_preset_wx(li);
            bool is_cur = (cur_layer == byte);
            if (is_cur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.85f,0.2f,1.0f));
            if (ImGui::MenuItem(mk2_layer_preset_label(li), is_cur ? "(current)" : nullptr, false, has_obj))
                assign_layer_to_object_targets(active, byte);
            if (is_cur) ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_obj && g_no < object_cap))
            duplicate_object_menu_targets(active);
        if (ImGui::MenuItem("Delete", "Del", false, has_obj))
            delete_object_menu_targets(active);
        ImGui::EndPopup();
    }

    ImGui::End();

    if (!g_preview_mode)
        return;

    /* ---- transport bar: small floating window below the game viewport ---- */
    {
        float bar_w = 500.0f;
        float bar_x = gx + (gw - bar_w) * 0.5f;
        float bar_y = gy + gh + 74.0f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        float max_bar_y = ds.y - 58.0f;
        if (bar_y > max_bar_y && max_bar_y >= gy + gh + 4.0f)
            bar_y = max_bar_y;
        ImGui::SetNextWindowPos(ImVec2(bar_x, bar_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(bar_w, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::Begin("##anim_bar", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleVar();

        /* ---- row 1: playback controls ---- */

        /* |< rewind */
        if (ImGui::SmallButton("|<")) {
            g_anim_fpos = (float)wx_min; g_scroll_pos = wx_min; g_anim_dir = 1;
            g_anim_playing = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rewind to start");
        ImGui::SameLine(0, 4);

        /* Play / Pause */
        const char *pp_lbl = g_anim_playing ? "|| Pause" : ">  Play";
        if (ImGui::SmallButton(pp_lbl)) {
            g_anim_playing = !g_anim_playing;
            if (g_anim_playing) { g_anim_fpos = (float)g_scroll_pos; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause  (Space)");
        ImGui::SameLine(0, 4);

        /* >| forward to end */
        if (ImGui::SmallButton(">|")) {
            g_anim_fpos = (float)scroll_max; g_scroll_pos = scroll_max; g_anim_dir = -1;
            g_anim_playing = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to end");
        ImGui::SameLine(0, 10);

        /* Bounce / Loop toggle */
        if (ImGui::SmallButton(g_anim_bounce ? "Bounce" : "Loop")) g_anim_bounce = !g_anim_bounce;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_anim_bounce
            ? "Mode: ping-pong - click to switch to loop"
            : "Mode: loop - click to switch to bounce");
        ImGui::SameLine(0, 10);

        /* Speed slider */
        ImGui::SetNextItemWidth(90.0f);
        ImGui::SliderFloat("##spd", &g_anim_speed, 10.0f, 400.0f, "%.0f px/s",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scroll speed in world pixels per second");
        ImGui::SameLine(0, 8);

        /* X progress scrub */
        if (scroll_max > wx_min) {
            float t = (float)(g_scroll_pos - wx_min) / (float)(scroll_max - wx_min);
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::SliderFloat("##pos", &t, 0.0f, 1.0f, "")) {
                g_anim_fpos  = wx_min + t * (scroll_max - wx_min);
                g_scroll_pos = (int)g_anim_fpos;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to scrub horizontal camera");
            ImGui::SameLine(0, 6);
            ImGui::TextDisabled("X%d", g_scroll_pos);
        } else {
            ImGui::TextDisabled("(fixed width)");
        }

        /* ---- row 2: extended controls ---- */
        ImGui::Separator();

        /* V Sweep toggle */
        if (g_anim_v_sweep)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("V Sweep")) {
            g_anim_v_sweep = !g_anim_v_sweep;
            if (!g_anim_v_sweep) { g_anim_vy_fpos = (float)wy_min; g_game_view_y = wy_min; g_anim_vy_dir = 1; }
        }
        if (g_anim_v_sweep) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animate vertical camera sweep");
        ImGui::SameLine(0, 6);

        if (g_anim_v_sweep) {
            ImGui::SetNextItemWidth(70.0f);
            ImGui::SliderFloat("##vyspd", &g_anim_vy_speed, 5.0f, 200.0f, "%.0f px/s");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical sweep speed");
            ImGui::SameLine(0, 6);
            if (scroll_y_max > 0) {
                float ty = (float)g_game_view_y / (float)scroll_y_max;
                ImGui::SetNextItemWidth(50.0f);
                if (ImGui::SliderFloat("##ypos", &ty, 0.0f, 1.0f, "")) {
                    g_anim_vy_fpos = ty * scroll_y_max;
                    g_game_view_y  = (int)g_anim_vy_fpos;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scrub vertical camera");
                ImGui::SameLine(0, 4);
                ImGui::TextDisabled("Y%d", g_game_view_y);
                ImGui::SameLine(0, 10);
            }
        } else {
            ImGui::SameLine(0, 0);
        }

        /* Split View toggle */
        if (g_split_view)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Split")) {
            g_split_view = !g_split_view;
            if (g_split_view) g_split_scroll_a = g_scroll_pos;
        }
        if (g_split_view) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Split view: compare two scroll positions side by side.\n"
            "Left half = A (fixed), right half = B (current).");
        ImGui::SameLine(0, 6);

        if (g_split_view && scroll_max > wx_min) {
            float ta = (float)(g_split_scroll_a - wx_min) / (float)(scroll_max - wx_min);
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::SliderFloat("##spa", &ta, 0.0f, 1.0f, ""))
                g_split_scroll_a = (int)(wx_min + ta * (scroll_max - wx_min));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scrub A position (left half)");
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("A%d", g_split_scroll_a);
            ImGui::SameLine(0, 10);
        }

        /* Record button */
        if (g_recording) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
            if (ImGui::SmallButton("Stop")) {
                g_recording = false; g_record_accum = 0.0f;
                fprintf(stderr, "record: stopped at frame %d, dir: %s\n",
                        g_record_n, g_record_dir);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "REC %d", g_record_n);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Recording frame %d/1800 to:\n%s", g_record_n, g_record_dir);
        } else {
            if (ImGui::SmallButton("Rec")) {
                char dir[512] = "";
                if (folder_dialog_open("Choose output folder for PNG frames", dir, sizeof dir)) {
                    strncpy(g_record_dir, dir, sizeof g_record_dir - 1);
                    g_record_n     = 0;
                    g_record_accum = 0.0f;
                    g_recording    = true;
                    if (!g_anim_playing) {
                        g_anim_playing = true;
                        g_anim_fpos    = (float)g_scroll_pos;
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Record animation as PNG frames (30 fps, max 60 s)");
        }
        ImGui::SameLine(0, 10);

        /* Palette Animation button */
        if (pal_animation_enabled())
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.35f, 1.0f));
        if (ImGui::SmallButton("Pal Anim")) g_show_pal_anim = !g_show_pal_anim;
        if (pal_animation_enabled()) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open palette animation panel\n(cycles palette entries to simulate water/fire)");

        ImGui::End();
    }
}

