#include "bg_editor_globals.h"

#include "imgui.h"

void draw_mk2_project_seed_tools(void)
{
    if (!g_have_bdb) {
        if (ImGui::Button("New MK2 Project", ImVec2(-1, 0)))
            open_new_mk2_project_from_template();
    }
    if (ImGui::Button("Full-Screen Proof Level", ImVec2(-1, 0)))
        request_unsaved_action(UNSAVED_ACTION_NEW_BG_PROOF);
    if (ImGui::Button("Checker Test Level", ImVec2(-1, 0)))
        request_unsaved_action(UNSAVED_ACTION_NEW_CHECKER);
}
