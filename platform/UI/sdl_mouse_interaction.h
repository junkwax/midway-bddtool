#ifndef SDL_MOUSE_INTERACTION_H
#define SDL_MOUSE_INTERACTION_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BddSdlMouseState {
    int dragging;
    int mmb_drag;
    int drag_ox;
    int drag_oy;
    int drag_vx;
    int drag_vy;

    int obj_drag_idx;
    int obj_drag_ox;
    int obj_drag_oy;
    int obj_drag_use_position_delta;
    int obj_drag_capacity;
    int *obj_drag_depth_a;
    int *obj_drag_sy_a;

    int guide_vx[8];
    int guide_vn;
    int guide_vy[8];
    int guide_hn;

    int sel_rx1;
    int sel_ry1;
    int sel_rx2;
    int sel_ry2;
    int sel_rect_active;
    int sel_rect_additive;
} BddSdlMouseState;

int bdd_sdl_mouse_state_init(BddSdlMouseState *state);
void bdd_sdl_mouse_state_shutdown(BddSdlMouseState *state);

void bdd_sdl_mouse_button_down(BddSdlMouseState *state,
                               const SDL_MouseButtonEvent *button,
                               int window_w,
                               int *view_x, int *view_y, int *zoom,
                               int *last_obj);
void bdd_sdl_mouse_button_up(BddSdlMouseState *state,
                             const SDL_MouseButtonEvent *button,
                             int view_x, int view_y, int zoom,
                             int window_w, int window_h,
                             int *last_obj);
void bdd_sdl_mouse_motion(BddSdlMouseState *state,
                          const SDL_MouseMotionEvent *motion,
                          int *view_x, int *view_y, int zoom,
                          int *hover_x, int *hover_y,
                          Uint32 *hover_since,
                          int *hover_printed);
void bdd_sdl_mouse_tick(BddSdlMouseState *state,
                        int *view_x, int *view_y,
                        int zoom, int window_w, int window_h,
                        int *hover_x, int *hover_y,
                        Uint32 *hover_since,
                        int *hover_printed);

#ifdef __cplusplus
}
#endif

#endif
