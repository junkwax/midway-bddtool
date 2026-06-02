#ifndef SDL_REFERENCE_OVERLAY_H
#define SDL_REFERENCE_OVERLAY_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_reference_overlay_draw(SDL_Renderer *rend, int view_x, int view_y, int zoom);

#ifdef __cplusplus
}
#endif

#endif
