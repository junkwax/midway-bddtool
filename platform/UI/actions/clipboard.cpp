#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "undo_manager.h"

#include <imgui.h>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
static std::vector<Obj> g_clipboard;
int  g_clip_count = 0;
static std::vector<int> g_clip_obj_lock;
static std::vector<int> g_clip_obj_hidden;
struct ClipPalette {
    int src_idx;
    int count;
    Uint32 colors[256];
    char name[64];
};
struct ClipImage {
    Img img;
    Uint8 *pix;
};
static std::vector<ClipPalette> g_clip_pals;
static int g_clip_pal_count = 0;
static std::vector<ClipImage> g_clip_images;
static int g_clip_image_count = 0;
static int g_clip_source_doc_id = 0;
static char g_clip_source_bdb_path[512] = "";
static char g_clip_source_bdd_path[512] = "";

static void clipboard_clear_assets(void)
{
    for (int i = 0; i < g_clip_image_count; i++) {
        free(g_clip_images[i].pix);
        g_clip_images[i].pix = NULL;
    }
    g_clipboard.clear();
    g_clip_obj_lock.clear();
    g_clip_obj_hidden.clear();
    g_clip_images.clear();
    g_clip_pals.clear();
    g_clip_count = 0;
    g_clip_image_count = 0;
    g_clip_pal_count = 0;
    g_clip_source_doc_id = 0;
    g_clip_source_bdb_path[0] = '\0';
    g_clip_source_bdd_path[0] = '\0';
}

static int current_document_id(void)
{
    if (g_cur_doc >= 0 && g_cur_doc < g_num_docs && g_docs[g_cur_doc].tab_id > 0)
        return g_docs[g_cur_doc].tab_id;
    return 0;
}

static int clipboard_is_cross_document(void)
{
    int cur_id = current_document_id();
    if (g_clip_source_doc_id > 0 && cur_id > 0 && g_clip_source_doc_id != cur_id)
        return 1;
    if (g_clip_source_bdb_path[0] && g_bdb_path[0] && strcasecmp(g_clip_source_bdb_path, g_bdb_path) != 0)
        return 1;
    if (g_clip_source_bdd_path[0] && g_bdd_path[0] && strcasecmp(g_clip_source_bdd_path, g_bdd_path) != 0)
        return 1;
    return 0;
}

static int clipboard_find_palette(int src_idx)
{
    for (int i = 0; i < g_clip_pal_count; i++)
        if (g_clip_pals[i].src_idx == src_idx) return i;
    return -1;
}

static int clipboard_add_palette(int src_idx)
{
    if (src_idx < 0 || src_idx >= g_n_pals) return -1;
    int existing = clipboard_find_palette(src_idx);
    if (existing >= 0) return existing;
    int palette_cap = editor_project_palette_capacity();
    if (palette_cap <= 0 || g_clip_pal_count >= palette_cap) return -1;

    ClipPalette cp = {};
    cp.src_idx = src_idx;
    cp.count = g_pal_count[src_idx];
    if (cp.count < 0) cp.count = 0;
    if (cp.count > 256) cp.count = 256;
    memcpy(cp.colors, g_pals[src_idx], sizeof(cp.colors));
    snprintf(cp.name, sizeof cp.name, "%s", g_pal_name[src_idx]);
    g_clip_pals.push_back(cp);
    g_clip_pal_count = (int)g_clip_pals.size();
    return g_clip_pal_count - 1;
}

static int clipboard_find_image(int src_idx)
{
    for (int i = 0; i < g_clip_image_count; i++)
        if (g_clip_images[i].img.idx == src_idx) return i;
    return -1;
}

static int clipboard_add_image_for_idx(int src_idx)
{
    if (clipboard_find_image(src_idx) >= 0) return 1;
    Img *src = img_find(src_idx);
    if (!src || !src->pix || src->w <= 0 || src->h <= 0) return 0;
    int image_cap = editor_project_image_capacity();
    if (image_cap <= 0 || g_clip_image_count >= image_cap) return 0;

    ClipImage ci = {};
    ci.img = *src;
    size_t sz = (size_t)src->w * (size_t)src->h;
    ci.pix = (Uint8 *)malloc(sz);
    if (!ci.pix) {
        return 0;
    }
    memcpy(ci.pix, src->pix, sz);
    ci.img.pix = ci.pix;
    clipboard_add_palette(src->pal_idx);
    g_clip_images.push_back(ci);
    g_clip_image_count = (int)g_clip_images.size();
    return 1;
}

