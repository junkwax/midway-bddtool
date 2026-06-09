#include "UI/overlays/sdl_reference_overlay.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"

SDL_Texture *g_ref_tex = NULL;
int g_ref_ox = 0;
int g_ref_oy = 0;

void bdd_reference_overlay_draw(SDL_Renderer *rend, int view_x, int view_y, int zoom)
{
    if (!g_ref_tex) return;

    SDL_Rect ref_dst = { 0, 0, 0, 0 };
    bdd_world_to_screen(g_ref_ox, g_ref_oy, view_x, view_y, zoom,
                        &ref_dst.x, &ref_dst.y);
    SDL_QueryTexture(g_ref_tex, NULL, NULL, &ref_dst.w, &ref_dst.h);
    ref_dst.w *= zoom;
    ref_dst.h *= zoom;
    SDL_SetTextureAlphaMod(g_ref_tex, 120);
    SDL_RenderCopy(rend, g_ref_tex, NULL, &ref_dst);
    SDL_SetTextureAlphaMod(g_ref_tex, 255);
}
