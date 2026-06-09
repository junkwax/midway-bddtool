#ifndef SDL_BITMAP_FONT_H
#define SDL_BITMAP_FONT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_sdl_font_draw_str(SDL_Surface *surf, int x, int y, const char *s, Uint32 fg);

#ifdef __cplusplus
}
#endif

#endif
