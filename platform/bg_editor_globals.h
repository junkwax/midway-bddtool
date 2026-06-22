#ifndef BG_EDITOR_GLOBALS_H
#define BG_EDITOR_GLOBALS_H

#include "Core/app_version.h"
#include "Core/bdd_format.h"
#include "Core/app_diagnostics.h"
#include "Core/bdd_core.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/image_processing.h"
#include "Core/mk2_shared_paths.h"
#include "Core/project_header.h"
#include "Core/project_snapshot.h"
#include "Core/recent_files.h"
#include "Core/tga_import.h"
#include "Core/viewer_load.h"
#include "Core/viewer_save.h"
#include "Core/world_module_utils.h"
#include "UI/tools/mk2_runtime_actor_tool.h"
#include "UI/view/toast_notifications.h"
#include <SDL.h>
#include <stddef.h>
#include <stdio.h>

#define MK2_LOAD2_MAX_STAGE_PALETTES BDD_CORE_MK2_LOAD2_MAX_STAGE_PALETTES
#define MK2_LOAD2_MAX_MODULES BDD_CORE_MK2_LOAD2_MAX_MODULES
#define MK2_LOAD2_MAX_BLOCKS BDD_CORE_MK2_LOAD2_MAX_BLOCKS
#define MK2_LOAD2_MAX_IMAGE_HEADERS BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS
#define MK2_LOAD2_MAX_DATA_BYTES BDD_CORE_MK2_LOAD2_MAX_DATA_BYTES
#define MK2_RUNTIME_PALETTE_SLOTS BDD_CORE_MK2_RUNTIME_PALETTE_SLOTS
#define MK2_DISPLAY_OBJECT_CAP BDD_CORE_MK2_DISPLAY_OBJECT_CAP
#define MK2_DISPLAY_OBJECT_WARN BDD_CORE_MK2_DISPLAY_OBJECT_WARN
#define MK2_DISPLAY_OBJECT_RUNTIME_RESERVE BDD_CORE_MK2_DISPLAY_OBJECT_RUNTIME_RESERVE
#define MK2_BG_DYNAMIC_PALETTE_SLOTS BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS
#define MAX_RUNTIME_EXTRA_GUIDES 64

struct Mk2Diag {
    int missing_images;
    int bad_palettes;
    int palette_high_nibble;
    int runtime_palette_count;
    int runtime_palette_pressure;
    int runtime_palette16_pressure;
    int max_module_palettes;
    char max_module_palette_name[64];
    int unassigned_objects;
    int module_bound_issues;
    int old_style_bounds;
    int load2_oversize_images;
    int max_load2_block_bytes;
    int max_load2_block_bpp;
    int load2_palette_overflow;
    int load2_module_overflow;
    int load2_image_header_overflow;
    int load2_block_table_overflow;
    int load2_narrow_padded_images;
    int load2_zero_compress_disabled;
    int max_visible_objects;
    int max_visible_objects_x;
    int display_object_overflow;
    int display_object_pressure;
    int high_color_images;
    int order_issues;
};

struct Mk2Budget {
    size_t raw_image_bytes;
    size_t estimated_payload;
    int max_image_pixels;
    int oversized_images;
    int high_color_images;
    int palette_entries;
    int unused_images;
};

struct RuntimeExtraGuide {
    char asset[64];
    char label[64];
    char source[64];
    int x, y, w, h;
    float scroll;
    int layer;
    int hfl;
    unsigned int color;
};

enum UnsavedAction {
    UNSAVED_ACTION_NONE = 0,
    UNSAVED_ACTION_CLOSE_APP,
    UNSAVED_ACTION_OPEN_STAGE,
    UNSAVED_ACTION_SHOW_NEW_PROJECT,
    UNSAVED_ACTION_APPLY_NEW_PROJECT,
    UNSAVED_ACTION_NEW_SIMPLE_MK2,
    UNSAVED_ACTION_NEW_BG_PROOF,
    UNSAVED_ACTION_NEW_CHECKER,
    UNSAVED_ACTION_CLOSE_DOC
};

#define MAX_DOCS 8

struct Document {
    bool  loaded;
    int   tab_id;
    ProjectSnapshot snapshot;
    char  bdb_path[512];
    char  bdd_path[512];
    int   dirty;
};

