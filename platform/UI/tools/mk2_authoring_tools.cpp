#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <cstdio>
void draw_mk2_authoring_tools(void)
{
    Mk2Diag d;
    mk2_collect_diag(&d);

    int hard_issues = mk2_diag_hard_issues(&d);
    int cautions = mk2_diag_cautions(&d);

    if (g_simple_mode) {
        ImGui::Text("Stage Check");
        if (hard_issues == 0 && cautions == 0)
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "All checks passed.");
        else if (hard_issues == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "%d warning(s), no blocking issue.", cautions);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "%d issue(s) need attention.", hard_issues);
    } else {
        ImGui::Text("LOAD2 Doctor");
        if (hard_issues == 0 && cautions == 0)
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Clean for current BDB/BDD authoring rules.");
        else if (hard_issues == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "%d caution(s), no blocking issue.", cautions);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "%d blocking issue(s), %d caution(s).", hard_issues, cautions);

        ImGui::Columns(2, "mk2_doctor_cols", false);
        ImGui::Text("Missing images"); ImGui::NextColumn(); ImGui::Text("%d", d.missing_images); ImGui::NextColumn();
        ImGui::Text("Bad palettes"); ImGui::NextColumn(); ImGui::Text("%d", d.bad_palettes); ImGui::NextColumn();
        ImGui::Text("Unassigned objects"); ImGui::NextColumn(); ImGui::Text("%d", d.unassigned_objects); ImGui::NextColumn();
        ImGui::Text("Module bounds"); ImGui::NextColumn(); ImGui::Text("%d bad, %d old-style", d.module_bound_issues, d.old_style_bounds); ImGui::NextColumn();
        ImGui::Text("LOAD2 file caps"); ImGui::NextColumn();
        ImGui::Text("%d pal, %d mod, %d img", g_n_pals, g_bdb_num_modules, g_ni);
        ImGui::NextColumn();
        ImGui::Text("LOAD2 block max"); ImGui::NextColumn();
        ImGui::Text("%d / %d bytes (%dbpp)",
                    d.max_load2_block_bytes, MK2_LOAD2_MAX_DATA_BYTES, d.max_load2_block_bpp);
        ImGui::NextColumn();
        ImGui::Text("LOAD2 oversize images"); ImGui::NextColumn(); ImGui::Text("%d", d.load2_oversize_images); ImGui::NextColumn();
        ImGui::Text("Palette >= 16 objects"); ImGui::NextColumn(); ImGui::Text("%d", d.palette_high_nibble); ImGui::NextColumn();
        ImGui::Text("Runtime palettes"); ImGui::NextColumn();
        ImGui::Text("%d used, max module %d", d.runtime_palette_count, d.max_module_palettes);
        ImGui::NextColumn();
        ImGui::Text("Visible objects"); ImGui::NextColumn();
        ImGui::Text("%d / %d at X %d", d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP,
                    d.max_visible_objects_x);
        ImGui::NextColumn();
        ImGui::Text("High-color images"); ImGui::NextColumn(); ImGui::Text("%d", d.high_color_images); ImGui::NextColumn();
        ImGui::Text("X-order cautions"); ImGui::NextColumn(); ImGui::Text("%d", d.order_issues); ImGui::NextColumn();
        ImGui::Columns(1);
        ImGui::TextDisabled("Palette >=16 is valid now; this confirms high-nibble BLKS encoding is required.");
        ImGui::TextDisabled("LOAD2 caps: 256 palettes, 100 modules, 400 image headers, 2500 blocks, 65500 bytes/block.");
        ImGui::TextDisabled("Runtime watch: 16 hardware palette slots and 358 total display objects are shared with fighters/effects/UI.");

        if (d.unassigned_objects > 0) {
            int first_outside = mk2_first_unassigned_object();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1),
                               "LOAD2 issue: %d object(s) are outside every module.",
                               d.unassigned_objects);
            if (first_outside >= 0) {
                Img *fim = img_find(g_obj[first_outside].ii);
                ImGui::TextDisabled("First: object %d at (%d,%d), image 0x%04X %dx%d",
                                    first_outside, g_obj[first_outside].depth, g_obj[first_outside].sy,
                                    g_obj[first_outside].ii, fim ? fim->w : 0, fim ? fim->h : 0);
            }
            ImGui::TextWrapped("LOAD2 assigns an object only when the full sprite rectangle fits inside a module bound. Outside objects can disappear or land in the wrong packed module.");
            if (ImGui::Button("Select Outside-Module Objects", ImVec2(-1, 0))) {
                int n = mk2_select_unassigned_objects();
                char msg[96];
                snprintf(msg, sizeof msg, "Selected %d outside-module object(s)", n);
                stage_set_toast(msg);
            }
            if (ImGui::Button("Include Outside Objects In Nearest Module", ImVec2(-1, 0))) {
                int n = mk2_include_unassigned_objects_in_modules();
                char msg[128];
                snprintf(msg, sizeof msg,
                         n ? "Expanded %d module bound(s)" : "No module bounds needed expansion",
                         n);
                stage_set_toast(msg);
            }
            if (ImGui::Button("Delete Outside Objects", ImVec2(-1, 0))) {
                int n = mk2_delete_unassigned_objects();
                mk2_toast_outside_delete_result(n);
            }
        }

        if (g_bdb_num_modules == 0) {
            if (ImGui::Button("Create Default Module", ImVec2(-1, 0))) {
                if (mk2_create_default_module())
                    snprintf(g_toast_msg, sizeof g_toast_msg, "Created inclusive TSTMOD bounds");
                else
                    snprintf(g_toast_msg, sizeof g_toast_msg, "Could not create module");
                g_toast_timer = 3.0f;
            }
        }
        if (ImGui::Button("Fit Module Bounds to Objects", ImVec2(-1, 0))) {
            int changed = mk2_fit_module_bounds_to_objects();
            snprintf(g_toast_msg, sizeof g_toast_msg,
                     changed ? "Fit %d module bound(s)" : "Module bounds already fit", changed);
            g_toast_timer = 3.0f;
        }
        if (ImGui::Button("Sort Objects X-Major for LOAD2", ImVec2(-1, 0))) {
            int changed = mk2_sort_objects_x_major();
            snprintf(g_toast_msg, sizeof g_toast_msg,
                     changed ? "Updated %d object order value(s)" : "Object order already X-major", changed);
            g_toast_timer = 3.0f;
        }
        if (ImGui::Button("Sync Header Counts", ImVec2(-1, 0))) {
            undo_save();
            sync_bdb_header_counts();
            snprintf(g_toast_msg, sizeof g_toast_msg, "Synced BDB header counts");
            g_toast_timer = 3.0f;
        }
    }

    ImGui::Separator();
    if (g_mk2_focus_tool == 5) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::CollapsingHeader("Stage Readiness Gate", ImGuiTreeNodeFlags_DefaultOpen))
        draw_mk2_stage_readiness_gate();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stage Package Handoff", ImGuiTreeNodeFlags_DefaultOpen))
        draw_mk2_stage_handoff_exports();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("One-Click Validation Run"))
        draw_mk2_one_click_validation_run();

    if (!g_simple_mode) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Runtime Extras", ImGuiTreeNodeFlags_DefaultOpen))
            draw_mk2_runtime_extras_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Patch Recipe Import"))
            draw_mk2_patch_recipe_import_tool();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stage Preview Dashboard"))
        draw_mk2_stage_preview_dashboard();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stock Stage Asset Explorer", ImGuiTreeNodeFlags_DefaultOpen))
        draw_mk2_asset_explorer();

    if (!g_simple_mode) {
        ImGui::Separator();
        draw_mk2_unused_asset_tools();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Layer Stack Inspector", ImGuiTreeNodeFlags_DefaultOpen))
            draw_mk2_layer_stack_inspector();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Stage Repair Mode"))
            draw_mk2_stage_repair_mode();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Auto Repair Suggestions"))
            draw_mk2_auto_repair_suggestions();

        ImGui::Separator();
        if (g_mk2_focus_tool == 9) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (ImGui::CollapsingHeader("ROM Preview Diff"))
            draw_mk2_preview_diff_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Palette Seam Detector"))
            draw_mk2_palette_seam_detector();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Uppercut Headroom Preview"))
            draw_mk2_uppercut_headroom_preview();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Parallax Sanity Checker"))
            draw_mk2_parallax_sanity_checker();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Transparent Color Advisor"))
            draw_mk2_transparency_advisor();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Foreground Occlusion Preview"))
            draw_mk2_foreground_occlusion_preview();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Mirrored Asset Tool"))
        draw_mk2_mirrored_asset_tool();

    if (!g_simple_mode) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Danger Palette Designer"))
            draw_mk2_danger_palette_designer();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stage FX Builder", ImGuiTreeNodeFlags_DefaultOpen))
        draw_mk2_stage_fx_builder_tool();

    if (!g_simple_mode) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Stage Layer Role Editor"))
            draw_mk2_stage_layer_role_editor();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera Bookmarks"))
        draw_mk2_camera_bookmarks();

    if (!g_simple_mode) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Pan Coverage Scanner"))
            draw_mk2_pan_coverage_scanner();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Duplicate / Mirror Finder"))
            draw_mk2_duplicate_mirror_finder();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Safe Dedup Assistant"))
            draw_mk2_safe_dedup_assistant();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Selected BPP Reducer"))
            draw_mk2_selected_bpp_reducer_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Small Artifact Finder"))
            draw_mk2_small_artifact_finder();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Batch Image Cleanup"))
            draw_mk2_batch_image_cleanup_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Palette Usage Optimizer"))
            draw_mk2_palette_usage_optimizer();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Smart Palette Grouper"))
            draw_mk2_smart_palette_grouper();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Palette Blend / Merge"))
            draw_palette_blend_merge_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Palette Remap / Compress"))
            draw_mk2_palette_remap_compress_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Trim Transparent Border"))
            draw_mk2_trim_transparent_border_tool();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Palette Builder"))
            draw_mk2_palette_builder_tool();
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Animation Planner"))
        draw_mk2_animation_planner_tool();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stage Template Wizard"))
        draw_mk2_template_wizard_tool();
}

