#include "bg_editor.h"
#include "bg_editor_globals.h"

#include "imgui.h"

#include <string.h>

void draw_mk2_layer_stack_inspector(void)
{
    ImGui::Text("Layer Stack Inspector");
    ImGui::Checkbox("Track world mouse", &g_probe_track_mouse);
    ImGui::SameLine();
    ImGui::Checkbox("Hidden", &g_probe_include_hidden);

    if (g_probe_track_mouse && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
        !ImGui::IsPopupOpen(""))
    {
        ImVec2 mp = ImGui::GetIO().MousePos;
        bdd_screen_to_world((int)mp.x, (int)mp.y, g_view_x, g_view_y, g_zoom,
                            &g_probe_x, &g_probe_y);
    }
    ImGui::InputInt("Probe X", &g_probe_x);
    ImGui::InputInt("Probe Y", &g_probe_y);

    int hit_count = 0;
    for (int i = 0; i < g_no; i++) {
        if (!g_probe_include_hidden && g_obj_hidden[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        if (object_pixel_at_world(&g_obj[i], im, g_probe_x, g_probe_y) >= 0)
            hit_count++;
    }
    ImGui::TextDisabled("%d object rectangle(s) cover this coordinate; top draw is listed first.", hit_count);

    if (ImGui::BeginTable("mk2_stack_table", 9,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupColumn("obj", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("ii", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("wx", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("pal", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("px", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("rgb", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("mod", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();

        for (int i = g_no - 1; i >= 0; i--) {
            if (!g_probe_include_hidden && g_obj_hidden[i]) continue;
            Obj *o = &g_obj[i];
            Img *im = img_find(o->ii);
            int px = object_pixel_at_world(o, im, g_probe_x, g_probe_y);
            if (px < 0) continue;

            ImGui::PushID(i);
            ImGui::TableNextRow();
            if (px == 0)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 60, 70, 80));
            else if (g_sel_flags[i])
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(70, 95, 120, 120));

            ImGui::TableNextColumn(); ImGui::Text("%d", i);
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", o->ii);
            ImGui::TableNextColumn(); ImGui::Text("%02X", (o->wx >> 8) & 0xFF);
            ImGui::TableNextColumn(); ImGui::Text("%d", o->fl);
            ImGui::TableNextColumn();
            if (px == 0)
                ImGui::TextDisabled("0");
            else
                ImGui::Text("%d", px);
            ImGui::TableNextColumn();
            Uint32 c = 0;
            if (px > 0 && o->fl >= 0 && o->fl < g_n_pals)
                c = g_pals[o->fl][px & 0xFF];
            ImVec4 col(((c >> 16) & 0xFF) / 255.0f,
                       ((c >> 8) & 0xFF) / 255.0f,
                       (c & 0xFF) / 255.0f,
                       px == 0 ? 0.25f : 1.0f);
            ImGui::ColorButton("##rgb", col, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(px == 0 ? "transparent pixel" : "#%02X%02X%02X",
                                  (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            ImGui::TableNextColumn();
            int mod = object_module_index(o, im);
            if (mod >= 0) ImGui::Text("%d", mod);
            else ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "-");
            ImGui::TableNextColumn();
            char state[16] = "";
            if (g_obj_hidden[i]) strncat(state, "H", sizeof(state) - strlen(state) - 1);
            if (g_obj_lock[i]) strncat(state, "L", sizeof(state) - strlen(state) - 1);
            if (px == 0) strncat(state, "T", sizeof(state) - strlen(state) - 1);
            ImGui::Text("%s", state[0] ? state : "-");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Sel")) {
                editor_project_clear_selection();
                g_sel_flags[i] = 1;
                g_hl_obj = i;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ctr")) {
                g_hl_obj = i;
                g_view_x = o->depth - 160;
                g_view_y = o->sy - 120;
                g_view_changed = 1;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
