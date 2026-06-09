#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>

bool g_validation_write_manifest = true;
bool g_validation_write_recipe = true;
bool g_validation_export_composite = true;
bool g_validation_safe_fixes = true;

void draw_mk2_one_click_validation_run(void)
{
    ImGui::Text("One-Click Validation Run");
    ImGui::TextDisabled("Run the non-MAME local checks/exports in the usual final-stage order.");
    ImGui::Checkbox("Safe fixes", &g_validation_safe_fixes);
    ImGui::SameLine();
    ImGui::Checkbox("Composite", &g_validation_export_composite);
    ImGui::Checkbox("Manifest", &g_validation_write_manifest);
    ImGui::SameLine();
    ImGui::Checkbox("Recipe", &g_validation_write_recipe);
    if (ImGui::Button("Run Local Validation + Exports", ImVec2(-1, 0))) {
        int fit = 0, sort = 0;
        if (g_validation_safe_fixes) {
            fit = mk2_fit_module_bounds_to_objects();
            sort = mk2_sort_objects_x_major();
            sync_bdb_header_counts();
        }
        stage_write_config();
        if (g_validation_export_composite) export_composite_png();
        bool manifest_ok = !g_validation_write_manifest || stage_write_package_manifest();
        bool recipe_ok = !g_validation_write_recipe || stage_write_patch_recipe();
        PanCoverageSummary ps = mk2_compute_pan_summary();
        char msg[160];
        snprintf(msg, sizeof msg, "Validation: bounds %d order %d pan %.0f/%.0f/%.0f %s",
                 fit, sort, ps.full, ps.top, ps.floor,
                 (manifest_ok && recipe_ok) ? "ok" : "export issue");
        stage_set_toast(msg);
    }
}
