#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>

static void draw_gate_row(const char *name, bool pass, const char *detail, const char *how = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(pass ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                       "%s", pass ? "PASS" : "FIX");
    ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(detail);
    /* Failing rows get a hover hint that explains how to clear the check, so the
       gate tells you what to do, not just that something is wrong. */
    if (!pass && how && how[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(how);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

void draw_mk2_stage_readiness_gate(void)
{
    ImGui::Text("Stage Readiness Gate");
    ImGui::TextDisabled("One-stop preflight before package/MAME: LOAD2, pan coverage, payload, palettes, and savings.");
    ImGui::InputInt("Payload Limit", &g_gate_payload_limit);
    ImGui::InputInt("Min Full Coverage", &g_gate_min_full_coverage);
    ImGui::InputInt("Min Top Coverage", &g_gate_min_top_coverage);
    ImGui::InputInt("Min Floor Coverage", &g_gate_min_floor_coverage);
    ImGui::Checkbox("Block on high-color images", &g_gate_block_on_high_color);
    if (g_gate_payload_limit < 0) g_gate_payload_limit = 0;
    if (g_gate_min_full_coverage < 0) g_gate_min_full_coverage = 0;
    if (g_gate_min_top_coverage < 0) g_gate_min_top_coverage = 0;
    if (g_gate_min_floor_coverage < 0) g_gate_min_floor_coverage = 0;
    if (g_gate_min_full_coverage > 100) g_gate_min_full_coverage = 100;
    if (g_gate_min_top_coverage > 100) g_gate_min_top_coverage = 100;
    if (g_gate_min_floor_coverage > 100) g_gate_min_floor_coverage = 100;

    if (!mk2_has_drawable_stage()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "No drawable BDB/BDD loaded.");
        ImGui::TextDisabled("Open or generate a stage with objects and images before package readiness checks.");
        if (ImGui::Button("Copy Readiness Report", ImVec2(-1, 0))) {
            char report[512];
            mk2_readiness_report(report, sizeof report);
            ImGui::SetClipboardText(report);
            stage_set_toast("Copied readiness report");
        }
        return;
    }

    Mk2Diag d;
    mk2_collect_diag(&d);
    Mk2Budget b = mk2_collect_budget();
    PanCoverageSummary ps = mk2_compute_pan_summary();
    size_t dup_savings = mk2_estimate_duplicate_savings();
    int hard = mk2_diag_hard_issues(&d);
    bool load2_ok = hard == 0;
    bool order_ok = d.order_issues == 0;
    bool budget_ok = b.estimated_payload <= (size_t)g_gate_payload_limit;
    bool full_ok = ps.full >= (float)g_gate_min_full_coverage;
    bool top_ok = ps.top >= (float)g_gate_min_top_coverage;
    bool floor_ok = ps.floor >= (float)g_gate_min_floor_coverage;
    bool color_ok = !g_gate_block_on_high_color || b.high_color_images == 0;
    bool ready = load2_ok && budget_ok && full_ok && top_ok && floor_ok && color_ok;
    bool clean = ready && order_ok && dup_savings == 0;

    if (clean)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "READY: clean to package.");
    else if (ready)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "READY WITH NOTES: packageable, but polish remains.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "BLOCKED: fix red rows before packaging.");
    ImGui::TextDisabled("Pan coverage samples BDB/BDD placements only; stock runtime extras are not counted here yet.");

    if (ImGui::BeginTable("readiness_gate", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("check", ImGuiTableColumnFlags_WidthFixed, 104.0f);
        ImGui::TableSetupColumn("detail");
        ImGui::TableHeadersRow();
        char detail[160];
        if (d.unassigned_objects > 0)
            snprintf(detail, sizeof detail, "%d outside-module object(s)", d.unassigned_objects);
        else if (d.missing_images > 0)
            snprintf(detail, sizeof detail, "%d missing image reference(s)", d.missing_images);
        else if (d.bad_palettes > 0)
            snprintf(detail, sizeof detail, "%d bad palette reference(s)", d.bad_palettes);
        else if (d.module_bound_issues > 0)
            snprintf(detail, sizeof detail, "%d bad module bound(s)", d.module_bound_issues);
        else if (d.load2_oversize_images > 0)
            snprintf(detail, sizeof detail, "%d image block(s) over %d bytes",
                     d.load2_oversize_images, MK2_LOAD2_MAX_DATA_BYTES);
        else
            snprintf(detail, sizeof detail, "%d hard issue(s)", hard);
        draw_gate_row("LOAD2", load2_ok, detail,
            "Open LOAD2 Doctor (Check section) and work its 'What to fix & how' list: missing art, bad palettes, module bounds, or oversize blocks.");
        bool load2_caps_ok = d.load2_palette_overflow == 0 &&
                             d.load2_module_overflow == 0 &&
                             d.load2_image_header_overflow == 0 &&
                             d.load2_block_table_overflow == 0 &&
                             d.load2_oversize_images == 0;
        snprintf(detail, sizeof detail, "%d/%d pal, %d/%d mod, %d/%d img, max %d/%d bytes",
                 g_n_pals, MK2_LOAD2_MAX_STAGE_PALETTES,
                 g_bdb_num_modules, MK2_LOAD2_MAX_MODULES,
                 g_ni, MK2_LOAD2_MAX_IMAGE_HEADERS,
                 d.max_load2_block_bytes, MK2_LOAD2_MAX_DATA_BYTES);
        draw_gate_row("LOAD2 caps", load2_caps_ok, detail,
            "A LOAD2 file table is full. Merge palettes (Optimize > Smart Palette Grouper), drop unused images, or shrink oversize art (Optimize > Selected BPP Reducer).");
        bool display_ok = d.display_object_overflow == 0;
        snprintf(detail, sizeof detail, "%d/%d at X %d before fighters/effects/UI",
                 d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP, d.max_visible_objects_x);
        draw_gate_row("Display objs", display_ok, detail,
            "Too many background objects are visible at once near this X. Thin out or merge objects there, or push some onto other layers.");
        snprintf(detail, sizeof detail, "%d X-order caution(s)", d.order_issues);
        draw_gate_row("Object order", order_ok, detail,
            "Draw order is not X-major. Click 'Run Safe Fixes' below, or 'Sort Objects X-Major for LOAD2' in the LOAD2 Doctor.");
        snprintf(detail, sizeof detail, "0x%zX / 0x%X", b.estimated_payload, g_gate_payload_limit);
        draw_gate_row("ROM budget", budget_ok, detail,
            "Payload exceeds the limit. Use the budget relief suggestions shown below, dedupe mirrored art (Optimize > Duplicate / Mirror Finder), or reduce color depth.");
        snprintf(detail, sizeof detail, "full %.1f%% at worst X %d", ps.full, ps.worst_x);
        draw_gate_row("Pan full", full_ok, detail,
            "A pan position is under-covered. Extend background art across the worst X ('Use Worst Pan X As Preview' below), or lower the threshold if the gap is intentional.");
        snprintf(detail, sizeof detail, "top %.1f%%", ps.top);
        draw_gate_row("Pan top", top_ok, detail,
            "Top-of-screen coverage is below target. Add upper-layer art, or lower 'Min Top Coverage' if intentional.");
        snprintf(detail, sizeof detail, "floor %.1f%%", ps.floor);
        draw_gate_row("Pan floor", floor_ok, detail,
            "Floor coverage is below target. Add floor/lower-layer art, or lower 'Min Floor Coverage' if intentional.");
        snprintf(detail, sizeof detail, "%d high-color image(s)", b.high_color_images);
        draw_gate_row("Color cost", color_ok, detail,
            "High-color images inflate ROM. Reduce with Optimize > Selected BPP Reducer or Smart Palette Grouper, or uncheck 'Block on high-color images'.");
        snprintf(detail, sizeof detail, "possible raw savings 0x%zX", dup_savings);
        draw_gate_row("Duplicate art", dup_savings == 0, detail,
            "Identical or mirrored art can be shared. Use Optimize > Duplicate / Mirror Finder, then the Safe Dedup Assistant.");
        ImGui::EndTable();
    }

    if (!budget_ok)
        draw_mk2_budget_relief_suggestions(&b, g_gate_payload_limit);

    if (d.unassigned_objects > 0) {
        ImGui::TextWrapped("LOAD2 detail: outside-module objects are not fully covered by any BDB module, so LOAD2 cannot assign them cleanly to a packed module.");
        if (ImGui::Button("Select Outside-Module Objects##gate", ImVec2(-1, 0))) {
            int n = mk2_select_unassigned_objects();
            char msg[96];
            snprintf(msg, sizeof msg, "Selected %d outside-module object(s)", n);
            stage_set_toast(msg);
        }
        if (ImGui::Button("Include Outside Objects In Nearest Module##gate", ImVec2(-1, 0))) {
            int n = mk2_include_unassigned_objects_in_modules();
            char msg[128];
            snprintf(msg, sizeof msg,
                     n ? "Expanded %d module bound(s)" : "No module bounds needed expansion",
                     n);
            stage_set_toast(msg);
        }
        if (ImGui::Button("Delete Outside Objects##gate", ImVec2(-1, 0))) {
            int n = mk2_delete_unassigned_objects();
            mk2_toast_outside_delete_result(n);
        }
    }

    if (ImGui::Button("Run Safe Fixes", ImVec2(-1, 0))) {
        int included = mk2_include_unassigned_objects_in_modules();
        int fit = mk2_fit_module_bounds_to_objects();
        int sort = mk2_sort_objects_x_major();
        sync_bdb_header_counts();
        char msg[128];
        snprintf(msg, sizeof msg, "Safe fixes: %d outside, %d bounds, %d order updates",
                 included, fit, sort);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Use Worst Pan X As Preview", ImVec2(-1, 0))) {
        g_stage_preview_worldx = ps.worst_x;
        stage_set_toast("Preview X set to readiness worst point");
    }
    if (ImGui::Button("Copy Readiness Report", ImVec2(-1, 0))) {
        char report[2048];
        mk2_readiness_report(report, sizeof report);
        ImGui::SetClipboardText(report);
        stage_set_toast("Copied readiness report");
    }
}
