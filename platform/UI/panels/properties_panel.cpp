#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/actions/object_position_undo.h"
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

static bool obj_prop_runtime_locked_object(int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= g_no) return false;
    Img *im = img_find(g_obj[obj_idx].ii);
    return runtime_actor_image_is_preview_import(im);
}

static void draw_object_image_summary(Obj *o, Img *im, int ii_idx)
{
    if (!o) return;
    if (!im) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.25f, 1.0f),
                           "Missing image 0x%02X", o->ii);
        ImGui::Separator();
        return;
    }

    SDL_Texture *tex = editor_texture_at(ii_idx);
    if (!tex) return;

    float avail = ImGui::GetContentRegionAvail().x;
    float target = avail < 320.0f ? 64.0f : 78.0f;
    int max_dim_i = im->w > im->h ? im->w : im->h;
    float scale = max_dim_i > 0 ? target / (float)max_dim_i : 1.0f;
    if (scale > 3.0f) scale = 3.0f;
    if (scale < 0.08f) scale = 0.08f;
    float iw = im->w * scale;
    float ih = im->h * scale;

    ImGui::BeginGroup();
    draw_editor_texture_transparent(tex, iw, ih);
    ImGui::EndGroup();
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    if (im->label[0])
        ImGui::TextWrapped("%s", im->label);
    else
        ImGui::Text(g_simple_mode ? "Image %d" : "Image 0x%02X", o->ii);
    if (g_simple_mode)
        ImGui::TextDisabled("%dx%d  pal %d", im->w, im->h, o->fl);
    else
        ImGui::TextDisabled("ii=0x%02X  %dx%d  pal=%d", o->ii, im->w, im->h, o->fl);
    ImGui::TextDisabled("obj #%d  order %d  uses x%d", g_hl_obj, o->order, image_use_count(im->idx));
    ImGui::TextDisabled("%dbpp  image pal %d  flags %d",
                        mk2_bpp_for_image(im), im->pal_idx, im->flags);
    if (im->source[0])
        ImGui::TextDisabled("src %s", im->source);
    if (runtime_actor_image_is_preview_import(im))
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                           "runtime source placement: move only");
    if (im->anix || im->aniy || im->anix2 || im->aniy2)
        ImGui::TextDisabled("anipoint %d,%d  alt %d,%d,%d",
                            im->anix, im->aniy, im->anix2, im->aniy2, im->aniz2);
    if (im->frm || im->opals || im->pttblnum || im->anix || im->aniy)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                           "anim frm=%d  pttbl=%d", im->frm, im->pttblnum);
    ImGui::EndGroup();
    ImGui::Separator();
}

static void draw_selected_image_asset_summary(void)
{
    if (g_place_tool_img < 0 || g_place_tool_img >= g_ni) {
        ImGui::TextDisabled("No object selected.");
        return;
    }

    Img *im = &g_img[g_place_tool_img];
    SDL_Texture *tex = editor_texture_at(g_place_tool_img);
    if (tex) {
        float target = 78.0f;
        int max_dim_i = im->w > im->h ? im->w : im->h;
        float scale = max_dim_i > 0 ? target / (float)max_dim_i : 1.0f;
        if (scale > 3.0f) scale = 3.0f;
        if (scale < 0.08f) scale = 0.08f;
        draw_editor_texture_transparent(tex, im->w * scale, im->h * scale);
        ImGui::SameLine(0, 8);
    }

    ImGui::BeginGroup();
    if (im->label[0])
        ImGui::TextWrapped("%s", im->label);
    else
        ImGui::Text(g_simple_mode ? "Image %d" : "Image 0x%02X", im->idx);
    ImGui::TextDisabled("selected image asset");
    ImGui::TextDisabled("id 0x%02X  %dx%d  pal %d  %dbpp",
                        im->idx, im->w, im->h, im->pal_idx, mk2_bpp_for_image(im));
    ImGui::TextDisabled("uses x%d", image_use_count(im->idx));
    if (im->source[0])
        ImGui::TextDisabled("src %s", im->source);
    bool runtime_locked_asset = runtime_actor_image_is_preview_import(im);
    if (runtime_locked_asset)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                           "runtime source art is read-only");
    if (im->frm || im->opals || im->pttblnum || im->anix || im->aniy)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                           "anim frm=%d  pttbl=%d", im->frm, im->pttblnum);
    ImGui::EndGroup();
    ImGui::Separator();

    if (runtime_locked_asset) ImGui::BeginDisabled();
    if (ImGui::Button("Place Sprite", ImVec2(-1, 0))) {
        g_place_tool_img = (int)(im - g_img);
        g_cur_tool = 1;
    }
    if (runtime_locked_asset) ImGui::EndDisabled();
    if (image_use_count(im->idx) > 0 && ImGui::Button("Select All Uses", ImVec2(-1, 0)))
        select_all_with_image_ii(im->idx);
}

