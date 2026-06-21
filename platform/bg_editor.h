#ifndef BG_EDITOR_H
#define BG_EDITOR_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void bg_editor_init(SDL_Window *window, SDL_Renderer *renderer);
void bg_editor_process_event(SDL_Event *event);
void bg_editor_new_frame(void);
void bg_editor_render(void);
void bg_editor_shutdown(void);
int  bg_editor_wants_input(void);
int  bg_editor_wants_wheel(void);
int bg_editor_wants_keyboard(void);
int bg_editor_request_close(void);
int bg_editor_take_close_approved(void);
int bg_editor_canvas_top_px(void);

/* Import a PNG file into the current project (dispatches to ImGui handler) */
void bg_editor_import_png(const char *path);
int  bg_editor_import_png_headless(const char *path);

/* Import a Midway IMG library into the current BDD image/palette bank */
int bg_editor_import_img(const char *path);

/* Import all Midway IMG libraries in one folder */
int bg_editor_import_img_folder(const char *dir);

/* Import every Midway IMG library referenced by a LOAD2 LOD file */
int bg_editor_import_lod(const char *path);

/* Auto-import known runtime LOD/IMG sprite sources after a BDB/BDD load */
int bg_editor_autoload_lod_assets(void);

/* Place the most recently imported image at world position (x, y) */
void bg_editor_place_last_import(int world_x, int world_y);

/* Calculate world bounds from object placements */
void bdd_get_world_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max);

/* Calculate bounds after first-fit modules are moved to runtime-local origins */
void bdd_get_runtime_layout_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max);

/* Calculate the edit-canvas bounds, including detached shelves for source-only foreground groups */
void bdd_get_editor_layout_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max);

/* Calculate padded edit-canvas camera bounds for the legacy SDL viewer */
void bdd_get_editor_view_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max);

/* Clamp the legacy SDL viewer camera inside the padded edit-canvas bounds */
void bdd_clamp_editor_view(int win_w, int win_h, int zoom, int *view_x, int *view_y);

/* Calculate the clamped 400x254 game-preview camera range */
void bdd_get_game_preview_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max);

/* Center the game-preview camera within the current stage-art bounds */
void bdd_center_game_preview_camera(void);

/* Read and apply the MK2 stage's match-load camera when available; falls back to centering. */
int bdd_get_stage_start_camera(int *camera_x, int *camera_y);
/* Read the stage's BGND.ASM scroll left/right limits (<stage>_mod words 5,6). */
int bdd_get_stage_scroll_limits(int *scroll_left, int *scroll_right);
/* Read the stage's in-game background colour (<stage>_mod word 1) as RGB555. */
int bdd_get_stage_bg_color(int *rgb555);
void bdd_reset_game_preview_camera(void);

/* Map one object from BDB source coordinates into its first-fit module-local origin */
int bdd_object_runtime_origin(int obj_index, int *rx, int *ry);

/* Map one object into the current edit-canvas origin, including detached shelves */
int bdd_object_editor_origin(int obj_index, int *ex, int *ey);

/* Game-preview origin/scroll helpers used by legacy SDL world rendering */
float bdd_object_game_scroll_factor(int obj_index);
void bdd_object_game_origin(int obj_index, int *gx, int *gy);
int bdd_object_runtime_draw_rank(int obj_index);
int bdd_object_uses_runtime_floor_y(int obj_index);
int bdd_runtime_floor_screen_y(int floor_y);
int bdd_runtime_floor_shear_per_line(void);
int bdd_object_game_screen_y(int obj_index, int game_y);
int bdd_stage_floor_descriptor(char *label, int label_sz,
                               char *palette, int palette_sz,
                               int *floor_y, int *floor_height);
int bdd_mkbgani_sprite_info(const char *label, int *w, int *h,
                            int *xoff, int *yoff, char *palette, int palette_sz);

/* One block from a module's <module>BLKS table in BGNDTBL.ASM. x/y are the
   block's module-local placement; hdr indexes st2HDRS == our loaded g_img/
   g_textures order (verified 1:1). flags carry the block's flip/render bits. */
typedef struct {
    int x, y;
    int hdr;     /* st2HDRS index == g_img index */
    int flags;
    int pal;     /* block palette: flags bits 3-0 + hdr-word bits 15-12 as 7-4 */
} BddBgndBlock;

/* Parse the <module>BLKS block table for a background module (BMOD suffix
   optional) straight from vanilla BGNDTBL.ASM, exactly as the game renders it.
   Returns the block count (capped at max), 0 if not found. */
int bdd_stage_module_blocks(const char *module, BddBgndBlock *out, int max);

/* Enumerate the loaded stage's background planes (BGND.ASM <stage>_mod order).
   bdd_stage_plane_info fills the plane's module name, placement offset, parallax
   scroll and dlists draw rank. Any out pointer may be NULL. */