#ifdef __cplusplus
extern "C" {
#endif

extern Img *g_img;
extern int  g_ni;
extern Obj *g_obj;
extern int  g_no;
extern char g_name[64];
extern int  g_have_bdb;
extern char g_bdb_path[512];
extern char g_bdd_path[512];
extern char g_stage_internal_name[32];
extern char g_stage_config_path[512];
extern char g_stage_dir[512];
extern char g_stage_display_name[64];
extern char g_stage_bg_source[512];
extern char g_stage_mid_source[512];
extern char g_stage_floor_source[512];
extern char g_stage_rom_preview[512];
extern char g_stage_mk2_root[512];
extern char g_stage_source_zip[256];
extern char g_stage_temple_scroll[32];
extern char g_stage_rear_fence_scroll[32];
extern char g_stage_front_rail_scroll[32];
extern char g_stage_floor_far_scroll[32];
extern char g_stage_floor_mid_scroll[32];
extern char g_stage_temple_main_source[512];
extern char g_stage_outer_rail_source[512];
extern char g_stage_rear_fence_source[512];
extern int  g_stage_world_width;
extern int  g_stage_bg_fit_height;
extern int  g_stage_bg_tile_w;
extern int  g_stage_bg_tile_h;
extern int  g_stage_floor_tile_w;
extern int  g_stage_floor_tile_h;
extern int  g_stage_overlay_frames;
extern int  g_stage_overlay_streaks;
extern int  g_stage_overlay_tile_w;
extern int  g_stage_overlay_tile_h;
extern int  g_stage_overlay_mode;
extern float g_stage_overlay_strength;
extern float g_stage_overlay_line_width;
extern float g_stage_overlay_phase_degrees;
extern int  g_stage_visible_colors;
extern int  g_stage_bg_palette_mode;
extern int  g_stage_bg_cols;
extern int  g_stage_bg_rows;
extern int  g_stage_sleep_ticks;
extern int  g_stage_temple_height;
extern int  g_stage_temple_y;
extern int  g_stage_outer_rail_height;
extern int  g_stage_outer_rail_y;
extern int  g_stage_rear_fence_height;
extern int  g_stage_rear_fence_y;
extern int  g_stage_floor_far_end_y;
extern int  g_stage_floor_mid_end_y;
extern bool g_stage_pan_mid;
extern bool g_stage_perspective_layers;
extern bool g_stage_floor_perspective;
extern bool g_stage_auto_fit_sources;
extern bool g_stage_stock_portal_sides;
extern bool g_stage_mirror_temple;
extern bool g_stage_no_install;
extern float g_stage_arch_blue_strength;
extern int  g_gate_payload_limit;
extern int  g_gate_min_full_coverage;
extern int  g_gate_min_top_coverage;
extern int  g_gate_min_floor_coverage;
extern bool g_gate_block_on_high_color;
extern int  g_asset_explorer_filter;
extern char g_stage_last_command[2048];
extern char g_stage_last_output[8192];
extern int  g_stage_last_rc;
extern int  g_stage_preview_worldx;
extern bool g_stage_start_camera_enabled;
extern int  g_stage_start_camera_x;
extern int  g_stage_start_camera_y;
extern bool g_stage_start_camera_patch_bgnd;
extern char g_stage_start_bgnd_path[512];
extern char g_stage_start_status[512];
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
extern int  g_dedup_keep_img;
extern int  g_dedup_replace_img;
extern bool g_dedup_replace_is_mirror;
extern bool g_palette_include_unused_images;
extern int  g_unused_enable_img;
extern int  g_unused_enable_x;
extern int  g_unused_enable_y;
extern int  g_unused_enable_pal;
extern int  g_unused_enable_layer;
extern bool g_unused_auto_fit;
extern char g_asset_explorer_search[32];
extern int  g_palette_compress_target;
extern bool g_palette_compress_new_palette;
extern int  g_palette_blend_a;
extern int  g_palette_blend_b;
extern int  g_palette_blend_strength;
extern int  g_palette_blend_mode;
extern bool g_palette_blend_create_new;
extern bool g_palette_merge_used_only;
extern char g_palette_blend_status[160];
extern int  g_smart_pal_target_colors;
extern bool g_smart_pal_selected_only;
extern bool g_smart_pal_module_aware;
extern bool g_smart_pal_remove_unused;
extern bool g_smart_pal_include_unused_images;
extern char g_smart_pal_status[192];
extern int  g_batch_trim_min_saved;
extern int  g_batch_preview_limit;
extern char g_import_src_bdb[512];
extern char g_import_src_bdd[512];
extern char g_import_stage_dir[512];
extern char g_import_stage_name[32];
extern char g_import_display_name[64];
extern bool g_import_open_after;
extern char g_import_status[256];
extern char g_cluster_png_path[512];
extern char g_cluster_stage_name[32];
extern int  g_cluster_tile_w;
extern int  g_cluster_tile_h;
extern int  g_cluster_visible_colors;
extern int  g_cluster_max_palettes;
extern int  g_cluster_layer;
extern int  g_cluster_start_x;
extern int  g_cluster_start_y;
extern bool g_cluster_replace_project;
extern bool g_cluster_skip_empty_tiles;
extern char g_cluster_status[256];
extern int  g_layer_role[6];
extern float g_layer_role_tint[6];
extern int  g_probe_x;
extern int  g_probe_y;
extern bool g_probe_track_mouse;
extern bool g_probe_include_hidden;
extern int  g_seam_threshold;
extern int  g_seam_max_gap;
extern bool g_seam_same_layer_only;
extern int  g_headroom_world_x;
extern int  g_headroom_lift;
extern int  g_headroom_width;
extern bool g_headroom_use_preview_x;
extern int  g_occ_camera_x;
extern bool g_occ_use_preview_x;
extern int  g_occ_player_y;
extern int  g_occ_player_w;
extern int  g_occ_player_h;
extern int  g_occ_p1_x;
extern int  g_occ_p2_x;
extern int  g_occ_min_layer;
extern int  g_mirror_gap;
extern int  g_mirror_pair_left_x;
extern int  g_mirror_pair_right_x;
extern int  g_mirror_pair_y;
extern int  g_mirror_layer;
extern float g_danger_palette_strength;
extern float g_danger_palette_keep_blue;
extern char g_stage_health_threshold[32];
extern char g_stage_comeback_margin[32];
extern int  g_stage_red_steps;
extern int  g_stage_red_step_delay;
extern int  g_stage_red_fade_in_frames;
extern int  g_stage_red_hold_frames;
extern int  g_stage_red_fade_out_frames;
extern int  g_stage_red_timer_threshold;
extern bool g_stage_red_enabled;
extern bool g_stage_red_match_point;
extern bool g_stage_red_low_health;
extern bool g_stage_red_timer;
extern bool g_stage_red_round_start;
extern bool g_stage_red_finish;
extern bool g_stage_red_comeback_recover;
extern bool g_stage_red_round3_only;
extern float g_stage_red_background_strength;
extern float g_stage_red_foreground_strength;
extern int  g_fx_palette_steps;
extern int  g_fx_preview_color_count;
extern int  g_cam_bookmark_count;
extern int  g_cam_bookmark_x[8];
extern char g_cam_bookmark_name[8][32];
extern int  g_cam_new_x;
extern char g_cam_new_name[32];
extern char g_bdb_header[256];
extern char (*g_bdb_modules)[256];
extern int  g_bdb_num_modules;
extern char g_outside_delete_backup_status[512];
extern Uint32 (*g_pals)[256];
extern int *g_pal_count;
extern int  g_n_pals;
extern char (*g_pal_name)[64];

extern int  g_show_grid;
extern int  g_show_borders;
extern int  g_show_objects;
extern int  g_show_labels;
extern Uint8 g_grid_color[3];
extern int  g_grid_sx;
extern int  g_grid_sy;
extern int *g_obj_lock;
extern int *g_obj_hidden;
extern int  g_ni_tex;
extern int  g_hl_obj;
extern int  g_grid_snap;
extern int *g_sel_flags;
extern int  g_confirm_save;
extern int  g_dirty;
extern int  g_ctx_obj;
extern Uint8 g_bg_color[3];
extern int  g_zoom;
extern int  g_view_x;
extern int  g_view_y;
extern int  g_cur_tool;
extern int  g_place_tool_img;
extern int  g_auto_save_tick;
extern int  g_hover_obj;
extern SDL_Texture *g_ref_tex;
extern int g_ref_ox;
extern int g_ref_oy;
extern SDL_Renderer *g_rend;
extern SDL_Texture **g_textures;
extern int  g_game_view;
extern int  g_scroll_pos;
extern int  g_game_view_y;
extern int  g_split_view;
extern int  g_split_scroll_a;
extern int  g_runtime_layout_view;
extern bool  g_anim_playing;
extern float g_anim_speed;
extern int   g_anim_dir;
extern float g_anim_fpos;
extern bool  g_anim_bounce;
extern bool  g_anim_v_sweep;
extern float g_anim_vy_fpos;
extern int   g_anim_vy_dir;
extern float g_anim_vy_speed;
extern bool  g_recording;
extern int   g_record_n;
extern char  g_record_dir[512];
extern float g_record_accum;
extern int  g_need_rebuild;
extern int  g_view_changed;

extern int g_sel_pal;
extern bool g_focus_obj_properties_next;

int bg_editor_snap_dist(void);
int bg_editor_object_snap_rect_at(int obj_index, int origin_x, int origin_y,
                                  int *x1, int *y1, int *x2, int *y2);

#ifdef __cplusplus
}
#endif

