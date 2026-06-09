#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

void draw_mk2_import_workspace(void)
{
    ImGui::Text("MK2 Stage Import Workspace");
    ImGui::TextDisabled("Copy an existing BDB/BDD pair into a tracked workspace, then open it for repair/editing.");
    draw_path_field("Source BDB", g_import_src_bdb, sizeof g_import_src_bdb,
                    "Select source BDB", "BDB Files\0*.BDB;*.bdb\0All Files\0*.*\0");
    draw_path_field("Source BDD", g_import_src_bdd, sizeof g_import_src_bdd,
                    "Select source BDD", "BDD Files\0*.BDD;*.bdd\0All Files\0*.*\0");
    ImGui::InputText("Destination", g_import_stage_dir, sizeof g_import_stage_dir);
    ImGui::SameLine();
    if (ImGui::SmallButton("...##import_dest")) {
        char path[512] = "";
        if (folder_dialog_open("Select import workspace", path, sizeof path))
            snprintf(g_import_stage_dir, sizeof g_import_stage_dir, "%s", path);
    }
    ImGui::InputText("LOAD2 Name", g_import_stage_name, sizeof g_import_stage_name);
    ImGui::InputText("Display Name", g_import_display_name, sizeof g_import_display_name);
    ImGui::Checkbox("Open after import", &g_import_open_after);
    if (ImGui::Button("Import BDB/BDD Workspace", ImVec2(-1, 0)))
        mk2_import_workspace_apply();
    if (g_import_status[0])
        ImGui::TextWrapped("%s", g_import_status);
}

void draw_mk2_clustered_png_importer(void)
{
    ImGui::Text("Stock-Style Clustered PNG Import");
    ImGui::TextDisabled("Splits a source PNG into BDD tiles, clusters local RGB555 palettes, and places one BDB object per tile.");
    draw_path_field("Source PNG", g_cluster_png_path, sizeof g_cluster_png_path,
                    "Select source PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
    ImGui::InputText("Stage Name", g_cluster_stage_name, sizeof g_cluster_stage_name);
    ImGui::InputInt("Tile W", &g_cluster_tile_w);
    ImGui::InputInt("Tile H", &g_cluster_tile_h);
    ImGui::InputInt("Visible Colors", &g_cluster_visible_colors);
    ImGui::InputInt("Max Palettes", &g_cluster_max_palettes);
    if (g_cluster_tile_w < 1) g_cluster_tile_w = 1;
    if (g_cluster_tile_h < 1) g_cluster_tile_h = 1;
    if (g_cluster_visible_colors < 1) g_cluster_visible_colors = 1;
    if (g_cluster_visible_colors > 255) g_cluster_visible_colors = 255;
    if (g_cluster_max_palettes < 1) g_cluster_max_palettes = 1;
    if (ImGui::BeginCombo("Layer", mk2_layer_label(g_cluster_layer))) {
        for (int li = 0; li < mk2_layer_preset_count(); li++) {
            int wx = mk2_layer_preset_wx(li);
            if (ImGui::Selectable(mk2_layer_preset_label(li), g_cluster_layer == wx))
                g_cluster_layer = wx;
        }
        ImGui::EndCombo();
    }
    ImGui::InputInt("Start X", &g_cluster_start_x);
    ImGui::InputInt("Start Y", &g_cluster_start_y);
    ImGui::Checkbox("Replace current project", &g_cluster_replace_project);
    ImGui::Checkbox("Skip transparent tiles", &g_cluster_skip_empty_tiles);
    if (ImGui::Button("Import Clustered Tiles", ImVec2(-1, 0)))
        mk2_clustered_png_import_apply();
    if (g_cluster_status[0])
        ImGui::TextWrapped("%s", g_cluster_status);
    ImGui::TextDisabled("For stock-like MK2 work, start with 40x32 or 40x64 tiles and <=63 visible colors.");
}
