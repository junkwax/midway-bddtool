#include "bg_editor_globals.h"
#include "libs/stb_image.h"
#include "undo_manager.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
static void path_basename_no_ext(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static void path_replace_ext(const char *path, const char *ext, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s", path ? path : "");
    char *dot = strrchr(out, '.');
    if (!dot) {
        snprintf(out + strlen(out), outsz - strlen(out), "%s", ext);
        return;
    }
    snprintf(dot, outsz - (size_t)(dot - out), "%s", ext);
}

static bool copy_file_binary(const char *src, const char *dst)
{
    if (src && dst && strcasecmp(src, dst) == 0) return true;
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[32768];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

bool mk2_import_workspace_apply(void)
{
    if (!g_import_src_bdb[0]) {
        snprintf(g_import_status, sizeof g_import_status, "Choose a source BDB.");
        return false;
    }
    char src_bdd[512];
    if (g_import_src_bdd[0])
        snprintf(src_bdd, sizeof src_bdd, "%s", g_import_src_bdd);
    else
        path_replace_ext(g_import_src_bdb, ".BDD", src_bdd, sizeof src_bdd);

    if (!stage_file_exists(g_import_src_bdb) || !stage_file_exists(src_bdd)) {
        snprintf(g_import_status, sizeof g_import_status, "Source BDB/BDD pair is incomplete.");
        return false;
    }
    if (!ensure_dir_recursive(g_import_stage_dir)) {
        snprintf(g_import_status, sizeof g_import_status, "Could not create destination folder.");
        return false;
    }

    char base[64];
    if (g_import_stage_name[0])
        snprintf(base, sizeof base, "%s", g_import_stage_name);
    else
        path_basename_no_ext(g_import_src_bdb, base, sizeof base);
    char dst_bdb[640], dst_bdd[640], fname[96];
    snprintf(fname, sizeof fname, "%s.BDB", base);
    path_join(dst_bdb, sizeof dst_bdb, g_import_stage_dir, fname);
    snprintf(fname, sizeof fname, "%s.BDD", base);
    path_join(dst_bdd, sizeof dst_bdd, g_import_stage_dir, fname);

    if (!copy_file_binary(g_import_src_bdb, dst_bdb) || !copy_file_binary(src_bdd, dst_bdd)) {
        snprintf(g_import_status, sizeof g_import_status, "Copy failed.");
        return false;
    }

    char manifest[640];
    path_join(manifest, sizeof manifest, g_import_stage_dir, "stage_import_manifest.json");
    FILE *f = fopen(manifest, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"source_type\": \"bdb_bdd_import\",\n");
        fprintf(f, "  \"stage_name\": "); json_write_string(f, base); fprintf(f, ",\n");
        fprintf(f, "  \"display_name\": "); json_write_string(f, g_import_display_name); fprintf(f, ",\n");
        fprintf(f, "  \"source_bdb\": "); json_write_string(f, g_import_src_bdb); fprintf(f, ",\n");
        fprintf(f, "  \"source_bdd\": "); json_write_string(f, src_bdd); fprintf(f, ",\n");
        fprintf(f, "  \"editable_bdb\": "); json_write_string(f, dst_bdb); fprintf(f, ",\n");
        fprintf(f, "  \"editable_bdd\": "); json_write_string(f, dst_bdd); fprintf(f, "\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    snprintf(g_stage_dir, sizeof g_stage_dir, "%s", g_import_stage_dir);
    snprintf(g_stage_internal_name, sizeof g_stage_internal_name, "%s", base);
    snprintf(g_stage_display_name, sizeof g_stage_display_name, "%s", g_import_display_name);
    path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");

    if (g_import_open_after) {
        request_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, dst_bdb);
    }
    snprintf(g_import_status, sizeof g_import_status, "Imported workspace: %s", dst_bdb);
    stage_set_toast("Imported MK2 BDB/BDD workspace");
    return true;
}

static unsigned short rgb555_from_rgba(int r, int g, int b)
{
    int r5 = (r * 31 + 127) / 255;
    int g5 = (g * 31 + 127) / 255;
    int b5 = (b * 31 + 127) / 255;
    if (r5 > 31) r5 = 31;
    if (g5 > 31) g5 = 31;
    if (b5 > 31) b5 = 31;
    return (unsigned short)((r5 << 10) | (g5 << 5) | b5);
}

static Uint32 rgb555_to_argb(unsigned short c)
{
    int r = (c >> 10) & 31;
    int g = (c >> 5) & 31;
    int b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

static int rgb555_distance(unsigned short a, unsigned short b)
{
    int ar = (a >> 10) & 31, ag = (a >> 5) & 31, ab = a & 31;
    int br = (b >> 10) & 31, bg = (b >> 5) & 31, bb = b & 31;
    int dr = ar - br, dg = ag - bg, db = ab - bb;
    return dr * dr + dg * dg + db * db;
}

static int color_index_in_vec(const std::vector<unsigned short> &colors, unsigned short c)
{
    for (int i = 0; i < (int)colors.size(); i++)
        if (colors[(size_t)i] == c) return i;
    return -1;
}

static void add_color_unique(std::vector<unsigned short> &colors, unsigned short c)
{
    if (color_index_in_vec(colors, c) < 0)
        colors.push_back(c);
}

struct ClusterTile {
    int x, y, w, h;
    int pal;
    bool empty;
    std::vector<unsigned short> pixels;
    std::vector<unsigned short> colors;
};

struct ClusterPalette {
    std::vector<unsigned short> colors;
};

static int nearest_palette_index(const std::vector<unsigned short> &colors, unsigned short c)
{
    int exact = color_index_in_vec(colors, c);
    if (exact >= 0) return exact + 1;
    if (colors.empty()) return 0;
    int best = 0;
    int best_d = INT_MAX;
    for (int i = 0; i < (int)colors.size(); i++) {
        int d = rgb555_distance(c, colors[(size_t)i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best + 1;
}

static void reduce_tile_colors(std::vector<std::pair<unsigned short, int>> &counts,
                               int visible_colors,
                               std::vector<unsigned short> &out)
{
    std::sort(counts.begin(), counts.end(),
              [](const std::pair<unsigned short, int> &a, const std::pair<unsigned short, int> &b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
              });
    out.clear();
    for (int i = 0; i < (int)counts.size() && i < visible_colors; i++)
        out.push_back(counts[(size_t)i].first);
}

bool mk2_clustered_png_import_apply(void)
{
    int visible = g_cluster_visible_colors;
    if (visible < 1) visible = 1;
    if (visible > 255) visible = 255;
    int max_pals = g_cluster_max_palettes;
    if (max_pals < 1) max_pals = 1;
    if (max_pals > editor_project_palette_capacity()) max_pals = editor_project_palette_capacity();
    int tile_w = g_cluster_tile_w < 1 ? 1 : g_cluster_tile_w;
    int tile_h = g_cluster_tile_h < 1 ? 1 : g_cluster_tile_h;

    int w = 0, h = 0, n = 0;
    unsigned char *rgba = stbi_load(g_cluster_png_path, &w, &h, &n, 4);
    if (!rgba) {
        snprintf(g_cluster_status, sizeof g_cluster_status, "Could not load PNG.");
        return false;
    }

    std::vector<ClusterTile> tiles;
    int source_over_cap = 0;
    for (int y0 = 0; y0 < h; y0 += tile_h) {
        for (int x0 = 0; x0 < w; x0 += tile_w) {
            ClusterTile t;
            t.x = x0;
            t.y = y0;
            t.w = (x0 + tile_w > w) ? (w - x0) : tile_w;
            t.h = (y0 + tile_h > h) ? (h - y0) : tile_h;
            t.pal = -1;
            t.empty = true;
            t.pixels.resize((size_t)t.w * (size_t)t.h);
            std::vector<std::pair<unsigned short, int>> counts;

            for (int yy = 0; yy < t.h; yy++) {
                for (int xx = 0; xx < t.w; xx++) {
                    int src = ((y0 + yy) * w + (x0 + xx)) * 4;
                    int a = rgba[src + 3];
                    unsigned short c = 0xFFFF;
                    if (a >= 32) {
                        c = rgb555_from_rgba(rgba[src], rgba[src + 1], rgba[src + 2]);
                        t.empty = false;
                        int found = -1;
                        for (int ci = 0; ci < (int)counts.size(); ci++) {
                            if (counts[(size_t)ci].first == c) { found = ci; break; }
                        }
                        if (found >= 0) counts[(size_t)found].second++;
                        else counts.push_back(std::make_pair(c, 1));
                    }
                    t.pixels[(size_t)yy * (size_t)t.w + (size_t)xx] = c;
                }
            }

            if (t.empty && g_cluster_skip_empty_tiles)
                continue;
            if ((int)counts.size() > visible) source_over_cap++;
            reduce_tile_colors(counts, visible, t.colors);
            tiles.push_back(t);
        }
    }
    stbi_image_free(rgba);

    if (tiles.empty()) {
        snprintf(g_cluster_status, sizeof g_cluster_status, "No non-empty tiles found.");
        return false;
    }

    std::vector<int> order;
    order.reserve(tiles.size());
    for (int i = 0; i < (int)tiles.size(); i++) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (tiles[(size_t)a].colors.size() != tiles[(size_t)b].colors.size())
            return tiles[(size_t)a].colors.size() > tiles[(size_t)b].colors.size();
        if (tiles[(size_t)a].x != tiles[(size_t)b].x) return tiles[(size_t)a].x < tiles[(size_t)b].x;
        return tiles[(size_t)a].y < tiles[(size_t)b].y;
    });

    std::vector<ClusterPalette> palettes;
    int palette_overflow_tiles = 0;
    for (int ord : order) {
        ClusterTile &tile = tiles[(size_t)ord];
        int best_pal = -1;
        int best_added = INT_MAX;
        int best_union = INT_MAX;
        int fallback_pal = -1;
        int fallback_added = INT_MAX;
        for (int pi = 0; pi < (int)palettes.size(); pi++) {
            int added = 0;
            for (unsigned short c : tile.colors)
                if (color_index_in_vec(palettes[(size_t)pi].colors, c) < 0) added++;
            int union_size = (int)palettes[(size_t)pi].colors.size() + added;
            if (union_size <= visible && (added < best_added || (added == best_added && union_size < best_union))) {
                best_pal = pi;
                best_added = added;
                best_union = union_size;
            }
            if (added < fallback_added) {
                fallback_pal = pi;
                fallback_added = added;
            }
        }
        if (best_pal < 0) {
            if ((int)palettes.size() < max_pals) {
                palettes.push_back(ClusterPalette());
                best_pal = (int)palettes.size() - 1;
            } else {
                best_pal = fallback_pal >= 0 ? fallback_pal : 0;
                palette_overflow_tiles++;
            }
        }
        tile.pal = best_pal;
        for (unsigned short c : tile.colors) {
            if ((int)palettes[(size_t)best_pal].colors.size() < visible)
                add_color_unique(palettes[(size_t)best_pal].colors, c);
        }
    }

    int palette_base = (g_cluster_replace_project || !g_have_bdb) ? 0 : g_n_pals;
    int needed_images = ((g_cluster_replace_project || !g_have_bdb) ? 0 : g_ni) + (int)tiles.size();
    int needed_objects = ((g_cluster_replace_project || !g_have_bdb) ? 0 : g_no) + (int)tiles.size();
    int needed_palettes = palette_base + (int)palettes.size();
    if (!editor_project_reserve_images(needed_images) ||
        !editor_project_reserve_objects(needed_objects) ||
        !editor_project_reserve_palettes(needed_palettes) ||
        !editor_project_reserve_modules(1)) {
        snprintf(g_cluster_status, sizeof g_cluster_status, "Import exceeds image/object/palette limits.");
        return false;
    }

    undo_save_ex("Cluster PNG Import");
    if (g_cluster_replace_project || !g_have_bdb) {
        editor_project_reset_loaded_stage();
        g_have_bdb = 1;
        snprintf(g_name, sizeof g_name, "%s", g_cluster_stage_name[0] ? g_cluster_stage_name : "BGPROF");
        snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d 255 1 0 0", g_name, w, h);
        char line[256];
        snprintf(line, sizeof line, "TSTMOD %d %d %d %d",
                 g_cluster_start_x, g_cluster_start_x + w - 1,
                 g_cluster_start_y, g_cluster_start_y + h - 1);
        editor_project_set_single_module_line(line);
        palette_base = 0;
    }

    for (int pi = 0; pi < (int)palettes.size(); pi++) {
        Uint32 colors[256] = {};
        colors[0] = 0;
        for (int ci = 0; ci < (int)palettes[(size_t)pi].colors.size(); ci++)
            colors[ci + 1] = rgb555_to_argb(palettes[(size_t)pi].colors[(size_t)ci]);
        char name[64];
        snprintf(name, sizeof name, "CLUST%02d", palette_base + pi);
        if (editor_project_append_palette_slot(name, (int)palettes[(size_t)pi].colors.size() + 1,
                                               colors) < 0)
            return false;
    }

    int max_idx = 0;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;
    int max_order = -1;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;

    int added = 0;
    for (const ClusterTile &t : tiles) {
        Img *im = editor_project_append_image_slot();
        if (!im)
            continue;
        int img_i = g_ni - 1;
        im->idx = ++max_idx;
        im->w = t.w;
        im->h = t.h;
        im->flags = 0;
        im->pal_idx = palette_base + t.pal;
        im->pix = (Uint8 *)malloc((size_t)t.w * (size_t)t.h);
        if (!im->pix) im->w = im->h = 0;
        else {
            const std::vector<unsigned short> &pal = palettes[(size_t)t.pal].colors;
            for (int yy = 0; yy < t.h; yy++) {
                for (int xx = 0; xx < t.w; xx++) {
                    unsigned short c = t.pixels[(size_t)yy * (size_t)t.w + (size_t)xx];
                    im->pix[(size_t)yy * (size_t)t.w + (size_t)xx] =
                        c == 0xFFFF ? 0 : (Uint8)nearest_palette_index(pal, c);
                }
            }
        }

        Obj *o = editor_project_append_object_slot();
        if (!o)
            continue;
        o->wx = (g_cluster_layer & 0xFF) << 8;
        o->depth = g_cluster_start_x + t.x;
        o->sy = g_cluster_start_y + t.y;
        o->ii = im->idx;
        o->fl = im->pal_idx;
        o->hfl = 0;
        o->vfl = 0;
        o->order = ++max_order;
        added++;
    }

    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_images = true;
    snprintf(g_cluster_status, sizeof g_cluster_status,
             "Imported %d tile(s), %d palette(s). %d tile(s) reduced, %d overflow assignment(s).",
             added, (int)palettes.size(), source_over_cap, palette_overflow_tiles);
    stage_set_toast("Clustered PNG import complete");
    return true;
}

/* ---- image reimport ---------------------------------------------- */

void reimport_image(int img_idx, const char *path)
{
    if (img_idx < 0 || img_idx >= g_ni) return;
    Img *im = &g_img[img_idx];

    int w, h, n;
    unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba) return;

    /* quantize to palette, match existing palette if possible */
    unsigned short pal[256];
    int pal_cnt = 0;
    unsigned char *new_pix = (unsigned char *)malloc((size_t)w * h);
    if (!new_pix) { stbi_image_free(rgba); return; }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 4;
            int r = rgba[off], g = rgba[off+1], b = rgba[off+2], a = rgba[off+3];
            if (a < 128) { new_pix[y * w + x] = 0; continue; }
            unsigned short c555 = rgb555_from_rgba(r, g, b);
            int best = 1, best_d = INT_MAX;
            for (int p = 0; p < pal_cnt; p++) {
                int d = rgb555_distance(c555, pal[p]);
                if (d < best_d) { best_d = d; best = p + 1; }
            }
            if (best_d > 0 && pal_cnt < 255) {
                pal[pal_cnt] = c555;
                best = ++pal_cnt;
            }
            new_pix[y * w + x] = (Uint8)best;
        }
    }

    /* replace image data */
    free(im->pix);
    im->w = w; im->h = h;
    im->pix = new_pix;

    /* update palette */
    int pi = im->pal_idx;
    if (pi >= 0 && pi < g_n_pals) {
        Uint32 colors[256] = {};
        colors[0] = 0;
        for (int i = 0; i < pal_cnt; i++)
            colors[i + 1] = rgb555_to_argb(pal[i]);
        editor_project_set_palette_slot(pi, NULL, pal_cnt + 1, colors);
    }

    stbi_image_free(rgba);
    g_need_rebuild = 1;
    g_dirty = 1;
}

