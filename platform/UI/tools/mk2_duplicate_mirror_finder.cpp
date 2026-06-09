#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdio.h>
#include <vector>

struct DuplicateImageCandidate {
    int a;
    int b;
    int mirror;
    int pixels;
};

static void add_duplicate_candidate(DuplicateImageCandidate *top, int top_n, const DuplicateImageCandidate *c)
{
    if (!top || !c) return;
    for (int i = 0; i < top_n; i++) {
        if (c->pixels <= top[i].pixels) continue;
        for (int j = top_n - 1; j > i; j--) top[j] = top[j - 1];
        top[i] = *c;
        return;
    }
}

void draw_mk2_duplicate_mirror_finder(void)
{
    ImGui::Text("Duplicate / Mirror Finder");
    ImGui::TextDisabled("Finds identical or mirrored BDD images that could share one image plus flipped placements.");
    ImGui::InputInt("Min Pixels", &g_dup_min_pixels);
    ImGui::Checkbox("Include mirrored matches", &g_dup_include_mirrors);
    if (g_dup_min_pixels < 1) g_dup_min_pixels = 1;

    std::vector<Uint32> hash((size_t)g_ni, 0);
    std::vector<Uint32> mirror_hash((size_t)g_ni, 0);
    for (int i = 0; i < g_ni; i++) {
        hash[(size_t)i] = image_pixel_hash(&g_img[i], false);
        mirror_hash[(size_t)i] = image_pixel_hash(&g_img[i], true);
    }

    DuplicateImageCandidate top[10];
    for (int i = 0; i < 10; i++) { top[i].a = -1; top[i].b = -1; top[i].mirror = 0; top[i].pixels = -1; }

    for (int a = 0; a < g_ni; a++) {
        Img *ia = &g_img[a];
        if (!ia->pix || ia->w * ia->h < g_dup_min_pixels) continue;
        for (int b = a + 1; b < g_ni; b++) {
            Img *ib = &g_img[b];
            if (!ib->pix || ia->w != ib->w || ia->h != ib->h) continue;
            int pixels = ia->w * ia->h;
            if (hash[(size_t)a] == hash[(size_t)b] && image_pixels_match(ia, ib, false)) {
                DuplicateImageCandidate c = { a, b, 0, pixels };
                add_duplicate_candidate(top, 10, &c);
            } else if (g_dup_include_mirrors && hash[(size_t)a] == mirror_hash[(size_t)b] && image_pixels_match(ia, ib, true)) {
                DuplicateImageCandidate c = { a, b, 1, pixels };
                add_duplicate_candidate(top, 10, &c);
            }
        }
    }

    size_t total_savings = 0;
    for (int i = 0; i < 10; i++) if (top[i].a >= 0) total_savings += (size_t)top[i].pixels;
    ImGui::Text("Top listed raw savings: 0x%zX bytes", total_savings);
    if (ImGui::BeginTable("dup_mirror_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 190))) {
        ImGui::TableSetupColumn("keep", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("dup", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableSetupColumn("use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("save", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 10; i++) {
            if (top[i].a < 0) continue;
            Img *a = &g_img[top[i].a];
            Img *b = &g_img[top[i].b];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", a->idx);
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", b->idx);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(top[i].mirror ? "mirror" : "exact");
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", a->w, a->h);
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", image_use_count(a->idx), image_use_count(b->idx));
            ImGui::TableNextColumn(); ImGui::Text("0x%X", top[i].pixels);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Sel A")) mk2_select_objects_by_image(a->idx);
            ImGui::SameLine();
            if (ImGui::SmallButton("Sel B")) mk2_select_objects_by_image(b->idx);
            ImGui::SameLine();
            if (ImGui::SmallButton("Rewrite")) {
                int changed = apply_safe_dedup(top[i].a, top[i].b, top[i].mirror != 0);
                char msg[64];
                snprintf(msg, sizeof msg, changed >= 0 ? "Rewrote %d placement(s)" : "Refused", changed);
                stage_set_toast(msg);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remap all placements of the duplicate image to the keep image.\n(Duplicate art stays in BDD but nothing references it.)");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    int pair_count = 0;
    for (int i = 0; i < 10; i++) if (top[i].a >= 0) pair_count++;
    if (pair_count > 0) {
        char batch_lbl[48];
        snprintf(batch_lbl, sizeof batch_lbl, "Rewrite All %d Listed Pairs", pair_count);
        if (ImGui::Button(batch_lbl, ImVec2(-1, 0))) {
            int total_changed = 0;
            for (int i = 0; i < 10; i++) {
                if (top[i].a < 0) continue;
                int changed = apply_safe_dedup(top[i].a, top[i].b, top[i].mirror != 0);
                if (changed > 0) total_changed += changed;
            }
            char msg[64];
            snprintf(msg, sizeof msg, "Batch rewrote %d placement(s)", total_changed);
            stage_set_toast(msg);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rewrite all listed duplicate/mirror pairs in one shot.");
    }
    ImGui::TextDisabled("'Rewrite' remaps placements; duplicate art stays dormant in BDD.");
}
