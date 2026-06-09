#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>

static void draw_scan_metric(const char *label, float pct)
{
    ImVec4 col = pct >= 95.0f ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) :
                 pct >= 70.0f ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f) :
                                ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
    ImGui::TextColored(col, "%s %.1f%%", label, pct);
}

void draw_mk2_pan_coverage_scanner(void)
{
    ImGui::Text("Pan Coverage Scanner");
    ImGui::TextDisabled("Sweeps camera X and catches transparent/black holes before a MAME run.");
    ImGui::InputInt("Start X", &g_pan_scan_start_x);
    ImGui::InputInt("End X", &g_pan_scan_end_x);
    ImGui::InputInt("Step", &g_pan_scan_step);
    ImGui::InputInt("Sample Stride", &g_pan_scan_stride);
    ImGui::InputInt("Viewport W", &g_pan_scan_view_w);
    ImGui::InputInt("Viewport H", &g_pan_scan_view_h);
    ImGui::InputInt("Min Layer", &g_pan_scan_min_layer);
    ImGui::InputInt("Max Layer", &g_pan_scan_max_layer);
    if (g_pan_scan_step < 1) g_pan_scan_step = 1;
    if (g_pan_scan_stride < 1) g_pan_scan_stride = 1;
    if (g_pan_scan_view_w < 16) g_pan_scan_view_w = 16;
    if (g_pan_scan_view_h < 16) g_pan_scan_view_h = 16;
    if (g_pan_scan_end_x < g_pan_scan_start_x) g_pan_scan_end_x = g_pan_scan_start_x;

    float worst_full = 101.0f, worst_top = 101.0f, worst_floor = 101.0f;
    int worst_x = g_pan_scan_start_x;
    int points = 0;
    for (int cam = g_pan_scan_start_x; cam <= g_pan_scan_end_x; cam += g_pan_scan_step) {
        float full = mk2_screen_band_coverage(cam, 0, g_pan_scan_view_h);
        float top = mk2_screen_band_coverage(cam, 0, g_pan_scan_view_h / 5);
        float floor = mk2_screen_band_coverage(cam, (g_pan_scan_view_h * 3) / 4, g_pan_scan_view_h);
        if (full < worst_full) { worst_full = full; worst_x = cam; }
        if (top < worst_top) worst_top = top;
        if (floor < worst_floor) worst_floor = floor;
        points++;
    }
    g_pan_scan_worst_x = worst_x;
    draw_scan_metric("Worst full-screen coverage:", worst_full);
    draw_scan_metric("Worst top band coverage:", worst_top);
    draw_scan_metric("Worst floor band coverage:", worst_floor);
    DisplayObjectSummary obj_pressure = mk2_compute_display_object_summary();
    g_pan_scan_worst_obj_x = obj_pressure.worst_x;
    g_pan_scan_worst_obj_count = obj_pressure.max_count;
    ImVec4 obj_col = obj_pressure.max_count > MK2_DISPLAY_OBJECT_CAP
        ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
        : (obj_pressure.max_count > MK2_DISPLAY_OBJECT_WARN
           ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
           : ImVec4(0.45f, 1.0f, 0.55f, 1.0f));
    ImGui::TextColored(obj_col, "Worst visible objects: %d / %d at X %d",
                       obj_pressure.max_count, MK2_DISPLAY_OBJECT_CAP, obj_pressure.worst_x);
    ImGui::TextDisabled("Scanned %d camera position(s). Lower stride is slower but more precise.", points);

    int pressure_layers[256];
    int pressure_counts[256];
    int pressure_n = mk2_visible_object_counts_by_layer_at_camera(
        obj_pressure.worst_x, pressure_layers, pressure_counts, 256);
    if (pressure_n > 0 &&
        ImGui::BeginTable("object_pressure_layers", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("scroll", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("objects", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();
        for (int i = 0; i < pressure_n; i++) {
            int layer = pressure_layers[i];
            int count = pressure_counts[i];
            float share = obj_pressure.max_count > 0
                ? (float)count * 100.0f / (float)obj_pressure.max_count
                : 0.0f;
            ImGui::PushID(layer);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(mk2_layer_label(layer));
            ImGui::TableNextColumn(); ImGui::Text("%.2f", gv_scroll_factor(layer));
            ImGui::TableNextColumn(); ImGui::Text("%d", count);
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", share);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Select")) {
                int n = mk2_select_visible_objects_at_camera_layer(obj_pressure.worst_x, layer);
                char msg[96];
                snprintf(msg, sizeof msg, "Selected %d object(s) in layer 0x%02X at X %d",
                         n, layer, obj_pressure.worst_x);
                stage_set_toast(msg);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ImGui::GetContentRegionAvail().x, 46.0f);
    if (sz.x < 100.0f) sz.x = 100.0f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(22,22,30,255));
    int span = g_pan_scan_end_x - g_pan_scan_start_x;
    if (span < 1) span = 1;
    for (int x = 0; x < (int)sz.x; x++) {
        int cam = g_pan_scan_start_x + (int)((float)x * (float)span / sz.x);
        float full = mk2_screen_band_coverage(cam, 0, g_pan_scan_view_h);
        ImU32 c = full >= 95.0f ? IM_COL32(90,210,120,230) :
                  full >= 70.0f ? IM_COL32(230,180,70,230) :
                                  IM_COL32(220,60,60,230);
        dl->AddLine(ImVec2(p.x + x, p.y + sz.y), ImVec2(p.x + x, p.y + sz.y - (full / 100.0f) * sz.y), c);
    }
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(160,160,190,180));
    ImGui::InvisibleButton("##pan_scan_chart", sz);

    if (ImGui::Button("Use Worst X As Preview", ImVec2(-1, 0))) {
        g_stage_preview_worldx = g_pan_scan_worst_x;
        stage_set_toast("Preview X moved to worst coverage point");
    }
    if (ImGui::Button("Use Worst Object X As Preview", ImVec2(-1, 0))) {
        g_stage_preview_worldx = g_pan_scan_worst_obj_x;
        g_scroll_pos = g_pan_scan_worst_obj_x;
        g_view_changed = 1;
        stage_set_toast("Preview X moved to worst object pressure point");
    }
    if (ImGui::Button("Select Objects Visible At Worst Object X", ImVec2(-1, 0))) {
        int n = mk2_select_visible_objects_at_camera(g_pan_scan_worst_obj_x);
        char msg[96];
        snprintf(msg, sizeof msg, "Selected %d visible object(s) at X %d",
                 n, g_pan_scan_worst_obj_x);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Center View On Worst X", ImVec2(-1, 0))) {
        g_view_x = g_pan_scan_worst_x;
        g_view_y = 0;
        g_view_changed = 1;
    }
}
