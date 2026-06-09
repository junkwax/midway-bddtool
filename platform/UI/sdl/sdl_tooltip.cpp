#include "UI/sdl/sdl_tooltip.h"

#include "Core/image_lookup.h"
#include "UI/sdl/sdl_bitmap_font.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>

enum {
    TIP_PAD = 5,
    TIP_LH = 11,
    TIP_MAX = 32,
    TIP_COL = 56
};

static SDL_Texture *g_tip_tex = NULL;
static int g_tip_x = 0;
static int g_tip_y = 0;
static int g_tip_w = 0;
static int g_tip_h = 0;

void bdd_tooltip_free(void)
{
    if (!g_tip_tex) return;
    SDL_DestroyTexture(g_tip_tex);
    g_tip_tex = NULL;
}

void bdd_tooltip_build_hover(SDL_Renderer *rend,
                             int mx, int my,
                             int view_x, int view_y, int zoom,
                             int ww, int wh)
{
    bdd_tooltip_free();

    int wx = 0, wy = 0;
    bdd_screen_to_world(mx, my, view_x, view_y, zoom, &wx, &wy);

    char lines[TIP_MAX][TIP_COL];
    int nl = 0;

    for (int i = g_no - 1; i >= 0 && nl + 1 < TIP_MAX; i--) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;
        if (wx < o->depth || wx >= o->depth + im->w) continue;
        if (wy < o->sy || wy >= o->sy + im->h) continue;
        int pal = (im->pal_idx >= 0) ? im->pal_idx : o->fl;
        snprintf(lines[nl++], TIP_COL,
                 "[%d] ii=0x%04X  %dx%d  pal=%d", i, o->ii, im->w, im->h, pal);
        snprintf(lines[nl++], TIP_COL,
                 "  Z=%-4d sy=%-4d  wx=0x%04X  hfl=%d vfl=%d",
                 o->depth, o->sy, o->wx, o->hfl, o->vfl);
    }

    if (nl == 0) return;

    int max_chars = 0;
    for (int i = 0; i < nl; i++) {
        int l = (int)strlen(lines[i]);
        if (l > max_chars) max_chars = l;
    }
    int sw = max_chars * 8 + TIP_PAD * 2;
    int sh = nl * TIP_LH + TIP_PAD * 2;

    SDL_Surface *surf = SDL_CreateRGBSurface(0, sw, sh, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return;

    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 14, 14, 24, 225));
    {
        Uint32 bc = SDL_MapRGBA(surf->format, 110, 110, 170, 255);
        int pitch = surf->pitch / 4;
        Uint32 *px = (Uint32 *)surf->pixels;
        for (int x = 0; x < sw; x++) { px[x] = bc; px[(sh - 1) * pitch + x] = bc; }
        for (int y = 0; y < sh; y++) { px[y * pitch] = bc; px[y * pitch + sw - 1] = bc; }
    }

    for (int i = 0; i < nl; i++) {
        Uint32 fg = ((i / 2) % 2 == 0)
            ? SDL_MapRGBA(surf->format, 240, 230, 150, 255)
            : SDL_MapRGBA(surf->format, 150, 220, 255, 255);
        bdd_sdl_font_draw_str(surf, TIP_PAD, TIP_PAD + i * TIP_LH, lines[i], fg);
    }

    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
    g_tip_tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!g_tip_tex) return;
    SDL_SetTextureBlendMode(g_tip_tex, SDL_BLENDMODE_BLEND);

    g_tip_w = sw;
    g_tip_h = sh;
    g_tip_x = mx + 14;
    g_tip_y = my + 14;
    if (g_tip_x + sw > ww) g_tip_x = mx - sw - 4;
    if (g_tip_y + sh > wh) g_tip_y = my - sh - 4;
}

void bdd_tooltip_build_object(SDL_Renderer *rend, int obj_i,
                              int ax, int ay, int ww, int wh)
{
    bdd_tooltip_free();
    if (obj_i < 0 || obj_i >= g_no) return;

    Obj *o = &g_obj[obj_i];
    Img *im = img_find(o->ii);
    int pal = im ? ((im->pal_idx >= 0) ? im->pal_idx : o->fl) : o->fl;

    char lines[4][TIP_COL];
    int nl = 0;
    snprintf(lines[nl++], TIP_COL,
             "[%d] ii=0x%04X  %dx%d  pal=%d",
             obj_i, o->ii, im ? im->w : 0, im ? im->h : 0, pal);
    snprintf(lines[nl++], TIP_COL,
             "  Z=%-4d sy=%-4d  wx=0x%04X  hfl=%d vfl=%d",
             o->depth, o->sy, o->wx, o->hfl, o->vfl);
    snprintf(lines[nl++], TIP_COL,
             "  layer 0x%02X", (o->wx >> 8) & 0xFF);

    int max_chars = 0;
    for (int i = 0; i < nl; i++) {
        int l = (int)strlen(lines[i]);
        if (l > max_chars) max_chars = l;
    }
    int sw = max_chars * 8 + TIP_PAD * 2;
    int sh = nl * TIP_LH + TIP_PAD * 2;

    SDL_Surface *surf = SDL_CreateRGBSurface(0, sw, sh, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return;
    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 14, 14, 24, 225));
    {
        Uint32 bc = SDL_MapRGBA(surf->format, 110, 200, 110, 255);
        int pitch = surf->pitch / 4;
        Uint32 *px = (Uint32 *)surf->pixels;
        for (int x = 0; x < sw; x++) { px[x] = bc; px[(sh - 1) * pitch + x] = bc; }
        for (int y = 0; y < sh; y++) { px[y * pitch] = bc; px[y * pitch + sw - 1] = bc; }
    }

    Uint32 fg0 = SDL_MapRGBA(surf->format, 240, 230, 150, 255);
    Uint32 fg1 = SDL_MapRGBA(surf->format, 120, 240, 140, 255);
    bdd_sdl_font_draw_str(surf, TIP_PAD, TIP_PAD + 0 * TIP_LH, lines[0], fg0);
    bdd_sdl_font_draw_str(surf, TIP_PAD, TIP_PAD + 1 * TIP_LH, lines[1], fg0);
    bdd_sdl_font_draw_str(surf, TIP_PAD, TIP_PAD + 2 * TIP_LH, lines[2], fg1);

    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
    g_tip_tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!g_tip_tex) return;
    SDL_SetTextureBlendMode(g_tip_tex, SDL_BLENDMODE_BLEND);

    g_tip_w = sw;
    g_tip_h = sh;
    g_tip_x = ax + 14;
    g_tip_y = ay + 14;
    if (g_tip_x + sw > ww) g_tip_x = ax - sw - 4;
    if (g_tip_y + sh > wh) g_tip_y = ay - sh - 4;
}

void bdd_tooltip_draw(SDL_Renderer *rend)
{
    if (!g_tip_tex || !g_show_labels) return;
    SDL_Rect dst = { g_tip_x, g_tip_y, g_tip_w, g_tip_h };
    SDL_RenderCopy(rend, g_tip_tex, NULL, &dst);
}
