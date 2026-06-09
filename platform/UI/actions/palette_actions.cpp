#include "bg_editor_globals.h"
#include "undo_manager.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>
/* ---- palette merge ------------------------------------------------ */

void merge_duplicate_palettes(void)
{
    if (g_n_pals < 2) { stage_set_toast("Only one palette - nothing to merge"); return; }

    const int pal_cap = editor_project_palette_capacity();
    if (pal_cap <= 0) return;

    std::vector<int> remap((size_t)pal_cap, -1);
    for (int i = 0; i < g_n_pals; i++) remap[i] = i;
    for (int i = 0; i < g_n_pals; i++) {
        for (int j = i + 1; j < g_n_pals; j++) {
            if (remap[j] != j) continue;
            if (memcmp(g_pals[i], g_pals[j], 256 * sizeof(Uint32)) == 0)
                remap[j] = i;
        }
    }

    int merge_count = 0;
    for (int i = 0; i < g_n_pals; i++) if (remap[i] != i) merge_count++;
    if (merge_count == 0) { stage_set_toast("No duplicate palettes found"); return; }

    undo_save_ex("Merge Palettes");

    /* apply canonical remap to all image and object references */
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
            g_img[i].pal_idx = remap[g_img[i].pal_idx];
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals)
            g_obj[i].fl = remap[g_obj[i].fl];

    /* compact: renumber kept palettes sequentially */
    std::vector<std::array<Uint32, 256>> new_pals((size_t)pal_cap);
    std::vector<int> new_counts((size_t)pal_cap, 0);
    std::vector<std::array<char, 64>> new_names((size_t)pal_cap);
    std::vector<int> compact((size_t)pal_cap, -1);
    int new_n = 0;
    for (int i = 0; i < g_n_pals; i++) {
        if (remap[i] == i) {
            compact[i] = new_n;
            memcpy(new_pals[(size_t)new_n].data(), g_pals[i], sizeof(g_pals[0]));
            new_counts[(size_t)new_n] = g_pal_count[i];
            memcpy(new_names[(size_t)new_n].data(), g_pal_name[i], 64);
            new_n++;
        }
    }
    /* second pass: update to new sequential indices */
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
            g_img[i].pal_idx = compact[g_img[i].pal_idx];
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals)
            g_obj[i].fl = compact[g_obj[i].fl];

    editor_project_replace_palettes(
        reinterpret_cast<const Uint32 (*)[256]>(new_pals.data()),
        new_counts.data(),
        reinterpret_cast<const char (*)[64]>(new_names.data()),
        new_n,
        new_n);
    if (g_sel_pal >= g_n_pals) g_sel_pal = g_n_pals > 0 ? g_n_pals - 1 : 0;
    g_dirty = 1; g_need_rebuild = 1;

    char msg[64];
    snprintf(msg, sizeof(msg), "Merged %d duplicate palette(s); %d remain", merge_count, g_n_pals);
    stage_set_toast(msg);
}

static void update_palette_references(int src, int dst)
{
    const int pal_cap = editor_project_palette_capacity();
    if (src < 0 || src >= pal_cap || dst < 0 || dst >= pal_cap) return;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].pal_idx == src) g_img[i].pal_idx = dst;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].fl == src) g_obj[i].fl = dst;
}

static Uint32 blend_argb(Uint32 a, Uint32 b, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = (ar * (100 - pct) + br * pct + 50) / 100;
    int g = (ag * (100 - pct) + bg * pct + 50) / 100;
    int bl = (ab * (100 - pct) + bb * pct + 50) / 100;
    return 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)bl;
}

void build_blended_palette(int dst_count, int a, int b, int pct,
                           bool reverse, Uint32 *out)
{
    if (!out) return;
    memset(out, 0, sizeof(Uint32) * 256);
    if (dst_count < 1) dst_count = 1;
    if (dst_count > 256) dst_count = 256;
    for (int i = 1; i < dst_count; i++) {
        Uint32 ca = palette_argb_at(a, i);
        Uint32 cb = palette_argb_at(b, i);
        out[i] = reverse ? blend_argb(cb, ca, pct) : blend_argb(ca, cb, pct);
    }
}

