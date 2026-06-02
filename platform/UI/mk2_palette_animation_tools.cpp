#include "bg_editor_globals.h"

#include "imgui.h"

#include <stddef.h>

void draw_mk2_palette_builder_tool(void)
{
    Mk2Budget b = mk2_collect_budget();
    ImGui::Text("Palette Builder");
    ImGui::TextDisabled("Stage Kit presets for quality/cost experiments. Rebuild after changing these.");
    ImGui::Text("Visible colors: %d  BG split: %s  high-color images: %d",
                g_stage_visible_colors, stage_palette_mode_name(), b.high_color_images);
    if (ImGui::Button("Preset: Space Saver 15-color Portal Core", ImVec2(-1, 0))) {
        g_stage_visible_colors = 15;
        g_stage_bg_palette_mode = 0;
        g_stage_bg_cols = 8;
        g_stage_bg_rows = 1;
        stage_set_toast("Applied space-saver palette preset");
    }
    if (ImGui::Button("Preset: Stock-like 63-color Tiles", ImVec2(-1, 0))) {
        g_stage_visible_colors = 63;
        g_stage_bg_palette_mode = 1;
        g_stage_bg_cols = 8;
        g_stage_bg_rows = 2;
        stage_set_toast("Applied stock-like palette preset");
    }
    if (ImGui::Button("Preset: Portal Core No Seam", ImVec2(-1, 0))) {
        g_stage_bg_palette_mode = 0;
        g_stage_visible_colors = 31;
        g_stage_bg_cols = 8;
        g_stage_bg_rows = 1;
        stage_set_toast("Applied portal-core no-seam preset");
    }
    ImGui::InputInt("Visible Colors", &g_stage_visible_colors);
    ImGui::InputInt("BG Palette Cols", &g_stage_bg_cols);
    ImGui::InputInt("BG Palette Rows", &g_stage_bg_rows);
}

void draw_mk2_animation_planner_tool(void)
{
    ImGui::Text("Animation Planner");
    size_t full_frame_cost = (size_t)g_stage_world_width * 254u;
    size_t full_estimated = full_frame_cost * (size_t)(g_stage_overlay_frames > 0 ? g_stage_overlay_frames : 1);
    size_t sparse_estimated = (size_t)g_stage_overlay_frames *
                              (size_t)g_stage_overlay_streaks *
                              (size_t)g_stage_overlay_tile_w *
                              (size_t)g_stage_overlay_tile_h;
    ImGui::Text("Overlay frames: %d  streaks: %d", g_stage_overlay_frames, g_stage_overlay_streaks);
    ImGui::Text("Sparse overlay raw estimate: 0x%zX", sparse_estimated);
    ImGui::Text("Full bitmap frame estimate: 0x%zX", full_estimated);
    if (full_estimated > 0x30000)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1), "Full portal spin is likely too expensive as raw frame animation.");
    else
        ImGui::TextColored(ImVec4(0.75f, 1.0f, 0.55f, 1), "Overlay/spark animation budget looks plausible.");
    if (ImGui::Button("Static Portal Fallback", ImVec2(-1, 0))) {
        g_stage_overlay_mode = 0;
        g_stage_overlay_frames = 1;
        g_stage_overlay_streaks = 0;
        stage_set_toast("Animation set to static fallback");
    }
    if (ImGui::Button("Calm 3-frame Portal Pulse", ImVec2(-1, 0))) {
        g_stage_overlay_mode = 0;
        g_stage_overlay_frames = 3;
        g_stage_overlay_streaks = 8;
        g_stage_overlay_tile_w = 16;
        g_stage_overlay_tile_h = 8;
        g_stage_overlay_strength = 0.36f;
        g_stage_overlay_line_width = 1.35f;
        g_stage_overlay_phase_degrees = 0.0f;
        g_stage_sleep_ticks = 24;
        stage_set_toast("Animation set to calm portal pulse");
    }
    if (ImGui::Button("Inner Portal Spin Probe", ImVec2(-1, 0))) {
        g_stage_overlay_mode = 2;
        g_stage_overlay_frames = 3;
        g_stage_overlay_streaks = 8;
        g_stage_overlay_tile_w = 16;
        g_stage_overlay_tile_h = 8;
        g_stage_overlay_strength = 0.42f;
        g_stage_overlay_line_width = 1.35f;
        g_stage_overlay_phase_degrees = 18.0f;
        g_stage_sleep_ticks = 16;
        stage_set_toast("Animation set to inner portal spin probe");
    }
    if (ImGui::Button("Visible 3-frame Vortex Motion", ImVec2(-1, 0))) {
        g_stage_overlay_mode = 1;
        g_stage_overlay_frames = 3;
        g_stage_overlay_streaks = 12;
        g_stage_overlay_tile_w = 16;
        g_stage_overlay_tile_h = 8;
        g_stage_overlay_strength = 0.95f;
        g_stage_overlay_line_width = 2.15f;
        g_stage_overlay_phase_degrees = 32.0f;
        stage_set_toast("Animation set to visible sparse vortex motion");
    }
    const char *overlay_mode = stage_overlay_mode_name();
    if (ImGui::BeginCombo("Mode", overlay_mode)) {
        if (ImGui::Selectable("pulse", g_stage_overlay_mode == 0)) g_stage_overlay_mode = 0;
        if (ImGui::Selectable("spin", g_stage_overlay_mode == 1)) g_stage_overlay_mode = 1;
        if (ImGui::Selectable("inner-spin", g_stage_overlay_mode == 2)) g_stage_overlay_mode = 2;
        ImGui::EndCombo();
    }
    ImGui::InputInt("Frames", &g_stage_overlay_frames);
    ImGui::InputInt("Streaks", &g_stage_overlay_streaks);
    ImGui::InputInt("Tile W", &g_stage_overlay_tile_w);
    ImGui::InputInt("Tile H", &g_stage_overlay_tile_h);
    ImGui::SliderFloat("Strength", &g_stage_overlay_strength, 0.0f, 1.0f, "%.2f");
    ImGui::InputFloat("Line Width", &g_stage_overlay_line_width, 0.1f, 0.5f, "%.2f");
    ImGui::InputFloat("Phase Degrees", &g_stage_overlay_phase_degrees, 1.0f, 5.0f, "%.1f");
}
