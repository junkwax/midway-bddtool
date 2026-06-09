#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>

int tile_fill_apply(void)
{
    if (g_tile_img < 0 || g_tile_img >= g_ni) return 0;
    if (g_tile_cols < 1) g_tile_cols = 1;
    if (g_tile_rows < 1) g_tile_rows = 1;
    if (g_tile_sx < 1) g_tile_sx = 1;
    if (g_tile_sy < 1) g_tile_sy = 1;
    if (!editor_project_reserve_objects(g_no + g_tile_cols * g_tile_rows)) return 0;

    undo_save();
    Img *im = &g_img[g_tile_img];
    int start = g_no;
    for (int r = 0; r < g_tile_rows; r++) {
        for (int c = 0; c < g_tile_cols; c++) {
            Obj *o = editor_project_append_object_slot();
            if (!o) break;
            o->wx    = (g_tile_layer << 8);
            o->depth = g_tile_ox + c * g_tile_sx + g_view_x;
            o->sy    = g_tile_oy + r * g_tile_sy + g_view_y;
            o->ii    = im->idx;
            o->fl    = (im->pal_idx >= 0) ? im->pal_idx : 0;
            o->hfl   = 0;
            o->vfl   = 0;
            o->order = g_no;
        }
    }
    editor_project_clear_selection();
    for (int i = start; i < g_no; i++) g_sel_flags[i] = 1;
    g_hl_obj = start;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    return g_no - start;
}

void draw_tile_fill(void)
{
    if (!g_show_tile) return;
    ImGui::OpenPopup("Tile Fill");
    if (ImGui::BeginPopupModal("Tile Fill", &g_show_tile,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        char tile_preview[32];
        snprintf(tile_preview, sizeof tile_preview, "%s",
                 (g_tile_img >= 0 && g_tile_img < g_ni) ? "select" : "select");
        if (g_tile_img >= 0 && g_tile_img < g_ni)
            snprintf(tile_preview, sizeof tile_preview, "0x%02X %dx%d",
                     g_img[g_tile_img].idx, g_img[g_tile_img].w, g_img[g_tile_img].h);
        if (ImGui::BeginCombo("Image", tile_preview, 0))
        {
            for (int i = 0; i < g_ni; i++) {
                char lbl[32];
                snprintf(lbl, sizeof lbl, "0x%02X  %dx%d", g_img[i].idx, g_img[i].w, g_img[i].h);
                if (ImGui::Selectable(lbl, i == g_tile_img)) g_tile_img = i;
            }
            ImGui::EndCombo();
        }
        ImGui::InputInt("Columns", &g_tile_cols);
        ImGui::InputInt("Rows",    &g_tile_rows);
        ImGui::InputInt("Spacing X", &g_tile_sx);
        ImGui::InputInt("Spacing Y", &g_tile_sy);
        ImGui::InputInt("Start X",   &g_tile_ox);
        ImGui::InputInt("Start Y",   &g_tile_oy);
        if (ImGui::Button("Fit World")) fit_tile_to_world();
        if (ImGui::BeginCombo("Layer", mk2_layer_label(g_tile_layer))) {
            int layer_count = mk2_layer_preset_count();
            for (int li = 0; li < layer_count; li++) {
                int wx = mk2_layer_preset_wx(li);
                if (ImGui::Selectable(mk2_layer_preset_label(li), g_tile_layer == wx))
                    g_tile_layer = wx;
            }
            ImGui::EndCombo();
        }

        if (g_tile_cols < 1) g_tile_cols = 1;
        if (g_tile_rows < 1) g_tile_rows = 1;
        if (g_tile_sx < 1) g_tile_sx = 1;
        if (g_tile_sy < 1) g_tile_sy = 1;

        ImGui::Separator();
        ImGui::Text("Will create %d objects", g_tile_cols * g_tile_rows);
        if (ImGui::Button("Fill", ImVec2(80, 0)) && tile_fill_apply() > 0) {
            g_show_tile = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            g_show_tile = false;
        ImGui::EndPopup();
    }
}
