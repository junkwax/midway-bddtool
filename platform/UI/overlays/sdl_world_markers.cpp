#include "UI/overlays/sdl_world_markers.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"

#include <cstdio>

void bdd_world_markers_draw(SDL_Renderer *rend, int view_x, int view_y,
                            int zoom, int ww, int wh)
{
    int ox = 0;
    int oy = 0;
    bdd_world_to_screen(0, 0, view_x, view_y, zoom, &ox, &oy);
    if (ox >= 0 && ox < ww && oy >= 0 && oy < wh) {
        int len = 8 * zoom;
        if (len < 12) len = 12;
        if (len > 32) len = 32;
        SDL_SetRenderDrawColor(rend, 200, 100, 100, 200);
        SDL_RenderDrawLine(rend, ox - len, oy, ox + len, oy);
        SDL_RenderDrawLine(rend, ox, oy - len, ox, oy + len);
        SDL_RenderDrawLine(rend, ox - len / 2, oy - len / 2, ox + len / 2, oy + len / 2);
        SDL_RenderDrawLine(rend, ox + len / 2, oy - len / 2, ox - len / 2, oy + len / 2);
    }

    if (!g_have_bdb || !g_bdb_header[0]) return;

    int world_w = 0, world_h = 0;
    char nm[64];
    int md, nm2, np, no2;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &world_w, &world_h, &md, &nm2, &np, &no2) < 7 ||
        world_w <= 0 || world_h <= 0)
        return;

    BddScreenRect rect;
    bdd_world_rect_screen_rect(0, 0, world_w, world_h,
                               view_x, view_y, zoom, ww, wh, &rect);
    int bx1 = rect.x;
    int by1 = rect.y;
    int bw = rect.w;
    int bh = rect.h;
    SDL_Rect wr = { bx1, by1, bw, bh };
    SDL_SetRenderDrawColor(rend, 80, 80, 120, 80);
    SDL_RenderDrawRect(rend, &wr);

    SDL_RenderDrawLine(rend, bx1, by1, bx1 + 4 * zoom, by1);
    SDL_RenderDrawLine(rend, bx1, by1, bx1, by1 + 4 * zoom);
    SDL_RenderDrawLine(rend, bx1 + bw, by1, bx1 + bw - 4 * zoom, by1);
    SDL_RenderDrawLine(rend, bx1 + bw, by1, bx1 + bw, by1 + 4 * zoom);
    SDL_RenderDrawLine(rend, bx1, by1 + bh, bx1 + 4 * zoom, by1 + bh);
    SDL_RenderDrawLine(rend, bx1, by1 + bh, bx1, by1 + bh - 4 * zoom);
    SDL_RenderDrawLine(rend, bx1 + bw, by1 + bh, bx1 + bw - 4 * zoom, by1 + bh);
    SDL_RenderDrawLine(rend, bx1 + bw, by1 + bh, bx1 + bw, by1 + bh - 4 * zoom);
}

void bdd_mouse_crosshair_draw(SDL_Renderer *rend, int hover_x, int hover_y)
{
    if (!g_have_bdb || hover_x < 0) return;

    SDL_SetRenderDrawColor(rend, 140, 140, 180, 100);
    SDL_RenderDrawLine(rend, hover_x - 8, hover_y, hover_x + 8, hover_y);
    SDL_RenderDrawLine(rend, hover_x, hover_y - 8, hover_x, hover_y + 8);
}
