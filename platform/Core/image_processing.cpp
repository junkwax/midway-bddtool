#include "Core/image_processing.h"

#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/project_header.h"
#include "Core/world_module_utils.h"
#include "undo_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
Uint32 palette_argb_at(int pal_idx, int color_idx)
{
    if (pal_idx >= 0 && pal_idx < g_n_pals &&
        color_idx >= 0 && color_idx < g_pal_count[pal_idx])
        return g_pals[pal_idx][color_idx];
    Uint8 v = (Uint8)(color_idx < 0 ? 0 : color_idx);
    return 0xFF000000u | ((Uint32)v << 16) | ((Uint32)v << 8) | (Uint32)v;
}

static int argb_luma(Uint32 c)
{
    int r = (int)((c >> 16) & 0xFF);
    int g = (int)((c >> 8) & 0xFF);
    int b = (int)(c & 0xFF);
    return (r * 299 + g * 587 + b * 114) / 1000;
}

static int argb_distance(Uint32 a, Uint32 b)
{
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int dr = ar - br, dg = ag - bg, db = ab - bb;
    return dr * dr + dg * dg + db * db;
}

struct BlockStyleCandidate {
    int index;
    Uint32 color;
    int luma;
};

static int block_match_collect_candidates(int ref_obj, bool used_only,
                                          std::vector<BlockStyleCandidate> &out)
{
    out.clear();
    if (ref_obj < 0 || ref_obj >= g_no) return 0;
    Obj *ro = &g_obj[ref_obj];
    Img *rim = img_find(ro->ii);
    if (!rim || !rim->pix || ro->fl < 0 || ro->fl >= g_n_pals) return 0;

    bool used[256];
    memset(used, 0, sizeof used);
    if (used_only) {
        size_t n = (size_t)rim->w * (size_t)rim->h;
        for (size_t i = 0; i < n; i++)
            if (rim->pix[i] > 0) used[rim->pix[i]] = true;
    } else {
        for (int i = 1; i < g_pal_count[ro->fl] && i < 256; i++)
            used[i] = true;
    }

    for (int i = 1; i < g_pal_count[ro->fl] && i < 256; i++) {
        if (!used[i]) continue;
        Uint32 c = palette_argb_at(ro->fl, i);
        BlockStyleCandidate cand;
        cand.index = i;
        cand.color = c;
        cand.luma = argb_luma(c);
        out.push_back(cand);
    }
    return (int)out.size();
}

int block_match_candidate_count(int ref_obj, bool used_only)
{
    std::vector<BlockStyleCandidate> candidates;
    return block_match_collect_candidates(ref_obj, used_only, candidates);
}

static int block_match_best_index(Uint32 src_color, const std::vector<BlockStyleCandidate> &candidates,
                                  float shade_weight)
{
    if (candidates.empty()) return 0;
    if (shade_weight < 0.0f) shade_weight = 0.0f;
    if (shade_weight > 1.0f) shade_weight = 1.0f;
    int src_luma = argb_luma(src_color);
    int best_i = candidates[0].index;
    double best_score = 1.0e30;
    for (const BlockStyleCandidate &cand : candidates) {
        int rgb_d = argb_distance(src_color, cand.color);
        int dl = src_luma - cand.luma;
        double shade_d = (double)(dl * dl) * 3.0;
        double score = (double)rgb_d * (1.0 - shade_weight) + shade_d * shade_weight;
        if (score < best_score) {
            best_score = score;
            best_i = cand.index;
        }
    }
    return best_i;
}

