#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>

void draw_mk2_trim_transparent_border_tool(void)
{
    ImGui::Text("Trim Transparent Border");
    ImGui::TextDisabled("Crop empty index-0 borders and move placements so the art stays visually aligned.");
    int img_i = active_image_index();
    if (img_i < 0 || img_i >= g_ni) {
        ImGui::TextDisabled("No active image.");
        return;
    }
    Img *im = &g_img[img_i];
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) {
        ImGui::TextDisabled("Image is empty/transparent.");
        return;
    }
    int save = im->w * im->h - (x2 - x1 + 1) * (y2 - y1 + 1);
    ImGui::Text("Active image 0x%02X: %dx%d", im->idx, im->w, im->h);
    ImGui::Text("Content bounds: x %d..%d, y %d..%d", x1, x2, y1, y2);
    ImGui::Text("Raw pixels saved: %d", save);
    if (save <= 0) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "No transparent border to trim.");
    }
    if (ImGui::Button("Trim Active Image", ImVec2(-1, 0))) {
        int changed = trim_image_transparent_border(img_i, true);
        char msg[128];
        snprintf(msg, sizeof msg, changed > 0 ? "Trimmed %d transparent pixel(s)" : "No trim applied", changed);
        stage_set_toast(msg);
    }
}

void draw_mk2_palette_remap_compress_tool(void)
{
    ImGui::Text("Palette Remap / Compress");
    ImGui::TextDisabled("Compact an active image to lower palette indexes when it already uses few enough colors.");
    int img_i = active_image_index();
    if (img_i < 0 || img_i >= g_ni) {
        ImGui::TextDisabled("No active image.");
        return;
    }
    Img *im = &g_img[img_i];
    bool used[256];
    memset(used, 0, sizeof used);
    int max_idx = 0, used_count = 0;
    if (im->pix) {
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t i = 0; i < n; i++) {
            int v = im->pix[i];
            if (v <= 0) continue;
            used[v] = true;
            if (v > max_idx) max_idx = v;
        }
        for (int i = 1; i < 256; i++) if (used[i]) used_count++;
    }
    ImGui::Text("Image 0x%02X: used colors %d, max index %d", im->idx, used_count, max_idx);
    ImGui::InputInt("Target Nonzero Colors", &g_palette_compress_target);
    if (g_palette_compress_target < 1) g_palette_compress_target = 1;
    if (g_palette_compress_target > 255) g_palette_compress_target = 255;
    ImGui::Checkbox("Create new palette", &g_palette_compress_new_palette);
    if (used_count > g_palette_compress_target)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Too many used colors for this target.");
    else
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "Can remap into indexes 1..%d.", g_palette_compress_target);
    if (ImGui::Button("Compress Active Image Palette", ImVec2(-1, 0))) {
        int changed = compress_active_image_palette(img_i, g_palette_compress_target, true);
        char msg[128];
        snprintf(msg, sizeof msg, changed >= 0 ? "Compressed %d color(s)" : "Palette compression refused", changed);
        stage_set_toast(msg);
    }
}

