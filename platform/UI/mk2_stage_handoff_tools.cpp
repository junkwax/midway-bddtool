#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>

char g_stage_manifest_path[512] = "stage_manifest.md";
char g_stage_patch_recipe_path[512] = "stage_patch_recipe.json";
bool g_stage_manifest_include_readiness = true;
bool g_stage_manifest_include_bookmarks = true;
bool g_stage_manifest_include_commands = true;

void draw_mk2_stage_handoff_exports(void)
{
    ImGui::Text("Stage Package Handoff");
    ImGui::TextDisabled("Generate reproducible handoff files for humans and mk2-main scripts.");
    ImGui::InputText("Manifest MD", g_stage_manifest_path, sizeof g_stage_manifest_path);
    ImGui::InputText("Patch Recipe JSON", g_stage_patch_recipe_path, sizeof g_stage_patch_recipe_path);
    ImGui::Checkbox("Manifest readiness", &g_stage_manifest_include_readiness);
    ImGui::SameLine();
    ImGui::Checkbox("bookmarks", &g_stage_manifest_include_bookmarks);
    ImGui::SameLine();
    ImGui::Checkbox("commands", &g_stage_manifest_include_commands);

    if (ImGui::Button("Write Package Manifest", ImVec2(-1, 0)))
        stage_write_package_manifest();
    if (ImGui::Button("Write Patch Recipe", ImVec2(-1, 0)))
        stage_write_patch_recipe();
    if (ImGui::Button("Write Both Handoff Files", ImVec2(-1, 0))) {
        bool a = stage_write_package_manifest();
        bool b = stage_write_patch_recipe();
        stage_set_toast((a && b) ? "Wrote manifest and recipe" : "Handoff export had errors");
    }

    char manifest[512], recipe[512];
    resolve_stage_file(manifest, sizeof manifest, g_stage_manifest_path);
    resolve_stage_file(recipe, sizeof recipe, g_stage_patch_recipe_path);
    ImGui::TextDisabled("Manifest: %s", manifest);
    ImGui::TextDisabled("Recipe: %s", recipe);
    if (ImGui::Button("Copy Handoff Paths", ImVec2(-1, 0))) {
        char paths[1200];
        snprintf(paths, sizeof paths, "%s\n%s", manifest, recipe);
        ImGui::SetClipboardText(paths);
        stage_set_toast("Copied handoff paths");
    }
}

void draw_mk2_patch_recipe_import_tool(void)
{
    ImGui::Text("Patch Recipe Import");
    ImGui::TextDisabled("Load a handoff recipe back into Stage Kit fields.");
    draw_path_field("Recipe JSON", g_stage_patch_recipe_path, sizeof g_stage_patch_recipe_path,
                    "Open Patch Recipe", "JSON Files\0*.json\0All Files\0*.*\0");
    if (ImGui::Button("Import Patch Recipe", ImVec2(-1, 0)))
        stage_import_patch_recipe();
}
