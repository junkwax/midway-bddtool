#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>

static int asset_matches_search(const Img *im, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    if (!im) return 0;
    char hay[128];
    snprintf(hay, sizeof hay, "%04X %02X %dx%d p%d pal%d",
             im->idx, im->idx & 0xFF, im->w, im->h, im->pal_idx, im->pal_idx);
    return strstr(hay, needle) != NULL;
}

void draw_mk2_asset_explorer(void)
{
    ImGui::Text("Stock Stage Asset Explorer");
    ImGui::InputTextWithHint("Search", g_simple_mode ? "name, size, palette..." : "hex ii, WxH, pN",
                             g_asset_explorer_search, sizeof g_asset_explorer_search);
    ImGui::Text("Show:"); ImGui::SameLine();
    if (ImGui::SmallButton("All##asset_filter")) g_asset_explorer_filter = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("Used##asset_filter")) g_asset_explorer_filter = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Unused##asset_filter")) g_asset_explorer_filter = 2;

    int shown = 0;
    int total_unused = 0;
    for (int i = 0; i < g_ni; i++)
        if (image_use_count(g_img[i].idx) == 0) total_unused++;
    ImGui::TextDisabled("%d images, %d dormant, %d objects", g_ni, total_unused, g_no);

    if (ImGui::BeginTable("mk2_asset_table", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupColumn(g_simple_mode ? "Img" : "ii", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn(g_simple_mode ? "pal" : "pal", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("uses", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();

        for (int i = 0; i < g_ni; i++) {
            Img *im = &g_img[i];
            int uses = image_use_count(im->idx);
            if (g_asset_explorer_filter == 1 && uses == 0) continue;
            if (g_asset_explorer_filter == 2 && uses != 0) continue;
            if (!asset_matches_search(im, g_asset_explorer_search)) continue;
            shown++;

            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (g_simple_mode) ImGui::Text("#%d", im->idx);
            else               ImGui::Text("0x%02X", im->idx);
            SDL_Texture *tex = editor_texture_at(i);
            if (ImGui::IsItemHovered() && tex) {
                ImGui::BeginTooltip();
                float sc = 128.0f / (float)(im->w > im->h ? im->w : im->h);
                if (sc > 2.0f) sc = 2.0f;
                draw_editor_texture_transparent(tex, im->w * sc, im->h * sc);
                ImGui::Text("%dx%d  pal %d  max pixel %d", im->w, im->h,
                            im->pal_idx, image_max_pixel(im));
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", im->w, im->h);
            ImGui::TableNextColumn(); ImGui::Text("%d", im->pal_idx);
            ImGui::TableNextColumn();
            if (uses == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1), "0");
            else
                ImGui::Text("%d", uses);
            ImGui::TableNextColumn(); ImGui::Text("%d", image_max_pixel(im));
            ImGui::TableNextColumn();
            if (uses > 0) {
                if (ImGui::SmallButton("Select")) {
                    int selected = mk2_select_objects_by_image(im->idx);
                    if (g_simple_mode)
                        snprintf(g_toast_msg, sizeof g_toast_msg, "Selected %d object(s)", selected);
                    else
                        snprintf(g_toast_msg, sizeof g_toast_msg, "Selected %d object(s) using 0x%02X",
                                 selected, im->idx);
                    g_toast_timer = 3.0f;
                }
            } else {
                if (ImGui::SmallButton("Enable")) {
                    g_unused_enable_img = i;
                    g_unused_enable_pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? im->pal_idx : 0;
                    if (mk2_enable_unused_asset(i)) {
                        if (g_simple_mode)
                            snprintf(g_toast_msg, sizeof g_toast_msg, "Image added to stage");
                        else
                            snprintf(g_toast_msg, sizeof g_toast_msg, "Enabled dormant image 0x%02X", im->idx);
                    } else {
                        if (g_simple_mode)
                            snprintf(g_toast_msg, sizeof g_toast_msg, "Could not add image");
                        else
                            snprintf(g_toast_msg, sizeof g_toast_msg, "Could not enable 0x%02X", im->idx);
                    }
                    g_toast_timer = 3.0f;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (shown == 0)
        ImGui::TextDisabled("No images match this filter.");
}

void draw_mk2_unused_asset_tools(void)
{
    int unused_count = 0;
    int used_count = 0;
    for (int i = 0; i < g_ni; i++) {
        if (image_use_count(g_img[i].idx) == 0) unused_count++;
        else used_count++;
    }

    ImGui::Text("Dormant BDD Assets");
    ImGui::TextDisabled("%d unused image(s), %d placed image(s)", unused_count, used_count);

    if (g_unused_enable_img < 0 || g_unused_enable_img >= g_ni ||
        image_use_count(g_img[g_unused_enable_img].idx) != 0)
        g_unused_enable_img = first_unused_image_index();

    if (g_unused_enable_img >= 0) {
        Img *im = &g_img[g_unused_enable_img];
        if (g_unused_enable_pal < 0 || g_unused_enable_pal >= g_n_pals)
            g_unused_enable_pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? im->pal_idx : 0;

        char preview[96];
        snprintf(preview, sizeof preview, "0x%02X  %dx%d  pal %d",
                 im->idx, im->w, im->h, im->pal_idx);
        if (ImGui::BeginCombo("Unused Image", preview)) {
            for (int i = 0; i < g_ni; i++) {
                if (image_use_count(g_img[i].idx) != 0) continue;
                char lbl[96];
                snprintf(lbl, sizeof lbl, "0x%02X  %dx%d  pal %d",
                         g_img[i].idx, g_img[i].w, g_img[i].h, g_img[i].pal_idx);
                if (ImGui::Selectable(lbl, i == g_unused_enable_img)) {
                    g_unused_enable_img = i;
                    g_unused_enable_pal = (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
                                        ? g_img[i].pal_idx : 0;
                }
            }
            ImGui::EndCombo();
        }

        if (SDL_Texture *tex = editor_texture_at(g_unused_enable_img)) {
            float max_sz = ImGui::GetContentRegionAvail().x;
            if (max_sz > 160.0f) max_sz = 160.0f;
            float sc = max_sz / (float)(im->w > im->h ? im->w : im->h);
            if (sc > 2.0f) sc = 2.0f;
            if (sc < 0.1f) sc = 0.1f;
            draw_editor_texture_transparent(tex, im->w * sc, im->h * sc);
        }

        ImGui::Checkbox("Auto-fit first module", &g_unused_auto_fit);
        if (!g_unused_auto_fit) {
            ImGui::InputInt("X", &g_unused_enable_x);
            ImGui::InputInt("Y", &g_unused_enable_y);
        } else if (!mk2_find_first_fit_for_image(im, NULL, NULL)) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1),
                               "No module fully fits this image; manual X/Y will be used.");
            ImGui::InputInt("X", &g_unused_enable_x);
            ImGui::InputInt("Y", &g_unused_enable_y);
        }

        if (ImGui::BeginCombo("Asset Layer", mk2_layer_label(g_unused_enable_layer))) {
            for (int li = 0; li < mk2_layer_preset_count(); li++) {
                if (ImGui::Selectable(mk2_layer_preset_label(li),
                                      g_unused_enable_layer == mk2_layer_preset_wx(li)))
                    g_unused_enable_layer = mk2_layer_preset_wx(li);
            }
            ImGui::EndCombo();
        }
        if (g_n_pals > 0) {
            if (g_unused_enable_pal < 0) g_unused_enable_pal = 0;
            if (g_unused_enable_pal >= g_n_pals) g_unused_enable_pal = g_n_pals - 1;
            ImGui::SliderInt("Asset Palette", &g_unused_enable_pal, 0, g_n_pals - 1);
        }

        if (g_hl_obj >= 0 && g_hl_obj < g_no && ImGui::Button("Use Selected Object Placement", ImVec2(-1, 0))) {
            g_unused_enable_x = g_obj[g_hl_obj].depth;
            g_unused_enable_y = g_obj[g_hl_obj].sy;
            g_unused_enable_layer = (g_obj[g_hl_obj].wx >> 8) & 0xFF;
            g_unused_enable_pal = g_obj[g_hl_obj].fl;
            g_unused_auto_fit = false;
        }

        if (ImGui::Button("Enable Unused Asset", ImVec2(-1, 0))) {
            if (mk2_enable_unused_asset(g_unused_enable_img))
                snprintf(g_toast_msg, sizeof g_toast_msg, "Enabled image 0x%02X as BDB object", im->idx);
            else
                snprintf(g_toast_msg, sizeof g_toast_msg, "Could not enable selected image");
            g_toast_timer = 3.0f;
        }
    } else {
        ImGui::TextDisabled("No dormant images. Import art or disable placed objects first.");
    }

    int sel = selected_count();
    bool can_disable = (sel > 0) || (g_hl_obj >= 0 && g_hl_obj < g_no);
    if (ImGui::Button("Disable Selected Objects, Keep Images", ImVec2(-1, 0)) && can_disable) {
        int removed = mk2_disable_selected_assets_keep_images();
        snprintf(g_toast_msg, sizeof g_toast_msg,
                 removed ? "Disabled %d object(s); BDD images kept" : "No object selected", removed);
        g_toast_timer = 3.0f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Removes BDB placements only. The BDD images remain available as unused assets.");
}