// Enums and C++ specific
enum RightPanelId {
    RIGHT_PANEL_OBJECTS = 0,
    RIGHT_PANEL_OBJ_PROPERTIES,
    RIGHT_PANEL_PALETTES,
    RIGHT_PANEL_IMAGES,
    RIGHT_PANEL_MODULES,
    RIGHT_PANEL_COUNT
};

struct PanCoverageSummary {
    float full;
    float top;
    float floor;
    int worst_x;
    int points;
};

struct DisplayObjectSummary {
    int max_count;
    int worst_x;
    int points;
};

struct Template {
    const char *label;
    const char *name;
    int w, h, depth, pals;
};

struct CardTemplate {
    const char *title;
    const char *desc;
    int ti;
};

void merge_duplicate_palettes(void);
extern bool g_canvas_scrollbar_mouse_capture;
void batch_palette_rebuild(void);
int remove_unused_palettes_impl(bool do_remove);
void capacity_warn_check(void);
extern bool g_show_new;
extern int  g_new_template;
extern char g_new_name[64];
extern int  g_new_w;
extern int  g_new_h;
extern int  g_new_depth;
extern int  g_new_pals;
extern bool g_new_project_apply_allowed_after_discard;
extern const Template g_templates[];
extern const int g_num_templates;
extern const CardTemplate g_cards[];
extern bool g_card_blank_open;
void new_project_apply(void);
void draw_new_project(void);
void open_save_error_popup(const char *detail);
void draw_save_error_popup(void);
void draw_palette_blend_merge_tool(void);
void draw_mk2_smart_palette_grouper(void);
void draw_selected_palette_swatches(int pal_idx);
void build_blended_palette(int dst_count, int a, int b, int pct,
                           bool reverse, Uint32 *out);