int apply_palette_blend_tool(void)
{
    int a = g_palette_blend_a;
    int b = g_palette_blend_b;
    if (a < 0 || a >= g_n_pals || b < 0 || b >= g_n_pals || a == b)
        return -1;
    int need = (g_palette_blend_mode == 2) ? 2 : 1;
    if (g_palette_blend_create_new && !editor_project_reserve_palettes(g_n_pals + need))
        return -2;

    undo_save_ex("Blend Palettes");
    int dst_a = a;
    int dst_b = b;
    Uint32 new_a[256], new_b[256];
    int count_a = g_pal_count[a];
    int count_b = g_pal_count[b];
    build_blended_palette(count_a, a, b, g_palette_blend_strength, false, new_a);
    build_blended_palette(count_b, a, b, g_palette_blend_strength, true, new_b);

    if (g_palette_blend_mode == 0 || g_palette_blend_mode == 2) {
        if (g_palette_blend_create_new) {
            char name[64];
            snprintf(name, sizeof name, "%.28s_BLEND", g_pal_name[a]);
            dst_a = editor_project_append_palette_slot(name, count_a, new_a);
            if (dst_a < 0) return -2;
            update_palette_references(a, dst_a);
        } else {
            editor_project_set_palette_slot(dst_a, NULL, count_a, new_a);
        }
    }
    if (g_palette_blend_mode == 1 || g_palette_blend_mode == 2) {
        if (g_palette_blend_create_new) {
            char name[64];
            snprintf(name, sizeof name, "%.28s_BLEND", g_pal_name[b]);
            dst_b = editor_project_append_palette_slot(name, count_b, new_b);
            if (dst_b < 0) return -2;
            update_palette_references(b, dst_b);
        } else {
            editor_project_set_palette_slot(dst_b, NULL, count_b, new_b);
        }
    }

    g_sel_pal = dst_a;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_mk2_palette_sync_dirty = true;
    snprintf(g_palette_blend_status, sizeof g_palette_blend_status,
             "Blended palette %d and %d at %d%%.", a, b, g_palette_blend_strength);
    stage_set_toast(g_palette_blend_status);
    return need;
}

static int color_index_exact(const Uint32 *pal, int count, Uint32 color)
{
    for (int i = 1; i < count; i++)
        if ((pal[i] & 0x00FFFFFFu) == (color & 0x00FFFFFFu)) return i;
    return -1;
}

static void collect_palette_pixel_indexes(int pal, bool used_only, bool *used)
{
    memset(used, 0, sizeof(bool) * 256);
    used[0] = true;
    if (!used_only) {
        int count = (pal >= 0 && pal < g_n_pals) ? g_pal_count[pal] : 0;
        if (count > 256) count = 256;
        for (int i = 1; i < count; i++) used[i] = true;
        return;
    }
    for (int ii = 0; ii < g_ni; ii++) {
        if (g_img[ii].pal_idx != pal || !g_img[ii].pix) continue;
        size_t n = (size_t)g_img[ii].w * (size_t)g_img[ii].h;
        for (size_t k = 0; k < n; k++)
            used[g_img[ii].pix[k]] = true;
    }
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].fl != pal) continue;
        Img *im = img_find(g_obj[oi].ii);
        if (!im || !im->pix) continue;
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t k = 0; k < n; k++)
            used[im->pix[k]] = true;
    }
}

