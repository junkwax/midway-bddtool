#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/object_position_undo.h"
#include "imgui.h"
#include <stdio.h>
#include <vector>

static ObjectRecordUndoCapture s_obj_prop_capture;
static int s_obj_prop_capture_active = 0;

static bool obj_prop_capture_one(int obj_idx)
{
    int object_cap = editor_project_object_capacity();
    if (obj_idx < 0 || obj_idx >= g_no || object_cap <= 0)
        return false;
    std::vector<unsigned char> mask((size_t)object_cap, 0);
    mask[(size_t)obj_idx] = 1;
    s_obj_prop_capture = ObjectRecordUndoCapture();
    s_obj_prop_capture_active = object_record_undo_capture_mask(&s_obj_prop_capture, mask.data()) ? 1 : 0;
    return s_obj_prop_capture_active != 0;
}

static void obj_prop_commit(const char *label)
{
    if (s_obj_prop_capture_active)
        object_record_undo_commit(&s_obj_prop_capture, label);
    s_obj_prop_capture = ObjectRecordUndoCapture();
    s_obj_prop_capture_active = 0;
}

void draw_obj_properties(void)
{
    if (g_hl_obj < 0 || g_hl_obj >= g_no) return;
    if (!g_show_obj_properties) return;

    int sel_count = 0;
    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_count++;

    Obj *o = &g_obj[g_hl_obj];
    Img *im = img_find(o->ii);
    int ii_idx = im ? (int)(im - g_img) : -1;

    right_panel_set_next(RIGHT_PANEL_OBJ_PROPERTIES);
    if (obj_properties_take_focus_request())
        ImGui::SetNextWindowFocus();
    bool open = ImGui::Begin("Object Properties", &g_show_obj_properties);
    right_panel_after_begin(RIGHT_PANEL_OBJ_PROPERTIES);
    if (!open) {
        ImGui::End();
        return;
    }

    if (sel_count >= 2) {
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "%d objects selected", sel_count);
        ImGui::Separator();

        int layer_vals[] = { 0x32, 0x3C, 0x40, 0x41, 0x43, 0x46 };
        const char *layer_labels[] = { "Sky/back","Mid","Floor/play","Floor alt","Near FG","Front FG" };
        int cur_layer = (o->wx >> 8) & 0xFF;
        int cur_li = -1;
        for (int li = 0; li < 6; li++) if (layer_vals[li] == cur_layer) { cur_li = li; break; }
        if (ImGui::BeginCombo("Layer (all)", cur_li >= 0 ? layer_labels[cur_li] : "mixed")) {
            for (int li = 0; li < 6; li++) {
                if (ImGui::Selectable(layer_labels[li], li == cur_li)) {
                    ObjectRecordUndoCapture undo;
                    object_record_undo_capture_selected(&undo);
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (layer_vals[li] << 8);
                    }
                    object_record_undo_commit(&undo, "Assign Layer");
                }
            }
            ImGui::EndCombo();
        }

        if (g_n_pals > 0) {
            char pal_preview[80];
            snprintf(pal_preview, sizeof pal_preview, "%d: %s", o->fl, g_pal_name[o->fl]);
            if (ImGui::BeginCombo("Palette (all)", pal_preview)) {
                for (int p = 0; p < g_n_pals; p++) {
                    char lbl[80]; snprintf(lbl, sizeof lbl, "%d: %s", p, g_pal_name[p]);
                    if (ImGui::Selectable(lbl, false)) {
                        ObjectRecordUndoCapture undo;
                        object_record_undo_capture_selected(&undo);
                        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) g_obj[i].fl = p;
                        object_record_undo_commit(&undo, "Assign Palette");
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Lower bit depth...", ImVec2(-1, 0))) {
            g_show_group_bpp_reducer = true;
        }
        if (ImGui::Button("Delete all selected", ImVec2(-1, 0))) {
            delete_object_targets_preserve_order(-1, "Delete");
        }
        ImGui::Separator();
        ImGui::TextDisabled("(showing lead object below)");
        ImGui::Spacing();
    }

    if (SDL_Texture *tex = editor_texture_at(ii_idx)) {
        float max_sz = ImGui::GetContentRegionAvail().x;
        float scale = max_sz / (float)(im->w > im->h ? im->w : im->h);
        if (scale > 4.0f) scale = 4.0f;
        float iw = im->w * scale, ih = im->h * scale;
        draw_editor_texture_transparent(tex, iw, ih);
        if (g_simple_mode)
            ImGui::Text("%dx%d  palette %d", im->w, im->h, o->fl);
        else
            ImGui::Text("Image: ii=0x%02X  %dx%d  pal=%d", o->ii, im->w, im->h, o->fl);
        ImGui::Separator();
    }

    {
        int cur = o->ii;
        char ii_preview[64];
        if (im && im->label[0]) {
            snprintf(ii_preview, sizeof ii_preview, "%.48s", im->label);
        } else if (g_simple_mode) {
            if (im) snprintf(ii_preview, sizeof ii_preview, "Img %d  (%dx%d)", cur, im->w, im->h);
            else    snprintf(ii_preview, sizeof ii_preview, "Img %d (missing)", cur);
        } else {
            if (im) snprintf(ii_preview, sizeof ii_preview, "0x%04X  %dx%d", cur, im->w, im->h);
            else    snprintf(ii_preview, sizeof ii_preview, "0x%04X (missing)", cur);
        }
        if (ImGui::BeginCombo(g_simple_mode ? "Image" : "Image (ii)", ii_preview)) {
            for (int i = 0; i < g_ni; i++) {
                char lbl[96];
                if (g_img[i].label[0])
                    snprintf(lbl, sizeof lbl, "%s  0x%04X  %dx%d",
                             g_img[i].label, g_img[i].idx, g_img[i].w, g_img[i].h);
                else if (g_simple_mode)
                    snprintf(lbl, sizeof lbl, "Img %d  (%dx%d)", g_img[i].idx, g_img[i].w, g_img[i].h);
                else
                    snprintf(lbl, sizeof lbl, "0x%04X  %dx%d", g_img[i].idx, g_img[i].w, g_img[i].h);
                if (ImGui::Selectable(lbl, cur == g_img[i].idx)) {
                    obj_prop_capture_one(g_hl_obj);
                    o->ii = g_img[i].idx;
                    Img *nim = img_find(o->ii);
                    if (nim && nim->pal_idx >= 0)
                        o->fl = nim->pal_idx;
                    obj_prop_commit("Change Image");
                }
            }
            ImGui::EndCombo();
        }
    }

    if (!g_simple_mode) {
        int wx_val = o->wx;
        bool wx_changed = ImGui::InputInt("wx (hex)", &wx_val, 16, 256, ImGuiInputTextFlags_CharsHexadecimal);
        if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
        if (wx_changed) {
            if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
            o->wx = wx_val & 0xFFFF;
            g_dirty = 1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Edit Object");
    }
    int layer = (o->wx >> 8) & 0xFF;
    const char *layer_names[] = {
        "0x32 - Sky / far background (0.2x)",
        "0x3C - Mid-depth background (0.5x)",
        "0x40 - Floor / playfield (1.0x)",
        "0x41 - Floor alt / play props (1.0x)",
        "0x43 - Near foreground (1.2x)",
        "0x46 - Front foreground (1.5x)"
    };
    int layer_vals[] = { 0x32, 0x3C, 0x40, 0x41, 0x43, 0x46 };
    int layer_sel = -1;
    for (int li = 0; li < 6; li++)
        if (layer == layer_vals[li]) { layer_sel = li; break; }
    if (ImGui::BeginCombo("Layer", layer_sel >= 0 ? layer_names[layer_sel] : "custom")) {
        for (int li = 0; li < 6; li++) {
            if (ImGui::Selectable(layer_names[li], li == layer_sel)) {
                obj_prop_capture_one(g_hl_obj);
                o->wx = (o->wx & 0x00FF) | (layer_vals[li] << 8);
                obj_prop_commit("Assign Layer");
            }
        }
        ImGui::EndCombo();
    }
    draw_layer_role_hint((o->wx >> 8) & 0xFF);

    if (im && g_bdb_num_modules > 0) {
        char mod_name[64] = "";
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        int mod = assign_module(o->depth, o->sy, im->w, im->h);
        if (mod >= 0 && parse_module_bounds(mod, mod_name, &mx1, &mx2, &my1, &my2)) {
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Module: %s  local (%d,%d)",
                               mod_name, o->depth - mx1, o->sy - my1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Module %d bounds: x %d..%d, y %d..%d",
                                  mod, mx1, mx2, my1, my2);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                               "Module: outside every module");
            if (ImGui::Button("Include in Nearest Module", ImVec2(-1, 0))) {
                int changed = mk2_include_object_in_nearest_module(g_hl_obj);
                stage_set_toast(changed ? "Expanded nearest module" : "Object already fits a module");
            }
        }
    }

    int depth_val = o->depth;
    bool depth_changed = ImGui::InputInt(g_simple_mode ? "X" : "Depth (Z)", &depth_val);
    if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
    if (depth_changed) {
        if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
        o->depth = depth_val;
        g_dirty = 1;
        g_need_rebuild = 1;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Move Object");
    int sy_val = o->sy;
    bool sy_changed = ImGui::InputInt(g_simple_mode ? "Y" : "Screen Y", &sy_val);
    if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
    if (sy_changed) {
        if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
        o->sy = sy_val;
        g_dirty = 1;
        g_need_rebuild = 1;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Move Object");

    if (g_n_pals > 0) {
        char pal_preview[80];
        snprintf(pal_preview, sizeof pal_preview, "pal %d: %s", o->fl, g_pal_name[o->fl]);
        if (ImGui::BeginCombo("Palette", pal_preview)) {
            for (int i = 0; i < g_n_pals; i++) {
                char lbl[80];
                snprintf(lbl, sizeof lbl, "%d: %s  [%d]", i, g_pal_name[i], g_pal_count[i]);
                if (ImGui::Selectable(lbl, o->fl == i)) {
                    obj_prop_capture_one(g_hl_obj);
                    o->fl = i;
                    obj_prop_commit("Assign Palette");
                }
            }
            ImGui::EndCombo();
        }
    } else {
        int pal_val = o->fl;
        bool pal_changed = ImGui::InputInt("Palette", &pal_val);
        if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
        if (pal_changed) {
            if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
            o->fl = pal_val;
            g_dirty = 1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Assign Palette");
    }

    int hfl = o->hfl, vfl = o->vfl;
    bool bhfl = (hfl != 0), bvfl = (vfl != 0);
    ImGui::Checkbox("HFlip", &bhfl);
    ImGui::SameLine();
    ImGui::Checkbox("VFlip", &bvfl);
    hfl = bhfl ? 1 : 0; vfl = bvfl ? 1 : 0;
    if (hfl != o->hfl || vfl != o->vfl) {
        obj_prop_capture_one(g_hl_obj);
        o->hfl = hfl;
        o->vfl = vfl;
        o->wx = (o->wx & ~0x30) | (hfl ? 0x10 : 0) | (vfl ? 0x20 : 0);
        obj_prop_commit("Flip Object");
    }

    ImGui::Separator();
    if (ImGui::Button("Edit Block", ImVec2(90, 0)) && ii_idx >= 0) {
        edit_block_for_object(g_hl_obj);
    }
    ImGui::SameLine();
    if (ImGui::Button("Resize", ImVec2(80, 0)) && ii_idx >= 0) {
        open_sprite_resize(ii_idx, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(80, 0))) {
        delete_object_menu_targets(g_hl_obj);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(80, 0)) && editor_project_reserve_objects(g_no + 1)) {
        duplicate_object_menu_targets(g_hl_obj);
    }

    ImGui::End();
}
