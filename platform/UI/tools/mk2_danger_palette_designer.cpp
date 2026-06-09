#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>

void draw_mk2_danger_palette_designer(void)
{
    ImGui::Text("Danger Palette Designer");
    ImGui::TextDisabled("Preview/create red danger variants without hand-tuning every color.");
    if (g_n_pals <= 0) {
        ImGui::TextDisabled("No palettes loaded.");
        return;
    }
    if (g_sel_pal < 0 || g_sel_pal >= g_n_pals) g_sel_pal = 0;
    ImGui::SliderFloat("Red Strength", &g_danger_palette_strength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Keep Blue", &g_danger_palette_keep_blue, 0.0f, 1.0f, "%.2f");
    ImGui::Text("Palette %d: %s", g_sel_pal, g_pal_name[g_sel_pal]);
    int shown = g_pal_count[g_sel_pal] < 32 ? g_pal_count[g_sel_pal] : 32;
    for (int i = 0; i < shown; i++) {
        if (i % 8 != 0) ImGui::SameLine();
        Uint32 c = i == 0 ? 0 : danger_tint_color(g_pals[g_sel_pal][i],
                                                   g_danger_palette_strength,
                                                   g_danger_palette_keep_blue);
        ImGui::PushID(i);
        ImGui::ColorButton("##danger",
                           ImVec4(((c >> 16) & 0xFF)/255.0f,
                                  ((c >> 8) & 0xFF)/255.0f,
                                  (c & 0xFF)/255.0f,
                                  i == 0 ? 0.25f : 1.0f),
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(18,18));
        ImGui::PopID();
    }
    if (ImGui::Button("Add Red Variant Palette", ImVec2(-1, 0))) {
        undo_save();
        Uint32 colors[256];
        for (int i = 0; i < 256; i++)
            colors[i] = (i == 0) ? 0 : danger_tint_color(g_pals[g_sel_pal][i],
                                                          g_danger_palette_strength,
                                                          g_danger_palette_keep_blue);
        char name[64];
        snprintf(name, sizeof name, "%s_RED", g_pal_name[g_sel_pal]);
        int dst = editor_project_append_palette_slot(name, g_pal_count[g_sel_pal], colors);
        if (dst < 0) {
            stage_set_toast("Could not add red variant palette");
            return;
        }
        g_sel_pal = dst;
        sync_bdb_header_counts();
        g_dirty = 1;
        stage_set_toast("Added red variant palette");
    }
}
