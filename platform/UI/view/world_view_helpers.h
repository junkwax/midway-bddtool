#ifndef WORLD_VIEW_HELPERS_H
#define WORLD_VIEW_HELPERS_H

#include "bdd_format.h"

float gv_scroll_factor(int layer_byte);
void gv_object_origin(int obj_index, int *x, int *y);
float mk2_scroll_factor_for_layer(int layer);
int active_image_index(void);
int object_pixel_at_screen(const Obj *o, const Img *im, int camera_x, int screen_x, int screen_y);
int img_anim_offset_x(const Img *im, int hfl);
int img_anim_offset_y(const Img *im, int vfl);

#endif
