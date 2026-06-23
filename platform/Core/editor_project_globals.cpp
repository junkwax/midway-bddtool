#include "Core/editor_project_globals.h"

#include <cstddef>

extern "C" {

Img  *g_img = NULL;
int   g_ni = 0;
Obj  *g_obj = NULL;
int   g_no = 0;
char  g_name[64] = "";
int   g_have_bdb = 0;

char  g_bdb_path[512] = "";
char  g_bdb_header[256] = "";
char (*g_bdb_modules)[256] = NULL;
int   g_bdb_num_modules = 0;

Uint32 (*g_pals)[256] = NULL;
int    *g_pal_count = NULL;
int     g_n_pals = 0;
char   (*g_pal_name)[64] = NULL;

char g_bdd_path[512] = "";

int   g_dirty = 0;
int   g_ctx_obj = -1;
int   g_ctx_module = -1;
int   g_ctx_module_wx = 0;
int   g_ctx_module_wy = 0;
int   g_runtime_binding_jump_module = -1;
Uint8 g_bg_color[3] = {18, 18, 28};

int   g_show_grid = 1;
int   g_show_borders = 1;
int   g_show_objects = 1;
int   g_hl_obj = -1;
int   g_zoom = 1;
int   g_view_x = 0;
int   g_view_y = 0;
int   g_grid_snap = 0;
int   g_show_labels = 0;
Uint8 g_grid_color[3] = {32, 32, 48};
int   g_grid_sx = 64;
int   g_grid_sy = 32;
int  *g_obj_lock = NULL;
int  *g_obj_hidden = NULL;
int  *g_sel_flags = NULL;

char g_open_path[512] = "";
int  g_view_changed = 0;

int  g_cur_tool = 0;
int  g_place_tool_img = -1;
int  g_hover_obj = -1;
int  g_game_view = 0;
int  g_scroll_pos = 0;
int  g_game_view_y = 0;
int  g_block_background_render = 1;
int  g_split_view = 0;
int  g_split_scroll_a = 0;
int  g_runtime_layout_view = 1;
bool  g_anim_playing = false;
float g_anim_speed = 120.0f;
int   g_anim_dir = 1;
float g_anim_fpos = 0.0f;
bool  g_anim_bounce = true;
bool  g_anim_v_sweep = false;
float g_anim_vy_fpos = 0.0f;
int   g_anim_vy_dir = 1;
float g_anim_vy_speed = 60.0f;
bool  g_recording = false;
int   g_record_n = 0;
char  g_record_dir[512] = "";
float g_record_accum = 0.0f;

}

bool g_snap_visible_pixels = false;
bool g_preview_mode = false;