int block_match_image_to_object_style(int img_i, int ref_obj, bool used_only,
                                      bool all_uses, float shade_weight,
                                      int *out_candidates)
{
    if (out_candidates) *out_candidates = 0;
    if (img_i < 0 || img_i >= g_ni || ref_obj < 0 || ref_obj >= g_no) return -1;
    Img *im = &g_img[img_i];
    Obj *ro = &g_obj[ref_obj];
    Img *rim = img_find(ro->ii);
    if (!im || !im->pix || !rim || !rim->pix) return -1;
    int src_pal = im->pal_idx;
    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != im->idx) continue;
        src_pal = g_obj[i].fl;
        break;
    }
    if (g_hl_obj >= 0 && g_hl_obj < g_no && g_obj[g_hl_obj].ii == im->idx)
        src_pal = g_obj[g_hl_obj].fl;
    if (src_pal < 0 || src_pal >= g_n_pals || ro->fl < 0 || ro->fl >= g_n_pals) return -1;

    std::vector<BlockStyleCandidate> candidates;
    int cand_count = block_match_collect_candidates(ref_obj, used_only, candidates);
    if (out_candidates) *out_candidates = cand_count;
    if (cand_count <= 0) return -1;

    undo_save_ex("Match Block Style");
    int changed = 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++) {
        Uint8 v = im->pix[i];
        if (v == 0) continue;
        Uint32 src = palette_argb_at(src_pal, v);
        int mapped = block_match_best_index(src, candidates, shade_weight);
        if (mapped > 0 && mapped < 256 && im->pix[i] != (Uint8)mapped) {
            im->pix[i] = (Uint8)mapped;
            changed++;
        }
    }

    im->pal_idx = ro->fl;
    int placement_updates = 0;
    if (all_uses) {
        for (int i = 0; i < g_no; i++) {
            if (g_obj[i].ii != im->idx) continue;
            if (g_obj[i].fl != ro->fl) placement_updates++;
            g_obj[i].fl = ro->fl;
        }
    } else if (g_hl_obj >= 0 && g_hl_obj < g_no && g_obj[g_hl_obj].ii == im->idx) {
        if (g_obj[g_hl_obj].fl != ro->fl) placement_updates++;
        g_obj[g_hl_obj].fl = ro->fl;
    }

    g_need_rebuild = 1;
    g_dirty = 1;
    return changed + placement_updates;
}

int edge_candidate_index(const Img *im, int *out_count, int *out_total)
{
    if (out_count) *out_count = 0;
    if (out_total) *out_total = 0;
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return -1;
    int hist[256];
    memset(hist, 0, sizeof hist);
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            if (x != 0 && y != 0 && x != im->w - 1 && y != im->h - 1) continue;
            Uint8 v = im->pix[y * im->w + x];
            if (v == 0) continue;
            hist[v]++;
            if (out_total) (*out_total)++;
        }
    }
    int best = -1, best_count = 0;
    for (int i = 1; i < 256; i++) {
        if (hist[i] > best_count) {
            best = i;
            best_count = hist[i];
        }
    }
    if (out_count) *out_count = best_count;
    return best;
}

int replace_image_index_with_zero(Img *im, int idx)
{
    if (!im || !im->pix || idx <= 0 || idx > 255) return 0;
    undo_save();
    int changed = 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++) {
        if (im->pix[i] != idx) continue;
        im->pix[i] = 0;
        changed++;
    }
    if (changed) {
        g_need_rebuild = 1;
        g_dirty = 1;
    }
    return changed;
}

static bool palette_color_near_black(int pal_idx, int color_idx)
{
    if (pal_idx < 0 || pal_idx >= g_n_pals || color_idx < 0 || color_idx >= g_pal_count[pal_idx])
        return false;
    Uint32 c = g_pals[pal_idx][color_idx];
    int r = (int)((c >> 16) & 0xFF);
    int g = (int)((c >> 8) & 0xFF);
    int b = (int)(c & 0xFF);
    return r <= 16 && g <= 16 && b <= 16;
}

int clear_image_edge_matte(int img_i, bool require_black, bool save_undo)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    Img *im = &g_img[img_i];
    int edge_total = 0;
    int edge_count = 0;
    int candidate = edge_candidate_index(im, &edge_count, &edge_total);
    if (candidate <= 0 || edge_total <= 0) return 0;
    if (edge_count < 4 || edge_count * 100 < edge_total * 12) return 0;
    if (require_black && !palette_color_near_black(im->pal_idx, candidate)) return 0;

    if (save_undo) undo_save_ex("Clear Image Matte");
    int changed = 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++) {
        if (im->pix[i] != candidate) continue;
        im->pix[i] = 0;
        changed++;
    }
    if (changed > 0) {
        g_need_rebuild = 1;
        g_dirty = 1;
    }
    return changed;
}

