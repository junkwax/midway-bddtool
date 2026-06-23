#ifndef WORLD_MODULE_UTILS_H
#define WORLD_MODULE_UTILS_H

#include "Core/bdd_format.h"
#include <cstddef>

int get_world_size(int *out_w, int *out_h);
void fit_tile_to_world(void);
int parse_module_bounds(int m, char *name, int *x1, int *x2, int *y1, int *y2);
int image_max_pixel(const Img *im);
int assign_module(int depth, int sy, int width, int height);
/* Smallest-area module fully containing the rect, or -1. Unlike assign_module
 * (LOAD2's real first-fit-by-file-order rule -- must never change), this is a
 * pure editor-UX resolution for "which module does this visually belong to"
 * when modules overlap/nest, matching the canvas hit-test's tie-break. */
int module_smallest_containing(int depth, int sy, int width, int height);
void simple_ensure_module(int obj_idx);
void module_generate_unique_name(char *out, size_t outsz, const char *base);
bool module_name_in_use(const char *name, int except_module_idx);

/* Session-only safety lock (not persisted to disk, same as object lock/hide)
 * preventing drag from moving objects out of a module by accident. Keyed by
 * name rather than module index so it survives modules being created,
 * deleted, or reordered around it. */
bool module_is_locked(const char *name);
void module_set_locked(const char *name, bool locked);
bool module_is_locked_by_index(int module_idx);
/* True if obj_index's *current* module (by position, not a cached value) is locked. */
bool object_in_locked_module(int obj_index);

#endif
