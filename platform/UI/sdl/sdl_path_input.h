#ifndef SDL_PATH_INPUT_H
#define SDL_PATH_INPUT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

int bdd_path_input_is_open(void);
void bdd_path_input_open(void);
void bdd_path_input_close(void);
void bdd_path_input_backspace(void);
void bdd_path_input_append_text(const char *text);
const char *bdd_path_input_text(void);
void bdd_path_input_draw(SDL_Renderer *rend, int ww, int wh);

#ifdef __cplusplus
}
#endif

#endif