int image_nonzero_bounds(const Img *im, int *x1, int *y1, int *x2, int *y2)
{
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return 0;
    int minx = im->w, miny = im->h, maxx = -1, maxy = -1;
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            if (im->pix[y * im->w + x] == 0) continue;
            if (x < minx) minx = x;
            if (y < miny) miny = y;
            if (x > maxx) maxx = x;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx < minx || maxy < miny) return 0;
    if (x1) *x1 = minx;
    if (y1) *y1 = miny;
    if (x2) *x2 = maxx;
    if (y2) *y2 = maxy;
    return 1;
}

void image_palette_usage_stats(const Img *im, int *used_count, int *max_idx)
{
    bool used[256];
    memset(used, 0, sizeof used);
    int maxv = 0, count = 0;
    if (im && im->pix && im->w > 0 && im->h > 0) {
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t i = 0; i < n; i++) {
            int v = im->pix[i];
            if (v <= 0) continue;
            used[v] = true;
            if (v > maxv) maxv = v;
        }
        for (int i = 1; i < 256; i++) if (used[i]) count++;
    }
    if (used_count) *used_count = count;
    if (max_idx) *max_idx = maxv;
}

int image_object_ref_count(int image_idx)
{
    int count = 0;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].ii == image_idx) count++;
    return count;
}

ImageModuleInfo image_module_info(const Img *im)
{
    const int module_cap = editor_project_module_capacity();
    ImageModuleInfo info = {-2, module_cap + 2, 0, 0, false, false};
    if (!im) return info;

    std::vector<int> seen;
    seen.reserve((size_t)(module_cap > 0 ? module_cap : 0));
    int seen_n = 0;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].ii != im->idx) continue;
        info.use_count++;
        Img *obj_im = img_find(g_obj[oi].ii);
        int ow = obj_im ? obj_im->w : (im->w > 0 ? im->w : 1);
        int oh = obj_im ? obj_im->h : (im->h > 0 ? im->h : 1);
        int mod = assign_module(g_obj[oi].depth, g_obj[oi].sy, ow, oh);
        if (mod < 0) {
            info.outside = true;
            continue;
        }
        bool dup = false;
        for (int si = 0; si < seen_n; si++) {
            if (seen[(size_t)si] == mod) {
                dup = true;
                break;
            }
        }
        if (!dup && seen_n < module_cap) {
            seen.push_back(mod);
            seen_n = (int)seen.size();
        }
    }

    if (info.use_count == 0) {
        info.primary_module = -2;
        info.bucket = module_cap + 2;
        return info;
    }

    if (seen_n > 0) {
        info.primary_module = seen[0];
        for (int si = 1; si < seen_n; si++)
            if (seen[(size_t)si] < info.primary_module)
                info.primary_module = seen[(size_t)si];
    } else {
        info.primary_module = -1;
    }
    info.group_count = seen_n + (info.outside ? 1 : 0);
    info.mixed = info.group_count > 1;
    info.bucket = info.primary_module >= 0 ? info.primary_module : module_cap + 1;
    return info;
}

void image_module_group_label(int bucket, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    const int module_cap = editor_project_module_capacity();
    if (bucket >= 0 && bucket < g_bdb_num_modules) {
        char name[64] = "";
        if (parse_module_bounds(bucket, name, NULL, NULL, NULL, NULL) && name[0])
            snprintf(out, out_sz, "Module %d: %s", bucket, name);
        else
            snprintf(out, out_sz, "Module %d", bucket);
    } else if (bucket == module_cap + 1) {
        snprintf(out, out_sz, "Outside module bounds");
    } else if (bucket == module_cap + 2) {
        snprintf(out, out_sz, "Unused images");
    } else {
        snprintf(out, out_sz, "Images");
    }
}

