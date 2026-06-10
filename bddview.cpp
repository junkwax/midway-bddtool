/*************************************************************
 * bddview.c — Midway BDD/BDB background viewer
 * Portable C99 + SDL2, no other dependencies.
 *
 * BDB: ASCII text, object placement list
 * BDD: binary image container (indexed palette)
 *
 * Usage:  bddview  <file.BDB>    auto-loads matching .BDD
 *         bddview  <file.BDD>    image-grid view, no world layout
 *
 * Keys:
 *   Arrow keys / left-drag   Scroll
 *   Scroll wheel / +/-       Zoom in/out
 *   Home                     Reset view
 *   Esc                      Quit
 *************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <SDL.h>

#include "Core/bdd_format.h"
#include "bg_editor.h"
#include "Core/app_diagnostics.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/viewer_cli_commands.h"
#include "Core/viewer_stage_io.h"
#include "UI/overlays/sdl_alignment_guides.h"
#include "UI/assets/app_icon.h"
#include "UI/sdl/sdl_path_input.h"
#include "UI/sdl/sdl_save_popup.h"
#include "UI/sdl/sdl_stage_open.h"
#include "UI/sdl/sdl_bitmap_font.h"
#include "UI/sdl/sdl_drop_file.h"
#include "UI/sdl/sdl_image_grid_view.h"
#include "UI/sdl/sdl_keyboard_actions.h"
#include "UI/sdl/sdl_mouse_interaction.h"
#include "UI/sdl/sdl_mouse_wheel.h"
#include "UI/sdl/sdl_object_picker.h"
#include "UI/overlays/sdl_reference_overlay.h"
#include "UI/sdl/sdl_tooltip.h"
#include "UI/overlays/sdl_view_hud.h"
#include "UI/overlays/sdl_world_grid_overlay.h"
#include "UI/overlays/sdl_world_markers.h"
#include "UI/sdl/sdl_world_objects.h"
#include "UI/assets/texture_cache.h"
#include "UI/tools/mk2_runtime_actor_tool.h"
#include "libs/stb_image_write.h"

#ifndef BDDVIEW_VERSION
#define BDDVIEW_VERSION "0.0.0-dev"
#endif

/* Headless capture of a stage to a PNG, for visual review without the GUI.
   Handles "--render-png <BDD> <out.png> [game|layout] [zoom]".
     layout (default): the whole runtime layout at 1:1.
     game: the in-game 400x254 view at the BGND camera start (parallax
           composited) -- the true arcade framing. Default zoom 2.
   Returns true if it consumed the flag (handled), with *exit_code set. */
