#include "bg_editor.h"
#include "bg_editor_globals.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

void draw_mk2_stage_start_camera_tool(void)
{
    static char loaded_stage[160] = "";
    int parsed_x = 0, parsed_y = 0, parsed_ground = 0;
    bool parsed_cam_ok = bdd_get_stage_start_camera(&parsed_x, &parsed_y) != 0;
    bool parsed_ground_ok = bdd_get_stage_ground_y(&parsed_ground) != 0;
    char key[160];
    snprintf(key, sizeof key, "%s|%s", g_name, g_bdb_path);
    if (strncmp(key, loaded_stage, sizeof loaded_stage) != 0) {
        snprintf(loaded_stage, sizeof loaded_stage, "%s", key);
        if (parsed_cam_ok) {
            g_stage_start_camera_x = parsed_x;
            g_stage_start_camera_y = parsed_y;
        }
        if (parsed_ground_ok)
            g_stage_start_ground_y = parsed_ground;
    }

    ImGui::Text("Match Start Placement");
    ImGui::TextDisabled("Camera open and fighter ground Y from the stage's BGND.ASM init words.");
    ImGui::Checkbox("Use custom start camera", &g_stage_start_camera_enabled);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Camera X", &g_stage_start_camera_x);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Y##start_camera_y", &g_stage_start_camera_y);
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

    ImGui::Separator();
    ImGui::Checkbox("Use custom fighter ground Y", &g_stage_start_ground_enabled);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Fighter Ground Y", &g_stage_start_ground_y);
    if (parsed_ground_ok)
        ImGui::TextDisabled("BGND fighter ground Y: %d", parsed_ground);
    else
        ImGui::TextDisabled("No fighter ground Y parsed yet.");
    if (ImGui::Button("Load Current BGND Values", ImVec2(-1, 0))) {
        if (parsed_cam_ok) {
            g_stage_start_camera_x = parsed_x;
            g_stage_start_camera_y = parsed_y;
            g_stage_start_camera_enabled = true;
        }
        if (parsed_ground_ok) {
            g_stage_start_ground_y = parsed_ground;
            g_stage_start_ground_enabled = true;
        }
        stage_set_toast((parsed_cam_ok || parsed_ground_ok)
                        ? "Loaded BGND match start values"
                        : "No BGND match start values parsed");
    }

    ImGui::Separator();
    ImGui::Checkbox("Patch BGND.ASM init block", &g_stage_start_camera_patch_bgnd);
    draw_path_field("BGND.ASM##startcam", g_stage_start_bgnd_path, sizeof g_stage_start_bgnd_path,
                    "Select BGND.ASM", "ASM Files\0*.ASM;*.asm\0All Files\0*.*\0");
    bool can_patch_camera = g_stage_start_camera_enabled && g_stage_start_camera_patch_bgnd;
    bool can_patch_ground = g_stage_start_ground_enabled && g_stage_start_camera_patch_bgnd;
    bool can_patch_all = can_patch_camera && can_patch_ground;
    if (!can_patch_all) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Match Start to BGND.ASM", ImVec2(-1, 0)))
        stage_start_apply_bgnd_start_placement();
    if (!can_patch_all) ImGui::EndDisabled();

    bool can_patch = can_patch_camera;
    if (!can_patch) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Start Camera to BGND.ASM", ImVec2(-1, 0)))
        stage_start_apply_bgnd_patch();
    if (!can_patch) ImGui::EndDisabled();

    if (!can_patch_ground) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Fighter Ground Y to BGND.ASM", ImVec2(-1, 0)))
        stage_start_apply_bgnd_ground(g_stage_start_ground_y);
    if (!can_patch_ground) ImGui::EndDisabled();

    if (g_stage_start_status[0])
        ImGui::TextWrapped("%s", g_stage_start_status);
}