int apply_palette_blend_tool(void);
int apply_palette_union_merge_tool(void);
int object_palette_for_image(const Obj *o, const Img *im);
int group_bpp_color_index(const Uint32 *pal, int count, Uint32 color);
void right_panel_set_next(int id);
void right_panel_after_begin(int id);
void right_panel_frame_begin(void);
void right_panel_frame_end(void);
void set_left_panel_default(float y, float w, float h);
SDL_Texture *editor_texture_at(int img_i);
void draw_editor_texture_transparent(SDL_Texture *tex, float width, float height);
void draw_editor_texture_transparent_uv(SDL_Texture *tex, float width, float height,
                                        float u0, float v0, float u1, float v1);
SDL_Texture *create_object_palette_preview_texture(int obj_i, int *out_w, int *out_h);
void draw_layer_role_hint(int layer);
void path_join(char *out, size_t outsz, const char *dir, const char *file);
bool stage_file_exists(const char *path);
void stage_dirname(const char *path, char *out, size_t outsz);
bool ensure_dir_recursive(const char *path);
int run_command_capture(const char *cmd, char *out, size_t outsz);
void clear_preview_image_file_texture(void);
void draw_preview_image_file(const char *label, const char *path);
void resolve_stage_file(char *out, size_t outsz, const char *path);
void stage_append(char *out, size_t outsz, const char *text);
void stage_append_arg(char *out, size_t outsz, const char *arg);
void json_write_string(FILE *f, const char *s);
char *stage_read_text_file(const char *path);
bool json_get_string_value(const char *json, const char *key, char *out, size_t outsz);
bool json_get_int_value(const char *json, const char *key, int *out);
bool json_get_float_value(const char *json, const char *key, float *out);
bool json_get_bool_value(const char *json, const char *key, bool *out);
bool json_get_object_value(const char *json, const char *key, char *out, size_t outsz);
void draw_path_field(const char *label, char *buf, size_t bufsz,
                     const char *dialog_title, const char *filter);
void draw_mk2_finish_line_gate(void);
void draw_mk2_integration_summary(void);
void draw_mk2_mame_preview_tool(void);
void draw_mk2_one_click_validation_run(void);
void draw_mk2_patch_recipe_import_tool(void);
void draw_mk2_runtime_preview_tool(void);
void draw_mk2_stage_handoff_exports(void);
void draw_mk2_preview_diff_tool(void);
void draw_mk2_stage_preview_dashboard(void);
void draw_mk2_project_seed_tools(void);
void draw_mk2_load2_doctor_tool(void);
void draw_mk2_stage_start_camera_tool(void);
void draw_mk2_camera_bookmarks(void);
void draw_mk2_template_wizard_tool(void);
void draw_mk2_palette_builder_tool(void);
void draw_mk2_animation_planner_tool(void);
void draw_mk2_rom_space_budget_tool(void);
void draw_mk2_stage_readiness_gate(void);
void draw_mk2_rom_space_reclaim(void);
void draw_mk2_wedge_risk_tool(void);
void draw_mk2_pan_coverage_scanner(void);
void draw_mk2_duplicate_mirror_finder(void);
void draw_mk2_palette_usage_optimizer(void);
void draw_mk2_stage_repair_mode(void);
void draw_mk2_asset_explorer(void);
void draw_mk2_unused_asset_tools(void);
void draw_mk2_authoring_tools(void);
void draw_mk2_layer_stack_inspector(void);
void draw_mk2_auto_repair_suggestions(void);
void draw_mk2_palette_seam_detector(void);
void draw_mk2_uppercut_headroom_preview(void);
void draw_mk2_parallax_sanity_checker(void);
void draw_mk2_foreground_occlusion_preview(void);
void draw_mk2_mirrored_asset_tool(void);
void draw_mk2_transparency_advisor(void);
void draw_mk2_trim_transparent_border_tool(void);
void draw_mk2_palette_remap_compress_tool(void);
void draw_mk2_batch_image_cleanup_tool(void);
void draw_mk2_danger_palette_designer(void);
void draw_mk2_stage_fx_builder_tool(void);
void draw_mk2_import_workspace(void);
void draw_mk2_clustered_png_importer(void);
void draw_mk2_stage_layer_role_editor(void);
void draw_mk2_safe_dedup_assistant(void);
bool mk2_import_workspace_apply(void);
bool mk2_clustered_png_import_apply(void);
const char *layer_role_name(int role);
float stage_fx_strength_at_frame(int frame);
void stage_fx_build_snippet(char *out, size_t outsz);
int stage_fx_generate_fade_palettes(void);
float stage_fx_layer_strength(int layer);
const char *stage_fx_trigger_summary(void);
void open_new_mk2_project_from_template(void);
void apply_mk2_template_preset(int which);
int stage_effective_preview_worldx(void);
bool stage_start_apply_bgnd_patch(void);
bool stage_start_apply_bgnd_limits(int scroll_left, int scroll_right);
bool stage_bgnd_set_module_offset(const char *module_name, int ox, int oy);
bool stage_bgnd_set_module_parallax(const char *module_name, float factor);
bool stage_bgnd_create_module_placement(const char *module_name, int ox, int oy);
bool stage_bgnd_set_bg_color(int r5, int g5, int b5);
const char *stage_palette_mode_name(void);
const char *stage_overlay_mode_name(void);
void mk2_preview_diff_use_source_and_rom(const char *source_preview, const char *rom_preview);
void mk2_preview_diff_use_composite_and_rom(const char *composite, const char *rom_preview);
void mk2_preview_diff_use_rom_and_mame(const char *rom_preview, const char *mame_output);
int mk2_include_object_in_nearest_module(int obj_idx);
int active_image_index(void);
int image_use_count(int ii);
int first_unused_image_index(void);
int mk2_max_object_order(void);
int mk2_find_first_fit_for_image(const Img *im, int *out_x, int *out_y);
int mk2_find_center_fit_for_image(const Img *im, int *out_x, int *out_y);
int mk2_enable_unused_asset(int img_i);
int mk2_add_object_for_image(int img_i, int x, int y, int layer, int pal, int hfl, int vfl,
                             bool save_undo);
