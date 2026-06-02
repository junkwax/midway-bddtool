#ifndef SDL_KEYBOARD_ACTIONS_H
#define SDL_KEYBOARD_ACTIONS_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BddSdlKeyboardState {
    SDL_Keycode repeat_sym;
    Uint32 repeat_next;
} BddSdlKeyboardState;

void bdd_sdl_keyboard_init(BddSdlKeyboardState *state);
void bdd_sdl_keyboard_key_down(BddSdlKeyboardState *state,
                               const SDL_KeyboardEvent *key,
                               SDL_Renderer *rend,
                               int *view_x, int *view_y, int *zoom,
                               int wx_min, int wy_min,
                               Uint64 *texture_sig,
                               int *running);
void bdd_sdl_keyboard_key_up(BddSdlKeyboardState *state,
                             const SDL_KeyboardEvent *key);
void bdd_sdl_keyboard_text_input(const SDL_TextInputEvent *text);
void bdd_sdl_keyboard_tick(BddSdlKeyboardState *state,
                           int *view_x, int *view_y, int zoom);

#ifdef __cplusplus
}
#endif

#endif
