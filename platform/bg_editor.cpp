#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/bdd_format.h"
#include "utils/compat.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

extern "C" {
    extern Img *g_img;
    extern int  g_ni;
    extern Obj *g_obj;
    extern int  g_no;
    extern char g_name[64];
    extern int  g_have_bdb;
    extern char g_bdb_path[512];
    extern char g_bdd_path[512];
    extern char g_bdb_header[256];
    extern char (*g_bdb_modules)[256];
    extern int  g_bdb_num_modules;
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
    extern SDL_Texture **g_textures;
    extern SDL_Renderer *g_rend;
    extern int  g_need_rebuild;
    extern int  g_view_changed;
    extern char g_open_path[512];

    extern int  bdb_save(const char *path);
    extern int  bdd_save(void);
    extern void bdd_save_logf(const char *fmt, ...);
    extern const char *bdd_last_save_error(void);
    extern void bdd_clear_last_save_error(void);
    extern int  bdd_import_tga(const char *tga_path);
}

/* Toolbar uses compact text labels (icons via font loaded at init) */

/* ---- multi-document tabs ----------------------------------------- */

int selected_count(void);
void set_left_panel_default(float y, float w, float h);
void right_panel_set_next(int id);
void right_panel_after_begin(int id);
void center_view_on_object(int idx);
float gv_scroll_factor(int layer_byte);
void gv_object_origin(int obj_index, int *x, int *y);
bool welcome_visible(void);
// enum UnsavedAction {
//     UNSAVED_ACTION_NONE = 0,
//     UNSAVED_ACTION_CLOSE_APP,
//     UNSAVED_ACTION_OPEN_STAGE,
//     UNSAVED_ACTION_SHOW_NEW_PROJECT,
//     UNSAVED_ACTION_APPLY_NEW_PROJECT,
//     UNSAVED_ACTION_NEW_SIMPLE_MK2,
//     UNSAVED_ACTION_NEW_BG_PROOF,
//     UNSAVED_ACTION_NEW_CHECKER,
//     UNSAVED_ACTION_CLOSE_DOC
// };
void request_unsaved_action(int action, const char *path, int doc_idx);

/* Tool icons - Material Symbols Sharp (verified codepoints from imgtool overlay) */
#define ICON_HITBOX "\xEE\x8F\x82" /* U+E3C2 crop_free */
#define ICON_PLACE  "\xEE\x95\x9F" /* U+E55F place/map pin */
#define ICON_OPEN   "\xEE\x8B\x88" /* U+E2C8 folder_open */
#define ICON_UNDO   "\xEE\x85\xA6" /* U+E166 undo */
#define ICON_MARK   "\xEE\xA2\x92" /* U+E892 label */






/* Tool icons - text labels always shown */
int  g_img_edit_idx = -1;
char g_img_edit_buf[16] = "";
bool g_show_grid_set = false;
bool g_show_tile    = false;
bool g_show_mk2_workflow  = false;
bool g_gv_needs_autozoom = false;  /* set on file load; consumed by game view block */
bool g_show_mk2_stage_kit = false;
int  g_mk2_workflow_section = 0;
int  g_mk2_stage_kit_section = 0;

