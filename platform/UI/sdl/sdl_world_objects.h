#ifndef SDL_WORLD_OBJECTS_H
#define SDL_WORLD_OBJECTS_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_world_objects_draw(SDL_Renderer *rend,
                            int view_x, int view_y, int zoom, int ww, int wh,
                            int sel_rect_active, int sel_rx1, int sel_ry1,
                            int sel_rx2, int sel_ry2);

#ifdef __cplusplus
}
#endif

#endif