int object_module_index(const Obj *o, const Img *im);
int object_pixel_at_world(const Obj *o, const Img *im, int wx, int wy);
int object_pixel_at_screen(const Obj *o, const Img *im, int camera_x, int screen_x, int screen_y);
Uint32 danger_tint_color(Uint32 c, float strength, float keep_blue);
Uint32 image_pixel_hash(const Img *im, bool hflip);
bool image_pixels_match(const Img *a, const Img *b, bool mirror);
bool image_is_imported_asset(const Img *im);
int next_free_image_index(int preferred);
extern int g_chop_tile_w;
extern int g_chop_tile_h;
extern bool g_chop_trim_tiles;
int chop_image_to_map(int img_i, int base_x, int base_y, int wx, int pal_idx,
                      bool hfl, bool vfl, int replace_obj, bool save_undo);
const char *layer_friendly_name(int layer_byte);
float mk2_scroll_factor_for_layer(int layer);
bool welcome_visible(void);
void draw_welcome(void);
extern Document g_docs[MAX_DOCS];
extern int g_num_docs;
extern int g_cur_doc;
extern int g_next_doc_id;
extern bool g_docs_init;
void doc_save(int idx);
void doc_restore(int idx);
void doc_add(void);
void doc_close(int idx);
float editor_canvas_top_y(void);
float gv_scroll_factor(int layer_byte);
void gv_object_origin(int obj_index, int *x, int *y);
int img_anim_offset_x(const Img *im, int hfl);
int img_anim_offset_y(const Img *im, int vfl);
int module_collect_stats(int module_idx, int *palette_count,
                         int *layer_count, int *first_obj);
int module_select_objects(int module_idx);
void module_center_view(int module_idx);
int mk2_first_unassigned_object(void);
int mk2_select_unassigned_objects(void);
int mk2_include_unassigned_objects_in_modules(void);
int mk2_delete_unassigned_objects(void);
void mk2_toast_outside_delete_result(int removed);
int mk2_fit_module_bounds_to_objects(void);
int mk2_sort_objects_x_major(void);
int mk2_diag_hard_issues(const Mk2Diag *d);
int mk2_diag_cautions(const Mk2Diag *d);
void mk2_collect_diag(Mk2Diag *d);
int mk2_create_default_module(void);
PanCoverageSummary mk2_compute_pan_summary(void);
DisplayObjectSummary mk2_compute_display_object_summary(void);
float mk2_screen_band_coverage(int camera_x, int y0, int y1);
int mk2_select_visible_objects_at_camera(int camera_x);
int mk2_select_visible_objects_at_camera_layer(int camera_x, int layer);
int mk2_visible_object_counts_by_layer_at_camera(int camera_x,
                                                 int *layers,
                                                 int *counts,
                                                 int max_layers);
