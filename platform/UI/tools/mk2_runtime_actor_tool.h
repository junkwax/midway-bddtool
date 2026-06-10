#ifndef MK2_RUNTIME_ACTOR_TOOL_H
#define MK2_RUNTIME_ACTOR_TOOL_H

#include "Core/bdd_format.h"

#include <stddef.h>

void draw_mk2_runtime_actor_tool(void);
void draw_mk2_runtime_actor_overlay(void);
void draw_mk2_runtime_actor_isolation_window(void);
bool runtime_actor_preview_hides_object(int obj_i);
bool runtime_actor_preview_imports_loaded(void);
bool runtime_actor_image_is_preview_import(const Img *im);
int runtime_actor_preview_import_count(void);
void runtime_actor_preview_import_status(char *out, size_t outsz);
int runtime_actor_count(void);
int runtime_actor_total_frame_count(void);
int runtime_actor_missing_frame_count(void);
int runtime_actor_name_contains_count(const char *needle);
bool runtime_actor_info(int actor_index, char *name, size_t namesz,
                        int *x, int *y, int *screen_space_y);
void runtime_actor_mark_preview_import_range(int image_base, int palette_base,
                                            int start, int end,
                                            const char *source_label);
bool runtime_actor_sidecar_load(void);
bool runtime_actor_sidecar_save(void);
int runtime_actor_import_inferred_level_animations(void);
void runtime_actor_autoload_for_stage(void);

#endif