void image_module_badge_label(const ImageModuleInfo *info, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    if (!info || info->use_count == 0) {
        snprintf(out, out_sz, "unused");
        return;
    }
    if (info->primary_module >= 0) {
        char name[64] = "";
        if (parse_module_bounds(info->primary_module, name, NULL, NULL, NULL, NULL) && name[0]) {
            if (info->mixed) snprintf(out, out_sz, "mod %s +%d", name, info->group_count - 1);
            else             snprintf(out, out_sz, "mod %s", name);
        } else {
            if (info->mixed) snprintf(out, out_sz, "mod %d +%d", info->primary_module, info->group_count - 1);
            else             snprintf(out, out_sz, "mod %d", info->primary_module);
        }
    } else {
        snprintf(out, out_sz, "outside modules");
    }
}

int trim_image_transparent_border(int img_i, bool save_undo)
{
    if (img_i < 0 || img_i >= g_ni) return -1;
    Img *im = &g_img[img_i];
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) return -1;
    if (x1 == 0 && y1 == 0 && x2 == im->w - 1 && y2 == im->h - 1) return 0;
    int old_w = im->w, old_h = im->h;
    int new_w = x2 - x1 + 1;
    int new_h = y2 - y1 + 1;
    Uint8 *pix = (Uint8 *)malloc((size_t)new_w * (size_t)new_h);
    if (!pix) return -1;

    if (save_undo) undo_save();
    for (int y = 0; y < new_h; y++)
        memcpy(pix + y * new_w, im->pix + (y + y1) * old_w + x1, (size_t)new_w);
    free(im->pix);
    im->pix = pix;
    im->w = new_w;
    im->h = new_h;
    im->anix -= x1;
    im->aniy -= y1;
    im->anix2 -= x1;
    im->aniy2 -= y1;

    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != im->idx) continue;
        int dx = g_obj[i].hfl ? (old_w - 1 - x2) : x1;
        int dy = g_obj[i].vfl ? (old_h - 1 - y2) : y1;
        g_obj[i].depth += dx;
        g_obj[i].sy += dy;
    }
    g_need_rebuild = 1;
    g_dirty = 1;
    return (old_w * old_h) - (new_w * new_h);
}

int compact_palettes_for_image_range(int start_img, int end_img, bool save_undo)
{
    if (start_img < 0) start_img = 0;
    if (end_img > g_ni) end_img = g_ni;
    if (end_img <= start_img) return 0;

    const int palette_cap = editor_project_palette_capacity();
    std::vector<unsigned char> target_pal((size_t)(palette_cap > 0 ? palette_cap : 0), 0);
    for (int i = start_img; i < end_img; i++) {
        Img *im = &g_img[i];
        if (im->pal_idx >= 0 && im->pal_idx < g_n_pals && im->pal_idx < palette_cap)
            target_pal[(size_t)im->pal_idx] = 1;
    }

    int compacted = 0;
    bool undo_done = false;
    for (int p = 0; p < g_n_pals && p < palette_cap; p++) {
        if (!target_pal[(size_t)p]) continue;

        bool used[256];
        memset(used, 0, sizeof used);
        bool any = false;
        for (int ii = 0; ii < g_ni; ii++) {
            Img *im = &g_img[ii];
            if (im->pal_idx != p || !im->pix) continue;
            size_t n = (size_t)im->w * (size_t)im->h;
            for (size_t k = 0; k < n; k++) {
                int v = im->pix[k];
                if (v <= 0) continue;
                used[v] = true;
                any = true;
            }
        }
        if (!any) continue;

        int map[256];
        Uint32 new_pal[256];
        memset(map, 0, sizeof map);
        memset(new_pal, 0, sizeof new_pal);
        int next = 1;
        bool already_contiguous = true;
        for (int i = 1; i < 256; i++) {
            if (!used[i]) continue;
            map[i] = next;
            new_pal[next] = g_pals[p][i];
            if (i != next) already_contiguous = false;
            next++;
        }
        if (next == g_pal_count[p] && already_contiguous)
            continue;

        if (save_undo && !undo_done) {
            undo_save_ex("Compact Imported Palettes");
            undo_done = true;
        }
        for (int ii = 0; ii < g_ni; ii++) {
            Img *im = &g_img[ii];
            if (im->pal_idx != p || !im->pix) continue;
            size_t n = (size_t)im->w * (size_t)im->h;
            for (size_t k = 0; k < n; k++)
                if (im->pix[k] > 0) im->pix[k] = (Uint8)map[im->pix[k]];
        }
        editor_project_set_palette_slot(p, NULL, next, new_pal);
        compacted++;
    }
    if (compacted > 0) {
        sync_bdb_header_counts();
        g_need_rebuild = 1;
        g_dirty = 1;
    }
    return compacted;
}

