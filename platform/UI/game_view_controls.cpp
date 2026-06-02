#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/object_position_undo.h"
#include <imgui.h>
#include <cstdio>

void draw_game_view_controls(void)
{
        /* game view auto-scroll (parallax play) */
        static bool  s_gv_play  = false;
        static float s_gv_speed      = 60.0f;  /* px/sec */
        static float s_gv_accum      = 0.0f;   /* sub-pixel accumulator */
        static int   s_gv_dir        = 1;      /* +1 right, -1 left */
    
        /* game view controls */
        if (g_game_view && g_have_bdb && !g_preview_mode) {
            int wx_min=0, wx_max=1024, wy_min=0, wy_max=0;
            bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
            int scroll_max = wx_max - 400;
            int scroll_y_max = wy_max - 254;
    
            /* auto-zoom: fire when game view first opens AND whenever window is resized */
            {
                static float s_last_dsx = 0.0f, s_last_dsy = 0.0f;
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                bool size_changed = (ds.x != s_last_dsx || ds.y != s_last_dsy);
                if (g_gv_needs_autozoom || size_changed) {
                    g_gv_needs_autozoom = false;
                    s_last_dsx = ds.x; s_last_dsy = ds.y;
                    fit_game_preview_zoom_to_window();
                    focus_editor_on_game_preview_screen();
                }
            }
    
            /* advance auto-scroll this frame */
            if (s_gv_play && scroll_max > wx_min) {
                s_gv_accum += s_gv_speed * ImGui::GetIO().DeltaTime * (float)s_gv_dir;
                int step = (int)s_gv_accum;
                if (step != 0) {
                    s_gv_accum -= (float)step;
                    g_scroll_pos += step;
                    if (g_scroll_pos >= scroll_max) { g_scroll_pos = scroll_max; s_gv_dir = -1; }
                    if (g_scroll_pos <= wx_min)      { g_scroll_pos = wx_min;     s_gv_dir =  1; }
                }
            }
            if (g_scroll_pos < wx_min)      g_scroll_pos = wx_min;
            if (g_scroll_pos > scroll_max)  g_scroll_pos = scroll_max;
            if (g_game_view_y < wy_min)     g_game_view_y = wy_min;
            if (g_game_view_y > scroll_y_max) g_game_view_y = scroll_y_max;
    
            /* Controls panel sits bottom-center, just above the Play/Bounce
               transport bar (which is centered under the game viewport). */
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            BddScreenRect vp;
            bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &vp);
            float anchor_y = (float)(vp.y + vp.h);            /* transport sits +6 below this */
            if (anchor_y > ds.y - 4.0f) anchor_y = ds.y - 4.0f;
            ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, anchor_y),
                                    ImGuiCond_Always, ImVec2(0.5f, 1.0f));
            ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);  /* height auto */
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f,0.08f,0.12f,0.92f));
            if (ImGui::Begin("##gameview", NULL, ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f), "Game Preview");
                ImGui::SameLine(); ImGui::TextDisabled("%s  |  Parallax on  |  400x254  |  Camera X: %d",
                    g_runtime_layout_view ? "Runtime Layout" : "BDB Source", g_scroll_pos);
    
                /* play/pause + manual step buttons */
                ImGui::SameLine(0, 16);
                if (s_gv_play) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.3f,0.2f,0.9f));
                    if (ImGui::SmallButton(" || ")) { s_gv_play = false; }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause auto-scroll");
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f,0.6f,0.3f,0.9f));
                    if (ImGui::SmallButton(" |> ")) { s_gv_play = true; s_gv_dir = 1; s_gv_accum = 0; }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-scroll (ping-pong)");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("|<")) { g_scroll_pos = wx_min;    s_gv_play = false; } ImGui::SameLine();
                if (ImGui::SmallButton("<<")) { g_scroll_pos -= 50;       s_gv_play = false; } ImGui::SameLine();
                if (ImGui::SmallButton("<"))  { g_scroll_pos -= 10;       s_gv_play = false; } ImGui::SameLine();
                if (ImGui::SmallButton(">"))  { g_scroll_pos += 10;       s_gv_play = false; } ImGui::SameLine();
                if (ImGui::SmallButton(">>")) { g_scroll_pos += 50;       s_gv_play = false; } ImGui::SameLine();
                if (ImGui::SmallButton(">|")) { g_scroll_pos = scroll_max; s_gv_play = false; }
    
                /* speed slider — only useful while playing */
                ImGui::SetNextItemWidth(120);
                ImGui::SliderFloat("##spd", &s_gv_speed, 10.0f, 400.0f, "%.0f px/s");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-scroll speed (px/sec)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##scroll", &g_scroll_pos, wx_min, scroll_max);
    
                /* layer parallax legend */
                /* vertical camera scroll — pan up/down to see floor or sky */
                ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f), "Y:");
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("^^")) { g_game_view_y = wy_min;        } ImGui::SameLine();
                if (ImGui::SmallButton("^"))  { g_game_view_y -= 20;           } ImGui::SameLine();
                if (ImGui::SmallButton("v"))  { g_game_view_y += 20;           } ImGui::SameLine();
                if (ImGui::SmallButton("vv")) { g_game_view_y = scroll_y_max;  } ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##scrolly", &g_game_view_y, wy_min, scroll_y_max);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertical camera offset - pan to show floor or sky");
    
                ImGui::TextDisabled("Layers:");
                static const struct { int wx; const char *n; float sf; } lp[] = {
                    {0x32,"Sky",0.2f},{0x3C,"Mid",0.5f},{0x40,"Floor",1.0f},
                    {0x41,"Floor+",1.0f},{0x43,"Near FG",1.2f},{0x46,"Front FG",1.5f}
                };
                for (int li = 0; li < 6; li++) {
                    bool has = false;
                    for (int i = 0; i < g_no && !has; i++)
                        if (((g_obj[i].wx >> 8) & 0xFF) == lp[li].wx) has = true;
                    if (!has) continue;
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s %.1fx", lp[li].n, lp[li].sf);
                }
    
                /* layer assignment row — shown when objects are selected */
                {
                    int sel_count = 0;
                    int cur_layer = -1;
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        sel_count++;
                        if (cur_layer == -1) cur_layer = (g_obj[i].wx >> 8) & 0xFF;
                    }
                    if (sel_count > 0) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f,0.85f,0.4f,1.0f),
                                           "Layer  (%d selected):", sel_count);
                        ImGui::SameLine();
                        for (int li = 0; li < 6; li++) {
                            bool is_cur = (cur_layer == lp[li].wx);
                            if (is_cur) {
                                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f,0.55f,0.90f,1.00f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f,0.65f,1.00f,1.00f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f,0.45f,0.80f,1.00f));
                            }
                            char lbl[32]; snprintf(lbl, sizeof lbl, "%s##la%d", lp[li].n, li);
                            if (ImGui::SmallButton(lbl)) {
                                ObjectRecordUndoCapture undo;
                                object_record_undo_capture_selected(&undo);
                                for (int i = 0; i < g_no; i++) {
                                    if (!g_sel_flags[i] || g_obj_lock[i]) continue;
                                    g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (lp[li].wx << 8);
                                }
                                if (object_record_undo_commit(&undo, "Assign Layer") > 0)
                                    g_dirty = 1;
                                g_view_changed = 1;
                            }
                            if (is_cur) ImGui::PopStyleColor(3);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Assign to %s (parallax %.1fx)", lp[li].n, lp[li].sf);
                            if (li < 5) ImGui::SameLine(0, 3);
                        }
                    }
                }
    
                /* out-of-viewport warning — objects outside current camera window */
                {
                    int below = 0;
                    for (int i = 0; i < g_no; i++) {
                        int oy = g_obj[i].sy;
                        Img *im = img_find(g_obj[i].ii);
                        if (!im) continue;
                        if (g_runtime_layout_view) {
                            int ox = g_obj[i].depth;
                            gv_object_origin(i, &ox, &oy);
                        }
                        if (oy >= g_game_view_y + 254 || oy + im->h <= g_game_view_y) below++;
                    }
                    if (below > 0) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                            "  %d object%s off-screen  (Y: %d-%d)",
                            below, below == 1 ? "" : "s", g_game_view_y, g_game_view_y + 254);
                        ImGui::SameLine(0, 8);
                        if (ImGui::SmallButton("World View  (see all)")) {
                            g_game_view = 0;
                            zoom_to_fit();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Switch to world view and fit all objects -\n"
                                "a gold border shows where the game screen is.");
                    }
                }
            }
            ImGui::PopStyleColor();
            ImGui::End();
        } else {
            s_gv_play = false;
            g_gv_needs_autozoom = true;  /* re-fit next time game view opens */
        }
    
    
}