static int palette_matches_clip(int dst_idx, const ClipPalette *cp)
{
    if (!cp || dst_idx < 0 || dst_idx >= g_n_pals) return 0;
    if (g_pal_count[dst_idx] != cp->count) return 0;
    return memcmp(g_pals[dst_idx], cp->colors, sizeof(cp->colors)) == 0;
}

static int ensure_clip_palette_in_project(const ClipPalette *cp)
{
    if (!cp) return -1;
    if (cp->src_idx >= 0 && cp->src_idx < g_n_pals && palette_matches_clip(cp->src_idx, cp))
        return cp->src_idx;
    for (int i = 0; i < g_n_pals; i++)
        if (palette_matches_clip(i, cp)) return i;
    return editor_project_append_palette_slot(cp->name[0] ? cp->name : "CLIP_PAL",
                                              cp->count, cp->colors);
}

int next_free_image_index(int preferred)
{
    if (preferred >= 0 && !img_find(preferred)) return preferred;
    int max_idx = -1;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;
    for (int idx = max_idx + 1; idx < 65536; idx++)
        if (!img_find(idx)) return idx;
    for (int idx = 0; idx < 65536; idx++)
        if (!img_find(idx)) return idx;
    return -1;
}

int chop_image_to_map(int img_i, int base_x, int base_y, int wx, int pal_idx,
                      bool hfl, bool vfl, int replace_obj, bool save_undo)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    Img *src = &g_img[img_i];
    if (!src->pix || src->w <= 0 || src->h <= 0) return 0;
    if (g_chop_tile_w < 4) g_chop_tile_w = 4;
    if (g_chop_tile_h < 4) g_chop_tile_h = 4;
    if (g_chop_tile_w > 256) g_chop_tile_w = 256;
    if (g_chop_tile_h > 256) g_chop_tile_h = 256;
    if (pal_idx < 0 || pal_idx >= g_n_pals)
        pal_idx = (src->pal_idx >= 0 && src->pal_idx < g_n_pals) ? src->pal_idx : 0;

    if (save_undo) undo_save_ex("Chop Sprite Placement");

    bool replace_pending = replace_obj >= 0 && replace_obj < g_no;

    int total_tile_pixels = 0;
    int added = 0;
    int max_order = -1;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;

    for (int ty = 0; ty < src->h; ty += g_chop_tile_h) {
        int th = g_chop_tile_h;
        if (ty + th > src->h) th = src->h - ty;
        for (int tx = 0; tx < src->w; tx += g_chop_tile_w) {
            int tw = g_chop_tile_w;
            if (tx + tw > src->w) tw = src->w - tx;

            int minx = tw, miny = th, maxx = -1, maxy = -1;
            for (int yy = 0; yy < th; yy++) {
                int vy = ty + yy;
                int sy = vfl ? (src->h - 1 - vy) : vy;
                for (int xx = 0; xx < tw; xx++) {
                    int vx = tx + xx;
                    int sx = hfl ? (src->w - 1 - vx) : vx;
                    Uint8 px = src->pix[(size_t)sy * (size_t)src->w + (size_t)sx];
                    if (!px) continue;
                    if (xx < minx) minx = xx;
                    if (yy < miny) miny = yy;
                    if (xx > maxx) maxx = xx;
                    if (yy > maxy) maxy = yy;
                }
            }
            if (maxx < minx || maxy < miny)
                continue;
            int crop_x = g_chop_trim_tiles ? minx : 0;
            int crop_y = g_chop_trim_tiles ? miny : 0;
            int cw = g_chop_trim_tiles ? (maxx - minx + 1) : tw;
            int ch = g_chop_trim_tiles ? (maxy - miny + 1) : th;
            if (!editor_project_reserve_images(g_ni + 1) ||
                !editor_project_reserve_objects(g_no + 1))
                break;
            src = &g_img[img_i];
            Uint8 *pix = (Uint8 *)malloc((size_t)cw * (size_t)ch);
            if (!pix) continue;
            for (int yy = 0; yy < ch; yy++) {
                int vy = ty + crop_y + yy;
                int sy = vfl ? (src->h - 1 - vy) : vy;
                for (int xx = 0; xx < cw; xx++) {
                    int vx = tx + crop_x + xx;
                    int sx = hfl ? (src->w - 1 - vx) : vx;
                    pix[(size_t)yy * (size_t)cw + (size_t)xx] =
                        src->pix[(size_t)sy * (size_t)src->w + (size_t)sx];
                }
            }

            int new_idx = next_free_image_index(src->idx + added + 1);
            if (new_idx < 0) {
                free(pix);
                continue;
            }
            Img *dst = editor_project_append_image_slot();
            if (!dst) {
                free(pix);
                continue;
            }
            dst->idx = new_idx;
            dst->w = cw;
            dst->h = ch;
            dst->flags = src->flags & 1;
            dst->pal_idx = pal_idx;
            dst->pix = pix;
            snprintf(dst->label, sizeof dst->label, "%.40s_%02d_%02d",
                     src->label[0] ? src->label : "CHOP", tx / g_chop_tile_w, ty / g_chop_tile_h);
            snprintf(dst->source, sizeof dst->source, "%.40s_CHOP",
                     src->source[0] ? src->source : (src->label[0] ? src->label : "BDD"));

            Obj *o = editor_project_append_object_slot();
            if (!o) {
                editor_project_delete_image_slot(g_ni - 1);
                continue;
            }
            int obj_idx = g_no - 1;
            o->wx = wx & ~0x30;
            o->depth = base_x + tx + crop_x;
            o->sy = base_y + ty + crop_y;
            o->ii = dst->idx;
            o->fl = pal_idx;
            o->hfl = 0;
            o->vfl = 0;
            o->order = ++max_order;
            g_sel_flags[obj_idx] = 1;
            simple_ensure_module(obj_idx);
            g_hl_obj = obj_idx;
            total_tile_pixels += cw * ch;
            added++;
        }
    }

    if (added > 0) {
        if (replace_pending) {
            editor_project_delete_object_slot(replace_obj);
            g_hl_obj = g_no > 0 ? g_no - 1 : -1;
        }
        sync_bdb_header_counts();
        g_need_rebuild = 1;
        g_dirty = 1;
        char msg[160];
        int raw_delta = src->w * src->h - total_tile_pixels;
        snprintf(msg, sizeof msg, "Chopped into %d tile(s), saved %d raw pixels before deleting source",
                 added, raw_delta);
        stage_set_toast(msg);
    }
    return added;
}

