#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

static void primary_module_name(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (g_bdb_num_modules > 0)
        sscanf(g_bdb_modules[0], "%63s", out);
    if (!out[0])
        snprintf(out, outsz, "MOD0");
}

void draw_mk2_integration_summary(void)
{
    if (g_bdb_num_modules > 0)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Module: ready");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1), "Module: will be auto-created on export");

    char bbb_line[96];
    char bmod_symbol[96];
    char mod_name[64];
    primary_module_name(mod_name, sizeof mod_name);
    snprintf(bbb_line, sizeof bbb_line, "BBB> %s", g_name[0] ? g_name : "WORLD");
    snprintf(bmod_symbol, sizeof bmod_symbol, "%sBMOD", mod_name);

    ImGui::TextUnformatted(bbb_line);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy##bbb"))
        ImGui::SetClipboardText(bbb_line);
    ImGui::Text("BMOD: %s", bmod_symbol);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy##bmod"))
        ImGui::SetClipboardText(bmod_symbol);
    if (g_name[0] && strlen(g_name) > 8)
        ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Name must be 8 chars or less for LOAD2.");
    ImGui::TextDisabled("mk2-main test LOD: ORNGBL.LOD at ***> 5d40000");
}
