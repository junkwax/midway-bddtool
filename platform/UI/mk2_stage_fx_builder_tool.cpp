#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>

void draw_mk2_stage_fx_builder_tool(void)
{
    ImGui::Text("Stage FX Builder");
    ImGui::TextDisabled("Author palette fades and game-state triggers from the Outer Haven workflow.");

    ImGui::Checkbox("Enable Red Shift", &g_stage_red_enabled);
    ImGui::Checkbox("Match point", &g_stage_red_match_point);
    ImGui::SameLine();
    ImGui::Checkbox("Low health", &g_stage_red_low_health);
    ImGui::Checkbox("Timer threshold", &g_stage_red_timer);
    ImGui::SameLine();
    ImGui::Checkbox("Round start", &g_stage_red_round_start);
    ImGui::Checkbox("Finish state", &g_stage_red_finish);
    ImGui::SameLine();
    ImGui::Checkbox("Fade back on comeback", &g_stage_red_comeback_recover);
    ImGui::Checkbox("Round 3 only", &g_stage_red_round3_only);
    ImGui::Text("Triggers: %s", stage_fx_trigger_summary());

    ImGui::InputText("Health Threshold", g_stage_health_threshold, sizeof g_stage_health_threshold);
    ImGui::InputText("Comeback Margin", g_stage_comeback_margin, sizeof g_stage_comeback_margin);
    ImGui::InputInt("Timer Threshold", &g_stage_red_timer_threshold);
    ImGui::SliderFloat("Background Red Strength", &g_stage_red_background_strength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Temple/Floor Red Strength", &g_stage_red_foreground_strength, 0.0f, 1.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Timeline");
    ImGui::InputInt("Fade In Frames", &g_stage_red_fade_in_frames);
    ImGui::InputInt("Hold Frames", &g_stage_red_hold_frames);
    ImGui::InputInt("Fade Out Frames", &g_stage_red_fade_out_frames);
    ImGui::InputInt("Palette Steps", &g_stage_red_steps);
    ImGui::InputInt("Step Delay", &g_stage_red_step_delay);
    if (g_stage_red_fade_in_frames < 1) g_stage_red_fade_in_frames = 1;
    if (g_stage_red_hold_frames < 0) g_stage_red_hold_frames = 0;
    if (g_stage_red_fade_out_frames < 1) g_stage_red_fade_out_frames = 1;
    if (g_stage_red_steps < 1) g_stage_red_steps = 1;
    if (g_stage_red_step_delay < 1) g_stage_red_step_delay = 1;

    int total_frames = g_stage_red_fade_in_frames + g_stage_red_hold_frames + g_stage_red_fade_out_frames;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ImGui::GetContentRegionAvail().x, 56.0f);
    if (sz.x < 120.0f) sz.x = 120.0f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(24,24,34,255));
    for (int x = 0; x < (int)sz.x; x++) {
        int frame = (int)((float)x * (float)total_frames / sz.x);
        float st = stage_fx_strength_at_frame(frame);
        ImU32 c = IM_COL32(100 + (int)(140.0f * st),
                           80 - (int)(45.0f * st),
                           140 - (int)(90.0f * st),
                           235);
        dl->AddLine(ImVec2(p.x + x, p.y + sz.y),
                    ImVec2(p.x + x, p.y + sz.y - st * sz.y),
                    c);
    }
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(160,160,190,180));
    ImGui::InvisibleButton("##stage_fx_curve", sz);
    ImGui::TextDisabled("Total %.2f sec at 53 Hz", total_frames / 53.0f);

    ImGui::Separator();
    ImGui::Text("Palette Fade Preview");
    if (g_n_pals <= 0) {
        ImGui::TextDisabled("No palettes loaded.");
    } else {
        if (g_sel_pal < 0 || g_sel_pal >= g_n_pals) g_sel_pal = 0;
        ImGui::InputInt("Generate Steps", &g_fx_palette_steps);
        ImGui::InputInt("Preview Colors", &g_fx_preview_color_count);
        if (g_fx_palette_steps < 1) g_fx_palette_steps = 1;
        if (g_fx_palette_steps > 32) g_fx_palette_steps = 32;
        if (g_fx_preview_color_count < 1) g_fx_preview_color_count = 1;
        if (g_fx_preview_color_count > 32) g_fx_preview_color_count = 32;
        int shown = g_pal_count[g_sel_pal] < g_fx_preview_color_count
                  ? g_pal_count[g_sel_pal]
                  : g_fx_preview_color_count;
        for (int row = 0; row <= 3; row++) {
            float t = (float)row / 3.0f;
            float strength = g_stage_red_background_strength * t;
            ImGui::Text("step %.0f%%", t * 100.0f);
            ImGui::SameLine(72.0f);
            for (int i = 0; i < shown; i++) {
                if (i > 0) ImGui::SameLine();
                Uint32 c = (i == 0)
                    ? g_pals[g_sel_pal][i]
                    : danger_tint_color(g_pals[g_sel_pal][i], strength, g_danger_palette_keep_blue);
                ImGui::PushID(row * 256 + i);
                ImGui::ColorButton("##fxpal",
                                   ImVec4(((c >> 16) & 0xFF)/255.0f,
                                          ((c >> 8) & 0xFF)/255.0f,
                                          (c & 0xFF)/255.0f,
                                          i == 0 ? 0.25f : 1.0f),
                                   ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(14,14));
                ImGui::PopID();
            }
        }
        if (ImGui::Button("Generate Intermediate Palettes", ImVec2(-1, 0))) {
            int made = stage_fx_generate_fade_palettes();
            if (made > 0) {
                char msg[96];
                snprintf(msg, sizeof msg, "Generated %d fade palette(s)", made);
                stage_set_toast(msg);
            } else {
                stage_set_toast("Could not generate fade palettes");
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Layer Behavior");
    int layer_vals[256], layer_n = 0, layer_counts[256];
    memset(layer_counts, 0, sizeof layer_counts);
    for (int i = 0; i < g_no; i++) {
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        int slot = -1;
        for (int j = 0; j < layer_n; j++) if (layer_vals[j] == layer) { slot = j; break; }
        if (slot < 0 && layer_n < 256) {
            slot = layer_n;
            layer_vals[layer_n++] = layer;
        }
        if (slot >= 0) layer_counts[slot]++;
    }
    for (int a = 0; a < layer_n - 1; a++)
        for (int b = a + 1; b < layer_n; b++)
            if (layer_vals[a] > layer_vals[b]) {
                int tv = layer_vals[a]; layer_vals[a] = layer_vals[b]; layer_vals[b] = tv;
                int tc = layer_counts[a]; layer_counts[a] = layer_counts[b]; layer_counts[b] = tc;
            }
    if (ImGui::BeginTable("stage_fx_layers", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("scroll", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("objs", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("red", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("role");
        ImGui::TableHeadersRow();
        for (int i = 0; i < layer_n; i++) {
            int layer = layer_vals[i];
            const char *role = layer < 0x40 ? "background/mid" :
                               (layer == 0x40 || layer == 0x41) ? "floor/playfield" :
                               "foreground";
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", layer);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", mk2_scroll_factor_for_layer(layer));
            ImGui::TableNextColumn(); ImGui::Text("%d", layer_counts[i]);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", stage_fx_layer_strength(layer));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(role);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("Save FX To Stage Config", ImVec2(-1, 0))) {
        stage_write_config();
    }
    if (ImGui::Button("Copy FX JSON Snippet", ImVec2(-1, 0))) {
        char snippet[2048];
        stage_fx_build_snippet(snippet, sizeof snippet);
        ImGui::SetClipboardText(snippet);
        stage_set_toast("Copied FX JSON snippet");
    }
}

Uint32 danger_tint_color(Uint32 c, float strength, float keep_blue)
{
    int r = (c >> 16) & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = c & 0xFF;
    float s = strength;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    float kb = keep_blue;
    if (kb < 0.0f) kb = 0.0f;
    if (kb > 1.0f) kb = 1.0f;
    int nr = (int)(r + (255 - r) * s * 0.65f);
    int ng = (int)(g * (1.0f - s * 0.65f));
    int nb = (int)(b * (1.0f - s * (0.85f - kb * 0.45f)));
    if (nr > 255) nr = 255;
    if (ng < 0) ng = 0;
    if (nb < 0) nb = 0;
    return 0xFF000000u | ((Uint32)nr << 16) | ((Uint32)ng << 8) | (Uint32)nb;
}

float stage_fx_strength_at_frame(int frame)
{
    int fade_in = g_stage_red_fade_in_frames < 1 ? 1 : g_stage_red_fade_in_frames;
    int hold = g_stage_red_hold_frames < 0 ? 0 : g_stage_red_hold_frames;
    int fade_out = g_stage_red_fade_out_frames < 1 ? 1 : g_stage_red_fade_out_frames;
    if (frame < fade_in)
        return (float)frame / (float)fade_in;
    frame -= fade_in;
    if (frame < hold)
        return 1.0f;
    frame -= hold;
    if (frame < fade_out)
        return 1.0f - ((float)frame / (float)fade_out);
    return 0.0f;
}

void stage_fx_build_snippet(char *out, size_t outsz)
{
    out[0] = '\0';
    stage_append(out, outsz, "\"red_shift\": {\n");
    char line[256];
    snprintf(line, sizeof line, "  \"enabled\": %s,\n", g_stage_red_enabled ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger\": \"danger\",\n");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger_match_point\": %s,\n", g_stage_red_match_point ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger_low_health\": %s,\n", g_stage_red_low_health ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger_timer\": %s,\n", g_stage_red_timer ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger_round_start\": %s,\n", g_stage_red_round_start ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"trigger_finish\": %s,\n", g_stage_red_finish ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"comeback_recover\": %s,\n", g_stage_red_comeback_recover ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"round3_only\": %s,\n", g_stage_red_round3_only ? "true" : "false");
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"health_threshold\": \"%s\",\n", g_stage_health_threshold);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"comeback_margin\": \"%s\",\n", g_stage_comeback_margin);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"timer_threshold\": %d,\n", g_stage_red_timer_threshold);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"steps\": %d,\n", g_stage_red_steps);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"step_delay\": %d,\n", g_stage_red_step_delay);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"fade_in_frames\": %d,\n", g_stage_red_fade_in_frames);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"hold_frames\": %d,\n", g_stage_red_hold_frames);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"fade_out_frames\": %d,\n", g_stage_red_fade_out_frames);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"background_strength\": %.2f,\n", g_stage_red_background_strength);
    stage_append(out, outsz, line);
    snprintf(line, sizeof line, "  \"foreground_strength\": %.2f\n", g_stage_red_foreground_strength);
    stage_append(out, outsz, line);
    stage_append(out, outsz, "}");
}

