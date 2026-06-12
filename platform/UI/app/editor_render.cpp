#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_commands.h"
#include "UI/app/PanelManager.h"

#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static int g_last_known_no = 0; /* tracks g_no across frames for lazy module enforcement */

int find_img_by_label_casefold(const char *name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].label[0] && strcasecmp(g_img[i].label, name) == 0) return i;
        if (g_img[i].source[0] && strcasecmp(g_img[i].source, name) == 0) return i;
    }
    return -1;
}

void bg_editor_render(void)
{
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    g_display_w_resized = (g_prev_display_w > 0.0f && g_prev_display_h > 0.0f &&
                           display_size.x > 0.0f && display_size.y > 0.0f &&
                           (display_size.x != g_prev_display_w ||
                            display_size.y != g_prev_display_h));
    g_prev_display_w = display_size.x;
    g_prev_display_h = display_size.y;

    /* Lazy module enforcement: objects added by bddview.c (Place / Brush / drop)
       between frames are caught here and silently fitted to a module. */
    if (g_no > g_last_known_no) {
        for (int ni = g_last_known_no; ni < g_no; ni++)
            simple_ensure_module(ni);
    }
    g_last_known_no = g_no;

    /* FPS counter */
    g_fps_count++;
    g_fps_timer += ImGui::GetIO().DeltaTime;
    if (g_fps_timer >= 1.0f) {
        g_fps = g_fps_count / g_fps_timer;
        g_fps_count = 0;
        g_fps_timer = 0.0f;
    }

    /* Init doc system on first render (data already loaded from cmd line) */
    if (!g_docs_init) {
        g_docs_init = true;
        if (g_ni > 0 || g_have_bdb) {
            g_cur_doc = g_num_docs;
            memset(&g_docs[g_cur_doc], 0, sizeof(Document));
            g_docs[g_cur_doc].tab_id = g_next_doc_id++;
            doc_save(g_cur_doc);
            g_num_docs++;
        } else {
            g_cur_doc = -1;
        }
    }

    /* On new stage open: initialize preview camera bookkeeping only.  World
       view positioning is user-controlled; Fit All remains an explicit action. */
    {
        static char s_last_bdb[512] = "";
        if (g_have_bdb && g_bdb_path[0] && strcmp(g_bdb_path, s_last_bdb) != 0) {
            snprintf(s_last_bdb, sizeof s_last_bdb, "%s", g_bdb_path);
            if (g_no > 0)
                bdd_reset_game_preview_camera();
        }
    }

    if (g_preview_mode) {
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_rend);
        return;
    }

    bool show_welcome = welcome_visible();
    g_panel_manager.render_panels(EditorPanelRegion::MainMenu);
    draw_mk2_workflow();
    g_panel_manager.render_panels(EditorPanelRegion::PrimaryToolbar);
    g_panel_manager.render_panels(EditorPanelRegion::DocumentTabs);
    editor_process_commands();
    draw_mk2_palette_sync_prompt();
    draw_mk2_lod_stale_warning();
    draw_save_error_popup();
    draw_unsaved_action_prompt();
    draw_img_import_picker();
    draw_mk2_simple_level_dialog();
    if (!show_welcome) {
        right_panel_frame_begin();
        g_panel_manager.render_panels(EditorPanelRegion::RightRail);
        right_panel_frame_end();
    }
    g_dock_right_panels_next = false;
    draw_welcome();
    draw_new_project();
    draw_help();
    draw_verify();
    draw_ref_settings();
    draw_tile_fill();
    draw_bg_picker();
    draw_img_idx_edit();
    draw_about();
    draw_world_context_overlay();
    draw_module_bounds_overlay();

    draw_world_object_overlays();
    draw_alignment_doctor_overlay();

    run_auto_save_tick();

    draw_grid_settings();

    draw_stage_toast_overlay();

    /* game view overlay (selection + drag) */
    poll_stage_overview_result();
    draw_world_game_rect();
    draw_mk2_runtime_extras_overlay();
    draw_mk2_runtime_actor_overlay();
    draw_game_view_overlay();
    draw_stage_overview();
    draw_mk2_runtime_actor_isolation_window();

    draw_game_view_controls();
    g_panel_manager.render_panels(EditorPanelRegion::Workspace);
    draw_quit_dialog();
    draw_preferences();
    draw_canvas_scrollbars();
    draw_status();
    capacity_warn_check();
    draw_toasts();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_rend);
}
