#include "UI/sdl/sdl_object_drag_autopan.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/sdl/sdl_tooltip.h"

#include <cstdlib>

void bdd_sdl_object_drag_update(int drag_idx,
                                int mouse_x, int mouse_y,
                                int drag_ox, int drag_oy,
                                int zoom,
                                int *drag_depth, int *drag_sy,
                                int *guide_vx, int *guide_vn,
                                int *guide_vy, int *guide_hn)
{
    int dx, dy;

    if (drag_idx < 0 || zoom <= 0 || !drag_depth || !drag_sy)
        return;

    dx = (mouse_x - drag_ox) / zoom;
    dy = (mouse_y - drag_oy) / zoom;

    if (guide_vn) *guide_vn = 0;
    if (guide_hn) *guide_hn = 0;
    if (!g_grid_snap && g_have_bdb) {
        int ox1 = 0, ox2 = 0, oy1 = 0, oy2 = 0;
        if (bg_editor_object_snap_rect_at(drag_idx,
                drag_depth[drag_idx] + dx,
                drag_sy[drag_idx] + dy,
                &ox1, &oy1, &ox2, &oy2))
        {
            int snap = bg_editor_snap_dist();
            int best_ax = snap + 1, best_ay = snap + 1;

            for (int ti = 0; ti < g_no; ti++) {
                if (g_sel_flags[ti] || g_obj_hidden[ti]) continue;
                int tx1 = 0, tx2 = 0, ty1 = 0, ty2 = 0;
                if (!bg_editor_object_snap_rect_at(ti, g_obj[ti].depth, g_obj[ti].sy,
                                                   &tx1, &ty1, &tx2, &ty2))
                    continue;

                int xpairs[4][2] = {{ox1,tx1},{ox2,tx2},{ox1,tx2},{ox2,tx1}};
                for (int p = 0; p < 4; p++) {
                    int adj = xpairs[p][1] - xpairs[p][0];
                    if (abs(adj) <= snap) {
                        if (abs(adj) < abs(best_ax)) best_ax = adj;
                        if (guide_vx && guide_vn) {
                            int gx = xpairs[p][1];
                            int dup = 0;
                            for (int g = 0; g < *guide_vn; g++) {
                                if (guide_vx[g] == gx) { dup = 1; break; }
                            }
                            if (!dup && *guide_vn < 8) guide_vx[(*guide_vn)++] = gx;
                        }
                    }
                }

                int ypairs[4][2] = {{oy1,ty1},{oy2,ty2},{oy1,ty2},{oy2,ty1}};
                for (int p = 0; p < 4; p++) {
                    int adj = ypairs[p][1] - ypairs[p][0];
                    if (abs(adj) <= snap) {
                        if (abs(adj) < abs(best_ay)) best_ay = adj;
                        if (guide_vy && guide_hn) {
                            int gy = ypairs[p][1];
                            int dup = 0;
                            for (int g = 0; g < *guide_hn; g++) {
                                if (guide_vy[g] == gy) { dup = 1; break; }
                            }
                            if (!dup && *guide_hn < 8) guide_vy[(*guide_hn)++] = gy;
                        }
                    }
                }
            }
            if (abs(best_ax) <= snap) dx += best_ax;
            if (abs(best_ay) <= snap) dy += best_ay;
        }
    }

    for (int si = 0; si < g_no; si++) {
        if (!g_sel_flags[si]) continue;
        int d2 = drag_depth[si] + dx;
        int s2 = drag_sy[si] + dy;
        if (g_grid_snap) {
            d2 = (d2 / g_grid_sx) * g_grid_sx;
            s2 = (s2 / g_grid_sy) * g_grid_sy;
        }
        g_obj[si].depth = d2;
        g_obj[si].sy = s2;
    }
}

void bdd_sdl_object_drag_auto_pan(int *drag_idx,
                                  int *guide_vn, int *guide_hn,
                                  int *view_x, int *view_y,
                                  int zoom, int ww, int wh,
                                  int *drag_depth, int *drag_sy,
                                  int *hover_x, int *hover_y,
                                  Uint32 *hover_since,
                                  int *hover_printed)
{
    int mx = 0, my = 0;
    Uint32 buttons;
    const int edge = 28;
    int sx = 0, sy = 0;
    int pan_x, pan_y;

    if (!drag_idx || *drag_idx < 0 || g_game_view)
        return;

    buttons = SDL_GetMouseState(&mx, &my);
    if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT))) {
        *drag_idx = -1;
        if (guide_vn) *guide_vn = 0;
        if (guide_hn) *guide_hn = 0;
        SDL_CaptureMouse(SDL_FALSE);
        return;
    }

    if (mx < edge) sx = -((edge - mx + 5) / 6);
    else if (mx > ww - edge) sx = (mx - (ww - edge) + 5) / 6;
    if (my < edge) sy = -((edge - my + 5) / 6);
    else if (my > wh - edge) sy = (my - (wh - edge) + 5) / 6;

    if (!sx && !sy)
        return;

    pan_x = sx / zoom;
    pan_y = sy / zoom;
    if (sx && pan_x == 0) pan_x = sx > 0 ? 1 : -1;
    if (sy && pan_y == 0) pan_y = sy > 0 ? 1 : -1;

    if (view_x) *view_x += pan_x;
    if (view_y) *view_y += pan_y;
    for (int si = 0; si < g_no; si++) {
        if (!g_sel_flags[si]) continue;
        if (drag_depth) drag_depth[si] += pan_x;
        if (drag_sy) drag_sy[si] += pan_y;
        g_obj[si].depth += pan_x;
        g_obj[si].sy += pan_y;
    }
    g_dirty = 1;
    if (hover_x) *hover_x = mx;
    if (hover_y) *hover_y = my;
    if (hover_since) *hover_since = SDL_GetTicks();
    if (hover_printed) *hover_printed = 0;
    bdd_tooltip_free();
}
