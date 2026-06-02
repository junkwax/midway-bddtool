#ifndef SDL_WORLD_GRID_OVERLAY_H
#define SDL_WORLD_GRID_OVERLAY_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_world_grid_overlay_draw(SDL_Renderer *rend,
                                 int view_x, int view_y, int zoom,
                                 int ww, int wh, int canvas_top);

#ifdef __cplusplus
}
#endif

#endif