static bool bdd_viewer_render_png(int argc, char *argv[], int *exit_code)
{
    const char *bdd_arg = nullptr;
    const char *out_arg = nullptr;
    bool game_mode = false;
    int zoom = 0;   /* 0 = mode default */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--render-png") == 0) {
            if (i + 2 < argc) { bdd_arg = argv[i + 1]; out_arg = argv[i + 2]; }
            for (int j = i + 3; j < argc; j++) {
                if (strcmp(argv[j], "game") == 0) game_mode = true;
                else if (strcmp(argv[j], "layout") == 0) game_mode = false;
                else { int z = atoi(argv[j]); if (z >= 1 && z <= 8) zoom = z; }
            }
            break;
        }
    }
    if (!bdd_arg || !out_arg)
        return false;
    *exit_code = 1;
    if (zoom <= 0) zoom = game_mode ? 2 : 1;

    char bdb_p[512] = "", bdd_p[512] = "";
    if (!bdd_viewer_load_stage_for_path(bdd_arg, bdb_p, sizeof bdb_p, bdd_p, sizeof bdd_p)) {
        fprintf(stderr, "render-png: failed to load %s\n", bdd_arg);
        return true;
    }
    runtime_actor_autoload_for_stage();
    g_show_objects = 1;

    int x0 = 0, x1 = 400, y0 = 0, y1 = 254;   /* layout bounds (layout mode) */
    int ww, wh, vx, vy;
    if (game_mode) {
        g_preview_mode = 1;        /* canvas_top = 0, no ImGui in viewport calc */
        g_runtime_layout_view = 1; /* game origin uses runtime (parallax) origins */
        g_game_view = 1;
        ww = 400 * zoom;
        wh = 254 * zoom;
        vx = 0;
        vy = 0;
    } else {
        g_runtime_layout_view = 1;
        g_game_view = 0;
        bdd_get_runtime_layout_bounds(&x0, &x1, &y0, &y1);
        if (x0 == INT_MAX || x1 == INT_MIN || y0 == INT_MAX || y1 == INT_MIN) {
            x0 = 0; x1 = 400; y0 = 0; y1 = 254;
        }
        const int pad = 8;
        x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
        ww = (x1 - x0) * zoom;
        wh = (y1 - y0) * zoom;
        vx = x0;
        vy = y0;
    }
    const int MAXDIM = 8192;
    if (ww < 16) ww = 16;
    if (wh < 16) wh = 16;
    if (ww > MAXDIM) ww = MAXDIM;
    if (wh > MAXDIM) wh = MAXDIM;

    if (game_mode)
        bdd_reset_game_preview_camera();   /* frame at the BGND stage start camera */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "render-png: SDL_Init: %s\n", SDL_GetError());
        return true;
    }
    SDL_Window *w = SDL_CreateWindow("bddview-render", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (w && !r) r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    if (!w || !r) {
        fprintf(stderr, "render-png: SDL setup failed: %s\n", SDL_GetError());
        if (r) SDL_DestroyRenderer(r);
        if (w) SDL_DestroyWindow(w);
        SDL_Quit();
        return true;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    bdd_texture_cache_set_renderer(r);
    bdd_texture_cache_rebuild_all();

    int ok = 0;
    SDL_Texture *target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_TARGET, ww, wh);
    if (target) {
        SDL_SetRenderTarget(r, target);
        SDL_SetRenderDrawColor(r, g_bg_color[0], g_bg_color[1], g_bg_color[2], 255);
        SDL_RenderClear(r);
        bdd_world_objects_draw(r, vx, vy, zoom, ww, wh, 0, 0, 0, 0, 0);

        unsigned char *buf = (unsigned char *)malloc((size_t)ww * wh * 4);
        if (buf && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ABGR8888,
                                        buf, ww * 4) == 0) {
            ok = stbi_write_png(out_arg, ww, wh, 4, buf, ww * 4);
        }
        free(buf);
        SDL_SetRenderTarget(r, nullptr);
        SDL_DestroyTexture(target);
    } else {
        fprintf(stderr, "render-png: target texture %dx%d: %s\n", ww, wh, SDL_GetError());
    }

    bdd_texture_cache_destroy();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();

    if (ok) {
        if (game_mode)
            fprintf(stderr, "render-png: wrote %s (%dx%d, game view zoom=%d cam=(%d,%d))\n",
                    out_arg, ww, wh, zoom, g_scroll_pos, g_game_view_y);
        else
            fprintf(stderr, "render-png: wrote %s (%dx%d, layout x[%d,%d] y[%d,%d])\n",
                    out_arg, ww, wh, x0, x1, y0, y1);
        *exit_code = 0;
    } else {
        fprintf(stderr, "render-png: failed to write %s\n", out_arg);
    }
    return true;
}

static int           g_last_obj = -1;   /* g_obj[] index of last dragged/placed object */

/* World view (BDB + BDD)                                              */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

#define WIN_W 1280
#define WIN_H  720

class BddViewApp {
public:
    int init(int argc, char *argv[]);
    bool should_run() const { return running != 0; }
    void run();
    void shutdown();

private:
    void process_events();
    void update();
    void render();

    SDL_Window *win = nullptr;
    SDL_Renderer *rend = nullptr;
    int running = 0;

    char bdb_path[512] = "";
    char bdd_path[512] = "";
    int wx_min = 0, wx_max = WIN_W, wy_min = 0, wy_max = WIN_H;

    int view_x = 0;
    int view_y = 0;
    int zoom = 1;
    
    BddSdlMouseState mouse_state;
    BddSdlKeyboardState keyboard_state;
    Uint64 palette_texture_sig = 0;

    char cur_title[256] = "";
    int hover_x = -1, hover_y = -1;
    Uint32 hover_since = 0;
    int hover_printed = 0;
};

