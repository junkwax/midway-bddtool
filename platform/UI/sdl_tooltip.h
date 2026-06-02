#ifndef SDL_TOOLTIP_H
#define SDL_TOOLTIP_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_tooltip_free(void);
void bdd_tooltip_build_hover(SDL_Renderer *rend,
                             int mx, int my,
                             int view_x, int view_y, int zoom,
                             int ww, int wh);
void bdd_tooltip_build_object(SDL_Renderer *rend, int obj_i,
                              int ax, int ay, int ww, int wh);
void bdd_tooltip_draw(SDL_Renderer *rend);

#ifdef __cplusplus
}
#endif

#endif
