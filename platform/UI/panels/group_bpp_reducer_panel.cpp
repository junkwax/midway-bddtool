#include "bg_editor_globals.h"
#include "imgui.h"

void draw_group_bpp_reducer_panel(void)
{
    if (!g_show_group_bpp_reducer) return;
    set_left_panel_default(300.0f, 680.0f, 620.0f);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float max_h = ds.y > 120.0f ? ds.y - 80.0f : 620.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(430.0f, 360.0f), ImVec2(900.0f, max_h));
    if (ImGui::Begin("Selected BPP Reducer##groupbpp", &g_show_group_bpp_reducer))
        draw_mk2_selected_bpp_reducer_tool();
    ImGui::End();
}