int apply_palette_union_merge_tool(void)
{
    int a = g_palette_blend_a;
    int b = g_palette_blend_b;
    if (a < 0 || a >= g_n_pals || b < 0 || b >= g_n_pals || a == b)
        return -1;
    if (!editor_project_reserve_palettes(g_n_pals + 1))
        return -2;

    bool used_a[256], used_b[256];
    int map_a[256], map_b[256];
    Uint32 merged[256];
    memset(map_a, 0, sizeof map_a);
    memset(map_b, 0, sizeof map_b);
    memset(merged, 0, sizeof merged);
    collect_palette_pixel_indexes(a, g_palette_merge_used_only, used_a);
    collect_palette_pixel_indexes(b, g_palette_merge_used_only, used_b);

    int next = 1;
    for (int pass = 0; pass < 2; pass++) {
        int pal = pass == 0 ? a : b;
        bool *used = pass == 0 ? used_a : used_b;
        int *map = pass == 0 ? map_a : map_b;
        for (int i = 1; i < 256; i++) {
            if (!used[i]) continue;
            Uint32 color = palette_argb_at(pal, i);
            int idx = color_index_exact(merged, next, color);
            if (idx < 0) {
                if (next >= 256) {
                    snprintf(g_palette_blend_status, sizeof g_palette_blend_status,
                             "Merge refused: union needs more than 255 colors.");
                    return -3;
                }
                idx = next++;
                merged[idx] = color;
            }
            map[i] = idx;
        }
    }

    undo_save_ex("Merge Palette Union");
    char name[64];
    snprintf(name, sizeof name, "%.20s_%.20s_MRG", g_pal_name[a], g_pal_name[b]);
    int dst = editor_project_append_palette_slot(name, next, merged);
    if (dst < 0) return -2;

    int remapped_images = 0;
    int updated_objects = 0;
    for (int ii = 0; ii < g_ni; ii++) {
        Img *im = &g_img[ii];
        int src_pal = (im->pal_idx == a || im->pal_idx == b) ? im->pal_idx : -1;
        if (src_pal < 0) continue;
        int *map = src_pal == a ? map_a : map_b;
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t k = 0; k < n; k++) {
            Uint8 v = im->pix[k];
            if (v > 0 && map[v] > 0) im->pix[k] = (Uint8)map[v];
        }
        im->pal_idx = dst;
        remapped_images++;
    }
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].fl != a && g_obj[oi].fl != b) continue;
        Img *im = img_find(g_obj[oi].ii);
        if (im && im->pal_idx == dst) {
            g_obj[oi].fl = dst;
            updated_objects++;
        }
    }

    g_sel_pal = dst;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_mk2_palette_sync_dirty = true;
    snprintf(g_palette_blend_status, sizeof g_palette_blend_status,
             "Merged palettes into %d colors, remapped %d image(s), %d object(s).",
             next - 1, remapped_images, updated_objects);
    stage_set_toast(g_palette_blend_status);
    return remapped_images;
}

/* ---- batch palette rebuild ---------------------------------------- */

void batch_palette_rebuild(void)
{
    /* For each palette: collect all images using it, build a union of used
       indices, remap every image pixel to a contiguous 1..N range, and update
       the shared palette entries accordingly. Single undo snapshot. */
    undo_save_ex("Batch Pal Rebuild");

    int rebuilt = 0;
    for (int p = 0; p < g_n_pals; p++) {
        /* find all images on this palette */
        bool any = false;
        for (int ii = 0; ii < g_ni && !any; ii++)
            if (g_img[ii].pal_idx == p && g_img[ii].pix) any = true;
        if (!any) continue;

        /* build union of used pixel indices across all images on this palette */
        bool used[256];
        memset(used, 0, sizeof used);
        for (int ii = 0; ii < g_ni; ii++) {
            if (g_img[ii].pal_idx != p || !g_img[ii].pix) continue;
            size_t n = (size_t)g_img[ii].w * (size_t)g_img[ii].h;
            for (size_t k = 0; k < n; k++)
                if (g_img[ii].pix[k] > 0) used[g_img[ii].pix[k]] = true;
        }

        /* build remap: old index → new contiguous index starting at 1 */
        int map[256]; memset(map, 0, sizeof map);
        Uint32 new_pal[256]; memset(new_pal, 0, sizeof new_pal);
        int next = 1;
        for (int i = 1; i < 256; i++) {
            if (!used[i]) continue;
            map[i] = next;
            new_pal[next] = g_pals[p][i];
            next++;
        }
        if (next == 1) continue; /* all-zero palette, skip */

        /* remap pixels in all images on this palette */
        for (int ii = 0; ii < g_ni; ii++) {
            if (g_img[ii].pal_idx != p || !g_img[ii].pix) continue;
            size_t n = (size_t)g_img[ii].w * (size_t)g_img[ii].h;
            for (size_t k = 0; k < n; k++)
                if (g_img[ii].pix[k] > 0) g_img[ii].pix[k] = (Uint8)map[g_img[ii].pix[k]];
        }

        editor_project_set_palette_slot(p, NULL, next, new_pal);
        rebuilt++;
    }

    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;

    char toast[128];
    snprintf(toast, sizeof toast, "Rebuilt %d palette(s)", rebuilt);
    stage_set_toast(toast);
}
