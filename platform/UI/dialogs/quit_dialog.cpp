#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/viewer_save.h"
#include "UI/dialogs/quit_dialog.h"
#include "UI/tools/palette_color_tools.h"
#include "imgui.h"

#include <stdlib.h>

bool save_all_project(void);

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
                /* Only quit once the save actually landed -- a never-saved
                   project prompts for a location, and cancelling it (or a
                   failed write) must not throw the work away. */
                if (save_all_project())
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
