#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>

void draw_mk2_mirrored_asset_tool(void)
{
    ImGui::Text("Mirrored Asset Tool");
    ImGui::TextDisabled("Reuse one image with HFlip instead of storing a second mirrored asset.");
    ImGui::InputInt("Gap", &g_mirror_gap);
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        Obj *o = &g_obj[g_hl_obj];
        Img *im = img_find(o->ii);
        ImGui::Text("Selected obj %d: ii=0x%02X", g_hl_obj, o->ii);
        if (ImGui::Button("Duplicate Mirrored To Right", ImVec2(-1, 0)) && im) {
            int layer = (o->wx >> 8) & 0xFF;
            int x = o->depth + im->w + g_mirror_gap;
            if (mk2_add_object_for_image((int)(im - g_img), x, o->sy, layer, o->fl,
                                         !o->hfl, o->vfl, true))
                stage_set_toast("Added mirrored placement to the right");
        }
        if (ImGui::Button("Duplicate Mirrored To Left", ImVec2(-1, 0)) && im) {
            int layer = (o->wx >> 8) & 0xFF;
            int x = o->depth - im->w - g_mirror_gap;
            if (mk2_add_object_for_image((int)(im - g_img), x, o->sy, layer, o->fl,
                                         !o->hfl, o->vfl, true))
                stage_set_toast("Added mirrored placement to the left");
        }
    } else {
        ImGui::TextDisabled("Select an object to mirror it.");
    }
    int img_i = active_image_index();
    if (img_i >= 0 && img_i < g_ni) {
        Img *im = &g_img[img_i];
        ImGui::Separator();
        ImGui::Text("Pair from active image 0x%02X", im->idx);
        ImGui::InputInt("Left X", &g_mirror_pair_left_x);
        ImGui::InputInt("Right X", &g_mirror_pair_right_x);
        ImGui::InputInt("Pair Y", &g_mirror_pair_y);
        if (ImGui::BeginCombo("Pair Layer", mk2_layer_label(g_mirror_layer))) {
            for (int li = 0; li < mk2_layer_preset_count(); li++) {
                if (ImGui::Selectable(mk2_layer_preset_label(li),
                                      g_mirror_layer == mk2_layer_preset_wx(li)))
                    g_mirror_layer = mk2_layer_preset_wx(li);
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Place Symmetric Pair", ImVec2(-1, 0))) {
            int pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? im->pal_idx : 0;
            undo_save_ex("Place Pair");
            int ok1 = mk2_add_object_for_image(img_i, g_mirror_pair_left_x,
                                               g_mirror_pair_y, g_mirror_layer,
                                               pal, 0, 0, false);
            int ok2 = mk2_add_object_for_image(img_i, g_mirror_pair_right_x,
                                               g_mirror_pair_y, g_mirror_layer,
                                               pal, 1, 0, false);
            snprintf(g_toast_msg, sizeof g_toast_msg, "Placed %d mirrored pair object(s)", ok1 + ok2);
            g_toast_timer = 3.0f;
        }
        ImGui::TextDisabled("VROM saved: one BDD image, two BDB placements.");
    }
}
