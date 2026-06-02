#ifndef EDITOR_PROJECT_STORAGE_H
#define EDITOR_PROJECT_STORAGE_H

#include "bdd_format.h"

#ifdef __cplusplus
extern "C" {
#endif

int editor_project_storage_init(void);
void editor_project_storage_shutdown(void);
int editor_project_image_capacity(void);
int editor_project_object_capacity(void);
int editor_project_module_capacity(void);
int editor_project_palette_capacity(void);
int editor_project_reserve_images(int min_capacity);
int editor_project_reserve_objects(int min_capacity);
int editor_project_reserve_modules(int min_capacity);
int editor_project_reserve_palettes(int min_capacity);
void editor_project_clear_selection(void);
void editor_project_clear_images(void);
void editor_project_clear_objects(void);
void editor_project_clear_modules(void);
void editor_project_clear_palettes(void);
void editor_project_clear_content(void);
void editor_project_reset_loaded_stage(void);
int editor_project_delete_image_slot(int img_i);
int editor_project_delete_marked_images(const unsigned char *delete_flags, int delete_flags_len);
Img *editor_project_append_image_slot(void);
int editor_project_swap_image_slots(int img_a, int img_b);
int editor_project_reorder_images(const int *image_order, int image_order_len);
int editor_project_truncate_images(int image_count);
int editor_project_replace_objects(const Obj *objects, int object_count, int source_capacity,
                                   const int *locks, const int *hidden, int include_flags);
int editor_project_replace_object_rows(const Obj *objects, int object_count, int source_capacity,
                                       const int *locks, const int *hidden, const int *selection);
int editor_project_replace_palettes(const Uint32 (*pals)[256], const int *counts,
                                    const char (*names)[64], int palette_count, int source_capacity);
int editor_project_replace_modules(const char (*modules)[256], int module_count, int source_capacity);
int editor_project_append_palette_slot(const char *name, int count, const Uint32 *colors);
int editor_project_set_palette_slot(int pal_i, const char *name, int count, const Uint32 *colors);
int editor_project_set_palette_color(int pal_i, int color_i, Uint32 color);
int editor_project_rotate_palette_range(int pal_i, int lo, int hi);
void editor_project_clear_palette_rgb555_cache(void);
int editor_project_set_palette_rgb555_cache(int pal_i, const Uint16 *rgb555, int count);
int editor_project_get_palette_rgb555_cache(int pal_i, Uint16 *out_rgb555, int max_count);
int editor_project_truncate_palettes(int palette_count);
int editor_project_delete_palette_slot(int pal_i);
Obj *editor_project_append_object_slot(void);
int editor_project_append_module_line(const char *line);
int editor_project_delete_object_slot(int obj_i);
int editor_project_move_object_slot(int src, int dst);
int editor_project_sort_objects_by_layer_order(void);
int editor_project_delete_module_line(int module_i);
int editor_project_set_module_line(int module_i, const char *line);
int editor_project_set_single_module_line(const char *line);

#ifdef __cplusplus
}
#endif

#endif
