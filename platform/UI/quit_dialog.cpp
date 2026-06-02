#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdlib.h>

void draw_quit_dialog(void)
{
    if (!g_quit_requested) return;
    ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (g_dirty) {
            ImGui::Text("You have unsaved changes.");
            ImGui::Text("Save before quitting?");
            ImGui::Separator();
            if (ImGui::Button("Save && Quit", ImVec2(110, 0))) {
                if (g_have_bdb && g_bdb_path[0]) bdb_save(g_bdb_path);
                if (g_bdd_path[0]) bdd_save();
                g_dirty = 0;
                exit(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(80, 0)))
                exit(0);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                g_quit_requested = false;
        } else {
            ImGui::Text("No unsaved changes. Quit?");
            ImGui::Separator();
            if (ImGui::Button("Quit", ImVec2(80, 0)))
                exit(0);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                g_quit_requested = false;
        }
        ImGui::EndPopup();
    }
}
