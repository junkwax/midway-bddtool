#ifndef SDL_VIEW_HUD_H
#define SDL_VIEW_HUD_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_view_hud_draw(SDL_Renderer *rend, int ww, int wh, int zoom);

#ifdef __cplusplus
}
#endif

#endif