void draw_mk2_batch_image_cleanup_tool(void)
{
    ImGui::Text("Batch Image Cleanup");
    ImGui::TextDisabled("Apply the proven trim/remap fixes across the whole BDD before packaging.");
    ImGui::InputInt("Min Trim Pixels", &g_batch_trim_min_saved);
    if (g_batch_trim_min_saved < 1) g_batch_trim_min_saved = 1;
    ImGui::InputInt("Compress Target Colors", &g_palette_compress_target);
    if (g_palette_compress_target < 1) g_palette_compress_target = 1;
    if (g_palette_compress_target > 255) g_palette_compress_target = 255;
    ImGui::Checkbox("New palette per compressed image", &g_palette_compress_new_palette);
    ImGui::InputInt("Preview Rows", &g_batch_preview_limit);
    if (g_batch_preview_limit < 1) g_batch_preview_limit = 1;
    if (g_batch_preview_limit > 64) g_batch_preview_limit = 64;

    int trim_count = 0, trim_pixels = 0;
    int compress_count = 0, palette_slots_needed = 0;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!im->pix || im->w <= 0 || im->h <= 0) continue;
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) {
            int save = im->w * im->h - (x2 - x1 + 1) * (y2 - y1 + 1);
            if (save >= g_batch_trim_min_saved) {
                trim_count++;
                trim_pixels += save;
            }
        }
        int used_count = 0, max_idx = 0;
        image_palette_usage_stats(im, &used_count, &max_idx);
        if (im->pal_idx >= 0 && im->pal_idx < g_n_pals &&
            used_count > 0 && used_count <= g_palette_compress_target &&
            max_idx > g_palette_compress_target)
        {
            compress_count++;
            if (g_palette_compress_new_palette) palette_slots_needed++;
        }
    }

    ImGui::Text("Trim candidates: %d image(s), %d raw pixels saved", trim_count, trim_pixels);
    ImGui::Text("Compress candidates: %d image(s), %d palette slot(s) needed",
                compress_count, palette_slots_needed);
    int palette_slots_available = editor_project_palette_capacity() - g_n_pals;
    if (palette_slots_available < 0) palette_slots_available = 0;
    if (g_palette_compress_new_palette && palette_slots_needed > palette_slots_available) {
        ImGui::TextDisabled("Palette storage will grow for %d new slot(s).", palette_slots_needed);
    } else if (!g_palette_compress_new_palette) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                           "In-place batch compression can affect other images sharing those palettes.");
    }

    if (ImGui::BeginTable("batch_cleanup_candidates", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("img", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("trim", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();
        int shown = 0;
        for (int i = 0; i < g_ni && shown < g_batch_preview_limit; i++) {
            Img *im = &g_img[i];
            if (!im->pix || im->w <= 0 || im->h <= 0) continue;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0, save = 0;
            if (image_nonzero_bounds(im, &x1, &y1, &x2, &y2))
                save = im->w * im->h - (x2 - x1 + 1) * (y2 - y1 + 1);
            int used_count = 0, max_idx = 0;
            image_palette_usage_stats(im, &used_count, &max_idx);
            bool can_trim = save >= g_batch_trim_min_saved;
            bool can_compress = im->pal_idx >= 0 && im->pal_idx < g_n_pals &&
                                used_count > 0 && used_count <= g_palette_compress_target &&
                                max_idx > g_palette_compress_target;
            if (!can_trim && !can_compress) continue;
            shown++;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", im->idx);
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", im->w, im->h);
            ImGui::TableNextColumn(); ImGui::Text("%d", image_object_ref_count(im->idx));
            ImGui::TableNextColumn(); ImGui::Text("%d", save);
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", used_count, max_idx);
            ImGui::TableNextColumn();
            if (can_trim && can_compress) ImGui::TextUnformatted("trim + remap");
            else if (can_trim) ImGui::TextUnformatted("trim");
            else ImGui::TextUnformatted("remap");
        }
        ImGui::EndTable();
        if (shown == 0)
            ImGui::TextDisabled("No cleanup candidates at the current thresholds.");
    }

    if (ImGui::Button("Batch Trim Eligible Images", ImVec2(-1, 0))) {
        int changed = 0, saved = 0;
        if (trim_count > 0) {
            undo_save();
            for (int i = 0; i < g_ni; i++) {
                Img *im = &g_img[i];
                int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                if (!image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) continue;
                int save = im->w * im->h - (x2 - x1 + 1) * (y2 - y1 + 1);
                if (save < g_batch_trim_min_saved) continue;
                int rc = trim_image_transparent_border(i, false);
                if (rc > 0) { changed++; saved += rc; }
            }
        }
        char msg[160];
        snprintf(msg, sizeof msg, "Batch trimmed %d image(s), saved %d raw pixels", changed, saved);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Batch Compress Eligible Palettes", ImVec2(-1, 0))) {
        if (g_palette_compress_new_palette &&
            !editor_project_reserve_palettes(g_n_pals + palette_slots_needed)) {
            stage_set_toast("Batch compression refused: not enough palette slots");
        } else {
            int changed = 0, colors = 0;
            if (compress_count > 0) {
                undo_save();
                for (int i = 0; i < g_ni; i++) {
                    Img *im = &g_img[i];
                    int used_count = 0, max_idx = 0;
                    image_palette_usage_stats(im, &used_count, &max_idx);
                    if (im->pal_idx < 0 || im->pal_idx >= g_n_pals ||
                        used_count <= 0 || used_count > g_palette_compress_target ||
                        max_idx <= g_palette_compress_target)
                        continue;
                    int rc = compress_active_image_palette(i, g_palette_compress_target, false);
                    if (rc >= 0) { changed++; colors += rc; }
                }
            }
            char msg[160];
            snprintf(msg, sizeof msg, "Batch remapped %d image(s), %d color reference(s)", changed, colors);
            stage_set_toast(msg);
        }
    }
}
