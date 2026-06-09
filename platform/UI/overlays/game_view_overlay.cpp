#include "bg_editor.h"
#include "bg_editor_globals.h"
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

    int layer = (g_obj[obj_idx].wx >> 8) & 0xFF;
    float scroll = gv_scroll_factor(layer);
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
        float f  = gv_scroll_factor((o->wx >> 8) & 0xFF);
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
            float f  = gv_scroll_factor((o->wx >> 8) & 0xFF);
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
    bool mouse_clicked = hov && ImGui::IsMouseClicked(0);
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
    if (hov && !s_drag) {
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
    if (hov && ImGui::IsMouseClicked(1) && !s_drag) {
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
        static const struct { int byte; const char *name; } layers[] = {
            {0x32,"Sky / far back  (0.2x)"}, {0x3C,"Mid distance  (0.5x)"},
            {0x40,"Floor / play  (1.0x)"},   {0x41,"Floor alt  (1.0x)"},
            {0x43,"Near foreground  (1.2x)"},{0x46,"Front foreground  (1.5x)"}
        };
        int sel_count = 0;
        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_count++;
        ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f),
                           sel_count > 1 ? "Assign Layer (%d objects)" : "Assign Layer",
                           sel_count);
        ImGui::Separator();
        /* show current layer of first selected object */
        int cur_layer = -1;
        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) { cur_layer = (g_obj[i].wx >> 8) & 0xFF; break; }
        for (int li = 0; li < 6; li++) {
            bool is_cur = (cur_layer == layers[li].byte);
            if (is_cur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.85f,0.2f,1.0f));
            if (ImGui::MenuItem(layers[li].name, is_cur ? "(current)" : nullptr, false, has_obj))
                assign_layer_to_object_targets(active, layers[li].byte);
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

    /* ---- transport bar: small floating window below the game viewport ---- */
    {
        float bar_w = 500.0f;
        float bar_x = gx + (gw - bar_w) * 0.5f;
        float bar_y = gy + gh + 74.0f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        if (bar_y > ds.y - 58.0f) bar_y = ds.y - 58.0f;
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

