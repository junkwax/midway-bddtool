#ifndef SDL_WORLD_MARKERS_H
#define SDL_WORLD_MARKERS_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_world_markers_draw(SDL_Renderer *rend, int view_x, int view_y,
                            int zoom, int ww, int wh);
void bdd_mouse_crosshair_draw(SDL_Renderer *rend, int hover_x, int hover_y);

#ifdef __cplusplus
}
#endif

#endif
