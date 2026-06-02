#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

void draw_mk2_safe_dedup_assistant(void)
{
    ImGui::Text("Safe Dedup Assistant");
    ImGui::TextDisabled("Rewrite duplicate-image placements to one kept image. Duplicate art stays dormant.");
    if (ImGui::Button("Load First Duplicate Pair", ImVec2(-1, 0))) {
        bool mirror = false;
        int keep = -1, rep = -1;
        if (find_first_duplicate_pair(&keep, &rep, &mirror)) {
            g_dedup_keep_img = keep;
            g_dedup_replace_img = rep;
            g_dedup_replace_is_mirror = mirror;
            stage_set_toast("Loaded duplicate pair");
        } else {
            stage_set_toast("No duplicate pair found");
        }
    }

    if (g_ni <= 0) {
        ImGui::TextDisabled("No images loaded.");
        return;
    }
    if (g_dedup_keep_img < 0) g_dedup_keep_img = 0;
    if (g_dedup_replace_img < 0) g_dedup_replace_img = 0;
    if (g_dedup_keep_img >= g_ni) g_dedup_keep_img = g_ni - 1;
    if (g_dedup_replace_img >= g_ni) g_dedup_replace_img = g_ni - 1;
    ImGui::SliderInt("Keep Image Slot", &g_dedup_keep_img, 0, g_ni - 1);
    ImGui::SliderInt("Replace Image Slot", &g_dedup_replace_img, 0, g_ni - 1);
    ImGui::Checkbox("Replacement is mirrored", &g_dedup_replace_is_mirror);

    Img *keep = &g_img[g_dedup_keep_img];
    Img *rep = &g_img[g_dedup_replace_img];
    bool match = keep && rep && keep != rep && image_pixels_match(keep, rep, g_dedup_replace_is_mirror);
    ImGui::Text("Keep 0x%02X (%dx%d), replace 0x%02X (%dx%d)",
                keep->idx, keep->w, keep->h, rep->idx, rep->w, rep->h);
    ImGui::Text("Uses: keep=%d replace=%d", image_use_count(keep->idx), image_use_count(rep->idx));
    if (match)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "Pixel match confirmed.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Images do not match with this mirror setting.");

    if (ImGui::Button("Rewrite Replace Placements To Keep", ImVec2(-1, 0))) {
        int changed = apply_safe_dedup(g_dedup_keep_img, g_dedup_replace_img, g_dedup_replace_is_mirror);
        char msg[128];
        snprintf(msg, sizeof msg, changed >= 0 ? "Rewrote %d placement(s)" : "Dedup rewrite refused", changed);
        stage_set_toast(msg);
    }
}
