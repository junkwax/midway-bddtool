#include "bg_editor_globals.h"
#include "imgui.h"

void draw_grid_settings(void)
{
    if (!g_show_grid_set) return;

    set_left_panel_default(92.0f, 220.0f, 150.0f);
    if (ImGui::Begin("Grid Settings", &g_show_grid_set)) {
        ImGui::SliderInt("Spacing X", &g_grid_sx, 8, 256);
        ImGui::SliderInt("Spacing Y", &g_grid_sy, 8, 256);
        float gc[3] = {
            g_grid_color[0] / 255.f,
            g_grid_color[1] / 255.f,
            g_grid_color[2] / 255.f
        };
        if (ImGui::ColorEdit3("Color", gc)) {
            g_grid_color[0] = (Uint8)(gc[0] * 255);
            g_grid_color[1] = (Uint8)(gc[1] * 255);
            g_grid_color[2] = (Uint8)(gc[2] * 255);
        }
    }
    ImGui::End();
}
