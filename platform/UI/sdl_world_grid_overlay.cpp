#include "UI/sdl_world_grid_overlay.h"

#include "UI/sdl_bitmap_font.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>

static void draw_grid_label(SDL_Renderer *rend, const char *label, int x, int y)
{
    int w = (int)strlen(label) * 8;
    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, 8, 32, 0, 0, 0, 0);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 18, 18, 28, 220));
    bdd_sdl_font_draw_str(surf, 0, 0, label, SDL_MapRGBA(surf->format, 140, 140, 180, 255));
    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_Rect dst = { x, y, w, 8 };
    SDL_RenderCopy(rend, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static SDL_Rect world_grid_clip_rect(int view_x, int view_y, int zoom,
                                     int ww, int wh, int canvas_top)
{
    SDL_Rect grid_clip = { 0, canvas_top, ww, wh - canvas_top };
    if (!g_game_view && zoom > 0) {
        int gx_min = 0, gx_max = 0, gy_min = 0, gy_max = 0;
        int pad_x = (g_grid_sx > 0 ? g_grid_sx : 64) * 3;
        int pad_y = (g_grid_sy > 0 ? g_grid_sy : 32) * 3;
        bdd_get_editor_view_bounds(&gx_min, &gx_max, &gy_min, &gy_max);
        gx_min -= pad_x; gx_max += pad_x;
        gy_min -= pad_y; gy_max += pad_y;
        grid_clip.x = (gx_min - view_x) * zoom;
        grid_clip.y = (gy_min - view_y) * zoom;
        grid_clip.w = (gx_max - gx_min) * zoom;
        grid_clip.h = (gy_max - gy_min) * zoom;
        if (grid_clip.x < 0) { grid_clip.w += grid_clip.x; grid_clip.x = 0; }
        if (grid_clip.y < canvas_top) { grid_clip.h -= canvas_top - grid_clip.y; grid_clip.y = canvas_top; }
        if (grid_clip.x + grid_clip.w > ww) grid_clip.w = ww - grid_clip.x;
        if (grid_clip.y + grid_clip.h > wh) grid_clip.h = wh - grid_clip.y;
        if (grid_clip.w < 0) grid_clip.w = 0;
        if (grid_clip.h < 0) grid_clip.h = 0;
    }
    return grid_clip;
}

void bdd_world_grid_overlay_draw(SDL_Renderer *rend,
                                 int view_x, int view_y, int zoom,
                                 int ww, int wh, int canvas_top)
{
    if (!g_show_grid || zoom <= 0 || g_grid_sx <= 0 || g_grid_sy <= 0)
        return;

    SDL_Rect grid_clip = world_grid_clip_rect(view_x, view_y, zoom, ww, wh, canvas_top);
    if (grid_clip.w <= 0 || grid_clip.h <= 0)
        return;

    SDL_RenderSetClipRect(rend, &grid_clip);
    SDL_SetRenderDrawColor(rend, g_grid_color[0], g_grid_color[1], g_grid_color[2], 255);
    for (int gx = (view_x / g_grid_sx) * g_grid_sx;
         gx < view_x + ww / zoom + g_grid_sx;
         gx += g_grid_sx) {
        int sx = (gx - view_x) * zoom;
        SDL_RenderDrawLine(rend, sx, canvas_top, sx, wh);
    }
    for (int gy = (view_y / g_grid_sy) * g_grid_sy;
         gy < view_y + wh / zoom + g_grid_sy;
         gy += g_grid_sy) {
        int sy = (gy - view_y) * zoom;
        SDL_RenderDrawLine(rend, 0, sy, ww, sy);
    }
    SDL_RenderSetClipRect(rend, NULL);

    if (g_zoom < 2) return;

    SDL_RenderSetClipRect(rend, &grid_clip);
    SDL_Color rc = { 120, 120, 160, 180 };
    for (int gx = (view_x / g_grid_sx) * g_grid_sx;
         gx < view_x + ww / zoom + g_grid_sx;
         gx += g_grid_sx) {
        int sx = (gx - view_x) * zoom;
        if (sx < 0 || sx > ww) continue;
        SDL_SetRenderDrawColor(rend, rc.r, rc.g, rc.b, 120);
        SDL_RenderDrawLine(rend, sx, canvas_top, sx, canvas_top + 6);
        SDL_RenderDrawLine(rend, sx, wh - 6, sx, wh);
        if (g_zoom >= 3) {
            char lbl[16];
            snprintf(lbl, sizeof lbl, "%d", gx);
            if (sx + (int)strlen(lbl) * 8 < ww)
                draw_grid_label(rend, lbl, sx, canvas_top + 6);
        }
    }
    for (int gy = (view_y / g_grid_sy) * g_grid_sy;
         gy < view_y + wh / zoom + g_grid_sy;
         gy += g_grid_sy) {
        int sy = (gy - view_y) * zoom;
        if (sy < 0 || sy > wh) continue;
        SDL_SetRenderDrawColor(rend, rc.r, rc.g, rc.b, 120);
        SDL_RenderDrawLine(rend, 0, sy, 6, sy);
        SDL_RenderDrawLine(rend, ww - 6, sy, ww, sy);
        if (g_zoom >= 3) {
            char lbl[16];
            snprintf(lbl, sizeof lbl, "%d", gy);
            if (sy + 8 < wh)
                draw_grid_label(rend, lbl, 6, sy);
        }
    }
    SDL_RenderSetClipRect(rend, NULL);
}
