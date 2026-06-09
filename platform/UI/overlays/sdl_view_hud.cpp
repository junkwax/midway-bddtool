#include "UI/overlays/sdl_view_hud.h"

#include "UI/sdl/sdl_bitmap_font.h"
#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>

static void draw_text_label(SDL_Renderer *rend, const char *text, int x, int y,
                            Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    int w = (int)strlen(text) * 8 + 4;
    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, 8, 32, 0, 0, 0, 0);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 10, 10, 18, 200));
    bdd_sdl_font_draw_str(surf, 2, 0, text, SDL_MapRGBA(surf->format, r, g, b, a));
    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_Rect dst = { x, y, (int)strlen(text) * 8, 8 };
    SDL_RenderCopy(rend, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

void bdd_view_hud_draw(SDL_Renderer *rend, int ww, int wh, int zoom)
{
    if (g_have_bdb) {
        int sel_c = 0;
        for (int si = 0; si < g_no; si++) {
            if (g_sel_flags && g_sel_flags[si]) sel_c++;
        }

        char hud[64];
        if (sel_c > 0)
            snprintf(hud, sizeof hud, "%dx  |  %d obj  |  %d sel", zoom, g_no, sel_c);
        else
            snprintf(hud, sizeof hud, "%dx  |  %d obj", zoom, g_no);

        draw_text_label(rend, hud, ww - (int)strlen(hud) * 8 - 6, 4,
                        180, 200, 220, 220);
    }

    if (!g_show_grid || zoom <= 0) return;

    int scale_len = g_grid_sx * zoom;
    if (scale_len > ww / 2) scale_len = (g_grid_sx / 2) * zoom;
    if (scale_len > ww / 2) scale_len = (g_grid_sx / 4) * zoom;

    int si_x = 10;
    int si_y = wh - 20;
    SDL_SetRenderDrawColor(rend, 180, 180, 200, 180);
    SDL_RenderDrawLine(rend, si_x, si_y, si_x + scale_len, si_y);
    SDL_RenderDrawLine(rend, si_x, si_y - 3, si_x, si_y + 3);
    SDL_RenderDrawLine(rend, si_x + scale_len, si_y - 3, si_x + scale_len, si_y + 3);

    int actual_len = scale_len / zoom;
    char slbl[16];
    snprintf(slbl, sizeof slbl, "%dpx", actual_len);
    draw_text_label(rend, slbl,
                    si_x + scale_len / 2 - (int)strlen(slbl) * 8 / 2,
                    si_y - 10, 180, 180, 200, 200);
}
