#ifndef EDITOR_APP_GLOBALS_H
#define EDITOR_APP_GLOBALS_H

#ifdef __cplusplus
extern "C" {
#endif

extern int  g_need_rebuild;
extern int  g_batch_trim_min_saved;
extern int  g_palette_compress_target;
extern bool g_palette_compress_new_palette;
extern int  g_sel_pal;
extern bool g_focus_obj_properties_next;
extern int  g_auto_save_tick;
extern char g_stage_internal_name[32];
extern char g_stage_display_name[64];
extern char g_stage_last_command[2048];
extern int  g_gate_payload_limit;
extern int  g_pan_scan_start_x;
extern int  g_pan_scan_end_x;
extern int  g_pan_scan_step;
extern int  g_pan_scan_stride;
extern int  g_pan_scan_view_w;
extern int  g_pan_scan_view_h;
extern int  g_pan_scan_min_layer;
extern int  g_pan_scan_max_layer;
extern int  g_pan_scan_worst_x;
extern int  g_pan_scan_worst_obj_x;
extern int  g_pan_scan_worst_obj_count;
extern int  g_dup_min_pixels;
extern bool g_dup_include_mirrors;
extern int  g_unused_enable_img;
extern int  g_unused_enable_x;
extern int  g_unused_enable_y;
extern int  g_unused_enable_pal;
extern int  g_unused_enable_layer;
extern bool g_unused_auto_fit;
extern bool g_stage_red_enabled;
extern char g_outside_delete_backup_status[512];

#ifdef __cplusplus
}
#endif

extern bool g_simple_mode;

extern bool g_import_optimize_after_import;
extern bool g_import_opt_trim;
extern bool g_import_opt_compact_palettes;
extern bool g_png_import_force_8bpp;
extern bool g_img_import_index0_transparent;
extern bool g_import_skip_existing_labels;
extern bool g_show_images;
extern bool g_mk2_palette_prompt_after_img_import;
extern bool g_mk2_palette_sync_dirty;
extern bool g_show_mk2_workflow;
extern bool g_show_module_bounds;
extern bool g_show_obj_properties;
extern bool g_show_modules;
extern bool g_preview_mode;
extern bool g_gv_needs_autozoom;
extern bool g_canvas_scrollbar_mouse_capture;
extern bool g_show_new;
extern bool g_new_project_apply_allowed_after_discard;
extern bool g_show_grid_set;
extern bool g_show_help;
extern bool g_about_open;
extern bool g_show_bg_picker;
extern bool g_show_ref_settings;
extern bool g_show_prefs;
extern bool g_quit_requested;
extern int  g_img_edit_idx;
extern char g_img_edit_buf[16];
extern char g_obj_filter[16];

extern int  g_last_import_img;
extern int  g_pref_grid_sx;
extern int  g_pref_grid_sy;
extern int  g_pref_snap_dist;
extern int  g_pref_autosave_s;
extern float g_pref_font_scale;
extern bool g_pref_autoload_runtime_extras;
extern bool g_snap_visible_pixels;

extern int  g_tile_img;
extern int  g_tile_cols;
extern int  g_tile_rows;
extern int  g_tile_sx;
extern int  g_tile_sy;
extern int  g_tile_ox;
extern int  g_tile_oy;
extern int  g_tile_layer;
extern bool g_tile_preview;

#endif
