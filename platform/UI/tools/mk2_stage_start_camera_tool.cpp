#include "bg_editor_globals.h"

#include "imgui.h"

void draw_mk2_stage_start_camera_tool(void)
{
    ImGui::Text("Stage Start Camera");
    ImGui::TextDisabled("Sets the intended match-open camera and can patch BGND.ASM's background init record.");
    ImGui::Checkbox("Use custom start camera", &g_stage_start_camera_enabled);
    ImGui::InputInt("Start X", &g_stage_start_camera_x);
    ImGui::InputInt("Start Y", &g_stage_start_camera_y);
    ImGui::TextDisabled("Package/ROM preview X: %d", stage_effective_preview_worldx());
    if (ImGui::Button("Use Current Preview Camera", ImVec2(-1, 0))) {
        g_stage_start_camera_x = g_scroll_pos;
        g_stage_start_camera_y = g_game_view_y;
        g_stage_preview_worldx = g_stage_start_camera_x;
        g_stage_start_camera_enabled = true;
        stage_set_toast("Start camera set from preview");
    }
    if (ImGui::Button("Use Package Preview X", ImVec2(-1, 0))) {
        g_stage_start_camera_x = g_stage_preview_worldx;
        g_stage_start_camera_enabled = true;
        stage_set_toast("Start camera X set from package preview");
    }
    ImGui::Checkbox("Patch BGND.ASM init block", &g_stage_start_camera_patch_bgnd);
    draw_path_field("BGND.ASM##startcam", g_stage_start_bgnd_path, sizeof g_stage_start_bgnd_path,
                    "Select BGND.ASM", "ASM Files\0*.ASM;*.asm\0All Files\0*.*\0");
    bool can_patch = g_stage_start_camera_enabled && g_stage_start_camera_patch_bgnd;
    if (!can_patch) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Start Camera to BGND.ASM", ImVec2(-1, 0)))
        stage_start_apply_bgnd_patch();
    if (!can_patch) ImGui::EndDisabled();
    if (g_stage_start_status[0])
        ImGui::TextWrapped("%s", g_stage_start_status);
}
