#include "bg_editor_globals.h"

#include "imgui.h"

#include <cstdio>

void apply_mk2_template_preset(int which)
{
    if (which == 0) {
        std::snprintf(g_new_name, sizeof g_new_name, "MK2WALK");
        g_new_w = 1200; g_new_h = 256; g_new_depth = 255; g_new_pals = 0;
        g_stage_world_width = 1200;
        g_stage_pan_mid = true;
        g_stage_stock_portal_sides = false;
    } else if (which == 1) {
        std::snprintf(g_new_name, sizeof g_new_name, "BGPROF");
        g_new_w = 1203; g_new_h = 254; g_new_depth = 255; g_new_pals = 0;
        g_stage_world_width = 1203;
        g_stage_pan_mid = true;
        g_stage_stock_portal_sides = true;
        g_stage_bg_palette_mode = 0;
    } else if (which == 2) {
        std::snprintf(g_new_name, sizeof g_new_name, "MK2FIX");
        g_new_w = 1024; g_new_h = 256; g_new_depth = 255; g_new_pals = 0;
        g_asset_explorer_filter = 2;
    } else {
        std::snprintf(g_new_name, sizeof g_new_name, "MK2LAYR");
        g_new_w = 1200; g_new_h = 256; g_new_depth = 255; g_new_pals = 0;
        g_stage_world_width = 1200;
        g_stage_pan_mid = true;
    }
    g_show_new = true;
}

void draw_mk2_template_wizard_tool(void)
{
    ImGui::Text("Stage Template Wizard");
    ImGui::TextDisabled("Creates blank BDB/BDD canvases with MK2-friendly dimensions and workflow defaults.");
    if (ImGui::Button("New MK2 Walkable Stage", ImVec2(-1, 0))) apply_mk2_template_preset(0);
    if (ImGui::Button("New Portal Layered Stage", ImVec2(-1, 0))) apply_mk2_template_preset(1);
    if (ImGui::Button("New Stock Stage Repair Canvas", ImVec2(-1, 0))) apply_mk2_template_preset(2);
    if (ImGui::Button("New Floor + Mid + Foreground Stage", ImVec2(-1, 0))) apply_mk2_template_preset(3);
}