int mk2_select_objects_by_image(int ii);
int mk2_disable_selected_assets_keep_images(void);
bool find_first_duplicate_pair(int *keep_i, int *replace_i, bool *mirror);
int apply_safe_dedup(int keep_i, int replace_i, bool mirror);
Mk2Budget mk2_collect_budget(void);
void mk2_workflow_show_check_section(void);
void mk2_workflow_show_optimize_section(void);
void draw_mk2_play_readiness_checklist(void);
int mk2_bpp_for_image(const Img *im);
int mk2_bpp_for_max_index(int max_px);
size_t mk2_estimate_image_bytes_for_bpp(const Img *im, int bpp);
size_t mk2_estimate_image_bytes(const Img *im);
void mk2_append_budget_relief_report(char *out, size_t outsz, size_t over_bytes);
void draw_mk2_budget_relief_suggestions(const Mk2Budget *budget, int payload_limit);
bool mk2_has_drawable_stage(void);
size_t mk2_estimate_duplicate_savings(void);
void mk2_readiness_report(char *out, size_t outsz);
bool stage_write_config(void);
bool stage_load_config(void);
void stage_use_current_project(void);
void stage_build_command(const char *action, char *out, size_t outsz);
void stage_copy_command(const char *action);
void stage_run_command(const char *action);
void stage_open_generated_bdb(void);
bool stage_write_package_manifest(void);
bool stage_write_patch_recipe(void);
bool stage_import_patch_recipe(void);
extern bool g_validation_write_manifest;
extern bool g_validation_write_recipe;
extern bool g_validation_export_composite;
extern bool g_validation_safe_fixes;
extern char g_stage_manifest_path[512];
extern char g_stage_patch_recipe_path[512];
extern bool g_stage_manifest_include_readiness;
extern bool g_stage_manifest_include_bookmarks;
extern bool g_stage_manifest_include_commands;

void request_unsaved_action(int action, const char *path = NULL, int doc_idx = -1);
void create_checker_test_level(void);
void create_bg_proof_level(void);
extern int  g_unsaved_action;
extern int  g_unsaved_doc_idx;
extern bool g_unsaved_prompt_open;
extern bool g_unsaved_action_bypass;
extern bool g_unsaved_continue_without_save;
extern bool g_close_approved;
extern char g_unsaved_action_path[512];
bool save_all_dirty_documents(void);
bool has_unsaved_work(void);
void clear_unsaved_action(void);
void execute_unsaved_action(int action);
void open_stage_path_now(const char *path);
void draw_unsaved_action_prompt(void);
bool file_dialog_open(const char *title, const char *filter, char *out, int outsz);
extern bool g_show_images;
extern bool g_png_import_force_8bpp;
extern bool g_import_optimize_after_import;
extern bool g_import_opt_trim;
extern bool g_import_opt_compact_palettes;
extern bool g_img_import_index0_transparent;
extern bool g_import_skip_existing_labels;
extern bool g_mk2_palette_prompt_after_save;
extern bool g_mk2_palette_prompt_after_img_import;
extern bool g_mk2_palette_auto_sync_on_save;
extern bool g_mk2_palette_allow_over_budget;
extern bool g_mk2_palette_sync_dirty;
extern bool g_mk2_palette_sync_popup;
extern bool g_mk2_lod_stale_warn_after_save;
extern char g_mk2_palette_sync_reason[128];
extern char g_mk2_palette_sync_asm[512];
extern char g_mk2_palette_sync_table[64];
extern char g_mk2_palette_sync_status[512];
extern char g_mk2_palette_sync_output[2048];
extern int  g_mk2_palette_sync_last_rc;
extern int g_last_import_img;
void import_png(const char *path, bool save_undo = true);
int import_img_file(const char *path, bool save_undo = true);
int import_img_file_filtered(const char *path, bool save_undo,
                             const unsigned char *selected, int selected_len);
void open_img_import_picker(const char *path);
void draw_img_import_picker(void);
bool img_label_exists_ci(const char *label);
bool folder_dialog_open(const char *title, char *out, int outsz);
int batch_import_img(const char *dir);
int import_lod_file(const char *path, bool save_undo = true);
int batch_import_png(const char *dir);
void export_composite_png(void);
void export_viewport_png(void);
void export_mk2_assembly(void);
void export_image_tga(Img *im);
void export_image_png(Img *im);
void export_sprite_sheet_png(void);
extern bool g_show_verify;
void draw_verify(void);
extern bool g_show_tile;
extern bool g_simple_mode;
extern bool g_show_mk2_workflow;
extern bool g_show_mk2_stage_kit;
extern int  g_mk2_workflow_section;
extern int  g_mk2_focus_tool;
extern int  g_mk2_stage_kit_section;
void open_mk2_simple_level_dialog(void);
void draw_mk2_simple_level_dialog(void);
bool create_mk2_simple_four_image_level(const char *bg_path,
                                        const char *floor_path,
                                        const char *corner_path,
                                        const char *front_path);
