#ifndef SDL_IMAGE_GRID_VIEW_H
#define SDL_IMAGE_GRID_VIEW_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_image_grid_view_draw(SDL_Renderer *rend, int win_w,
                              int scroll_x, int scroll_y, int zoom);

#ifdef __cplusplus
}
#endif

#endif
