#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stdio.h>

void draw_undo_history(void)
{
    if (!g_show_undo_history) return;
    set_left_panel_default(420.0f, 220.0f, 0.0f);
    if (!ImGui::Begin("History##undo_hist", &g_show_undo_history,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End(); return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));
    ImGui::Text(">> Current  (%d spr, %d img)", g_no, g_ni);
    ImGui::PopStyleColor();

    if (undo_get_count() == 0) {
        ImGui::TextDisabled("  (no history)");
        ImGui::End(); return;
    }

    ImGui::Separator();

    int clicked_depth = -1;
    for (int d = 0; d < undo_get_count(); d++) {
        const char *lbl = undo_get_history_label(d);
        int obj_count = undo_get_history_objects_count(d);
        int img_count = undo_get_history_images_count(d);
        char row[96];
        snprintf(row, sizeof row, "%s  (%d spr, %d img)##uhr%d", lbl, obj_count, img_count, d);
        if (ImGui::Selectable(row)) clicked_depth = d + 1;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ctrl+Z x %d", d + 1);
    }

    ImGui::End();

    if (clicked_depth > 0)
        for (int s = 0; s < clicked_depth; s++) undo_restore();
}