void mk2_palette_sync_request_prompt(const char *reason, bool allow_if_unknown_path = false);
bool mk2_palette_sync_auto_apply_if_ready(const char *reason);
int  mk2_bgndpal_compact(const char *bgnpal, char *status, size_t statussz);
void draw_mk2_palette_sync_prompt(void);
void mk2_lod_stale_check_after_save(void);
void draw_mk2_lod_stale_warning(void);
bool save_all_project(void);
bool file_dialog_save(const char *title, const char *filter, char *out, int outsz);
void set_project_save_paths_from_any(const char *path);
void ensure_bdb_header_for_save(void);
void stage_export_bundle(void);
extern bool g_show_prefs;
extern bool g_quit_requested;
extern bool g_welcome_show;
void draw_quit_dialog(void);
void draw_empty_canvas_hint(void);
void draw_canvas_scrollbars(void);
void draw_status(void);
extern int g_img_edit_idx;
extern char g_img_edit_buf[16];
void draw_img_idx_edit(void);
extern int g_tile_img;
extern int g_tile_cols;
extern int g_tile_rows;
extern int g_tile_sx;
extern int g_tile_sy;
extern int g_tile_ox;
extern int g_tile_oy;
extern int g_tile_layer;
extern bool g_tile_preview;
void draw_tile_preview(void);
const char *mk2_layer_label(int wx);
int mk2_layer_preset_count(void);
const char *mk2_layer_preset_label(int index);
int mk2_layer_preset_wx(int index);
int tile_fill_apply(void);
void draw_tile_fill(void);
void draw_mk2_quick_tile_tools(void);
void draw_mk2_level_start_helper_tool(void);
void draw_mk2_stage_kit_controls(void);
void draw_mk2_runtime_extras_tool(void);
extern bool g_runtime_extras_overlay;
extern bool g_runtime_extras_labels;
extern bool g_runtime_guide_mouse_capture;
extern int  g_runtime_recipe_kind;
extern RuntimeExtraGuide g_tower_runtime_guides[MAX_RUNTIME_EXTRA_GUIDES];
extern const RuntimeExtraGuide g_tower_runtime_guide_defaults[];
extern const int g_tower_runtime_guide_defaults_count;
extern const RuntimeExtraGuide g_battle_runtime_guide_defaults[];
extern const int g_battle_runtime_guide_defaults_count;
extern bool g_tower_runtime_guides_init;
extern bool g_tower_runtime_guides_dirty;
extern int  g_tower_runtime_selected;
extern int  g_tower_runtime_stage_kind;
extern int  g_tower_runtime_guide_n;
int tower_runtime_guide_count(void);
void tower_runtime_guides_init_once(void);
void runtime_guides_clear_session(void);
bool mk2_current_stage_is_battle(void);
bool mk2_current_stage_has_known_runtime_extras(void);
int mk2_runtime_autoload_stage_recipe(void);
bool mk2_find_sibling_data_file(const char *filename, char *out, size_t outsz);
int import_runtime_lod_sources_for_active_guides(bool save_undo);
int import_runtime_lod_source_labels(const char *lod_token,
                                     const char *const *labels,
                                     int label_count,
                                     bool save_undo);
int delete_all_runtime_guide_objects(bool save_undo);
int delete_runtime_guide_images_and_objects(int *out_objects, int *out_images);
int hide_all_runtime_guides_for_session(void);
int mk2_bake_runtime_guides_to_bdb(bool save_undo, bool allow_guide_images);
void mk2_copy_selected_runtime_recipe(void);
int find_img_by_label_casefold(const char *name);
void runtime_guide_visual_rect(const RuntimeExtraGuide *e, const Img *im,
                               int *x, int *y, int *w, int *h);
bool object_matches_runtime_guide(const Obj *o, const RuntimeExtraGuide *e);
int runtime_guide_existing_object_count(const RuntimeExtraGuide *e);
int runtime_guide_existing_object_for_index(int guide_idx);
int sync_runtime_guide_object_placement(int guide_idx);
int select_runtime_guide_objects(int guide_idx);
int delete_runtime_guide_objects(int guide_idx);
int hide_runtime_guide_for_session(int guide_idx);
void draw_mk2_runtime_extras_overlay(void);
void draw_game_view_overlay(void);
void draw_game_view_controls(void);
void draw_world_context_overlay(void);
void draw_world_object_overlays(void);
void run_auto_save_tick(void);
void derive_stage_pair_paths(const char *path,
                             char *bdd, size_t bddsz,
                             char *bdb, size_t bdbsz);
const char *path_basename_ptr(const char *path);
void lod_tag_imported_range(int start, int end, const char *lod_path);
bool runtime_actor_preview_imports_loaded(void);
bool runtime_actor_image_is_preview_import(const Img *im);
void runtime_actor_mark_preview_import_range(int image_base, int palette_base,
                                            int start, int end,
                                            const char *source_label);
void draw_mk2_workflow(void);
extern int g_pref_grid_sx;
extern int g_pref_grid_sy;
extern int g_pref_snap_dist;
extern int g_pref_autosave_s;
extern float g_pref_font_scale;
extern bool g_pref_autoload_runtime_extras;
extern bool g_runtime_autoload_pref_loaded;
void draw_preferences(void);
int copy_selected_objects_to_clipboard(void);
extern int g_clip_count;
int delete_object_targets_preserve_order(int active, const char *undo_label);
void mk2_delete_object_preserve_order(int idx);
int paste_clipboard_objects(int offset_x, int offset_y);
int flip_object_targets_mirrored(int active, bool horizontal, const char *undo_label);
int selected_count(void);
int delete_unused_images_impl(bool imported_only, const char *undo_label);
int delete_image_slot_if_unused(int img_i);
bool image_passes_list_filter(const Img *im, int img_filter,
                              const char *img_search, int search_idx);