int  g_tile_img     = 0;
int  g_tile_cols    = 5;
int  g_tile_rows    = 5;
int  g_tile_sx      = 64;
int  g_tile_sy      = 32;
int  g_tile_ox      = 0;
int  g_tile_oy      = 0;
int  g_tile_layer   = 0x40;
bool g_tile_preview = false;
int  g_unused_enable_img   = -1;
int  g_unused_enable_x     = 0;
int  g_unused_enable_y     = 0;
int  g_unused_enable_pal   = 0;
int  g_unused_enable_layer = 0x40;
bool g_unused_auto_fit     = true;
int  g_asset_explorer_filter = 0; /* 0=all, 1=used, 2=unused */
char g_asset_explorer_search[32] = "";
int  g_probe_x = 0;
int  g_probe_y = 0;
bool g_probe_track_mouse = true;
bool g_probe_include_hidden = false;
char g_obj_filter[16] = "";
int  g_seam_threshold = 48;
int  g_seam_max_gap = 1;
bool g_seam_same_layer_only = true;
int  g_headroom_world_x = 401;
int  g_headroom_lift = 32;
int  g_headroom_width = 400;
bool g_headroom_use_preview_x = true;
int  g_occ_camera_x = 401;
bool g_occ_use_preview_x = true;
int  g_occ_player_y = 104;
int  g_occ_player_w = 44;
int  g_occ_player_h = 128;
int  g_occ_p1_x = 92;
int  g_occ_p2_x = 264;
int  g_occ_min_layer = 0x43;
int  g_mirror_gap = 0;
int  g_mirror_pair_left_x = 0;
int  g_mirror_pair_right_x = 800;
int  g_mirror_pair_y = 0;
int  g_mirror_layer = 0x40;
float g_danger_palette_strength = 0.85f;
float g_danger_palette_keep_blue = 0.25f;
char g_stage_config_path[512] = "reference\\stages\\bgprof_portal_movie_static_walk\\stage_config.json";
char g_stage_dir[512] = "reference\\stages\\bgprof_portal_movie_static_walk";
char g_stage_internal_name[32] = "BGPROF";
char g_stage_display_name[64] = "OUTER HAVEN";
char g_stage_bg_source[512] = "runtime_p_bg_source.png";
char g_stage_mid_source[512] = "runtime_p_2_temple_rails_source.png";
char g_stage_floor_source[512] = "runtime_p_1_floor_source.png";
char g_stage_rom_preview[512] = "stage_preview_outer_haven_center.png";
char g_stage_mk2_root[512] = "..\\mk2-main";
char g_stage_source_zip[256] = "rom\\mk2.zip.bak";
char g_stage_temple_scroll[32] = "0x18000";
char g_stage_rear_fence_scroll[32] = "0x18000";
char g_stage_front_rail_scroll[32] = "0x17000";
char g_stage_floor_far_scroll[32] = "0x18000";
char g_stage_floor_mid_scroll[32] = "0x1C000";
char g_stage_health_threshold[32] = "0x20";
char g_stage_comeback_margin[32] = "0x10";
char g_stage_temple_main_source[512] = "runtime_p_temple_source.png";
char g_stage_outer_rail_source[512] = "runtime_p_outer_rail_source.png";
char g_stage_rear_fence_source[512] = "runtime_p_rear_fence_source.png";
int  g_stage_world_width = 1203;
int  g_stage_bg_fit_height = 320;
int  g_stage_bg_tile_w = 40;
int  g_stage_bg_tile_h = 85;
int  g_stage_floor_tile_w = 151;
int  g_stage_floor_tile_h = 16;
int  g_stage_overlay_frames = 1;
int  g_stage_overlay_streaks = 0;
int  g_stage_overlay_tile_w = 16;
int  g_stage_overlay_tile_h = 8;
int  g_stage_overlay_mode = 0; /* 0=pulse, 1=spin, 2=inner-spin */
float g_stage_overlay_strength = 0.36f;
float g_stage_overlay_line_width = 1.35f;
float g_stage_overlay_phase_degrees = 0.0f;
int  g_stage_visible_colors = 15;
int  g_stage_bg_palette_mode = 0; /* 0=portal-core, 1=grid */
int  g_stage_bg_cols = 8;
int  g_stage_bg_rows = 1;
int  g_stage_red_steps = 8;
int  g_stage_red_step_delay = 6;
int  g_stage_red_fade_in_frames = 48;
int  g_stage_red_hold_frames = 0;
int  g_stage_red_fade_out_frames = 36;
int  g_stage_red_timer_threshold = 20;
int  g_stage_sleep_ticks = 24;
int  g_stage_temple_height = 160;
int  g_stage_temple_y = 22;
int  g_stage_outer_rail_height = 58;
int  g_stage_outer_rail_y = 134;
int  g_stage_rear_fence_height = 38;
int  g_stage_rear_fence_y = 146;
int  g_stage_floor_far_end_y = 204;
int  g_stage_floor_mid_end_y = 204;
int  g_stage_preview_worldx = 401;
bool g_stage_start_camera_enabled = false;
int  g_stage_start_camera_x = 401;
int  g_stage_start_camera_y = 0;
bool g_stage_start_camera_patch_bgnd = true;
char g_stage_start_bgnd_path[512] = "";
char g_stage_start_status[512] = "";
bool g_stage_pan_mid = true;
bool g_stage_perspective_layers = true;
bool g_stage_floor_perspective = false;
bool g_stage_auto_fit_sources = false;
bool g_stage_stock_portal_sides = true;
bool g_stage_mirror_temple = true;
bool g_stage_red_enabled = true;
bool g_stage_red_match_point = true;
bool g_stage_red_low_health = true;
bool g_stage_red_timer = false;
bool g_stage_red_round_start = false;
bool g_stage_red_finish = false;
bool g_stage_red_comeback_recover = true;
bool g_stage_red_round3_only = true;
bool g_stage_no_install = false;
float g_stage_red_background_strength = 1.00f;
float g_stage_red_foreground_strength = 0.60f;
float g_stage_arch_blue_strength = 0.48f;
int  g_fx_palette_steps = 8;
int  g_fx_preview_color_count = 16;
int  g_pan_scan_start_x = 0;
int  g_pan_scan_end_x = 800;
int  g_pan_scan_step = 32;
int  g_pan_scan_stride = 8;
int  g_pan_scan_view_w = 400;
int  g_pan_scan_view_h = 254;
int  g_pan_scan_min_layer = 0x00;
int  g_pan_scan_max_layer = 0xFF;
int  g_pan_scan_worst_x = 0;
int  g_pan_scan_worst_obj_x = 0;
int  g_pan_scan_worst_obj_count = 0;
int  g_dup_min_pixels = 64;
bool g_dup_include_mirrors = true;
bool g_palette_include_unused_images = true;
int  g_gate_payload_limit = 0x20000;
int  g_gate_min_full_coverage = 95;
int  g_gate_min_top_coverage = 90;
int  g_gate_min_floor_coverage = 95;
bool g_gate_block_on_high_color = false;
bool g_budget_relief_show_used = true;
bool g_budget_relief_show_unused = true;
int  g_budget_relief_rows = 10;
int  g_budget_relief_highlight_img_ii = -1;
int  g_cam_bookmark_count = 3;
int  g_cam_bookmark_x[8] = {0, 401, 803, 0, 0, 0, 0, 0};
char g_cam_bookmark_name[8][32] = {"Left", "Center", "Right", "", "", "", "", ""};
int  g_cam_new_x = 401;
char g_cam_new_name[32] = "Custom";
int  g_dedup_keep_img = 0;
int  g_dedup_replace_img = 0;
bool g_dedup_replace_is_mirror = false;
int  g_palette_compress_target = 31;
bool g_palette_compress_new_palette = true;
bool g_show_group_bpp_reducer = false;
int  g_palette_blend_a = 0;
int  g_palette_blend_b = 1;
int  g_palette_blend_strength = 35;
int  g_palette_blend_mode = 2; /* 0=A->B, 1=B->A, 2=both */
bool g_palette_blend_create_new = true;
bool g_palette_merge_used_only = true;
char g_palette_blend_status[160] = "";
int  g_smart_pal_target_colors = 63;
bool g_smart_pal_selected_only = true;
bool g_smart_pal_module_aware = true;
bool g_smart_pal_remove_unused = true;
bool g_smart_pal_include_unused_images = true;
char g_smart_pal_status[192] = "";
bool g_import_optimize_after_import = true;
bool g_import_opt_trim = true;
bool g_import_opt_compact_palettes = true;
bool g_img_import_index0_transparent = true;
bool g_import_skip_existing_labels = false;
int  g_chop_tile_w = 32;
int  g_chop_tile_h = 32;
bool g_chop_trim_tiles = true;
static bool g_chop_replace_original = true;
int  g_batch_trim_min_saved = 1;
int  g_batch_preview_limit = 12;
int  g_layer_role[6] = {0, 1, 2, 2, 3, 3}; /* bg, mid, floor, floor, fg, fg */
float g_layer_role_tint[6] = {1.00f, 0.80f, 0.60f, 0.60f, 0.45f, 0.45f};
char g_stage_last_command[2048] = "";
char g_stage_last_output[8192] = "";
int  g_stage_last_rc = 0;
char g_import_src_bdb[512] = "";
char g_import_src_bdd[512] = "";
char g_import_stage_dir[512] = "reference\\stages\\imported_mk2_stage";
char g_import_stage_name[32] = "IMPORTED";
char g_import_display_name[64] = "IMPORTED STAGE";
bool g_import_open_after = true;
char g_import_status[256] = "";
char g_cluster_png_path[512] = "";
char g_cluster_stage_name[32] = "BGPROF";
int  g_cluster_tile_w = 40;
int  g_cluster_tile_h = 32;
int  g_cluster_visible_colors = 63;
int  g_cluster_max_palettes = 16;
int  g_cluster_layer = 0x40;
int  g_cluster_start_x = 0;
int  g_cluster_start_y = 0;
bool g_cluster_replace_project = true;
bool g_cluster_skip_empty_tiles = true;
char g_cluster_status[256] = "";
bool g_runtime_extras_overlay = false;
bool g_runtime_extras_labels = false;
bool g_runtime_guide_mouse_capture = false;
bool g_canvas_scrollbar_mouse_capture = false;
int  g_auto_save_tick = 0;
int  g_hover_img_ii = -1; /* cross-panel highlight from Image List */
float g_fps = 0.0f;
int g_fps_count = 0;
float g_fps_timer = 0.0f;
bool g_show_minimap    = false;
bool g_show_layers     = false;
bool g_show_level_stats  = false;
bool g_show_bpp_preview  = false;
bool g_show_gc           = false;
bool g_show_checkpoints  = false;
bool g_show_obj_properties = true;
bool g_show_modules      = false;
bool g_focus_obj_properties_next = false;
bool g_show_module_bounds = false;
char g_outside_delete_backup_status[512] = "";
bool g_welcome_show = true;  /* loaded from file at init */
bool g_simple_mode  = true;  /* false = Advanced mode; toggled via View menu */
bool g_layer_tint   = false; /* tint sprites by layer color in viewport */
bool g_show_alignment_doctor = false;
bool g_pref_autoload_runtime_extras = false;
bool g_runtime_autoload_pref_loaded = false;
int  g_runtime_recipe_kind = 0; /* 0 none, 1 Tower preset, 2 Wasteland preset, 3 custom */
int  g_mk2_focus_tool = 0;   /* one-shot MK2 menu jump target */
float g_prev_display_w = 0.0f;
float g_prev_display_h = 0.0f;
bool g_display_w_resized = false;
/* preferences (persisted to bddview_settings.cfg v4) */
int   g_pref_grid_sx     = 8;
int   g_pref_grid_sy     = 8;
int   g_pref_snap_dist   = 8;
int   g_pref_autosave_s  = 60;
float g_pref_font_scale  = 1.0f;

