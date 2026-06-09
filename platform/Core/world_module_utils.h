#ifndef WORLD_MODULE_UTILS_H
#define WORLD_MODULE_UTILS_H

#include "bdd_format.h"

int get_world_size(int *out_w, int *out_h);
void fit_tile_to_world(void);
int parse_module_bounds(int m, char *name, int *x1, int *x2, int *y1, int *y2);
int image_max_pixel(const Img *im);
int assign_module(int depth, int sy, int width, int height);
void simple_ensure_module(int obj_idx);

#endif