int optimize_image_range_for_space(int start_img, int end_img, bool save_undo,
                                   int *trimmed_images, int *trimmed_pixels,
                                   int *compacted_palettes)
{
    if (trimmed_images) *trimmed_images = 0;
    if (trimmed_pixels) *trimmed_pixels = 0;
    if (compacted_palettes) *compacted_palettes = 0;
    if (start_img < 0) start_img = 0;
    if (end_img > g_ni) end_img = g_ni;
    if (end_img <= start_img) return 0;

    bool undo_done = false;
    int changed = 0;
    if (g_import_opt_trim) {
        for (int i = start_img; i < end_img; i++) {
            if (!g_img[i].pix) continue;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (!image_nonzero_bounds(&g_img[i], &x1, &y1, &x2, &y2)) continue;
            int save = g_img[i].w * g_img[i].h - (x2 - x1 + 1) * (y2 - y1 + 1);
            if (save < g_batch_trim_min_saved) continue;
            if (save_undo && !undo_done) {
                undo_save_ex("Optimize Imported Sprites");
                undo_done = true;
            }
            int rc = trim_image_transparent_border(i, false);
            if (rc > 0) {
                changed++;
                if (trimmed_images) (*trimmed_images)++;
                if (trimmed_pixels) (*trimmed_pixels) += rc;
            }
        }
    }

    if (g_import_opt_compact_palettes) {
        int rc = compact_palettes_for_image_range(start_img, end_img,
                                                  save_undo && !undo_done);
        if (rc > 0) {
            changed += rc;
            if (compacted_palettes) *compacted_palettes = rc;
        }
    }

    return changed;
}

int compress_active_image_palette(int img_i, int target, bool save_undo)
{
    if (img_i < 0 || img_i >= g_ni) return -1;
    Img *im = &g_img[img_i];
    if (!im->pix || im->pal_idx < 0 || im->pal_idx >= g_n_pals) return -1;
    bool used[256];
    memset(used, 0, sizeof used);
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++) if (im->pix[i] > 0) used[im->pix[i]] = true;
    int used_count = 0;
    for (int i = 1; i < 256; i++) if (used[i]) used_count++;
    if (used_count > target) return -1;

    int dst_pal = im->pal_idx;
    if (g_palette_compress_new_palette && !editor_project_reserve_palettes(g_n_pals + 1))
        return -1;

    if (save_undo) undo_save();
    Uint32 newpal[256];
    memset(newpal, 0, sizeof newpal);
    int map[256];
    memset(map, 0, sizeof map);
    int next = 1;
    for (int i = 1; i < 256; i++) {
        if (!used[i]) continue;
        map[i] = next;
        newpal[next] = g_pals[im->pal_idx][i];
        next++;
    }
    for (size_t i = 0; i < n; i++)
        if (im->pix[i] > 0) im->pix[i] = (Uint8)map[im->pix[i]];

    if (g_palette_compress_new_palette) {
        char name[64];
        snprintf(name, sizeof name, "CMP%d_%02X", target, im->idx);
        dst_pal = editor_project_append_palette_slot(name, target + 1, newpal);
        if (dst_pal < 0) return -1;
    } else {
        editor_project_set_palette_slot(dst_pal, NULL, target + 1, newpal);
    }
    im->pal_idx = dst_pal;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].ii == im->idx) g_obj[i].fl = dst_pal;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    return used_count;
}

void assign_selected_layer(int wx_layer)
{
    int any = 0;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        if (!any) { undo_save(); any = 1; }
        g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (wx_layer << 8);
    }
    if (any) {
        g_dirty = 1;
        sync_bdb_header_counts();
    }
}

