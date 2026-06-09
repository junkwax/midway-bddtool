#ifndef SDL_SELECTION_RECT_H
#define SDL_SELECTION_RECT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_selection_rect_draw(SDL_Renderer *rend, int x1, int y1, int x2, int y2);
void bdd_selection_rect_apply(int x1, int y1, int x2, int y2,
                              int view_x, int view_y, int zoom,
                              int window_w, int window_h,
                              int additive);

#ifdef __cplusplus
}
#endif

#endif