int delete_image_slot_if_unused(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    if (image_use_count(g_img[img_i].idx) != 0) return 0;
    if (!editor_project_delete_image_slot(img_i)) return 0;
    if (g_place_tool_img >= g_ni) g_place_tool_img = g_ni - 1;
    if (g_last_import_img >= g_ni) g_last_import_img = g_ni - 1;
    return 1;
}

int remove_unused_palettes_impl(bool save_undo)
{
    int palette_cap = editor_project_palette_capacity();
    if (palette_cap <= 0) return 0;
    std::vector<unsigned char> used((size_t)palette_cap, 0);
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
            used[g_img[i].pal_idx] = 1;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals)
            used[g_obj[i].fl] = 1;

    std::vector<int> remap((size_t)palette_cap, -1);
    int new_n = 0, removed = 0;
    for (int i = 0; i < g_n_pals; i++) {
        if (used[i]) remap[i] = new_n++;
        else { remap[i] = -1; removed++; }
    }
    if (removed <= 0) return 0;
    if (save_undo) undo_save_ex("Remove Unused Palettes");

    std::vector<std::array<Uint32, 256>> new_pals((size_t)palette_cap);
    std::vector<int> new_counts((size_t)palette_cap, 0);
    std::vector<std::array<char, 64>> new_names((size_t)palette_cap);
    for (int i = 0; i < g_n_pals; i++) {
        if (remap[i] < 0) continue;
        memcpy(new_pals[(size_t)remap[i]].data(), g_pals[i], sizeof g_pals[0]);
        new_counts[(size_t)remap[i]] = g_pal_count[i];
        memcpy(new_names[(size_t)remap[i]].data(), g_pal_name[i], sizeof g_pal_name[0]);
    }
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
            g_img[i].pal_idx = remap[g_img[i].pal_idx];
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals)
            g_obj[i].fl = remap[g_obj[i].fl];
    editor_project_replace_palettes(
        reinterpret_cast<const Uint32 (*)[256]>(new_pals.data()),
        new_counts.data(),
        reinterpret_cast<const char (*)[64]>(new_names.data()),
        new_n,
        new_n);
    if (g_sel_pal >= g_n_pals) g_sel_pal = g_n_pals > 0 ? g_n_pals - 1 : 0;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_mk2_palette_sync_dirty = true;
    return removed;
}

