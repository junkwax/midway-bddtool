#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>

static void palette_usage_report(int pal, char *out, size_t outsz)
{
    bool used[256];
    memset(used, 0, sizeof used);
    int object_count = 0, image_count = 0, max_index = 0;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].fl != pal) continue;
        Img *im = img_find(g_obj[oi].ii);
        if (!im || !im->pix) continue;
        object_count++;
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t i = 0; i < n; i++) {
            int v = im->pix[i];
            if (v <= 0) continue;
            used[v] = true;
            if (v > max_index) max_index = v;
        }
    }
    if (g_palette_include_unused_images) {
        for (int ii = 0; ii < g_ni; ii++) {
            if (g_img[ii].pal_idx != pal || image_use_count(g_img[ii].idx) > 0) continue;
            if (!g_img[ii].pix) continue;
            image_count++;
            size_t n = (size_t)g_img[ii].w * (size_t)g_img[ii].h;
            for (size_t i = 0; i < n; i++) {
                int v = g_img[ii].pix[i];
                if (v <= 0) continue;
                used[v] = true;
                if (v > max_index) max_index = v;
            }
        }
    }
    int used_count = 0;
    for (int i = 1; i < 256; i++) if (used[i]) used_count++;
    const char *tier = max_index <= 15 ? "15-color compact" :
                       max_index <= 31 ? "31-color moderate" :
                       max_index <= 63 ? "63-color stock-like" :
                                         "high-color expensive";
    snprintf(out, outsz,
             "Palette %d (%s)\nobjects: %d\nunused images included: %d\nused nonzero indexes: %d\nmax index: %d\nrecommendation: %s",
             pal, (pal >= 0 && pal < g_n_pals) ? g_pal_name[pal] : "", object_count, image_count,
             used_count, max_index, tier);
}

void draw_mk2_palette_usage_optimizer(void)
{
    ImGui::Text("Palette Usage Optimizer");
    ImGui::TextDisabled("Shows whether a palette is compact, stock-like, or drifting into expensive high indexes.");
    if (g_n_pals <= 0) {
        ImGui::TextDisabled("No palettes loaded.");
        return;
    }
    if (g_sel_pal < 0 || g_sel_pal >= g_n_pals) g_sel_pal = 0;
    if (ImGui::BeginCombo("Palette", g_pal_name[g_sel_pal])) {
        for (int p = 0; p < g_n_pals; p++) {
            char label[96];
            snprintf(label, sizeof label, "%d: %s", p, g_pal_name[p]);
            if (ImGui::Selectable(label, p == g_sel_pal)) g_sel_pal = p;
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Include unused images assigned to palette", &g_palette_include_unused_images);

    bool used[256];
    memset(used, 0, sizeof used);
    int object_count = 0, max_index = 0;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].fl != g_sel_pal) continue;
        Img *im = img_find(g_obj[oi].ii);
        if (!im || !im->pix) continue;
        object_count++;
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t i = 0; i < n; i++) {
            int v = im->pix[i];
            if (v <= 0) continue;
            used[v] = true;
            if (v > max_index) max_index = v;
        }
    }
    int used_count = 0;
    for (int i = 1; i < 256; i++) if (used[i]) used_count++;
    ImGui::Text("Objects: %d  used indexes: %d  max index: %d", object_count, used_count, max_index);
    if (max_index <= 15)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Compact: fits the 15-color portal-core style.");
    else if (max_index <= 63)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "Stock-like: reasonable, but costs more than compact art.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "High-color: likely expensive in VROM.");

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int idx = row * 16 + col;
            if (col > 0) ImGui::SameLine();
            Uint32 c = g_pals[g_sel_pal][idx];
            float alpha = idx == 0 ? 0.20f : (used[idx] ? 1.0f : 0.22f);
            ImGui::PushID(idx);
            ImGui::ColorButton("##paluse", ImVec4(((c >> 16) & 0xFF)/255.0f, ((c >> 8) & 0xFF)/255.0f, (c & 0xFF)/255.0f, alpha),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(11,11));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%d %s", idx, used[idx] ? "used" : "unused");
            ImGui::PopID();
        }
    }
    if (ImGui::Button("Select Objects Using Palette", ImVec2(-1, 0))) {
        int n = 0;
        editor_project_clear_selection();
        for (int i = 0; i < g_no; i++) {
            if (g_obj[i].fl != g_sel_pal) continue;
            g_sel_flags[i] = 1;
            g_hl_obj = i;
            n++;
        }
        char msg[96];
        snprintf(msg, sizeof msg, "Selected %d object(s) using palette", n);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Copy Palette Usage Report", ImVec2(-1, 0))) {
        char report[1024];
        palette_usage_report(g_sel_pal, report, sizeof report);
        ImGui::SetClipboardText(report);
        stage_set_toast("Copied palette usage report");
    }
}