void draw_obj_properties_contents(void)
{
    if (g_hl_obj < 0 || g_hl_obj >= g_no) {
        draw_selected_image_asset_summary();
        return;
    }

    int sel_count = 0;
    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_count++;

    Obj *o = &g_obj[g_hl_obj];
    Img *im = img_find(o->ii);
    int ii_idx = im ? (int)(im - g_img) : -1;
    bool runtime_locked = runtime_actor_image_is_preview_import(im);

    if (sel_count >= 2) {
        int runtime_locked_count = 0;
        for (int i = 0; i < g_no; i++)
            if (g_sel_flags[i] && obj_prop_runtime_locked_object(i))
                runtime_locked_count++;
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "%d objects selected", sel_count);
        if (runtime_locked_count > 0)
            ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f),
                               "%d runtime placement(s): move-only", runtime_locked_count);
        ImGui::Separator();

        if (runtime_locked_count > 0) ImGui::BeginDisabled();
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
        if (runtime_locked_count > 0) ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("(showing lead object below)");
        ImGui::Spacing();
    }

    draw_object_image_summary(o, im, ii_idx);

    if (runtime_locked) ImGui::BeginDisabled();
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
    if (runtime_locked) ImGui::EndDisabled();

    if (runtime_locked) ImGui::BeginDisabled();
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
    if (runtime_locked) ImGui::EndDisabled();

    if (ImGui::BeginTable("obj_prop_position", 2, ImGuiTableFlags_SizingStretchProp)) {
        int depth_val = o->depth;
        ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", g_simple_mode ? "X" : "Depth (Z)");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        bool depth_changed = ImGui::InputInt("##obj_depth", &depth_val);
        if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
        if (depth_changed) {
            if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
            o->depth = depth_val;
            g_dirty = 1;
            g_need_rebuild = 1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Move Object");

        int sy_val = o->sy;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", g_simple_mode ? "Y" : "Screen Y");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        bool sy_changed = ImGui::InputInt("##obj_sy", &sy_val);
        if (ImGui::IsItemActivated()) obj_prop_capture_one(g_hl_obj);
        if (sy_changed) {
            if (!s_obj_prop_capture_active) obj_prop_capture_one(g_hl_obj);
            o->sy = sy_val;
            g_dirty = 1;
            g_need_rebuild = 1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) obj_prop_commit("Move Object");
        ImGui::EndTable();
    }

    if (runtime_locked) ImGui::BeginDisabled();
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
    if (ImGui::BeginTable("obj_prop_actions", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button("Edit Block", ImVec2(-1, 0)) && ii_idx >= 0)
            edit_block_for_object(g_hl_obj);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Resize", ImVec2(-1, 0)) && ii_idx >= 0)
            open_sprite_resize(ii_idx, true);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button("Duplicate", ImVec2(-1, 0)) && editor_project_reserve_objects(g_no + 1))
            duplicate_object_menu_targets(g_hl_obj);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Delete", ImVec2(-1, 0)))
            delete_object_menu_targets(g_hl_obj);
        ImGui::EndTable();
    }
    if (runtime_locked) ImGui::EndDisabled();

}

void draw_obj_properties(void)
{
    if (!g_show_obj_properties) return;
    right_panel_set_next(RIGHT_PANEL_OBJ_PROPERTIES);
    if (obj_properties_take_focus_request())
        ImGui::SetNextWindowFocus();
    bool open = ImGui::Begin("Object Properties", &g_show_obj_properties);
    right_panel_after_begin(RIGHT_PANEL_OBJ_PROPERTIES);
    if (open)
        draw_obj_properties_contents();
    ImGui::End();
}
