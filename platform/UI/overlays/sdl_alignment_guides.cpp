#include "UI/overlays/sdl_alignment_guides.h"

#include "bg_editor.h"

void bdd_alignment_guides_draw(SDL_Renderer *rend,
                               const int *guide_vx, int guide_vn,
                               const int *guide_vy, int guide_hn,
                               int view_x, int view_y, int zoom,
                               int ww, int wh)
{
    if (guide_vn <= 0 && guide_hn <= 0) return;

    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(rend, 80, 200, 255, 200);
    for (int g = 0; g < guide_vn; g++) {
        int sx = 0;
        bdd_world_to_screen(guide_vx[g], view_y, view_x, view_y, zoom, &sx, NULL);
        SDL_RenderDrawLine(rend, sx, 0, sx, wh);
    }
    for (int g = 0; g < guide_hn; g++) {
        int sy = 0;
        bdd_world_to_screen(view_x, guide_vy[g], view_x, view_y, zoom, NULL, &sy);
        SDL_RenderDrawLine(rend, 0, sy, ww, sy);
    }
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
}
