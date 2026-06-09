#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>

/* Render one actionable line for the "What to fix & how" section: a colored
   problem statement plus an indented, plain-English remedy that names the tool
   or button that resolves it. Nothing draws when the issue is not present. */
static void mk2_doctor_fix(bool show, bool blocking, const char *problem, const char *how)
{
    if (!show) return;
    ImGui::TextColored(blocking ? ImVec4(1.0f, 0.45f, 0.35f, 1) : ImVec4(1.0f, 0.78f, 0.35f, 1),
                       "%s %s", blocking ? "Fix:" : "Check:", problem);
    ImGui::Indent();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("How: %s", how);
    ImGui::PopStyleColor();
    ImGui::Unindent();
}

void draw_mk2_load2_doctor_tool(void)
{
    Mk2Diag d;
    mk2_collect_diag(&d);

    int hard_issues = mk2_diag_hard_issues(&d);
    int cautions = mk2_diag_cautions(&d);

    ImGui::Text("LOAD2 Doctor");
    if (hard_issues == 0 && cautions == 0)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Clean for current BDB/BDD authoring rules.");
    else if (hard_issues == 0)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "%d caution(s), no blocking issue.", cautions);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "%d blocking issue(s), %d caution(s).", hard_issues, cautions);

    ImGui::Columns(2, "mk2_doctor_cols_compact", false);
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
    ImGui::Text("LOAD2 oversize"); ImGui::NextColumn(); ImGui::Text("%d", d.load2_oversize_images); ImGui::NextColumn();
    ImGui::Text("Palette >= 16"); ImGui::NextColumn(); ImGui::Text("%d", d.palette_high_nibble); ImGui::NextColumn();
    ImGui::Text("Runtime palettes"); ImGui::NextColumn();
    ImGui::Text("%d used, max module %d", d.runtime_palette_count, d.max_module_palettes);
    ImGui::NextColumn();
    ImGui::Text("Visible objects"); ImGui::NextColumn();
    ImGui::Text("%d / %d at X %d", d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP,
                d.max_visible_objects_x);
    ImGui::NextColumn();
    ImGui::Text("High-color images"); ImGui::NextColumn(); ImGui::Text("%d", d.high_color_images); ImGui::NextColumn();
    ImGui::Text("Narrow images"); ImGui::NextColumn();
    ImGui::Text("%d padded, %d no-zero-compress",
                d.load2_narrow_padded_images, d.load2_zero_compress_disabled);
    ImGui::NextColumn();
    ImGui::Text("X-order cautions"); ImGui::NextColumn(); ImGui::Text("%d", d.order_issues); ImGui::NextColumn();
    ImGui::Columns(1);

    /* Translate the raw counts above into concrete remedies. Only issues that
       lack a dedicated fix button further down get a hint here, so the section
       stays focused on "what do I actually do about this". */
    bool any_guided = d.missing_images > 0 || d.bad_palettes > 0 ||
                      d.module_bound_issues > 0 || d.old_style_bounds > 0 ||
                      d.load2_oversize_images > 0 || d.palette_high_nibble > 0 ||
                      d.high_color_images > 0 || d.load2_narrow_padded_images > 0 ||
                      d.order_issues > 0;
    if (any_guided) {
        ImGui::SeparatorText("What to fix & how");
        mk2_doctor_fix(d.missing_images > 0, true,
            "Some objects point at image IDs that are not in the BDD.",
            "Re-import the source IMG/LOD art (MK2 Workflow > Import), or open the Images panel, filter to the missing art, and delete the orphan objects.");
        mk2_doctor_fix(d.bad_palettes > 0, true,
            "One or more palettes have an invalid color count.",
            "Open the Palette panel and set each flagged palette to 1-256 colors, then click 'Sync Header Counts' below.");
        mk2_doctor_fix(d.module_bound_issues > 0 || d.old_style_bounds > 0, true,
            "Module bounds are malformed or use the old single-point style.",
            "Click 'Fit Module Bounds to Objects' below, or edit each bound in the Modules panel into a full TSTMOD rectangle.");
        mk2_doctor_fix(d.load2_oversize_images > 0, true,
            "At least one block is larger than the LOAD2 byte budget.",
            "Shrink the sprite (Optimize > Trim Transparent Border) or lower its color depth (Optimize > Selected BPP Reducer); split very large art across blocks.");
        mk2_doctor_fix(d.palette_high_nibble > 0, false,
            "Palette indices >= 16 are used where MK2 expects a low nibble.",
            "Remap the art onto a compact palette with Optimize > Palette Remap / Compress, then re-run this check.");
        mk2_doctor_fix(d.high_color_images > 0, false,
            "Some images use more colors than the LOAD2 target depth.",
            "Reduce the color count with Optimize > Selected BPP Reducer or Smart Palette Grouper.");
        mk2_doctor_fix(d.load2_narrow_padded_images > 0, false,
            "Images narrower than 3px are padded by LOAD2.",
            "Widen the art to 3px or more, or accept the automatic padding if the on-screen result is unaffected.");
        mk2_doctor_fix(d.order_issues > 0, false,
            "Object draw order is not X-major, which LOAD2 expects.",
            "Click 'Sort Objects X-Major for LOAD2' below to reorder every object automatically.");
        ImGui::Spacing();
    }

    if (d.load2_palette_overflow || d.load2_module_overflow ||
        d.load2_image_header_overflow || d.load2_block_table_overflow) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1),
                           "LOAD2 file cap exceeded: %d palette, %d module, %d image-header, %d block over.",
                           d.load2_palette_overflow, d.load2_module_overflow,
                           d.load2_image_header_overflow, d.load2_block_table_overflow);
    }
    if (d.display_object_overflow || d.display_object_pressure) {
        ImGui::TextColored(d.display_object_overflow ? ImVec4(1.0f, 0.35f, 0.25f, 1) : ImVec4(1.0f, 0.65f, 0.25f, 1),
                           "Sampled background draw pressure: %d / %d objects at X %d before fighters/effects/UI.",
                           d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP, d.max_visible_objects_x);
        if (ImGui::Button("Jump To Object Pressure X", ImVec2(-1, 0))) {
            g_scroll_pos = d.max_visible_objects_x;
            g_view_x = d.max_visible_objects_x;
            g_view_changed = 1;
            stage_set_toast("Jumped to worst object pressure point");
        }
    }
    if (d.runtime_palette16_pressure > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1),
                           "Hardware has %d simultaneous palette slots; current BDD uses %d, max module %d.",
                           MK2_RUNTIME_PALETTE_SLOTS, d.runtime_palette_count, d.max_module_palettes);
    }
    if (d.runtime_palette_pressure > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1),
                           "MK2 background dynamic palette table budget is %d; pack or merge palettes before packaging.",
                           MK2_BG_DYNAMIC_PALETTE_SLOTS);
        if (d.max_module_palettes > MK2_BG_DYNAMIC_PALETTE_SLOTS && d.max_module_palette_name[0])
            ImGui::TextDisabled("Highest module: %s (%d palette(s))",
                                d.max_module_palette_name, d.max_module_palettes);
    }
    if (d.unassigned_objects > 0) {
        int first_outside = mk2_first_unassigned_object();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1),
                           "LOAD2 issue: %d object(s) are outside every module.",
                           d.unassigned_objects);
        if (first_outside >= 0) {
            Img *fim = img_find(g_obj[first_outside].ii);
            ImGui::TextDisabled("First: object %d at (%d,%d), image 0x%04X %dx%d",
                                first_outside, g_obj[first_outside].depth, g_obj[first_outside].sy,
                                g_obj[first_outside].ii, fim ? fim->w : 0, fim ? fim->h : 0);
        }
        ImGui::TextWrapped("LOAD2 only assigns objects whose full sprite rectangle fits inside a module bound.");
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