static int image_matches_clip(int dst_i, const ClipImage *ci, int mapped_pal)
{
    if (!ci || dst_i < 0 || dst_i >= g_ni) return 0;
    const Img *dst = &g_img[dst_i];
    const Img *src = &ci->img;
    if (dst->w != src->w || dst->h != src->h || dst->pal_idx != mapped_pal) return 0;
    if (!dst->pix || !ci->pix) return 0;
    return memcmp(dst->pix, ci->pix, (size_t)src->w * (size_t)src->h) == 0;
}

static int ensure_clip_image_in_project(const ClipImage *ci, int mapped_pal)
{
    if (!ci || !ci->pix || ci->img.w <= 0 || ci->img.h <= 0) return ci ? ci->img.idx : -1;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx == ci->img.idx && image_matches_clip(i, ci, mapped_pal))
            return g_img[i].idx;
    for (int i = 0; i < g_ni; i++)
        if (image_matches_clip(i, ci, mapped_pal))
            return g_img[i].idx;
    int dst_idx = next_free_image_index(ci->img.idx);
    if (dst_idx < 0) return -1;

    Img *dst = editor_project_append_image_slot();
    if (!dst) return -1;
    *dst = ci->img;
    dst->idx = dst_idx;
    dst->pal_idx = mapped_pal;
    size_t sz = (size_t)ci->img.w * (size_t)ci->img.h;
    dst->pix = (Uint8 *)malloc(sz);
    if (!dst->pix) {
        editor_project_delete_image_slot(g_ni - 1);
        return -1;
    }
    memcpy(dst->pix, ci->pix, sz);
    return dst_idx;
}

int copy_selected_objects_to_clipboard(void)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return 0;
    clipboard_clear_assets();
    g_clip_source_doc_id = current_document_id();
    snprintf(g_clip_source_bdb_path, sizeof g_clip_source_bdb_path, "%s", g_bdb_path);
    snprintf(g_clip_source_bdd_path, sizeof g_clip_source_bdd_path, "%s", g_bdd_path);
    g_clipboard.reserve((size_t)g_no);
    g_clip_obj_lock.reserve((size_t)g_no);
    g_clip_obj_hidden.reserve((size_t)g_no);
    for (int i = 0; i < g_no && g_clip_count < object_cap; i++) {
        if (!g_sel_flags[i]) continue;
        g_clipboard.push_back(g_obj[i]);
        g_clip_obj_lock.push_back(g_obj_lock[i]);
        g_clip_obj_hidden.push_back(g_obj_hidden[i]);
        g_clip_count = (int)g_clipboard.size();
        clipboard_add_palette(g_obj[i].fl);
        clipboard_add_image_for_idx(g_obj[i].ii);
    }
    return g_clip_count;
}

struct RectBounds {
    int valid;
    int x1, y1, x2, y2;
};

static void bounds_reset(RectBounds *b)
{
    if (!b) return;
    b->valid = 0;
    b->x1 = b->y1 = INT_MAX;
    b->x2 = b->y2 = INT_MIN;
}