/* g_auto_save_tick declared above as global */

bool g_about_open     = false;
int  g_sel_pal        = 0;

bool g_show_ref_settings = false;

bool file_dialog_open(const char *title, const char *filter, char *out, int outsz);
bool folder_dialog_open(const char *title, char *out, int outsz);
int  apply_safe_dedup(int keep_i, int replace_i, bool mirror);
void sync_bdb_header_counts(void);
int assign_module(int depth, int sy, int width, int height);
void create_checker_test_level(void);
void create_bg_proof_level(void);
void open_mk2_simple_level_dialog(void);
void new_project_apply(void);
bool save_all_dirty_documents(void);
void draw_mk2_authoring_tools(void);
void draw_mk2_runtime_extras_tool(void);
int mk2_bake_runtime_guides_to_bdb(bool save_undo, bool allow_guide_images);
void mk2_copy_selected_runtime_recipe(void);
bool mk2_import_workspace_apply(void);
bool mk2_clustered_png_import_apply(void);
int selected_count(void);
void stage_append(char *out, size_t outsz, const char *text);
void stage_append_arg(char *out, size_t outsz, const char *arg);
void stage_set_toast(const char *msg);
bool stage_write_config(void);
void stage_build_command(const char *action, char *out, size_t outsz);
void stage_run_command(const char *action);
void stage_open_generated_bdb(void);
void json_write_string(FILE *f, const char *s);
char *stage_read_text_file(const char *path);
bool json_get_string_value(const char *json, const char *key, char *out, size_t outsz);
bool json_get_int_value(const char *json, const char *key, int *out);
bool json_get_float_value(const char *json, const char *key, float *out);
bool json_get_bool_value(const char *json, const char *key, bool *out);
bool json_get_object_value(const char *json, const char *key, char *out, size_t outsz);
bool stage_load_config(void);
const char *stage_palette_mode_name(void);
const char *stage_overlay_mode_name(void);
void export_composite_png(void);
void export_viewport_png(void);
bool file_dialog_save(const char *title, const char *filter, char *out, int outsz);
bool hint_badge(bool *flag, const char *id);
void runtime_guides_clear_session(void);
void add_image_to_view_center(int img_i);
void select_all_with_image_ii(int image_ii);
void reimport_image(int img_idx, const char *path);
int image_nonzero_bounds(const Img *im, int *x1, int *y1, int *x2, int *y2);
void image_palette_usage_stats(const Img *im, int *used_count, int *max_idx);
int trim_image_transparent_border(int img_i, bool save_undo);
int compress_active_image_palette(int img_i, int target, bool save_undo);
int compact_palettes_for_image_range(int start_img, int end_img, bool save_undo);
int optimize_image_range_for_space(int start_img, int end_img, bool save_undo,
                                   int *trimmed_images, int *trimmed_pixels,
                                   int *compacted_palettes);