int bdd_stage_plane_count(void);
int bdd_stage_plane_info(int index, char *name, int name_sz,
                         int *ox, int *oy, float *scroll, int *draw_rank);
int bdd_stage_plane_scroll_origin(int index, int *scroll_x);

/* 1 when an object belongs to a known background plane (so the block-table
   renderer draws it from *BLKS and the BDB object copy is suppressed). */
int bdd_object_in_background_plane(int obj_index);

/* Draw rank of the floor's -1/floor_code dlists slot (INT_MAX if the stage has
   no floor), so block planes can be split far/near around it. */
int bdd_stage_floor_rank(void);

/* Master toggle for the block-table background renderer (default on). */
extern int g_block_background_render;

/* A background animation actor derived from the loaded stage's BGND.ASM:
   the calla spawns `proc` (create pid_bani,proc), which cycles the `sequence`
   (a_*) frame table of MKBGANI sprite labels. */
#define BDD_STAGE_ACTOR_MAX 24
#define BDD_STAGE_ACTOR_FRAME_MAX 48
#define BDD_STAGE_ACTOR_POS_MAX 8
typedef struct {
    char proc[48];
    char sequence[48];
    int frame_count;
    char frames[BDD_STAGE_ACTOR_FRAME_MAX][32];
    int pos_count;                 /* static y:x spawn coords found in the proc */
    int pos_x[BDD_STAGE_ACTOR_POS_MAX];
    int pos_y[BDD_STAGE_ACTOR_POS_MAX];
    int motion_x;                  /* oxvel px/frame (movi >v,a0 -> oxvel), 0 if static */
    int motion_y;                  /* oyvel px/frame, 0 if static */
    int insert_baklst;             /* baklst plane the proc inserts into, -1 if unknown */
    float scroll;                  /* parallax factor of that baklst (0 = screen-fixed) */
    int screen_anchored;           /* coord is worldtlx-relative (mover) -> screen-fixed */
} BddStageActor;
int bdd_stage_runtime_actors(BddStageActor *out, int max_actors);

typedef struct BddScreenRect {
    int x, y, w, h;
    int clip_x, clip_y, clip_w, clip_h;
} BddScreenRect;

/* Calculate the on-screen 400x254 game-preview viewport rectangle. */
void bdd_game_view_screen_rect(int zoom, int window_w, int window_h,
                               BddScreenRect *out_rect);

/* Project one editor-world point into screen space. */
void bdd_world_to_screen(int world_x, int world_y,
                         int view_x, int view_y, int zoom,
                         int *screen_x, int *screen_y);

/* Project one screen point into editor-world space. */
void bdd_screen_to_world(int screen_x, int screen_y,
                         int view_x, int view_y, int zoom,
                         int *world_x, int *world_y);

/* Project an exclusive editor-world rectangle into screen space.
   Returns nonzero when the rectangle intersects the window clip. */
int bdd_world_rect_screen_rect(int world_x1, int world_y1,
                               int world_x2, int world_y2,
                               int view_x, int view_y, int zoom,
                               int window_w, int window_h,
                               BddScreenRect *out_rect);

/* Project an object image rectangle into the current editor/game-preview screen space.
   Returns nonzero when the rectangle intersects the active render clip. */
int bdd_object_screen_rect(int obj_index, int image_w, int image_h,
                           int view_x, int view_y, int zoom,
                           int window_w, int window_h,
                           int game_scroll, BddScreenRect *out_rect);

/* Project an object's snap/visible-pixel bounds into screen space. */
int bdd_object_screen_snap_rect(int obj_index,
                                int view_x, int view_y, int zoom,
                                int window_w, int window_h,
                                int game_scroll, BddScreenRect *out_rect);

/* Save an undo snapshot (callable from C) */
void bg_editor_undo_save(void);

/* Set a human-readable label consumed by the next bg_editor_undo_save() call */
void bg_editor_set_action_label(const char *label);

/* Returns the user's snap-distance preference (world pixels) */
int bg_editor_snap_dist(void);

/* Returns nonzero when smart edge snapping should use visible pixels instead of full BDD rectangles */
int bg_editor_visible_pixel_snap_enabled(void);

/* Compute the snap rectangle for an object at a supplied source-space origin.
   The rectangle is x2/y2 exclusive and honors the visible-pixel snap setting. */
int bg_editor_object_snap_rect_at(int obj_index, int origin_x, int origin_y,
                                  int *x1, int *y1, int *x2, int *y2);

/* Hit-test an object at a supplied origin. When visible-pixel snap is enabled,
   transparent pixels do not count as hits. */
int bg_editor_object_hit_test_at(int obj_index, int origin_x, int origin_y,
                                 int x, int y);

#ifdef __cplusplus
}
#endif

#endif
