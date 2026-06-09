#include "UI/sdl/sdl_save_popup.h"

#include "UI/sdl/sdl_bitmap_font.h"
#include <cstring>

extern "C" {
int g_confirm_save = 0;
}

static SDL_Texture *g_popup_tex = nullptr;
static int g_popup_w = 0;
static int g_popup_h = 0;

static void popup_free(void)
{
    if (!g_popup_tex) return;
    SDL_DestroyTexture(g_popup_tex);
    g_popup_tex = nullptr;
}

void bdd_save_popup_cancel(void)
{
    g_confirm_save = 0;
    popup_free();
}

static void popup_build(SDL_Renderer *rend, const char *path)
{
    popup_free();

    const char *line0 = "Save changes to:";
    const char *line2 = "Y = save    N = cancel";
    const char *save_path = path ? path : "";
    int namelen = (int)strlen(save_path);
    int wide = namelen > (int)strlen(line2) ? namelen : (int)strlen(line2);
    if (wide < (int)strlen(line0)) wide = (int)strlen(line0);

    int pad = 10;
    int lh = 12;
    int sw = wide * 8 + pad * 2;
    int sh = 4 * lh + pad * 2;

    SDL_Surface *surf = SDL_CreateRGBSurface(0, sw, sh, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return;

    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 20, 20, 36, 240));

    Uint32 bcol = SDL_MapRGBA(surf->format, 200, 200, 80, 255);
    int pitch = surf->pitch / 4;
    Uint32 *pixels = (Uint32 *)surf->pixels;
    for (int x = 0; x < sw; x++) {
        pixels[x] = bcol;
        pixels[(sh - 1) * pitch + x] = bcol;
    }
    for (int y = 0; y < sh; y++) {
        pixels[y * pitch] = bcol;
        pixels[y * pitch + sw - 1] = bcol;
    }

    Uint32 white = SDL_MapRGBA(surf->format, 255, 255, 255, 255);
    Uint32 yellow = SDL_MapRGBA(surf->format, 240, 230, 100, 255);
    bdd_sdl_font_draw_str(surf, pad, pad, line0, white);
    bdd_sdl_font_draw_str(surf, pad, pad + lh, save_path, yellow);
    bdd_sdl_font_draw_str(surf, pad, pad + lh * 3, line2, white);

    g_popup_tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!g_popup_tex) return;
    SDL_SetTextureBlendMode(g_popup_tex, SDL_BLENDMODE_BLEND);
    g_popup_w = sw;
    g_popup_h = sh;
}

void bdd_save_popup_draw(SDL_Renderer *rend, int ww, int wh, const char *path)
{
    if (!g_confirm_save)
        return;
    if (!g_popup_tex)
        popup_build(rend, path);
    if (!g_popup_tex)
        return;

    SDL_Rect dst = { (ww - g_popup_w) / 2, (wh - g_popup_h) / 2,
                     g_popup_w, g_popup_h };
    SDL_RenderCopy(rend, g_popup_tex, NULL, &dst);
}
