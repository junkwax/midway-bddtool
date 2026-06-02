#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

static void draw_palette_strip_preview(const char *label, const Uint32 *pal, int count)
{
    ImGui::TextDisabled("%s", label);
    int shown = count < 32 ? count : 32;
    for (int i = 0; i < shown; i++) {
        if (i) ImGui::SameLine(0, 2);
        Uint32 c = pal ? pal[i] : 0;
        ImGui::PushID(label);
        ImGui::PushID(i);
        ImGui::ColorButton("##blendstrip",
            ImVec4(((c >> 16) & 0xFF) / 255.0f,
                   ((c >> 8) & 0xFF) / 255.0f,
                   (c & 0xFF) / 255.0f,
                   i == 0 ? 0.25f : 1.0f),
            ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d #%02X%02X%02X", i,
                              (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        ImGui::PopID();
        ImGui::PopID();
    }
}

void draw_palette_blend_merge_tool(void)
{
    if (g_n_pals < 2) {
        ImGui::TextDisabled("Need at least two palettes.");
        return;
    }
    if (g_palette_blend_a < 0 || g_palette_blend_a >= g_n_pals) g_palette_blend_a = 0;
    if (g_palette_blend_b < 0 || g_palette_blend_b >= g_n_pals) g_palette_blend_b = g_n_pals > 1 ? 1 : 0;
    if (g_palette_blend_b == g_palette_blend_a && g_n_pals > 1)
        g_palette_blend_b = (g_palette_blend_a + 1) % g_n_pals;

    auto palette_combo = [](const char *label, int *idx) {
        char preview[96];
        snprintf(preview, sizeof preview, "%d: %s", *idx, g_pal_name[*idx]);
        if (ImGui::BeginCombo(label, preview)) {
            for (int p = 0; p < g_n_pals; p++) {
                char row[96];
                snprintf(row, sizeof row, "%d: %s", p, g_pal_name[p]);
                if (ImGui::Selectable(row, p == *idx)) *idx = p;
            }
            ImGui::EndCombo();
        }
    };

    palette_combo("Palette A", &g_palette_blend_a);
    palette_combo("Palette B", &g_palette_blend_b);
    ImGui::SliderInt("Blend strength", &g_palette_blend_strength, 0, 100, "%d%%");
    const char *mode_label = g_palette_blend_mode == 0 ? "A toward B" :
                             g_palette_blend_mode == 1 ? "B toward A" : "Both toward each other";
    if (ImGui::BeginCombo("Blend mode", mode_label)) {
        if (ImGui::Selectable("A toward B", g_palette_blend_mode == 0)) g_palette_blend_mode = 0;
        if (ImGui::Selectable("B toward A", g_palette_blend_mode == 1)) g_palette_blend_mode = 1;
        if (ImGui::Selectable("Both toward each other", g_palette_blend_mode == 2)) g_palette_blend_mode = 2;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Create replacement palettes", &g_palette_blend_create_new);

    Uint32 preview_a[256], preview_b[256];
    build_blended_palette(g_pal_count[g_palette_blend_a], g_palette_blend_a, g_palette_blend_b,
                          g_palette_blend_strength, false, preview_a);
    build_blended_palette(g_pal_count[g_palette_blend_b], g_palette_blend_a, g_palette_blend_b,
                          g_palette_blend_strength, true, preview_b);
    draw_palette_strip_preview("A", g_pals[g_palette_blend_a], g_pal_count[g_palette_blend_a]);
    draw_palette_strip_preview("B", g_pals[g_palette_blend_b], g_pal_count[g_palette_blend_b]);
    if (g_palette_blend_mode != 1)
        draw_palette_strip_preview("A blended", preview_a, g_pal_count[g_palette_blend_a]);
    if (g_palette_blend_mode != 0)
        draw_palette_strip_preview("B blended", preview_b, g_pal_count[g_palette_blend_b]);

    if (ImGui::Button("Apply Palette Blend", ImVec2(-1, 0))) {
        int rc = apply_palette_blend_tool();
        if (rc == -2) stage_set_toast("Blend refused: not enough palette slots");
        else if (rc < 0) stage_set_toast("Blend refused: choose two valid palettes");
    }
    ImGui::Separator();
    ImGui::Checkbox("Union uses only", &g_palette_merge_used_only);
    if (ImGui::Button("Merge A+B to Shared Palette", ImVec2(-1, 0))) {
        int rc = apply_palette_union_merge_tool();
        if (rc == -3) stage_set_toast(g_palette_blend_status);
        else if (rc == -2) stage_set_toast("Merge refused: no free palette slots");
        else if (rc < 0) stage_set_toast("Merge refused: choose two valid palettes");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Creates one union palette from A/B colors and remaps images whose own palette is A or B.");
    if (g_palette_blend_status[0])
        ImGui::TextWrapped("%s", g_palette_blend_status);
}
