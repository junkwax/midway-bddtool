#include "bg_editor_globals.h"
#include "imgui.h"

void draw_group_bpp_reducer_panel(void)
{
    if (!g_show_group_bpp_reducer) return;
    set_left_panel_default(300.0f, 760.0f, 0.0f);
    if (ImGui::Begin("Selected BPP Reducer##groupbpp", &g_show_group_bpp_reducer,
                     ImGuiWindowFlags_AlwaysAutoResize))
        draw_mk2_selected_bpp_reducer_tool();
    ImGui::End();
}