int BddViewApp::init(int argc, char *argv[])
{
    bdd_diag_init(argc, argv);
    if (!editor_project_storage_init()) return 1;

    int cli_exit_code = 0;
    if (bdd_viewer_run_cli_command(argc, argv, &cli_exit_code))
        return cli_exit_code;

    if (bdd_viewer_render_png(argc, argv, &cli_exit_code))
        return cli_exit_code;

    if (argc >= 2) {
        if (!bdd_viewer_load_stage_for_path(argv[1], bdb_path, sizeof bdb_path, bdd_path, sizeof bdd_path))
            return 1;

        if (g_runtime_layout_view)
            bdd_get_runtime_layout_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
        else
            bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
        if (wx_min == INT_MAX) { wx_min = 0; wx_max = WIN_W; wy_min = 0; wy_max = WIN_H; }
    }

    bdd_prepare_app_identity();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    char title[256];
    snprintf(title, sizeof title, "BDD Viewer v%s  -  %s", BDDVIEW_VERSION,
             g_name[0] ? g_name : (argc >= 2 ? argv[1] : "(no file)"));

    win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    bdd_set_app_icon(win);

    rend = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rend)
        rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!rend) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);

    bdd_texture_cache_set_renderer(rend);
    bg_editor_init(win, rend);

    if (!bdd_texture_cache_rebuild_all()) {
        fprintf(stderr, "out of memory\n");
        bg_editor_shutdown();
        img_free();
        editor_project_storage_shutdown();
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    palette_texture_sig = bdd_texture_cache_signature();

    view_x = wx_min;
    view_y = wy_min;
    zoom = 1;

    if (!bdd_sdl_mouse_state_init(&mouse_state)) {
        fprintf(stderr, "out of memory\n");
        bg_editor_shutdown();
        bdd_texture_cache_destroy();
        img_free();
        editor_project_storage_shutdown();
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    bdd_sdl_keyboard_init(&keyboard_state);

    running = 1;
    return 0;
}

void BddViewApp::process_events()
{
    SDL_Event ev;
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);

    while (SDL_PollEvent(&ev)) {
        bg_editor_process_event(&ev);
        switch (ev.type) {
        case SDL_QUIT:
            if (bg_editor_request_close())
                running = 0;
            break;

        case SDL_DROPFILE:
            bdd_sdl_handle_drop_file(ev.drop.file, hover_x, hover_y, zoom,
                                     bdb_path, sizeof bdb_path,
                                     bdd_path, sizeof bdd_path,
                                     WIN_W, WIN_H,
                                     &wx_min, &wx_max, &wy_min, &wy_max,
                                     &view_x, &view_y,
                                     &palette_texture_sig);
            SDL_free(ev.drop.file);
            break;

        case SDL_KEYDOWN:
            if (bg_editor_wants_keyboard()) break;
            bdd_sdl_keyboard_key_down(&keyboard_state, &ev.key, rend,
                                      &view_x, &view_y, &zoom,
                                      wx_min, wy_min,
                                      &palette_texture_sig,
                                      &running);
            break;

        case SDL_KEYUP:
            bdd_sdl_keyboard_key_up(&keyboard_state, &ev.key);
            break;

        case SDL_TEXTINPUT:
            bdd_sdl_keyboard_text_input(&ev.text);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (bg_editor_wants_input()) break;
            bdd_sdl_mouse_button_down(&mouse_state, &ev.button, ww,
                                      &view_x, &view_y, &zoom,
                                      &g_last_obj);
            break;

        case SDL_MOUSEBUTTONUP:
            if (bg_editor_wants_input()) break;
            bdd_sdl_mouse_button_up(&mouse_state, &ev.button,
                                    view_x, view_y, zoom, ww, wh,
                                    &g_last_obj);
            break;

        case SDL_MOUSEMOTION:
            if (bg_editor_wants_input()) break;
            bdd_sdl_mouse_motion(&mouse_state, &ev.motion,
                                 &view_x, &view_y, zoom,
                                 &hover_x, &hover_y,
                                 &hover_since, &hover_printed);
            break;

        case SDL_MOUSEWHEEL:
            if (bg_editor_wants_wheel()) break;
            bdd_sdl_mouse_wheel_handle(&ev.wheel, rend, wh, ww, hover_x, hover_y,
                                       &view_x, &view_y, &zoom,
                                       &g_last_obj, &hover_printed);
            break;
        }
    }
}

