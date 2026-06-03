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

#include "bdd_format.h"
#include "bg_editor.h"
#include "Core/app_diagnostics.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/viewer_cli_commands.h"
#include "Core/viewer_stage_io.h"
#include "UI/sdl_alignment_guides.h"
#include "UI/app_icon.h"
#include "UI/sdl_path_input.h"
#include "UI/sdl_save_popup.h"
#include "UI/sdl_stage_open.h"
#include "UI/sdl_bitmap_font.h"
#include "UI/sdl_drop_file.h"
#include "UI/sdl_image_grid_view.h"
#include "UI/sdl_keyboard_actions.h"
#include "UI/sdl_mouse_interaction.h"
#include "UI/sdl_mouse_wheel.h"
#include "UI/sdl_object_picker.h"
#include "UI/sdl_reference_overlay.h"
#include "UI/sdl_tooltip.h"
#include "UI/sdl_view_hud.h"
#include "UI/sdl_world_grid_overlay.h"
#include "UI/sdl_world_markers.h"
#include "UI/sdl_world_objects.h"
#include "UI/texture_cache.h"

#ifndef BDDVIEW_VERSION
#define BDDVIEW_VERSION "1.0"
#endif

static int           g_last_obj = -1;   /* g_obj[] index of last dragged/placed object */

/* World view (BDB + BDD)                                              */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

#define WIN_W 1280
#define WIN_H  720

int main(int argc, char *argv[])
{
    bdd_diag_init(argc, argv);
    if (!editor_project_storage_init()) return 1;

    char bdb_path[512] = "", bdd_path[512] = "";
    int  wx_min = 0, wx_max = WIN_W, wy_min = 0, wy_max = WIN_H;
    int  cli_exit_code = 0;

    if (bdd_viewer_run_cli_command(argc, argv, &cli_exit_code))
        return cli_exit_code;

    if (argc >= 2) {
        if (!bdd_viewer_load_stage_for_path(argv[1], bdb_path, sizeof bdb_path, bdd_path, sizeof bdd_path))
            return 1;

        /* World bounds — X axis = depth, Y axis = sy */
        if (g_runtime_layout_view)
            bdd_get_runtime_layout_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
        else
            bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
        if (wx_min == INT_MAX) { wx_min = 0; wx_max = WIN_W; wy_min = 0; wy_max = WIN_H; }
    }

    bdd_prepare_app_identity();

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    char title[256];
    snprintf(title, sizeof title, "BDD Viewer v%s  -  %s", BDDVIEW_VERSION,
             g_name[0] ? g_name : (argc >= 2 ? argv[1] : "(no file)"));

    SDL_Window *win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    bdd_set_app_icon(win);

    SDL_Renderer *rend = SDL_CreateRenderer(win, -1,
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
    Uint64 palette_texture_sig = bdd_texture_cache_signature();

    /* View state */
    int view_x     = wx_min;   /* world X at left edge */
    int view_y     = wy_min;   /* world Y at top edge  */
    int zoom       = 1;
    /* Toggle flags — shared with bg_editor menus */

    BddSdlMouseState mouse_state;
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

    BddSdlKeyboardState keyboard_state;
    bdd_sdl_keyboard_init(&keyboard_state);

    /* Title caching — only update when string changes */
    char cur_title[256] = "";

    /* Hover state for debug info */
#define HOVER_DELAY_MS 1200
    int    hover_x = -1, hover_y = -1;   /* last mouse pos */
    Uint32 hover_since = 0;               /* ticks when mouse settled */
    int    hover_printed = 0;             /* already printed for this hover */

    int running = 1;
    SDL_Event ev;

    while (running) {
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

        /* Software key repeat */
        bdd_sdl_keyboard_tick(&keyboard_state, &view_x, &view_y, zoom);

        bdd_sdl_mouse_tick(&mouse_state,
                           &view_x, &view_y, zoom, ww, wh,
                           &hover_x, &hover_y,
                           &hover_since, &hover_printed);

        /* Hover tooltip */
        if (g_show_labels && g_have_bdb && hover_x >= 0 && !hover_printed &&
            SDL_GetTicks() - hover_since >= HOVER_DELAY_MS)
        {
            hover_printed = 1;
            bdd_tooltip_build_hover(rend, hover_x, hover_y, view_x, view_y, zoom, ww, wh);
        }

        /* Update window title only when state changes */
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

        /* File open request from bg_editor (File > Open) */
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

        /* Apply view changes from bg_editor (zoom-to-fit, etc.) */
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
        int canvas_top = bg_editor_canvas_top_px();
        if (canvas_top < 0) canvas_top = 0;
        if (canvas_top > wh - 1) canvas_top = wh - 1;

        SDL_SetRenderDrawColor(rend, g_bg_color[0], g_bg_color[1], g_bg_color[2], 255);
        SDL_RenderClear(rend);

        bdd_view_hud_draw(rend, ww, wh, zoom);

        /* reference background image */
        bdd_reference_overlay_draw(rend, view_x, view_y, zoom);

        if (!g_have_bdb) {
            /* ---- image grid view ---- */
            bdd_image_grid_view_draw(rend, ww, view_x, view_y, zoom);
        } else {
            /* ---- world layout view ---- */
            bdd_world_grid_overlay_draw(rend, view_x, view_y, zoom, ww, wh, canvas_top);
            bdd_world_markers_draw(rend, view_x, view_y, zoom, ww, wh);

            bdd_world_objects_draw(rend, view_x, view_y, zoom, ww, wh,
                                   mouse_state.sel_rect_active,
                                   mouse_state.sel_rx1, mouse_state.sel_ry1,
                                   mouse_state.sel_rx2, mouse_state.sel_ry2);
        }

        /* Path-input overlay */
        if (bdd_path_input_is_open())
            bdd_path_input_draw(rend, ww, wh);

        /* Smart alignment guides */
        if (mouse_state.obj_drag_idx >= 0)
            bdd_alignment_guides_draw(rend,
                                      mouse_state.guide_vx, mouse_state.guide_vn,
                                      mouse_state.guide_vy, mouse_state.guide_hn,
                                      view_x, view_y, zoom, ww, wh);

        /* Mouse crosshair */
        bdd_mouse_crosshair_draw(rend, hover_x, hover_y);

        /* Ghost image — follows cursor when a placement is pending */
        bdd_object_picker_draw_placement_ghost(rend, hover_x, hover_y, zoom);

        /* Object picker panel */
        if (bdd_object_picker_is_open())
            bdd_object_picker_draw(rend, ww, wh, hover_x, hover_y);

        /* Tooltip overlay */
        bdd_tooltip_draw(rend);

        bdd_save_popup_draw(rend, ww, wh, g_bdb_path);

        /* Sync view state for bg_editor status display */
        g_zoom    = zoom;
        g_view_x  = view_x;
        g_view_y  = view_y;

        bg_editor_render();
        if (bg_editor_take_close_approved())
            running = 0;

        SDL_RenderPresent(rend);
    }

    /* Cleanup */
    bg_editor_shutdown();
    bdd_object_picker_free_labels();
    bdd_save_popup_cancel();
    bdd_tooltip_free();
    bdd_texture_cache_destroy();
    img_free();
    bdd_sdl_mouse_state_shutdown(&mouse_state);
    editor_project_storage_shutdown();
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
