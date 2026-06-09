#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <stdio.h>
#include <vector>

/* ----------------------------------------------------------------------------
 * One-click ROM space reclaim.
 *
 * Composes the existing safe per-image operations into a single whole-stage
 * pass with a before/after payload report, so a level can be shrunk for ROM
 * without hunting sprite-by-sprite. Lossless by default; an opt-in pass merges
 * near-duplicate palette colors for extra savings.
 *
 * Payload model mirrors mk2_collect_budget(): pixel data dominates, and an
 * image's bit depth is driven by its highest used palette index, so collapsing
 * indices (lossless) or near-duplicate colors (lossy) can drop a sprite to a
 * smaller bpp with no or minimal visible change.
 * ------------------------------------------------------------------------- */

static bool g_reclaim_merge_dupes = true;
static bool g_reclaim_include_mirrors = false;
static bool g_reclaim_drop_unused = true;
static bool g_reclaim_lossy_colors = false;
static int  g_reclaim_color_distance = 6;

static bool g_reclaim_has_report = false;
static size_t g_reclaim_before = 0;
static size_t g_reclaim_after = 0;
static int g_reclaim_trimmed = 0;
static int g_reclaim_merged_dupes = 0;
static int g_reclaim_removed = 0;
static int g_reclaim_recolored = 0;

static int reclaim_argb_distance2(Uint32 a, Uint32 b)
{
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int dr = ar - br, dg = ag - bg, db = ab - bb;
    return dr * dr + dg * dg + db * db;
}

/* Remap one image's pixels so near-duplicate colors collapse onto the lowest
   index of their group. Only pixels are touched; the following palette
   compaction pass drops the now-unused indices and lowers bpp. Returns the
   number of pixels remapped. */
static int reclaim_merge_image_colors(Img *im, int distance)
{
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return 0;
    int pal = im->pal_idx;
    if (pal < 0 || pal >= g_n_pals) return 0;
    int thresh2 = distance * distance * 3;

    int map[256];
    for (int i = 0; i < 256; i++) map[i] = i;
    for (int a = 1; a < 256; a++) {
        if (map[a] != a) continue;
        Uint32 ca = g_pals[pal][a];
        for (int b = a + 1; b < 256; b++) {
            if (map[b] != b) continue;
            if (reclaim_argb_distance2(ca, g_pals[pal][b]) <= thresh2)
                map[b] = a;
        }
    }

    int changed = 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t k = 0; k < n; k++) {
        Uint8 v = im->pix[k];
        if (v == 0) continue;
        if (map[v] != v) { im->pix[k] = (Uint8)map[v]; changed++; }
    }
    return changed;
}

/* Repoint object references off duplicate images onto the first identical one,
   leaving the duplicates unused so the drop-unused step can reclaim them. */
static int reclaim_merge_duplicate_images(bool include_mirrors)
{
    int merged = 0;
    std::vector<char> gone((size_t)(g_ni > 0 ? g_ni : 0), 0);
    for (int keep = 0; keep < g_ni; keep++) {
        if (gone[(size_t)keep] || !g_img[keep].pix) continue;
        for (int rep = keep + 1; rep < g_ni; rep++) {
            if (gone[(size_t)rep] || !g_img[rep].pix) continue;
            bool mirror = false;
            if (image_pixels_match(&g_img[keep], &g_img[rep], false)) {
                /* exact match */
            } else if (include_mirrors && image_pixels_match(&g_img[keep], &g_img[rep], true)) {
                mirror = true;
            } else {
                continue;
            }
            int rep_idx = g_img[rep].idx;
            int keep_idx = g_img[keep].idx;
            for (int o = 0; o < g_no; o++) {
                if (g_obj[o].ii != rep_idx) continue;
                g_obj[o].ii = keep_idx;
                if (mirror) {
                    g_obj[o].hfl = !g_obj[o].hfl;
                    g_obj[o].wx = (g_obj[o].wx & ~0x10) | (g_obj[o].hfl ? 0x10 : 0);
                }
            }
            gone[(size_t)rep] = 1;
            merged++;
        }
    }
    return merged;
}

static int reclaim_remove_unused_images(void)
{
    int removed = 0;
    for (int i = g_ni - 1; i >= 0; i--) {
        if (image_use_count(g_img[i].idx) != 0) continue;
        if (editor_project_delete_image_slot(i)) removed++;
    }
    return removed;
}

