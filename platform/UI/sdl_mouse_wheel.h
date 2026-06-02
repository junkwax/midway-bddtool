#ifndef SDL_MOUSE_WHEEL_H
#define SDL_MOUSE_WHEEL_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_sdl_mouse_wheel_handle(const SDL_MouseWheelEvent *wheel,
                                SDL_Renderer *rend,
                                int wh, int ww,
                                int hover_x, int hover_y,
                                int *view_x, int *view_y,
                                int *zoom,
                                int *last_obj,
                                int *hover_printed);

#ifdef __cplusplus
}
#endif

#endif
