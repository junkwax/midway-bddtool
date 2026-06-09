#include "UI/sdl/sdl_selection_rect.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"

#include <cstdlib>

void bdd_selection_rect_draw(SDL_Renderer *rend, int x1, int y1, int x2, int y2)
{
    int rx = x1 < x2 ? x1 : x2;
    int ry = y1 < y2 ? y1 : y2;
    int rw = abs(x2 - x1);
    int rh = abs(y2 - y1);

    SDL_Rect sr = { rx, ry, rw, rh };
    SDL_SetRenderDrawColor(rend, 100, 200, 255, 60);
    SDL_RenderFillRect(rend, &sr);
    SDL_SetRenderDrawColor(rend, 100, 200, 255, 200);
    SDL_RenderDrawRect(rend, &sr);
}

void bdd_selection_rect_apply(int x1, int y1, int x2, int y2,
                              int view_x, int view_y, int zoom,
                              int window_w, int window_h,
                              int additive)
{
    int sx1 = x1 < x2 ? x1 : x2;
    int sy1 = y1 < y2 ? y1 : y2;
    int sx2 = x1 > x2 ? x1 : x2;
    int sy2 = y1 > y2 ? y1 : y2;

    if (!additive)
        editor_project_clear_selection();

    for (int si = 0; si < g_no; si++) {
        if (g_obj_hidden[si]) continue;
        BddScreenRect rect;
        if (!bdd_object_screen_snap_rect(si, view_x, view_y, zoom,
                                         window_w, window_h, g_scroll_pos,
                                         &rect))
            continue;
        if (rect.x < sx2 && rect.x + rect.w > sx1 &&
            rect.y < sy2 && rect.y + rect.h > sy1) {
            g_sel_flags[si] = 1;
            g_hl_obj = si;
        }
    }
}
