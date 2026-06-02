#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

static char g_preview_source_path[512] = "";

void draw_mk2_stage_preview_dashboard(void)
{
    ImGui::Text("Stage Preview Dashboard");
    ImGui::TextDisabled("Keep source/editor/ROM/MAME preview paths together and feed the diff tool.");
    if (!g_preview_source_path[0]) {
        char fname[128];
        snprintf(fname, sizeof fname, "%s_preview.png", g_stage_internal_name);
        resolve_stage_file(g_preview_source_path, sizeof g_preview_source_path, fname);
    }
    char composite[512] = "";
    if (g_bdb_path[0]) {
        snprintf(composite, sizeof composite, "%s", g_bdb_path);
        char *dot = strrchr(composite, '.');
        if (dot) *dot = '\0';
        strncat(composite, "_composite.png", sizeof(composite) - strlen(composite) - 1);
    }
    char rom_preview[512];
    resolve_stage_file(rom_preview, sizeof rom_preview, g_stage_rom_preview);
    ImGui::InputText("Source Preview", g_preview_source_path, sizeof g_preview_source_path);
    ImGui::Text("Editor Composite: %s", composite[0] ? composite : "(save/open BDB first)");
    ImGui::Text("ROM Preview: %s", rom_preview);
    ImGui::Text("Live MAME: %s", g_mame_output);
    if (ImGui::Button("Export Editor Composite", ImVec2(-1, 0))) {
        export_composite_png();
        stage_set_toast("Composite export requested");
    }
    if (ImGui::Button("Diff Source -> ROM", ImVec2(-1, 0))) {
        mk2_preview_diff_use_source_and_rom(g_preview_source_path, rom_preview);
        stage_set_toast("Diff paths set: source to ROM");
    }
    if (ImGui::Button("Diff Composite -> ROM", ImVec2(-1, 0))) {
        mk2_preview_diff_use_composite_and_rom(composite, rom_preview);
        stage_set_toast("Diff paths set: composite to ROM");
    }
    if (ImGui::Button("Diff ROM -> MAME", ImVec2(-1, 0))) {
        mk2_preview_diff_use_rom_and_mame(rom_preview, g_mame_output);
        stage_set_toast("Diff paths set: ROM to MAME");
    }
}