static void reclaim_run(void)
{
    undo_save_ex("Reclaim ROM Space");
    g_reclaim_before = mk2_collect_budget().estimated_payload;
    g_reclaim_trimmed = 0;
    g_reclaim_merged_dupes = 0;
    g_reclaim_removed = 0;
    g_reclaim_recolored = 0;

    /* 1. Lossless: tight-trim every sprite. */
    for (int i = 0; i < g_ni; i++) {
        if (!g_img[i].pix) continue;
        if (trim_image_transparent_border(i, false) > 0)
            g_reclaim_trimmed++;
    }

    /* 2. Optional lossy: collapse near-duplicate colors (pixels only). */
    if (g_reclaim_lossy_colors && g_reclaim_color_distance > 0) {
        for (int i = 0; i < g_ni; i++) {
            if (reclaim_merge_image_colors(&g_img[i], g_reclaim_color_distance) > 0)
                g_reclaim_recolored++;
        }
    }

    /* 3. Lossless: drop unused palette indices and lower bpp where possible. */
    compact_palettes_for_image_range(0, g_ni, false);

    /* 4. Lossless: fold identical images together. */
    if (g_reclaim_merge_dupes)
        g_reclaim_merged_dupes = reclaim_merge_duplicate_images(g_reclaim_include_mirrors);

    /* 5. Reclaim images that no object references anymore. */
    if (g_reclaim_drop_unused)
        g_reclaim_removed = reclaim_remove_unused_images();

    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    g_reclaim_after = mk2_collect_budget().estimated_payload;
    g_reclaim_has_report = true;

    char toast[128];
    size_t saved = g_reclaim_before > g_reclaim_after ? g_reclaim_before - g_reclaim_after : 0;
    snprintf(toast, sizeof toast, "Reclaimed 0x%zX bytes (%.1f%%)",
             saved, g_reclaim_before ? 100.0 * (double)saved / (double)g_reclaim_before : 0.0);
    stage_set_toast(toast);
}

void draw_mk2_rom_space_reclaim(void)
{
    ImGui::Text("Reclaim ROM Space");
    if (g_ni == 0) {
        ImGui::TextDisabled("No images loaded.");
        return;
    }

    Mk2Budget b = mk2_collect_budget();
    size_t pixel_bytes = b.raw_image_bytes;
    size_t pal_bytes = (size_t)b.palette_entries * 2u;
    size_t hdr_bytes = (size_t)g_no * 8u + (size_t)g_ni * 12u + (size_t)g_bdb_num_modules * 16u;
    size_t total = b.estimated_payload;

    ImGui::Text("Current payload estimate: 0x%zX bytes", total);
    if (ImGui::BeginTable("reclaim_breakdown", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("part");
        ImGui::TableSetupColumn("bytes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();
        struct Row { const char *name; size_t bytes; };
        Row rows[3] = {
            { "Sprite pixels", pixel_bytes },
            { "Palette colors", pal_bytes },
            { "Headers/objects/modules", hdr_bytes },
        };
        for (int i = 0; i < 3; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rows[i].name);
            ImGui::TableNextColumn(); ImGui::Text("0x%zX", rows[i].bytes);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f%%", total ? 100.0 * (double)rows[i].bytes / (double)total : 0.0);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Sprite pixels dominate; bit depth follows each image's highest used color index.");

    ImGui::SeparatorText("Lossless");
    ImGui::Checkbox("Merge identical images", &g_reclaim_merge_dupes);
    ImGui::SameLine();
    ImGui::Checkbox("incl. mirrored", &g_reclaim_include_mirrors);
    ImGui::Checkbox("Remove unused art", &g_reclaim_drop_unused);
    ImGui::TextDisabled("Always: tight-trim sprites and compact palettes to the smallest bit depth.");

    ImGui::SeparatorText("Optional lossy");
    ImGui::Checkbox("Merge near-duplicate colors", &g_reclaim_lossy_colors);
    if (g_reclaim_lossy_colors) {
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("Color distance", &g_reclaim_color_distance, 1, 32);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Per-color RGB distance. Higher merges more aggressively (more savings, more visible change). Distance 0-4 is usually imperceptible.");
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f),
                           "Lossy: collapses similar colors so sprites need fewer bits.");
    }

    if (ImGui::Button("Reclaim ROM Space", ImVec2(-1, 0)))
        reclaim_run();
    ImGui::TextDisabled("Undoable. Save All still writes backups before overwriting.");

    if (g_reclaim_has_report) {
        ImGui::SeparatorText("Last reclaim");
        size_t saved = g_reclaim_before > g_reclaim_after ? g_reclaim_before - g_reclaim_after : 0;
        double pct = g_reclaim_before ? 100.0 * (double)saved / (double)g_reclaim_before : 0.0;
        ImGui::Text("Before: 0x%zX   After: 0x%zX", g_reclaim_before, g_reclaim_after);
        ImGui::TextColored(saved > 0 ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Saved 0x%zX bytes (%.1f%%)", saved, pct);
        ImGui::TextDisabled("%d trimmed, %d palette-recolored, %d duplicate(s) merged, %d image(s) removed",
                            g_reclaim_trimmed, g_reclaim_recolored,
                            g_reclaim_merged_dupes, g_reclaim_removed);
    }
}
