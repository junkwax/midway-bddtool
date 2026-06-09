#ifndef SDL_CONTEXT_H
#define SDL_CONTEXT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

extern SDL_Renderer *g_rend;
extern SDL_Texture *g_ref_tex;
extern int g_ref_ox;
extern int g_ref_oy;

#ifdef __cplusplus
}
#endif

#endif
