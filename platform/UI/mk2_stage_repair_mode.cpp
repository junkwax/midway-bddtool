#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>

void draw_mk2_stage_repair_mode(void)
{
    Mk2Budget b = mk2_collect_budget();
    ImGui::Text("Stage Repair Mode");
    ImGui::TextDisabled("For stock stages: find dormant art, place it, then probe the draw stack.");
    ImGui::Text("%d dormant image(s), %d selected object(s)", b.unused_images, selected_count());
    if (ImGui::Button("Show Dormant Images in Explorer", ImVec2(-1, 0))) {
        g_asset_explorer_filter = 2;
        stage_set_toast("Asset Explorer filtered to dormant images");
    }
    if (ImGui::Button("Select Objects Using Highlighted Image", ImVec2(-1, 0)) &&
        g_hl_obj >= 0 && g_hl_obj < g_no)
    {
        int n = mk2_select_objects_by_image(g_obj[g_hl_obj].ii);
        snprintf(g_toast_msg, sizeof g_toast_msg, "Selected %d matching placement(s)", n);
        g_toast_timer = 3.0f;
    }
    if (ImGui::Button("Disable Selected, Keep Art", ImVec2(-1, 0))) {
        int removed = mk2_disable_selected_assets_keep_images();
        snprintf(g_toast_msg, sizeof g_toast_msg,
                 removed ? "Disabled %d object(s); art kept" : "No object selected", removed);
        g_toast_timer = 3.0f;
    }
    ImGui::TextDisabled("Dead Pool vent backing workflow: filter dormant -> enable likely backing art -> use stack inspector to move/layer.");
}
