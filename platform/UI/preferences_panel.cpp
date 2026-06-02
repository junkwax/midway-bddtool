#include "bg_editor_globals.h"
#include "imgui.h"

void draw_preferences(void)
{
    if (!g_show_prefs) return;
    set_left_panel_default(92.0f, 360.0f, 300.0f);
    if (!ImGui::Begin("Preferences", &g_show_prefs))
        return;

    bool changed = false;
    ImGui::SeparatorText("Grid");
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Grid width",  &g_pref_grid_sx)) { if (g_pref_grid_sx < 1) g_pref_grid_sx = 1; changed = true; g_grid_sx = g_pref_grid_sx; }
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Grid height", &g_pref_grid_sy)) { if (g_pref_grid_sy < 1) g_pref_grid_sy = 1; changed = true; g_grid_sy = g_pref_grid_sy; }

    ImGui::SeparatorText("Snap");
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Snap distance (px)", &g_pref_snap_dist)) { if (g_pref_snap_dist < 0) g_pref_snap_dist = 0; changed = true; }

    ImGui::SeparatorText("Auto-save");
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Auto-save interval (s, 0=off)", &g_pref_autosave_s)) {
        if (g_pref_autosave_s < 0) g_pref_autosave_s = 0;
        changed = true;
    }

    ImGui::SeparatorText("UI");
    ImGui::SetNextItemWidth(140);
    if (ImGui::SliderFloat("Font scale", &g_pref_font_scale, 0.75f, 2.0f, "%.2f")) {
        ImGui::GetIO().FontGlobalScale = g_pref_font_scale;
        changed = true;
    }

    ImGui::Spacing();
    if (changed) settings_save();

    ImGui::End();
}
