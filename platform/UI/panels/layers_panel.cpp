#include "bg_editor_globals.h"
#include "UI/actions/object_position_undo.h"
#include "imgui.h"

#include <stdio.h>

void draw_layers(void)
{
    if (!g_show_layers || !g_have_bdb || g_no == 0) return;
    set_left_panel_default(92.0f, 300.0f, 280.0f);
    if (!ImGui::Begin("Layers", &g_show_layers)) return;

    int layer_vals[256], layer_n = 0;
    for (int i = 0; i < g_no; i++) {
        int lv = (g_obj[i].wx >> 8) & 0xFF;
        int dup = 0;
        for (int j = 0; j < layer_n; j++) if (layer_vals[j] == lv) { dup = 1; break; }
        if (!dup && layer_n < 256) layer_vals[layer_n++] = lv;
    }

    for (int a = 0; a < layer_n-1; a++) for (int b = a+1; b < layer_n; b++)
        if (layer_vals[a] > layer_vals[b]) { int t = layer_vals[a]; layer_vals[a] = layer_vals[b]; layer_vals[b] = t; }

    Uint8 lc[256][3];
    for (int li = 0; li < layer_n; li++) {
        float hue = (layer_vals[li] - 0x30) * 12.0f;
        float h = hue; while (h >= 360) h -= 360; while (h < 0) h += 360;
        float s = 0.7f, v = 0.9f;
        int hi = (int)(h / 60) % 6;
        float f = h / 60 - (int)(h / 60);
        float p = v * (1 - s), q = v * (1 - f * s), t2 = v * (1 - (1 - f) * s);
        float r2,g2,b2;
        if (hi==0){r2=v;g2=t2;b2=p;}else if(hi==1){r2=q;g2=v;b2=p;}else if(hi==2){r2=p;g2=v;b2=t2;}
        else if(hi==3){r2=p;g2=q;b2=v;}else if(hi==4){r2=t2;g2=p;b2=v;}else{r2=v;g2=p;b2=q;}
        lc[li][0]=(Uint8)(r2*255); lc[li][1]=(Uint8)(g2*255); lc[li][2]=(Uint8)(b2*255);
    }

    int hidden_total = 0;
    for (int i = 0; i < g_no; i++)
        if (g_obj_hidden[i]) hidden_total++;

    ImGui::Text("Unique layers: %d  |  Objects: %d  |  Hidden: %d",
                layer_n, g_no, hidden_total);
    if (ImGui::SmallButton("Show All")) {
        for (int i = 0; i < g_no; i++) g_obj_hidden[i] = 0;
        g_view_changed = 1;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide All")) {
        for (int i = 0; i < g_no; i++) g_obj_hidden[i] = 1;
        g_view_changed = 1;
    }
    ImGui::Separator();

    for (int li = 0; li < layer_n; li++) {
        int cnt = 0, sel = 0, hidden = 0, locked = 0;
        for (int i = 0; i < g_no; i++) {
            if (((g_obj[i].wx >> 8) & 0xFF) != layer_vals[li]) continue;
            cnt++;
            if (g_sel_flags[i]) sel++;
            if (g_obj_hidden[i]) hidden++;
            if (g_obj_lock[i]) locked++;
        }

        ImGui::PushID(li);
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImU32 sw_col = IM_COL32(lc[li][0],lc[li][1],lc[li][2],200);
        ImGui::GetWindowDrawList()->AddRectFilled(sp, ImVec2(sp.x+14,sp.y+14), sw_col);
        ImGui::InvisibleButton("##sw", ImVec2(14,14));
        if (ImGui::IsItemClicked()) {
            snprintf(g_obj_filter, sizeof g_obj_filter, "wx%02X", layer_vals[li]);
        }
        ImGui::SameLine(2);

        char lname[32];
        if (g_simple_mode)
            snprintf(lname, sizeof lname, "%s", layer_friendly_name(layer_vals[li]));
        else
            snprintf(lname, sizeof lname, "0x%02X", layer_vals[li]);
        ImGui::Text("%s", lname); ImGui::SameLine();
        ImGui::TextDisabled("(%d)", cnt);
        if (sel > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "[%d]", sel); }

        if (sel > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Assign")) {
                ObjectRecordUndoCapture undo;
                object_record_undo_capture_selected(&undo);
                for (int i = 0; i < g_no; i++)
                    if (g_sel_flags[i]) g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (layer_vals[li] << 8);
                if (object_record_undo_commit(&undo, "Assign Layer") > 0)
                    g_dirty = 1;
            }
        }

        ImGui::SameLine();
        {
            bool all_hidden = (hidden == cnt && cnt > 0);
            if (hidden > 0 && hidden < cnt) {
                ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f), "partial");
                ImGui::SameLine();
            } else {
                ImGui::TextColored(all_hidden ? ImVec4(0.65f,0.65f,0.65f,1.0f)
                                              : ImVec4(0.45f,0.9f,0.55f,1.0f),
                                   all_hidden ? "hidden" : "visible");
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(all_hidden ? "Show" : "Hide")) {
                int new_hidden = all_hidden ? 0 : 1;
                for (int i = 0; i < g_no; i++)
                    if (((g_obj[i].wx >> 8) & 0xFF) == layer_vals[li])
                        g_obj_hidden[i] = new_hidden;
                g_view_changed = 1;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(all_hidden ? "Show layer" : "Hide layer");
        }
        ImGui::SameLine();
        {
            bool all_locked = (locked == cnt && cnt > 0);
            char lk_lbl[8]; snprintf(lk_lbl, sizeof lk_lbl, "%s", all_locked ? "[L]" : " L ");
            if (ImGui::SmallButton(lk_lbl)) {
                int new_locked = all_locked ? 0 : 1;
                for (int i = 0; i < g_no; i++)
                    if (((g_obj[i].wx >> 8) & 0xFF) == layer_vals[li])
                        g_obj_lock[i] = new_locked;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(all_locked ? "Unlock layer" : "Lock layer");
        }

        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Click swatch to filter  |  Assign = set selected to layer");
    ImGui::End();
}
