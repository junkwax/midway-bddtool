#include "bg_editor_globals.h"
#include "imgui.h"

enum Mk2WorkflowSection {
    MK2_WF_START = 0,
    MK2_WF_IMPORT,
    MK2_WF_CHECK,
    MK2_WF_PREVIEW,
    MK2_WF_ASSETS,
    MK2_WF_REPAIR,
    MK2_WF_OPTIMIZE,
    MK2_WF_FX,
    MK2_WF_FINISH,
    MK2_WF_COUNT
};

static const char *mk2_workflow_section_name(int section)
{
    static const char *names[MK2_WF_COUNT] = {
        "Start", "Import", "Check", "Preview",
        "Assets", "Repair", "Optimize", "FX", "Finish"
    };
    if (section < 0 || section >= MK2_WF_COUNT) section = 0;
    return names[section];
}

static bool mk2_tool_header(const char *label, bool default_open = false)
{
    return ImGui::CollapsingHeader(label, default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}

void draw_mk2_workflow(void)
{
    if (!g_show_mk2_workflow || g_preview_mode) return;

    set_left_panel_default(92.0f, 440.0f, 560.0f);
    if (!ImGui::Begin("MK2 Workflow", &g_show_mk2_workflow)) {
        ImGui::End();
        return;
    }

    if (g_mk2_workflow_section < 0 || g_mk2_workflow_section >= MK2_WF_COUNT)
        g_mk2_workflow_section = g_have_bdb ? MK2_WF_CHECK : MK2_WF_START;

    if (ImGui::BeginCombo("Section", mk2_workflow_section_name(g_mk2_workflow_section))) {
        for (int i = 0; i < MK2_WF_COUNT; i++) {
            if (ImGui::Selectable(mk2_workflow_section_name(i), g_mk2_workflow_section == i))
                g_mk2_workflow_section = i;
        }
        ImGui::EndCombo();
    }
    ImGui::Text("Objects: %d   Images: %d   Palettes: %d", g_no, g_ni, g_n_pals);
    ImGui::Separator();

    switch (g_mk2_workflow_section) {
    case MK2_WF_START:
        draw_mk2_project_seed_tools();
        ImGui::Separator();
        draw_mk2_level_start_helper_tool();
        if (g_have_bdb) {
            ImGui::Separator();
            draw_mk2_integration_summary();
        }
        break;

    case MK2_WF_IMPORT:
        if (mk2_tool_header("Import Existing MK2 Stage", g_mk2_focus_tool == 2))
            draw_mk2_import_workspace();
        ImGui::Separator();
        if (mk2_tool_header("Clustered PNG Stage Import", g_mk2_focus_tool == 3))
            draw_mk2_clustered_png_importer();
        ImGui::Separator();
        if (mk2_tool_header("Quick PNG / Tile Placement", true))
            draw_mk2_quick_tile_tools();
        break;

    case MK2_WF_CHECK:
        if (!g_have_bdb) {
            draw_mk2_project_seed_tools();
            break;
        }
        if (mk2_tool_header("LOAD2 Doctor", true))
            draw_mk2_load2_doctor_tool();
        ImGui::Separator();
        if (mk2_tool_header("Stage Readiness Gate", g_mk2_focus_tool == 5))
            draw_mk2_stage_readiness_gate();
        break;

    case MK2_WF_PREVIEW:
        if (mk2_tool_header("Stage Preview Dashboard", true))
            draw_mk2_stage_preview_dashboard();
        ImGui::Separator();
        if (mk2_tool_header("Stage Start Camera"))
            draw_mk2_stage_start_camera_tool();
        ImGui::Separator();
        if (mk2_tool_header("ROM Preview Diff", g_mk2_focus_tool == 9))
            draw_mk2_preview_diff_tool();
        break;

    case MK2_WF_ASSETS:
        if (!g_have_bdb) {
            draw_mk2_project_seed_tools();
            break;
        }
        if (mk2_tool_header("Stock Stage Asset Explorer", true))
            draw_mk2_asset_explorer();
        ImGui::Separator();
        if (mk2_tool_header("Dormant BDD Assets"))
            draw_mk2_unused_asset_tools();
        ImGui::Separator();
        if (mk2_tool_header("Layer Stack Inspector"))
            draw_mk2_layer_stack_inspector();
        ImGui::Separator();
        if (mk2_tool_header("Runtime Extras"))
            draw_mk2_runtime_extras_tool();
        break;

    case MK2_WF_REPAIR:
        if (mk2_tool_header("Stage Repair Mode", true))
            draw_mk2_stage_repair_mode();
        ImGui::Separator();
        if (mk2_tool_header("Auto Repair Suggestions"))
            draw_mk2_auto_repair_suggestions();
        ImGui::Separator();
        if (mk2_tool_header("Transparent Color Advisor"))
            draw_mk2_transparency_advisor();
        ImGui::Separator();
        if (mk2_tool_header("Foreground Occlusion Preview"))
            draw_mk2_foreground_occlusion_preview();
        break;

    case MK2_WF_OPTIMIZE:
        if (mk2_tool_header("Palette Seam Detector", true))
            draw_mk2_palette_seam_detector();
        ImGui::Separator();
        if (mk2_tool_header("Uppercut Headroom Preview"))
            draw_mk2_uppercut_headroom_preview();
        ImGui::Separator();
        if (mk2_tool_header("Parallax Sanity Checker"))
            draw_mk2_parallax_sanity_checker();
        ImGui::Separator();
        if (mk2_tool_header("Mirrored Asset Tool"))
            draw_mk2_mirrored_asset_tool();
        ImGui::Separator();
        if (mk2_tool_header("Duplicate / Mirror Finder"))
            draw_mk2_duplicate_mirror_finder();
        ImGui::Separator();
        if (mk2_tool_header("Safe Dedup Assistant"))
            draw_mk2_safe_dedup_assistant();
        ImGui::Separator();
        if (mk2_tool_header("Selected BPP Reducer"))
            draw_mk2_selected_bpp_reducer_tool();
        ImGui::Separator();
        if (mk2_tool_header("Small Artifact Finder"))
            draw_mk2_small_artifact_finder();
        ImGui::Separator();
        if (mk2_tool_header("Batch Image Cleanup"))
            draw_mk2_batch_image_cleanup_tool();
        ImGui::Separator();
        if (mk2_tool_header("Palette Usage Optimizer"))
            draw_mk2_palette_usage_optimizer();
        ImGui::Separator();
        if (mk2_tool_header("Smart Palette Grouper"))
            draw_mk2_smart_palette_grouper();
        ImGui::Separator();
        if (mk2_tool_header("Palette Blend / Merge"))
            draw_palette_blend_merge_tool();
        ImGui::Separator();
        if (mk2_tool_header("Palette Remap / Compress"))
            draw_mk2_palette_remap_compress_tool();
        ImGui::Separator();
        if (mk2_tool_header("Trim Transparent Border"))
            draw_mk2_trim_transparent_border_tool();
        ImGui::Separator();
        if (mk2_tool_header("Palette Builder"))
            draw_mk2_palette_builder_tool();
        break;

    case MK2_WF_FX:
        if (mk2_tool_header("Danger Palette Designer", true))
            draw_mk2_danger_palette_designer();
        ImGui::Separator();
        if (mk2_tool_header("Stage FX Builder"))
            draw_mk2_stage_fx_builder_tool();
        ImGui::Separator();
        if (mk2_tool_header("Stage Layer Role Editor"))
            draw_mk2_stage_layer_role_editor();
        ImGui::Separator();
        if (mk2_tool_header("Camera Bookmarks"))
            draw_mk2_camera_bookmarks();
        ImGui::Separator();
        if (mk2_tool_header("Pan Coverage Scanner"))
            draw_mk2_pan_coverage_scanner();
        ImGui::Separator();
        if (mk2_tool_header("Animation Planner"))
            draw_mk2_animation_planner_tool();
        ImGui::Separator();
        if (mk2_tool_header("Stage Template Wizard"))
            draw_mk2_template_wizard_tool();
        break;

    case MK2_WF_FINISH:
        if (mk2_tool_header("Stage Package Handoff", true))
            draw_mk2_stage_handoff_exports();
        ImGui::Separator();
        if (mk2_tool_header("One-Click Validation Run"))
            draw_mk2_one_click_validation_run();
        ImGui::Separator();
        if (mk2_tool_header("Patch Recipe Import"))
            draw_mk2_patch_recipe_import_tool();
        ImGui::Separator();
        if (mk2_tool_header("Export / Save"))
        {
            if (ImGui::Button("Validate", ImVec2(-1, 0)))
                g_show_verify = true;
            if (ImGui::Button("Save All", ImVec2(-1, 0)))
                save_all_project();
            if (!g_simple_mode) {
                if (ImGui::Button("Export Assembly Skeleton", ImVec2(-1, 0)))
                    export_mk2_assembly();
            }
        }
        break;
    }

    ImGui::Separator();
    ImGui::Text("Objects: %d   Images: %d   Palettes: %d", g_no, g_ni, g_n_pals);
    g_mk2_focus_tool = 0;
    ImGui::End();
}