static void bounds_add_rect(RectBounds *b, int x1, int y1, int x2, int y2)
{
    if (!b || x2 <= x1 || y2 <= y1) return;
    if (!b->valid) {
        b->valid = 1;
        b->x1 = x1; b->y1 = y1; b->x2 = x2; b->y2 = y2;
        return;
    }
    if (x1 < b->x1) b->x1 = x1;
    if (y1 < b->y1) b->y1 = y1;
    if (x2 > b->x2) b->x2 = x2;
    if (y2 > b->y2) b->y2 = y2;
}

static int clipboard_image_dims(int ii, int *w, int *h)
{
    int ci = clipboard_find_image(ii);
    if (ci >= 0) {
        if (w) *w = g_clip_images[ci].img.w;
        if (h) *h = g_clip_images[ci].img.h;
        return g_clip_images[ci].img.w > 0 && g_clip_images[ci].img.h > 0;
    }
    Img *im = img_find(ii);
    if (!im) return 0;
    if (w) *w = im->w;
    if (h) *h = im->h;
    return im->w > 0 && im->h > 0;
}

static int object_bounds_at(int obj_idx, RectBounds *out)
{
    if (obj_idx < 0 || obj_idx >= g_no || !out) return 0;
    Img *im = img_find(g_obj[obj_idx].ii);
    if (!im || im->w <= 0 || im->h <= 0) return 0;
    bounds_reset(out);
    bounds_add_rect(out, g_obj[obj_idx].depth, g_obj[obj_idx].sy,
                    g_obj[obj_idx].depth + im->w, g_obj[obj_idx].sy + im->h);
    return out->valid;
}

static RectBounds clipboard_bounds(void)
{
    RectBounds b;
    bounds_reset(&b);
    for (int i = 0; i < g_clip_count; i++) {
        int w = 0, h = 0;
        if (!clipboard_image_dims(g_clipboard[i].ii, &w, &h)) continue;
        bounds_add_rect(&b, g_clipboard[i].depth, g_clipboard[i].sy,
                        g_clipboard[i].depth + w, g_clipboard[i].sy + h);
    }
    return b;
}

static RectBounds selected_object_bounds(void)
{
    RectBounds b;
    bounds_reset(&b);
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i] || g_obj_hidden[i]) continue;
        RectBounds ob;
        if (object_bounds_at(i, &ob))
            bounds_add_rect(&b, ob.x1, ob.y1, ob.x2, ob.y2);
    }
    return b;
}

int object_palette_for_image(const Obj *o, const Img *im)
{
    if (o && o->fl >= 0 && o->fl < g_n_pals) return o->fl;
    if (im && im->pal_idx >= 0 && im->pal_idx < g_n_pals) return im->pal_idx;
    return -1;
}

SDL_Texture *create_object_palette_preview_texture(int obj_i, int *out_w, int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!g_rend || obj_i < 0 || obj_i >= g_no) return NULL;
    Obj *o = &g_obj[obj_i];
    Img *im = img_find(o->ii);
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return NULL;
    if ((size_t)im->w * (size_t)im->h > 2097152u) return NULL;

    int pal = object_palette_for_image(o, im);
    std::vector<Uint32> px((size_t)im->w * (size_t)im->h, 0u);
    for (int y = 0; y < im->h; y++) {
        int sy = o->vfl ? (im->h - 1 - y) : y;
        for (int x = 0; x < im->w; x++) {
            int sx = o->hfl ? (im->w - 1 - x) : x;
            int v = im->pix[(size_t)sy * (size_t)im->w + (size_t)sx];
            if (v > 0)
                px[(size_t)y * (size_t)im->w + (size_t)x] = palette_argb_at(pal, v);
        }
    }

    SDL_Texture *tex = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (!tex) return NULL;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, NULL, px.data(), im->w * (int)sizeof(Uint32));
    if (out_w) *out_w = im->w;
    if (out_h) *out_h = im->h;
    return tex;
}

