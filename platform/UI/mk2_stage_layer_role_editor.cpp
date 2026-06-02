#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

void draw_mk2_stage_layer_role_editor(void)
{
    ImGui::Text("Stage Layer Role Editor");
    ImGui::TextDisabled("Assign semantic roles to common MK2 layers for handoff/export and FX tint planning.");
    if (ImGui::BeginTable("layer_roles", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("role", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("scroll", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("tint", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("objects");
        ImGui::TableHeadersRow();
        int layer_count = mk2_layer_preset_count();
        for (int i = 0; i < layer_count && i < 6; i++) {
            int layer = mk2_layer_preset_wx(i);
            int count = 0;
            for (int o = 0; o < g_no; o++)
                if (((g_obj[o].wx >> 8) & 0xFF) == layer) count++;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", layer);
            ImGui::TableNextColumn();
            if (ImGui::BeginCombo("##role", layer_role_name(g_layer_role[i]))) {
                for (int r = 0; r < 5; r++)
                    if (ImGui::Selectable(layer_role_name(r), g_layer_role[i] == r)) g_layer_role[i] = r;
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn(); ImGui::Text("%.1f", mk2_scroll_factor_for_layer(layer));
            ImGui::TableNextColumn(); ImGui::SliderFloat("##tint", &g_layer_role_tint[i], 0.0f, 1.0f, "%.2f");
            ImGui::TableNextColumn(); ImGui::Text("%d", count);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Copy Layer Role JSON", ImVec2(-1, 0))) {
        char out[2048];
        out[0] = '\0';
        stage_append(out, sizeof out, "\"layer_roles\": [\n");
        int layer_count = mk2_layer_preset_count();
        for (int i = 0; i < layer_count && i < 6; i++) {
            int layer = mk2_layer_preset_wx(i);
            char line[256];
            snprintf(line, sizeof line,
                     "  {\"layer\": \"0x%02X\", \"role\": \"%s\", \"scroll\": %.2f, \"red_strength\": %.2f}%s\n",
                     layer, layer_role_name(g_layer_role[i]),
                     mk2_scroll_factor_for_layer(layer),
                     g_layer_role_tint[i], (i + 1 < layer_count && i + 1 < 6) ? "," : "");
            stage_append(out, sizeof out, line);
        }
        stage_append(out, sizeof out, "]");
        ImGui::SetClipboardText(out);
        stage_set_toast("Copied layer role JSON");
    }
}
