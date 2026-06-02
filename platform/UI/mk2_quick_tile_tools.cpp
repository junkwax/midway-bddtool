#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

void draw_mk2_quick_tile_tools(void)
{
    if (ImGui::Button("Import PNG...", ImVec2(g_hint_import ? -26.0f : -1.0f, 0.0f))) {
        char path[512] = "";
        if (file_dialog_open("Import PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path)) {
            import_png(path);
            g_hint_import = false;
        }
    }
    hint_badge(&g_hint_import, "hint_import");

    if (g_ni <= 0) {
        ImGui::TextDisabled("Import at least one PNG, then tile it into the world.");
        return;
    }

    if (g_tile_img < 0 || g_tile_img >= g_ni) g_tile_img = 0;
    char img_preview[64];
    if (g_simple_mode)
        snprintf(img_preview, sizeof img_preview, "#%d  %dx%d",
                 g_img[g_tile_img].idx, g_img[g_tile_img].w, g_img[g_tile_img].h);
    else
        snprintf(img_preview, sizeof img_preview, "0x%02X  %dx%d",
                 g_img[g_tile_img].idx, g_img[g_tile_img].w, g_img[g_tile_img].h);
    if (ImGui::BeginCombo("Image", img_preview)) {
        for (int i = 0; i < g_ni; i++) {
            char lbl[64];
            const char *pal_name = "";
            int pi = g_img[i].pal_idx;
            if (pi >= 0 && pi < g_n_pals) pal_name = g_pal_name[pi];
            if (g_simple_mode)
                snprintf(lbl, sizeof lbl, "#%d  %dx%d  %s",
                         g_img[i].idx, g_img[i].w, g_img[i].h, pal_name);
            else
                snprintf(lbl, sizeof lbl, "0x%02X  %dx%d  %s",
                         g_img[i].idx, g_img[i].w, g_img[i].h, pal_name);
            if (ImGui::Selectable(lbl, i == g_tile_img)) g_tile_img = i;
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Layer", mk2_layer_label(g_tile_layer))) {
        for (int li = 0; li < mk2_layer_preset_count(); li++) {
            int wx = mk2_layer_preset_wx(li);
            if (ImGui::Selectable(mk2_layer_preset_label(li), g_tile_layer == wx))
                g_tile_layer = wx;
        }
        ImGui::EndCombo();
    }

    ImGui::InputInt("Cols", &g_tile_cols);
    ImGui::InputInt("Rows", &g_tile_rows);
    ImGui::InputInt("Step X", &g_tile_sx);
    ImGui::InputInt("Step Y", &g_tile_sy);
    if (g_tile_cols < 1) g_tile_cols = 1;
    if (g_tile_rows < 1) g_tile_rows = 1;
    if (g_tile_sx < 1) g_tile_sx = 1;
    if (g_tile_sy < 1) g_tile_sy = 1;

    if (ImGui::Button("Fit World"))
        fit_tile_to_world();
    ImGui::SameLine();
    if (ImGui::Button("Tile Now")) {
        int added = tile_fill_apply();
        g_tile_preview = false;
        if (added > 0) {
            char msg[128];
            snprintf(msg, sizeof msg, "Placed %d objects on %s",
                     added, mk2_layer_label(g_tile_layer));
            stage_set_toast(msg);
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Preview##tileprev", &g_tile_preview);

    int sel = selected_count();
    if (sel > 0) {
        if (ImGui::Button("Apply Layer to Selected", ImVec2(-1, 0)))
            assign_selected_layer(g_tile_layer);
        ImGui::TextDisabled("%d selected", sel);
    }
    if (ImGui::Button("Advanced Tile Fill...", ImVec2(-1, 0)))
        g_show_tile = true;
}
