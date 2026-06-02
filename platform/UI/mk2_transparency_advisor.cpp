#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>

void draw_mk2_transparency_advisor(void)
{
    ImGui::Text("Transparent Color Advisor");
    ImGui::TextDisabled("Finds the most common nonzero edge color in the active image.");
    int img_i = active_image_index();
    if (img_i < 0 || img_i >= g_ni) {
        ImGui::TextDisabled("No image selected.");
        return;
    }
    Img *im = &g_img[img_i];
    int edge_total = 0, edge_count = 0;
    int candidate = edge_candidate_index(im, &edge_count, &edge_total);
    ImGui::Text("Image 0x%02X  %dx%d  pal %d", im->idx, im->w, im->h, im->pal_idx);
    if (SDL_Texture *tex = editor_texture_at(img_i)) {
        float max_w = ImGui::GetContentRegionAvail().x;
        if (max_w > 180.0f) max_w = 180.0f;
        float sc = max_w / (float)(im->w > im->h ? im->w : im->h);
        if (sc > 2.0f) sc = 2.0f;
        if (sc < 0.1f) sc = 0.1f;
        draw_editor_texture_transparent(tex, im->w * sc, im->h * sc);
    }
    if (candidate < 0) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Edges are already transparent or empty.");
        return;
    }
    Uint32 c = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? g_pals[im->pal_idx][candidate] : 0;
    float pct = edge_total > 0 ? (edge_count * 100.0f) / (float)edge_total : 0.0f;
    ImGui::Text("Candidate index: %d  edge coverage: %.1f%%", candidate, pct);
    ImVec4 col(((c >> 16) & 0xFF) / 255.0f,
               ((c >> 8) & 0xFF) / 255.0f,
               (c & 0xFF) / 255.0f,
               1.0f);
    ImGui::ColorButton("Candidate Color", col, ImGuiColorEditFlags_NoTooltip, ImVec2(36, 24));
    if (pct >= 35.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "Likely matte/edge fill. Good transparency candidate.");
    else
        ImGui::TextDisabled("Low edge coverage; inspect before replacing.");
    if (ImGui::Button("Replace Candidate With Transparent 0", ImVec2(-1, 0))) {
        int changed = replace_image_index_with_zero(im, candidate);
        snprintf(g_toast_msg, sizeof g_toast_msg, "Replaced %d pixel(s) with transparency", changed);
        g_toast_timer = 3.0f;
    }
    if (ImGui::Button("Select All Objects Using This Image", ImVec2(-1, 0))) {
        int n = mk2_select_objects_by_image(im->idx);
        snprintf(g_toast_msg, sizeof g_toast_msg, "Selected %d placement(s)", n);
        g_toast_timer = 3.0f;
    }
}
