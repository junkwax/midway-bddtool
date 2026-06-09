#ifndef SDL_SAVE_POPUP_H
#define SDL_SAVE_POPUP_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_confirm_save;

void bdd_save_popup_cancel(void);
void bdd_save_popup_draw(SDL_Renderer *rend, int ww, int wh, const char *path);

#ifdef __cplusplus
}
#endif

#endif