int delete_unused_images_impl(bool imported_only, const char *undo_label);
int chop_image_to_map(int img_i, int base_x, int base_y, int wx, int pal_idx,
                      bool hfl, bool vfl, int replace_obj, bool save_undo);
void open_split_object_dialog(int obj_idx);
void stage_export_bundle(void);
void batch_palette_rebuild(void);
int delete_all_runtime_guide_objects(bool save_undo);
int delete_runtime_guide_images_and_objects(int *out_objects, int *out_images);
int hide_all_runtime_guides_for_session(void);
void draw_mk2_selected_bpp_reducer_tool(void);
void draw_mk2_small_artifact_finder(void);
void draw_mk2_smart_palette_grouper(void);
void draw_palette_blend_merge_tool(void);
void draw_group_bpp_reducer_panel(void);
void draw_alignment_doctor_overlay(void);
void draw_path_field(const char *label, char *buf, size_t bufsz,
                     const char *dialog_title, const char *filter);

#include "undo_manager.h"

/* backward-compat alias used throughout the file */
#define g_undo_avail undo_is_available()

bool g_show_undo_history = false;

bool g_show_help      = false;
bool g_show_prefs     = false;

/* onboarding hint badges — dismissed per session when clicked */
bool g_hint_import = true;
bool g_hint_place  = true;
bool g_hint_save   = true;