static long long rect_distance2(const RectBounds *a, const RectBounds *b)
{
    long long dx = 0;
    long long dy = 0;
    if (a->x2 < b->x1) dx = (long long)b->x1 - a->x2;
    else if (b->x2 < a->x1) dx = (long long)a->x1 - b->x2;
    if (a->y2 < b->y1) dy = (long long)b->y1 - a->y2;
    else if (b->y2 < a->y1) dy = (long long)a->y1 - b->y2;
    return dx * dx + dy * dy;
}

static int nearest_object_bounds_to(const RectBounds *src, RectBounds *out)
{
    if (!src || !src->valid || !out) return 0;
    bounds_reset(out);
    long long best_dist = LLONG_MAX;
    int best_center_tie = INT_MAX;
    int scx = (src->x1 + src->x2) / 2;
    int scy = (src->y1 + src->y2) / 2;
    for (int i = 0; i < g_no; i++) {
        if (g_obj_hidden[i]) continue;
        RectBounds ob;
        if (!object_bounds_at(i, &ob)) continue;
        long long dist = rect_distance2(src, &ob);
        int ocx = (ob.x1 + ob.x2) / 2;
        int ocy = (ob.y1 + ob.y2) / 2;
        int tie = abs(ocx - scx) + abs(ocy - scy);
        if (dist < best_dist || (dist == best_dist && tie < best_center_tie)) {
            best_dist = dist;
            best_center_tie = tie;
            *out = ob;
        }
    }
    return out->valid;
}

static int rounded_div_nearest(int value, int step)
{
    if (step <= 0) return 0;
    if (value >= 0) return (value + step / 2) / step;
    return -((-value + step / 2) / step);
}

static int visible_world_rect(RectBounds *out)
{
    if (!out) return 0;
    bounds_reset(out);
    int z = g_zoom > 0 ? g_zoom : 1;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int vw = (int)(ds.x / (float)z);
    int vh = (int)(ds.y / (float)z);
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    bounds_add_rect(out, g_view_x, g_view_y, g_view_x + vw, g_view_y + vh);
    return out->valid;
}

static void center_group_delta_in_view(const RectBounds *clip, int *dx, int *dy)
{
    if (!clip || !clip->valid || !dx || !dy) return;
    RectBounds view;
    if (!visible_world_rect(&view)) return;
    int group_w = clip->x2 - clip->x1;
    int group_h = clip->y2 - clip->y1;
    int target_x = view.x1 + ((view.x2 - view.x1) - group_w) / 2;
    int target_y = view.y1 + ((view.y2 - view.y1) - group_h) / 2;
    *dx = target_x - clip->x1;
    *dy = target_y - clip->y1;
    if (g_grid_snap && g_grid_sx > 0 && g_grid_sy > 0) {
        int px = clip->x1 + *dx;
        int py = clip->y1 + *dy;
        px = rounded_div_nearest(px, g_grid_sx) * g_grid_sx;
        py = rounded_div_nearest(py, g_grid_sy) * g_grid_sy;
        *dx = px - clip->x1;
        *dy = py - clip->y1;
    }
}

static int rect_intersects(const RectBounds *a, const RectBounds *b)
{
    return a && b && a->valid && b->valid &&
           a->x1 < b->x2 && a->x2 > b->x1 &&
           a->y1 < b->y2 && a->y2 > b->y1;
}

static void center_view_on_bounds(const RectBounds *b)
{
    if (!b || !b->valid) return;
    RectBounds view;
    if (!visible_world_rect(&view)) return;
    int vw = view.x2 - view.x1;
    int vh = view.y2 - view.y1;
    int cx = (b->x1 + b->x2) / 2;
    int cy = (b->y1 + b->y2) / 2;
    g_view_x = cx - vw / 2;
    g_view_y = cy - vh / 2;
    g_view_changed = 1;
}

