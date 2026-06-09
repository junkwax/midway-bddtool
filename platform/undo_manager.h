#ifndef UNDO_MANAGER_H
#define UNDO_MANAGER_H

#include "Core/bdd_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void undo_manager_init(void);
void undo_manager_shutdown(void);

void undo_save(void);
int undo_save_object_position_delta_for_mask(const int *before_depth,
                                             const int *before_sy,
                                             const unsigned char *object_mask,
                                             int object_capacity,
                                             const char *lbl);
int undo_save_object_position_delta_for_selection(const int *before_depth,
                                                  const int *before_sy,
                                                  int object_capacity,
                                                  const char *lbl);
int undo_save_object_record_delta_for_mask(const Obj *before_objects,
                                           const unsigned char *object_mask,
                                           int object_capacity,
                                           const char *lbl);
int undo_save_module_line_delta_for_mask(const char *before_lines,
                                         const unsigned char *module_mask,
                                         int module_capacity,
                                         const char *lbl);
int undo_save_palette_slot_delta(int pal_i,
                                 const Uint32 *before_colors,
                                 int before_count,
                                 const char *before_name,
                                 const Uint16 *before_rgb555,
                                 int before_rgb555_valid,
                                 int before_rgb555_count,
                                 const char *lbl);
int undo_save_image_index_delta(int img_i, int before_idx, const char *lbl);
int undo_save_image_pixels_delta(int img_i,
                                 int width,
                                 int height,
                                 const Uint8 *before_pixels,
                                 const char *lbl);
void undo_restore(void);
void redo_restore(void);
void undo_save_ex(const char *lbl);
void undo_set_action_label(const char *lbl);

bool undo_is_available(void);
bool redo_is_available(void);

int undo_get_count(void);
const char* undo_get_history_label(int depth);
int undo_get_history_objects_count(int depth);
int undo_get_history_images_count(int depth);

#define CHECKPOINT_N 8
bool checkpoint_is_used(int index);
const char* checkpoint_get_label(int index);
void checkpoint_save(int index, const char* label);
void checkpoint_restore(int index);
void checkpoint_delete(int index);

#ifdef __cplusplus
}
#endif

#endif
