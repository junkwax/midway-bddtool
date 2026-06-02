#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>

void draw_mk2_camera_bookmarks(void)
{
    ImGui::Text("Camera Bookmarks");
    ImGui::TextDisabled("Store camera positions for left/center/right/worst checks and jump between them.");
    if (g_cam_bookmark_count < 0) g_cam_bookmark_count = 0;
    if (g_cam_bookmark_count > 8) g_cam_bookmark_count = 8;
    if (ImGui::BeginTable("camera_bookmarks", 7,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 190)))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("full", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("top", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("floor", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("use", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("edit");
        ImGui::TableHeadersRow();
        for (int i = 0; i < g_cam_bookmark_count; i++) {
            ImGui::PushID(i);
            float full = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h);
            float top = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h / 5);
            float floor = mk2_screen_band_coverage(g_cam_bookmark_x[i], (g_pan_scan_view_h * 3) / 4, g_pan_scan_view_h);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::InputText("##name", g_cam_bookmark_name[i], sizeof g_cam_bookmark_name[i]);
            ImGui::TableNextColumn(); ImGui::InputInt("##x", &g_cam_bookmark_x[i], 0, 0);
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", full);
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", top);
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", floor);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Prev")) {
                g_stage_preview_worldx = g_cam_bookmark_x[i];
                stage_set_toast("Preview X set from bookmark");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Start")) {
                g_stage_start_camera_x = g_cam_bookmark_x[i];
                g_stage_preview_worldx = g_cam_bookmark_x[i];
                g_stage_start_camera_enabled = true;
                stage_set_toast("Start camera set from bookmark");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Ctr")) {
                g_view_x = g_cam_bookmark_x[i];
                g_view_y = 0;
                g_view_changed = 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Del")) {
                for (int j = i; j < g_cam_bookmark_count - 1; j++) {
                    g_cam_bookmark_x[j] = g_cam_bookmark_x[j + 1];
                    snprintf(g_cam_bookmark_name[j], sizeof g_cam_bookmark_name[j], "%s", g_cam_bookmark_name[j + 1]);
                }
                g_cam_bookmark_count--;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::InputText("New Name", g_cam_new_name, sizeof g_cam_new_name);
    ImGui::InputInt("New X", &g_cam_new_x);
    if (ImGui::Button("Use Preview X")) g_cam_new_x = g_stage_preview_worldx;
    ImGui::SameLine();
    if (ImGui::Button("Use Worst X")) g_cam_new_x = g_pan_scan_worst_x;
    if (ImGui::Button("Add Bookmark", ImVec2(-1, 0)) && g_cam_bookmark_count < 8) {
        int i = g_cam_bookmark_count++;
        g_cam_bookmark_x[i] = g_cam_new_x;
        snprintf(g_cam_bookmark_name[i], sizeof g_cam_bookmark_name[i], "%s", g_cam_new_name[0] ? g_cam_new_name : "Camera");
    }
    if (ImGui::Button("Set Pan Scan Range From Bookmarks", ImVec2(-1, 0)) && g_cam_bookmark_count > 0) {
        int mn = g_cam_bookmark_x[0], mx = g_cam_bookmark_x[0];
        for (int i = 1; i < g_cam_bookmark_count; i++) {
            if (g_cam_bookmark_x[i] < mn) mn = g_cam_bookmark_x[i];
            if (g_cam_bookmark_x[i] > mx) mx = g_cam_bookmark_x[i];
        }
        g_pan_scan_start_x = mn;
        g_pan_scan_end_x = mx;
        stage_set_toast("Pan scan range set from bookmarks");
    }
    if (ImGui::Button("Copy Bookmark Report", ImVec2(-1, 0))) {
        char report[2048];
        report[0] = '\0';
        char line[192];
        for (int i = 0; i < g_cam_bookmark_count; i++) {
            float full = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h);
            float top = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h / 5);
            float floor = mk2_screen_band_coverage(g_cam_bookmark_x[i], (g_pan_scan_view_h * 3) / 4, g_pan_scan_view_h);
            snprintf(line, sizeof line, "%s x=%d full=%.1f top=%.1f floor=%.1f\n",
                     g_cam_bookmark_name[i], g_cam_bookmark_x[i], full, top, floor);
            stage_append(report, sizeof report, line);
        }
        ImGui::SetClipboardText(report);
        stage_set_toast("Copied camera bookmark report");
    }
}