int stage_fx_generate_fade_palettes(void)
{
    if (g_n_pals <= 0 || g_sel_pal < 0 || g_sel_pal >= g_n_pals) return 0;
    int steps = g_fx_palette_steps < 1 ? 1 : g_fx_palette_steps;
    if (steps > 32) steps = 32;
    if (!editor_project_reserve_palettes(g_n_pals + steps)) return 0;

    undo_save();
    int src = g_sel_pal;
    for (int s = 1; s <= steps; s++) {
        float t = (float)s / (float)steps;
        float strength = g_stage_red_background_strength * t;
        if (strength > 1.0f) strength = 1.0f;
        Uint32 colors[256];
        for (int i = 0; i < 256; i++) {
            colors[i] = (i == 0) ? g_pals[src][i] : danger_tint_color(g_pals[src][i], strength, g_danger_palette_keep_blue);
        }
        char name[64];
        snprintf(name, sizeof name, "FX%02d_%s", s, g_pal_name[src]);
        if (editor_project_append_palette_slot(name, g_pal_count[src], colors) < 0)
            return s - 1;
    }
    sync_bdb_header_counts();
    g_dirty = 1;
    return steps;
}

float stage_fx_layer_strength(int layer)
{
    if (layer < 0x40) return g_stage_red_background_strength;
    if (layer == 0x40 || layer == 0x41) return g_stage_red_foreground_strength;
    return g_stage_red_foreground_strength * 0.75f;
}

const char *stage_fx_trigger_summary(void)
{
    static char buf[256];
    buf[0] = '\0';
    if (g_stage_red_match_point) stage_append(buf, sizeof buf, "match point, ");
    if (g_stage_red_low_health) stage_append(buf, sizeof buf, "low health, ");
    if (g_stage_red_timer) stage_append(buf, sizeof buf, "timer, ");
    if (g_stage_red_round_start) stage_append(buf, sizeof buf, "round start, ");
    if (g_stage_red_finish) stage_append(buf, sizeof buf, "finish state, ");
    size_t n = strlen(buf);
    if (n >= 2) buf[n - 2] = '\0';
    if (!buf[0]) snprintf(buf, sizeof buf, "manual/off");
    if (g_stage_red_round3_only) stage_append(buf, sizeof buf, " (round 3 only)");
    return buf;
}