/* world-view plain LMB drag state (intercepts SDL before bddview.c) */
static bool g_wv_drag           = false;
static float g_wv_drag_smx      = 0.0f, g_wv_drag_smy = 0.0f;
bool g_quit_requested = false;
int  g_unsaved_action = UNSAVED_ACTION_NONE;
int  g_unsaved_doc_idx = -1;
bool g_unsaved_prompt_open = false;
bool g_unsaved_action_bypass = false;
bool g_unsaved_continue_without_save = false;
bool g_new_project_apply_allowed_after_discard = false;
bool g_close_approved = false;
char g_unsaved_action_path[512] = "";
bool g_show_images    = true;
bool g_show_bg_picker = false;
/* ---- validation --------------------------------------------------- */

bool g_show_verify = false;

/* ---- MK2 assembly export ----------------------------------------- */

/* ---- batch import ------------------------------------------------- */

void import_png(const char *path, bool save_undo);
int import_img_file(const char *path, bool save_undo); /* forward decl */
int import_img_file_filtered(const char *path, bool save_undo,
                             const unsigned char *selected, int selected_len); /* forward decl */
int import_lod_file(const char *path, bool save_undo);
void mk2_palette_sync_request_prompt(const char *reason, bool allow_if_unknown_path);
void draw_mk2_palette_sync_prompt(void); /* forward decl */
bool mk2_palette_sync_auto_apply_if_ready(const char *reason); /* forward decl */
int g_last_import_img = -1;
bool g_png_import_force_8bpp = false;
bool g_mk2_palette_prompt_after_save = true;
bool g_mk2_palette_prompt_after_img_import = true;
bool g_mk2_palette_auto_sync_on_save = false;
bool g_mk2_palette_sync_dirty = false;
bool g_mk2_palette_sync_popup = false;
bool g_mk2_lod_stale_warn_after_save = true;
char g_mk2_palette_sync_reason[128] = "";
char g_mk2_palette_sync_asm[512] = "";
char g_mk2_palette_sync_table[64] = "";
char g_mk2_palette_sync_status[512] = "";
char g_mk2_palette_sync_output[2048] = "";
int  g_mk2_palette_sync_last_rc = 0;

/* ---- new project dialog ------------------------------------------- */

/* ---- zoom-to-fit --------------------------------------------------- */

/* ---- menu bar ------------------------------------------------------ */

/* ---- object list + properties ------------------------------------- */

void draw_obj_properties(void);

/* ---- welcome screen ------------------------------------------------ */

/* ---- MK2 stage kit ------------------------------------------------- */

/* ---- public API ---------------------------------------------------- */
