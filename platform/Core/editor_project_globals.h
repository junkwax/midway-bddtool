#ifndef EDITOR_PROJECT_GLOBALS_H
#define EDITOR_PROJECT_GLOBALS_H

#include "bdd_format.h"
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern Img  *g_img;
extern int   g_ni;
extern Obj  *g_obj;
extern int   g_no;
extern char  g_name[64];
extern int   g_have_bdb;

extern char  g_bdb_path[512];
extern char  g_bdb_header[256];
extern char (*g_bdb_modules)[256];
extern int   g_bdb_num_modules;

extern Uint32 (*g_pals)[256];
extern int    *g_pal_count;
extern int     g_n_pals;
extern char   (*g_pal_name)[64];

extern char g_bdd_path[512];

extern int   g_dirty;
extern int   g_ctx_obj;
extern Uint8 g_bg_color[3];

extern int    g_show_grid;
extern int    g_show_borders;
extern int    g_show_objects;
extern int    g_hl_obj;
extern int    g_zoom;
extern int    g_view_x;
extern int    g_view_y;
extern int    g_grid_snap;
extern int    g_show_labels;
extern Uint8  g_grid_color[3];
extern int    g_grid_sx;
extern int    g_grid_sy;
extern int   *g_obj_lock;
extern int   *g_obj_hidden;
extern int   *g_sel_flags;

extern char g_open_path[512];
extern int  g_view_changed;

extern int  g_cur_tool;
extern int  g_place_tool_img;
extern int  g_hover_obj;
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

#ifdef __cplusplus
}
#endif

#endif