static void snap_group_delta_to_asset_grid(const RectBounds *clip, const RectBounds *anchor,
                                           int *dx, int *dy)
{
    if (!clip || !clip->valid || !anchor || !anchor->valid || !dx || !dy) return;

    int px = clip->x1 + *dx;
    int py = clip->y1 + *dy;
    if (g_grid_sx > 1) {
        int rel = px - anchor->x1;
        px = anchor->x1 + rounded_div_nearest(rel, g_grid_sx) * g_grid_sx;
        *dx = px - clip->x1;
    }
    if (g_grid_sy > 1) {
        int rel = py - anchor->y2;
        py = anchor->y2 + rounded_div_nearest(rel, g_grid_sy) * g_grid_sy;
        *dy = py - clip->y1;
    }
}

static void smart_snap_group_delta_to_edges(const RectBounds *clip, int *dx, int *dy)
{
    if (!clip || !clip->valid || !dx || !dy) return;
    int snap = bg_editor_snap_dist();
    if (snap < 1) return;

    int gx1 = clip->x1 + *dx;
    int gy1 = clip->y1 + *dy;
    int gx2 = clip->x2 + *dx;
    int gy2 = clip->y2 + *dy;
    int gcx = (gx1 + gx2) / 2;
    int gcy = (gy1 + gy2) / 2;
    int best_ax = snap + 1;
    int best_ay = snap + 1;

    for (int i = 0; i < g_no; i++) {
        if (g_obj_hidden[i]) continue;
        RectBounds ob;
        if (g_snap_visible_pixels) {
            int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
            if (!bg_editor_object_snap_rect_at(i, g_obj[i].depth, g_obj[i].sy,
                                               &sx1, &sy1, &sx2, &sy2))
                continue;
            bounds_reset(&ob);
            bounds_add_rect(&ob, sx1, sy1, sx2, sy2);
        } else if (!object_bounds_at(i, &ob)) {
            continue;
        }
        int tcx = (ob.x1 + ob.x2) / 2;
        int tcy = (ob.y1 + ob.y2) / 2;
        int xpairs[7][2] = {
            {gx1, ob.x1}, {gx2, ob.x2}, {gx1, ob.x2}, {gx2, ob.x1},
            {gcx, tcx},  {gx1, tcx},   {gx2, tcx}
        };
        int ypairs[7][2] = {
            {gy1, ob.y1}, {gy2, ob.y2}, {gy1, ob.y2}, {gy2, ob.y1},
            {gcy, tcy},  {gy1, tcy},   {gy2, tcy}
        };
        for (int p = 0; p < 7; p++) {
            int adj = xpairs[p][1] - xpairs[p][0];
            if (abs(adj) <= snap && abs(adj) < abs(best_ax)) best_ax = adj;
            adj = ypairs[p][1] - ypairs[p][0];
            if (abs(adj) <= snap && abs(adj) < abs(best_ay)) best_ay = adj;
        }
    }

    if (abs(best_ax) <= snap) *dx += best_ax;
    if (abs(best_ay) <= snap) *dy += best_ay;
}

static void choose_smart_paste_delta(const RectBounds *clip, int fallback_x, int fallback_y,
                                     int prefer_view_center, int *dx, int *dy, int *used_anchor,
                                     int *used_view_center)
{
    if (dx) *dx = fallback_x;
    if (dy) *dy = fallback_y;
    if (used_anchor) *used_anchor = 0;
    if (used_view_center) *used_view_center = 0;
    if (!clip || !clip->valid || !dx || !dy) return;

    RectBounds anchor = selected_object_bounds();
    if (!anchor.valid && prefer_view_center) {
        center_group_delta_in_view(clip, dx, dy);
        smart_snap_group_delta_to_edges(clip, dx, dy);
        if (used_view_center) *used_view_center = 1;
        return;
    }
    if (!anchor.valid)
        nearest_object_bounds_to(clip, &anchor);
    if (anchor.valid) {
        *dx = anchor.x1 - clip->x1;
        *dy = anchor.y2 - clip->y1;
        snap_group_delta_to_asset_grid(clip, &anchor, dx, dy);
        smart_snap_group_delta_to_edges(clip, dx, dy);
        if (used_anchor) *used_anchor = 1;
        return;
    }

    center_group_delta_in_view(clip, dx, dy);
    smart_snap_group_delta_to_edges(clip, dx, dy);
    if (used_view_center) *used_view_center = 1;
}

