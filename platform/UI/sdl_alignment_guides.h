#ifndef SDL_ALIGNMENT_GUIDES_H
#define SDL_ALIGNMENT_GUIDES_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_alignment_guides_draw(SDL_Renderer *rend,
                               const int *guide_vx, int guide_vn,
                               const int *guide_vy, int guide_hn,
                               int view_x, int view_y, int zoom,
                               int ww, int wh);

#ifdef __cplusplus
}
#endif

#endif
