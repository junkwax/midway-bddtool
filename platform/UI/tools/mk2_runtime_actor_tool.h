#ifndef MK2_RUNTIME_ACTOR_TOOL_H
#define MK2_RUNTIME_ACTOR_TOOL_H

#include <stddef.h>

void draw_mk2_runtime_actor_tool(void);
void draw_mk2_runtime_actor_overlay(void);
void draw_mk2_runtime_actor_isolation_window(void);
bool runtime_actor_preview_hides_object(int obj_i);
bool runtime_actor_preview_imports_loaded(void);
int runtime_actor_preview_import_count(void);
void runtime_actor_preview_import_status(char *out, size_t outsz);
bool runtime_actor_sidecar_load(void);
bool runtime_actor_sidecar_save(void);
int runtime_actor_import_inferred_level_animations(void);
void runtime_actor_autoload_for_stage(void);

#endif
