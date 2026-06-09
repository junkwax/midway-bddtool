#ifndef SDL_OBJECT_DRAG_AUTOPAN_H
#define SDL_OBJECT_DRAG_AUTOPAN_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_sdl_object_drag_auto_pan(int *drag_idx,
                                  int *guide_vn, int *guide_hn,
                                  int *view_x, int *view_y,
                                  int zoom, int ww, int wh,
                                  int *drag_depth, int *drag_sy,
                                  int *hover_x, int *hover_y,
                                  Uint32 *hover_since,
                                  int *hover_printed);

void bdd_sdl_object_drag_update(int drag_idx,
                                int mouse_x, int mouse_y,
                                int drag_ox, int drag_oy,
                                int zoom,
                                int *drag_depth, int *drag_sy,
                                int *guide_vx, int *guide_vn,
                                int *guide_vy, int *guide_hn);

#ifdef __cplusplus
}
#endif

#endif