int collect_matching_imported_images(int img_filter, const char *img_search,
                                     int search_idx, unsigned char *match,
                                     int *out_pixels, int *out_uses);
int select_matching_imported_image_uses(int img_filter, const char *img_search, int search_idx);
int delete_matching_imported_images_and_uses(int img_filter, const char *img_search, int search_idx);
int mk2_set_image_as_static_background(int img_i);
void reimport_image(int img_idx, const char *path);
int active_object_index(void);
void open_object_properties(int idx);
bool obj_properties_take_focus_request(void);
void edit_block_for_object(int idx);
void open_sprite_resize(int img_idx, bool selected_only_default);
void open_group_sprite_resize(void);
void draw_sprite_resize_dialog(void);
void open_split_object_dialog(int obj_idx);
void draw_split_object_dialog(void);
extern bool g_show_group_bpp_reducer;
void draw_mk2_selected_bpp_reducer_tool(void);
void draw_group_bpp_reducer_panel(void);
extern bool g_budget_relief_show_used;
extern bool g_budget_relief_show_unused;
extern int g_budget_relief_rows;
extern int g_budget_relief_highlight_img_ii;
void toggle_object_selection(int idx);
void move_object_to_index(int src, int dst);
void center_view_on_object(int idx);
void export_object_image_png_dialog(int idx);
void duplicate_object_menu_targets(int active);
void delete_object_menu_targets(int active);
void flip_object_menu_targets(int active, bool horizontal);
int reorder_object_menu_targets(int active, bool to_front);
void select_all_with_image_ii(int image_ii);
void select_all_in_layer_byte(int layer);
bool wrap_selected_objects_in_region(void);
bool create_module_from_selection(void);
void assign_layer_to_object_targets(int active, int layer);
void assign_palette_to_object_targets(int active, int pal);
void assign_module_to_object_targets(int active, int module_idx);
int active_menu_image_index(void);
extern bool g_hint_place;
extern bool g_hint_import;
extern bool g_hint_save;
bool hint_badge(bool *flag, const char *id);
void add_image_to_view_center(int img_i);
extern int g_block_edit_img;
extern int g_block_edit_zoom;
extern int g_block_edit_col;
extern bool g_block_edit_open;
void draw_block_editor(void);
extern bool g_show_module_bounds;
void draw_module_bounds_overlay(void);
extern bool g_layer_tint;
extern bool g_show_alignment_doctor;
void draw_alignment_doctor_overlay(void);
void draw_world_game_rect(void);
bool stage_overview_is_open(void);
void toggle_stage_overview(void);
void poll_stage_overview_result(void);
void draw_stage_overview(void);
extern bool g_show_grid_set;
void draw_grid_settings(void);
extern bool g_show_bg_picker;
void draw_bg_picker(void);
extern bool g_snap_visible_pixels;
extern bool g_preview_mode;
void route_to_game_preview_screen(bool recenter_camera, bool fit_zoom);
extern bool g_gv_needs_autozoom;
void fit_game_preview_zoom_to_window(void);
void focus_editor_on_game_preview_screen(void);
extern bool g_show_pal_anim;
bool pal_animation_enabled(void);
void pal_animation_step(float dt);
void draw_pal_anim_panel(void);
extern bool g_show_minimap;
void draw_minimap(void);
extern bool g_show_layers;
extern char g_obj_filter[16];
void draw_layers(void);
extern bool g_show_modules;
void draw_modules(void);
extern bool g_show_obj_properties;
void draw_obj_list(void);
void draw_obj_list_contents(void);
extern int g_hover_img_ii;
extern float g_fps;
extern int g_fps_count;
extern float g_fps_timer;
extern float g_prev_display_w;
extern float g_prev_display_h;
extern bool g_display_w_resized;
void draw_image_list(void);
extern bool g_show_undo_history;
void draw_undo_history(void);
extern bool g_show_debug_info;
void draw_debug_info(void);
extern bool g_show_level_stats;
extern bool g_show_bpp_preview;
extern bool g_show_gc;
void draw_mk2_small_artifact_finder(void);
extern bool g_show_checkpoints;
void open_mk2_tool(int tool);
void zoom_to_fit(void);
void zoom_to_selection(void);
extern bool g_dock_right_panels_next;
void settings_save(void);
void settings_load(void);
void settings_load_runtime_autoload_pref_once(void);
extern bool g_show_help;
extern bool g_about_open;
void draw_help(void);
void draw_about(void);

extern bool g_show_ref_settings;
void draw_ref_settings(void);
void draw_obj_properties(void);
void draw_obj_properties_contents(void);
#endif // BG_EDITOR_GLOBALS_H
