#ifndef SDL_OBJECT_PICKER_H
#define SDL_OBJECT_PICKER_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_place_img;

int bdd_object_picker_width(void);
int bdd_object_picker_item_height(void);
int bdd_object_picker_is_open(void);
void bdd_object_picker_toggle(void);
void bdd_object_picker_close(void);
void bdd_object_picker_cancel_place(void);
void bdd_object_picker_free_labels(void);
void bdd_object_picker_select_at_y(int y);
void bdd_object_picker_scroll(int wheel_y, int wh);
void bdd_object_picker_draw(SDL_Renderer *rend, int ww, int wh, int mx, int my);
void bdd_object_picker_draw_placement_ghost(SDL_Renderer *rend, int x, int y, int zoom);

#ifdef __cplusplus
}
#endif

#endif