int paste_clipboard_objects(int offset_x, int offset_y)
{
    int object_cap = editor_project_object_capacity();
    int palette_cap = editor_project_palette_capacity();
    int image_cap = editor_project_image_capacity();
    if (object_cap <= 0 || palette_cap <= 0 || image_cap <= 0) return 0;
    if (g_clip_count <= 0 || g_no + g_clip_count > object_cap) return 0;
    if (g_n_pals + g_clip_pal_count > palette_cap || g_ni + g_clip_image_count > image_cap) {
        snprintf(g_toast_msg, sizeof g_toast_msg, "Not enough image/palette slots for paste");
        g_toast_timer = 3.0f;
        return 0;
    }
    RectBounds clip = clipboard_bounds();
    int paste_dx = offset_x;
    int paste_dy = offset_y;
    int smart_anchor = 0;
    int view_center = 0;
    int cross_doc = clipboard_is_cross_document();
    choose_smart_paste_delta(&clip, offset_x, offset_y, cross_doc,
                             &paste_dx, &paste_dy, &smart_anchor, &view_center);

    undo_save_ex("Paste");
    std::vector<int> pal_map((size_t)palette_cap, -1);
    std::vector<int> img_map((size_t)g_clip_image_count, -1);

    for (int i = 0; i < g_clip_pal_count; i++) {
        int dst_pal = ensure_clip_palette_in_project(&g_clip_pals[i]);
        if (dst_pal < 0) return 0;
        if (g_clip_pals[i].src_idx >= 0 && g_clip_pals[i].src_idx < palette_cap)
            pal_map[g_clip_pals[i].src_idx] = dst_pal;
    }
    for (int i = 0; i < g_clip_image_count; i++) {
        int src_pal = g_clip_images[i].img.pal_idx;
        int dst_pal = (src_pal >= 0 && src_pal < palette_cap && pal_map[src_pal] >= 0)
            ? pal_map[src_pal] : g_clip_images[i].img.pal_idx;
        int dst_img = ensure_clip_image_in_project(&g_clip_images[i], dst_pal);
        if (dst_img < 0) return 0;
        img_map[i] = dst_img;
    }

    int max_order = 0;
    for (int i = 0; i < g_no; i++)
        if (g_obj[i].order > max_order) max_order = g_obj[i].order;
    editor_project_clear_selection();
    int start = g_no;
    RectBounds pasted_bounds;
    bounds_reset(&pasted_bounds);
    for (int i = 0; i < g_clip_count; i++) {
        Obj *o = editor_project_append_object_slot();
        if (!o) break;
        int obj_i = g_no - 1;
        *o = g_clipboard[i];
        int ci = clipboard_find_image(o->ii);
        if (ci >= 0 && ci < g_clip_image_count && img_map[ci] >= 0)
            o->ii = img_map[ci];
        int sp = o->fl;
        if (sp >= 0 && sp < palette_cap && pal_map[sp] >= 0)
            o->fl = pal_map[sp];
        o->depth += paste_dx;
        o->sy += paste_dy;
        o->order = max_order + 1 + i;
        g_obj_lock[obj_i] = g_clip_obj_lock[i];
        g_obj_hidden[obj_i] = g_clip_obj_hidden[i];
        g_sel_flags[obj_i] = 1;
        RectBounds ob;
        if (object_bounds_at(obj_i, &ob))
            bounds_add_rect(&pasted_bounds, ob.x1, ob.y1, ob.x2, ob.y2);
    }
    g_hl_obj = start;
    g_dirty = 1;
    g_need_rebuild = 1;
    RectBounds visible;
    if (visible_world_rect(&visible) && !rect_intersects(&pasted_bounds, &visible))
        center_view_on_bounds(&pasted_bounds);
    snprintf(g_toast_msg, sizeof g_toast_msg,
             smart_anchor ? "Pasted %d object(s) below nearest asset" :
             view_center ? "Pasted %d object(s) at screen center" :
             "Pasted %d object(s)",
             g_clip_count);
    g_toast_timer = 2.0f;
    return g_clip_count;
}