void BddViewApp::update()
{
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);

    bdd_sdl_keyboard_tick(&keyboard_state, &view_x, &view_y, zoom);

    bdd_sdl_mouse_tick(&mouse_state,
                       &view_x, &view_y, zoom, ww, wh,
                       &hover_x, &hover_y,
                       &hover_since, &hover_printed);

    if (g_show_labels && g_have_bdb && hover_x >= 0 && !hover_printed &&
        SDL_GetTicks() - hover_since >= 1200)
    {
        hover_printed = 1;
        bdd_tooltip_build_hover(rend, hover_x, hover_y, view_x, view_y, zoom, ww, wh);
    }

    {
        char t[256];
        snprintf(t, sizeof t,
            "BDD Viewer v%s  -  %s   [%dx zoom]  pos (%d,%d)  objects=%d  images=%d",
            BDDVIEW_VERSION,
            g_name[0] ? g_name : "(no file)",
            zoom, view_x, view_y, g_no, g_ni);
        if (strcmp(t, cur_title) != 0) {
            SDL_SetWindowTitle(win, t);
            snprintf(cur_title, sizeof cur_title, "%s", t);
        }
    }

    if (g_open_path[0]) {
        bdd_sdl_open_stage_file(g_open_path,
                                bdb_path, sizeof bdb_path,
                                bdd_path, sizeof bdd_path,
                                WIN_W, WIN_H,
                                &wx_min, &wx_max, &wy_min, &wy_max,
                                &view_x, &view_y,
                                &palette_texture_sig);
        g_open_path[0] = '\0';
    }

    if (g_view_changed) {
        view_x = g_view_x;
        view_y = g_view_y;
        zoom   = g_zoom;
        g_view_changed = 0;
    }

    if (!g_game_view)
        bdd_clamp_editor_view(ww, wh, zoom, &view_x, &view_y);

    bdd_texture_cache_refresh_if_needed(&palette_texture_sig);

    bg_editor_new_frame();

    g_zoom    = zoom;
    g_view_x  = view_x;
    g_view_y  = view_y;
}

void BddViewApp::render()
{
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);

    int canvas_top = bg_editor_canvas_top_px();
    if (canvas_top < 0) canvas_top = 0;
    if (canvas_top > wh - 1) canvas_top = wh - 1;

    SDL_SetRenderDrawColor(rend, g_bg_color[0], g_bg_color[1], g_bg_color[2], 255);
    SDL_RenderClear(rend);

    bdd_view_hud_draw(rend, ww, wh, zoom);
    bdd_reference_overlay_draw(rend, view_x, view_y, zoom);

    if (!g_have_bdb) {
        bdd_image_grid_view_draw(rend, ww, view_x, view_y, zoom);
    } else {
        bdd_world_grid_overlay_draw(rend, view_x, view_y, zoom, ww, wh, canvas_top);
        bdd_world_markers_draw(rend, view_x, view_y, zoom, ww, wh);

        bdd_world_objects_draw(rend, view_x, view_y, zoom, ww, wh,
                               mouse_state.sel_rect_active,
                               mouse_state.sel_rx1, mouse_state.sel_ry1,
                               mouse_state.sel_rx2, mouse_state.sel_ry2);
    }

    if (bdd_path_input_is_open())
        bdd_path_input_draw(rend, ww, wh);

    if (mouse_state.obj_drag_idx >= 0)
        bdd_alignment_guides_draw(rend,
                                  mouse_state.guide_vx, mouse_state.guide_vn,
                                  mouse_state.guide_vy, mouse_state.guide_hn,
                                  view_x, view_y, zoom, ww, wh);

    bdd_mouse_crosshair_draw(rend, hover_x, hover_y);
    bdd_object_picker_draw_placement_ghost(rend, hover_x, hover_y, zoom);

    if (bdd_object_picker_is_open())
        bdd_object_picker_draw(rend, ww, wh, hover_x, hover_y);

    bdd_tooltip_draw(rend);
    bdd_save_popup_draw(rend, ww, wh, g_bdb_path);

    bg_editor_render();
    if (bg_editor_take_close_approved())
        running = 0;

    SDL_RenderPresent(rend);
}

void BddViewApp::run()
{
    while (running) {
        process_events();
        update();
        render();
    }
}

void BddViewApp::shutdown()
{
    bg_editor_shutdown();
    bdd_object_picker_free_labels();
    bdd_save_popup_cancel();
    bdd_tooltip_free();
    bdd_texture_cache_destroy();
    img_free();
    bdd_sdl_mouse_state_shutdown(&mouse_state);
    editor_project_storage_shutdown();
    if (rend) { SDL_DestroyRenderer(rend); rend = nullptr; }
    if (win) { SDL_DestroyWindow(win); win = nullptr; }
    SDL_Quit();
}

int main(int argc, char *argv[])
{
    BddViewApp app;
    int ret = app.init(argc, argv);
    if (ret != 0 || !app.should_run()) return ret;
    
    app.run();
    app.shutdown();
    return 0;
}
