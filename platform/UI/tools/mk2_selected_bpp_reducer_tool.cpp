#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

static int  g_group_bpp_target = 4;
static bool g_group_bpp_override = false;
static bool g_group_bpp_include_shared = false;
static int  g_group_bpp_preview_bg = 0; /* 0=black, 1=white */

struct GroupBppPaletteColor {
    int old_idx;
    Uint32 color;
    size_t count;
};

struct GroupBppCandidate {
    int img_i;
    int image_idx;
    int source_pal;
    int pack_idx;
    int total_uses;
    int selected_uses;
    int used_colors;
    int old_max_idx;
    int old_bpp;
    int new_bpp;
    size_t before_bytes;
    size_t after_bytes;
    size_t saved_bytes;
    bool can_apply;
    bool lossy;
    unsigned long long palette_error;
    int max_color_error;
    char reason[96];
    int map[256];
    Uint32 new_pal[256];
    int new_pal_count;
    int base_map[256];
    Uint32 base_pal[256];
    int base_pal_count;
};

struct GroupBppPack {
    Uint32 colors[256];
    int count;
    int candidate_count;
};

struct SelectedBppBounds {
    int x1, y1, x2, y2;
    bool valid;
};

static int selected_bpp_argb_distance(Uint32 a, Uint32 b)
{
    int ar = (int)((a >> 16) & 0xFF);
    int ag = (int)((a >> 8) & 0xFF);
    int ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF);
    int bg = (int)((b >> 8) & 0xFF);
    int bb = (int)(b & 0xFF);
    int dr = ar - br;
    int dg = ag - bg;
    int db = ab - bb;
    return dr * dr + dg * dg + db * db;
}

static void selected_bpp_bounds_reset(SelectedBppBounds *b)
{
    b->x1 = b->y1 = INT_MAX;
    b->x2 = b->y2 = INT_MIN;
    b->valid = false;
}

static void selected_bpp_bounds_add_rect(SelectedBppBounds *b, int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) return;
    if (!b->valid) {
        b->x1 = x1; b->y1 = y1; b->x2 = x2; b->y2 = y2;
        b->valid = true;
        return;
    }
    if (x1 < b->x1) b->x1 = x1;
    if (y1 < b->y1) b->y1 = y1;
    if (x2 > b->x2) b->x2 = x2;
    if (y2 > b->y2) b->y2 = y2;
}

static SelectedBppBounds selected_bpp_object_bounds(void)
{
    SelectedBppBounds b;
    selected_bpp_bounds_reset(&b);
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i] || g_obj_hidden[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        if (!im || im->w <= 0 || im->h <= 0) continue;
        selected_bpp_bounds_add_rect(&b, g_obj[i].depth, g_obj[i].sy,
                                     g_obj[i].depth + im->w, g_obj[i].sy + im->h);
    }
    return b;
}

static void selected_bpp_draw_solid_backdrop_image(SDL_Texture *tex, ImVec2 size, bool white)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 bg = white ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
    ImU32 edge = white ? IM_COL32(40, 40, 40, 160) : IM_COL32(210, 210, 210, 120);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg);
    ImGui::Image(tex, size);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), edge);
}

static int group_bpp_index_limit(void)
{
    if (g_group_bpp_target >= 8) return 255;
    int limit = (1 << g_group_bpp_target) - 1;
    return limit < 1 ? 1 : limit;
}

static void init_group_bpp_candidate(GroupBppCandidate *c, int img_i, int image_idx, int pal)
{
    std::memset(c, 0, sizeof(*c));
    c->img_i = img_i;
    c->image_idx = image_idx;
    c->source_pal = pal;
    c->pack_idx = -1;
    c->can_apply = true;
    std::snprintf(c->reason, sizeof c->reason, "ready");
}

static int group_bpp_best_palette_slot(Uint32 color,
                                       const std::vector<GroupBppPaletteColor> &colors,
                                       const std::vector<int> &chosen,
                                       int *out_dist)
{
    int best_slot = 0;
    int best_dist = INT_MAX;
    for (size_t i = 0; i < chosen.size(); i++) {
        int pos = chosen[i];
        if (pos < 0 || pos >= (int)colors.size()) continue;
        int d = selected_bpp_argb_distance(color, colors[(size_t)pos].color);
        if (d < best_dist) {
            best_dist = d;
            best_slot = (int)i;
        }
    }
    if (out_dist) *out_dist = best_dist == INT_MAX ? 0 : best_dist;
    return best_slot;
}

static void group_bpp_build_nearest_palette(GroupBppCandidate *c,
                                            const std::vector<GroupBppPaletteColor> &colors,
                                            int limit)
{
    std::vector<int> chosen;
    if (limit < 1) limit = 1;
    int want = (int)colors.size() < limit ? (int)colors.size() : limit;
    if (want <= 0) {
        c->new_pal_count = 1;
        c->new_bpp = 1;
        return;
    }

    int first = 0;
    for (size_t i = 1; i < colors.size(); i++) {
        if (colors[i].count > colors[(size_t)first].count)
            first = (int)i;
    }
    chosen.push_back(first);

    while ((int)chosen.size() < want) {
        long long best_score = -1;
        int best_pos = -1;
        for (size_t i = 0; i < colors.size(); i++) {
            bool already = false;
            for (size_t j = 0; j < chosen.size(); j++) {
                if (chosen[j] == (int)i) {
                    already = true;
                    break;
                }
            }
            if (already) continue;

            int nearest = 0;
            group_bpp_best_palette_slot(colors[i].color, colors, chosen, &nearest);
            long long score = (long long)nearest * (long long)(colors[i].count + 1u);
            if (score > best_score) {
                best_score = score;
                best_pos = (int)i;
            }
        }
        if (best_pos < 0) break;
        chosen.push_back(best_pos);
    }

    c->new_pal_count = (int)chosen.size() + 1;
    for (size_t i = 0; i < chosen.size(); i++)
        c->new_pal[i + 1] = colors[(size_t)chosen[i]].color;

    for (const GroupBppPaletteColor &color : colors) {
        int dist = 0;
        int slot = group_bpp_best_palette_slot(color.color, colors, chosen, &dist);
        int mapped = slot + 1;
        if (mapped < 1) mapped = 1;
        if (mapped >= 256) mapped = 255;
        c->map[color.old_idx] = mapped;
        c->palette_error += (unsigned long long)dist * (unsigned long long)color.count;
        if (dist > c->max_color_error) c->max_color_error = dist;
    }
    c->new_bpp = mk2_bpp_for_max_index(c->new_pal_count - 1);
}

int group_bpp_color_index(const Uint32 *pal, int count, Uint32 color)
{
    Uint32 want = color & 0x00FFFFFFu;
    for (int i = 1; i < count; i++)
        if ((pal[i] & 0x00FFFFFFu) == want) return i;
    return -1;
}

static int group_bpp_pack_extra_colors(const GroupBppPack &pack,
                                       const GroupBppCandidate &cand,
                                       int *out_union)
{
    int extra = 0;
    int union_count = pack.count;
    for (int i = 1; i < cand.new_pal_count; i++) {
        if (group_bpp_color_index(pack.colors, union_count, cand.new_pal[i]) >= 0)
            continue;
        extra++;
        union_count++;
    }
    if (out_union) *out_union = union_count;
    return extra;
}

static void group_bpp_add_candidate_to_pack(GroupBppPack *pack,
                                            const GroupBppCandidate &cand,
                                            int limit)
{
    if (!pack) return;
    for (int i = 1; i < cand.new_pal_count; i++) {
        if (group_bpp_color_index(pack->colors, pack->count, cand.new_pal[i]) >= 0)
            continue;
        if (pack->count <= limit && pack->count < 256)
            pack->colors[pack->count++] = cand.new_pal[i];
    }
    pack->candidate_count++;
}

static int group_bpp_pack_candidates(std::vector<GroupBppCandidate> &cands,
                                     std::vector<GroupBppPack> *out_packs)
{
    if (out_packs) out_packs->clear();
    int limit = group_bpp_index_limit();
    std::vector<int> order;
    for (int i = 0; i < (int)cands.size(); i++) {
        GroupBppCandidate &cand = cands[(size_t)i];
        cand.pack_idx = -1;
        if (cand.base_pal_count > 0) {
            std::memcpy(cand.map, cand.base_map, sizeof cand.map);
            std::memcpy(cand.new_pal, cand.base_pal, sizeof cand.new_pal);
            cand.new_pal_count = cand.base_pal_count;
            cand.new_bpp = mk2_bpp_for_max_index(cand.new_pal_count - 1);
            if (cand.img_i >= 0 && cand.img_i < g_ni) {
                cand.after_bytes = mk2_estimate_image_bytes_for_bpp(&g_img[cand.img_i], cand.new_bpp);
                cand.saved_bytes = cand.before_bytes > cand.after_bytes ? cand.before_bytes - cand.after_bytes : 0;
            }
        }
        if (!cands[(size_t)i].can_apply) {
            continue;
        }
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const GroupBppCandidate &ca = cands[(size_t)a];
        const GroupBppCandidate &cb = cands[(size_t)b];
        if (ca.new_pal_count != cb.new_pal_count)
            return ca.new_pal_count > cb.new_pal_count;
        if (ca.selected_uses != cb.selected_uses)
            return ca.selected_uses > cb.selected_uses;
        return ca.image_idx < cb.image_idx;
    });

    std::vector<GroupBppPack> packs;
    for (int ci : order) {
        GroupBppCandidate &cand = cands[(size_t)ci];
        int best = -1;
        int best_extra = INT_MAX;
        int best_union = INT_MAX;
        for (int pi = 0; pi < (int)packs.size(); pi++) {
            int union_count = 0;
            int extra = group_bpp_pack_extra_colors(packs[(size_t)pi], cand, &union_count);
            if (union_count > limit + 1) continue;
            if (extra < best_extra || (extra == best_extra && union_count < best_union)) {
                best = pi;
                best_extra = extra;
                best_union = union_count;
            }
        }
        if (best < 0) {
            GroupBppPack pack;
            std::memset(&pack, 0, sizeof pack);
            pack.count = 1;
            packs.push_back(pack);
            best = (int)packs.size() - 1;
        }
        cand.pack_idx = best;
        group_bpp_add_candidate_to_pack(&packs[(size_t)best], cand, limit);
    }

    for (GroupBppCandidate &cand : cands) {
        if (!cand.can_apply || cand.pack_idx < 0 || cand.pack_idx >= (int)packs.size())
            continue;
        GroupBppPack &pack = packs[(size_t)cand.pack_idx];
        int local_to_pack[256];
        std::memset(local_to_pack, 0, sizeof local_to_pack);
        for (int i = 1; i < cand.new_pal_count; i++) {
            int idx = group_bpp_color_index(pack.colors, pack.count, cand.new_pal[i]);
            local_to_pack[i] = idx > 0 ? idx : 0;
        }
        for (int i = 1; i < 256; i++) {
            int local = cand.map[i];
            cand.map[i] = (local > 0 && local < 256) ? local_to_pack[local] : 0;
        }
        std::memset(cand.new_pal, 0, sizeof cand.new_pal);
        std::memcpy(cand.new_pal, pack.colors, sizeof(Uint32) * (size_t)pack.count);
        cand.new_pal_count = pack.count;
        cand.new_bpp = mk2_bpp_for_max_index(cand.new_pal_count - 1);
        cand.after_bytes = mk2_estimate_image_bytes_for_bpp(&g_img[cand.img_i], cand.new_bpp);
        cand.saved_bytes = cand.before_bytes > cand.after_bytes ? cand.before_bytes - cand.after_bytes : 0;
    }

    if (out_packs) *out_packs = packs;
    return (int)packs.size();
}

static int group_bpp_count_candidate_packs(const std::vector<GroupBppCandidate> &cands)
{
    const int image_cap = editor_project_image_capacity();
    std::vector<unsigned char> used((size_t)(image_cap > 0 ? image_cap : 0), 0);
    int count = 0;
    for (const GroupBppCandidate &cand : cands) {
        if (!cand.can_apply || cand.pack_idx < 0 || cand.pack_idx >= image_cap) continue;
        if (!used[(size_t)cand.pack_idx]) {
            used[(size_t)cand.pack_idx] = 1;
            count++;
        }
    }
    return count;
}

static void build_group_bpp_candidates(std::vector<GroupBppCandidate> &out,
                                       int *out_selected, int *out_eligible,
                                       size_t *out_before, size_t *out_after,
                                       size_t *out_saved)
{
    out.clear();
    if (out_selected) *out_selected = 0;
    if (out_eligible) *out_eligible = 0;
    if (out_before) *out_before = 0;
    if (out_after) *out_after = 0;
    if (out_saved) *out_saved = 0;

    const int image_cap = editor_project_image_capacity();
    std::vector<int> cand_for_img((size_t)(image_cap > 0 ? image_cap : 0), -1);

    for (int oi = 0; oi < g_no; oi++) {
        if (!g_sel_flags[oi]) continue;
        if (out_selected) (*out_selected)++;
        Obj *o = &g_obj[oi];
        Img *im = img_find(o->ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        int img_i = (int)(im - g_img);
        int pal = object_palette_for_image(o, im);
        if (img_i < 0 || img_i >= image_cap) continue;

        int ci = cand_for_img[(size_t)img_i];
        if (ci < 0) {
            GroupBppCandidate c;
            init_group_bpp_candidate(&c, img_i, im->idx, pal);
            out.push_back(c);
            ci = (int)out.size() - 1;
            cand_for_img[(size_t)img_i] = ci;
        } else if (pal != out[(size_t)ci].source_pal) {
            out[(size_t)ci].can_apply = false;
            std::snprintf(out[(size_t)ci].reason, sizeof out[(size_t)ci].reason,
                          "selected uses have mixed palettes");
        }
        out[(size_t)ci].selected_uses++;
    }

    int limit = group_bpp_index_limit();
    for (size_t ci = 0; ci < out.size(); ci++) {
        GroupBppCandidate *c = &out[ci];
        Img *im = (c->img_i >= 0 && c->img_i < g_ni) ? &g_img[c->img_i] : NULL;
        if (!im || !im->pix || c->source_pal < 0 || c->source_pal >= g_n_pals) {
            c->can_apply = false;
            std::snprintf(c->reason, sizeof c->reason, "missing image or palette");
            continue;
        }

        c->total_uses = image_object_ref_count(c->image_idx);
        if (!g_group_bpp_include_shared && c->total_uses != c->selected_uses) {
            c->can_apply = false;
            std::snprintf(c->reason, sizeof c->reason, "shared outside selection");
        }
        if (g_group_bpp_include_shared) {
            for (int oi = 0; oi < g_no; oi++) {
                if (g_obj[oi].ii != c->image_idx) continue;
                int pal = object_palette_for_image(&g_obj[oi], im);
                if (pal == c->source_pal) continue;
                c->can_apply = false;
                std::snprintf(c->reason, sizeof c->reason, "all uses need one source palette");
                break;
            }
        }

        size_t uses[256];
        std::memset(uses, 0, sizeof uses);
        c->old_max_idx = 0;
        size_t pix_count = (size_t)im->w * (size_t)im->h;
        for (size_t k = 0; k < pix_count; k++) {
            int v = im->pix[k];
            if (v <= 0) continue;
            uses[v]++;
            if (v > c->old_max_idx) c->old_max_idx = v;
        }
        c->used_colors = 0;
        std::vector<GroupBppPaletteColor> colors;
        for (int i = 1; i < 256; i++) {
            if (!uses[i]) continue;
            GroupBppPaletteColor color;
            color.old_idx = i;
            color.color = palette_argb_at(c->source_pal, i);
            color.count = uses[i];
            colors.push_back(color);
            c->used_colors++;
        }

        c->old_bpp = mk2_bpp_for_image(im);
        c->before_bytes = mk2_estimate_image_bytes(im);
        if (out_before) *out_before += c->before_bytes;

        if (c->can_apply && c->used_colors > limit && !g_group_bpp_override) {
            c->can_apply = false;
            std::snprintf(c->reason, sizeof c->reason, "%d colors need >%dbpp",
                          c->used_colors, g_group_bpp_target);
        }
        if (c->can_apply && c->used_colors > limit && g_group_bpp_override) {
            c->lossy = true;
            std::snprintf(c->reason, sizeof c->reason, "nearest %d -> %d colors",
                          c->used_colors, limit);
        }

        std::memset(c->map, 0, sizeof c->map);
        std::memset(c->new_pal, 0, sizeof c->new_pal);
        c->palette_error = 0;
        c->max_color_error = 0;
        if (c->lossy) {
            group_bpp_build_nearest_palette(c, colors, limit);
        } else {
            int next = 1;
            for (const GroupBppPaletteColor &color : colors) {
                c->map[color.old_idx] = next;
                c->new_pal[next] = color.color;
                next++;
            }
            c->new_pal_count = next > 1 ? next : 1;
            c->new_bpp = mk2_bpp_for_max_index(c->new_pal_count - 1);
        }
        c->after_bytes = mk2_estimate_image_bytes_for_bpp(im, c->new_bpp);
        if (c->before_bytes > c->after_bytes)
            c->saved_bytes = c->before_bytes - c->after_bytes;
        else
            c->saved_bytes = 0;
        std::memcpy(c->base_map, c->map, sizeof c->base_map);
        std::memcpy(c->base_pal, c->new_pal, sizeof c->base_pal);
        c->base_pal_count = c->new_pal_count;

        if (c->can_apply && c->old_bpp <= c->new_bpp) {
            c->can_apply = false;
            std::snprintf(c->reason, sizeof c->reason, "already %dbpp", c->old_bpp);
        }
        if (c->can_apply && c->saved_bytes == 0) {
            c->can_apply = false;
            std::snprintf(c->reason, sizeof c->reason, "no byte saving");
        }
    }

    for (size_t pass = 0; pass <= out.size(); pass++) {
        bool filtered_after_pack = false;
        group_bpp_pack_candidates(out, NULL);
        for (GroupBppCandidate &c : out) {
            if (!c.can_apply) continue;
            if (c.old_bpp <= c.new_bpp) {
                c.can_apply = false;
                std::snprintf(c.reason, sizeof c.reason, "shared pack is %dbpp", c.new_bpp);
                filtered_after_pack = true;
            } else if (c.saved_bytes == 0) {
                c.can_apply = false;
                std::snprintf(c.reason, sizeof c.reason, "no byte saving");
                filtered_after_pack = true;
            }
        }
        if (!filtered_after_pack) break;
    }
    for (size_t ci = 0; ci < out.size(); ci++) {
        GroupBppCandidate *c = &out[ci];
        if (c->can_apply) {
            if (out_eligible) (*out_eligible)++;
            if (out_saved) *out_saved += c->saved_bytes;
        }
        if (out_after) *out_after += c->can_apply ? c->after_bytes : c->before_bytes;
    }
}

static const GroupBppCandidate *find_group_bpp_candidate(const std::vector<GroupBppCandidate> &cands,
                                                        int img_i)
{
    for (size_t i = 0; i < cands.size(); i++)
        if (cands[i].img_i == img_i) return &cands[i];
    return NULL;
}

static SDL_Texture *create_group_bpp_preview_texture(const std::vector<GroupBppCandidate> &cands,
                                                     bool after, int *out_w, int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    SelectedBppBounds b = selected_bpp_object_bounds();
    if (!b.valid || !g_rend) return NULL;
    int w = b.x2 - b.x1;
    int h = b.y2 - b.y1;
    if (w <= 0 || h <= 0 || (size_t)w * (size_t)h > 2097152u) return NULL;

    std::vector<Uint32> px((size_t)w * (size_t)h, 0u);
    for (int oi = 0; oi < g_no; oi++) {
        if (!g_sel_flags[oi]) continue;
        Obj *o = &g_obj[oi];
        Img *im = img_find(o->ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        int img_i = (int)(im - g_img);
        int pal = object_palette_for_image(o, im);
        const GroupBppCandidate *cand = after ? find_group_bpp_candidate(cands, img_i) : NULL;

        for (int y = 0; y < im->h; y++) {
            int sy = o->vfl ? (im->h - 1 - y) : y;
            int dy = o->sy + y - b.y1;
            if (dy < 0 || dy >= h) continue;
            for (int x = 0; x < im->w; x++) {
                int sx = o->hfl ? (im->w - 1 - x) : x;
                int dx = o->depth + x - b.x1;
                if (dx < 0 || dx >= w) continue;
                int v = im->pix[(size_t)sy * (size_t)im->w + (size_t)sx];
                if (v <= 0) continue;

                Uint32 c = 0;
                if (cand && cand->can_apply && cand->map[v] > 0)
                    c = cand->new_pal[cand->map[v]];
                else
                    c = palette_argb_at(pal, v);
                px[(size_t)dy * (size_t)w + (size_t)dx] = c;
            }
        }
    }

    SDL_Texture *tex = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return NULL;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, NULL, px.data(), w * (int)sizeof(Uint32));
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return tex;
}

static void draw_group_bpp_preview_pair(const std::vector<GroupBppCandidate> &cands)
{
    static SDL_Texture *s_before = NULL;
    static SDL_Texture *s_after = NULL;
    static int s_frame = -1;
    static int s_w = 0, s_h = 0;
    int frame = ImGui::GetFrameCount();
    if (s_frame != frame) {
        if (s_before) { SDL_DestroyTexture(s_before); s_before = NULL; }
        if (s_after)  { SDL_DestroyTexture(s_after);  s_after = NULL; }
        int aw = 0, ah = 0;
        s_before = create_group_bpp_preview_texture(cands, false, &s_w, &s_h);
        s_after  = create_group_bpp_preview_texture(cands, true, &aw, &ah);
        s_frame = frame;
    }

    if (!s_before || !s_after || s_w <= 0 || s_h <= 0) {
        ImGui::TextDisabled("Preview unavailable for this selection.");
        return;
    }

    float avail = ImGui::GetContentRegionAvail().x;
    bool side_by_side = avail >= 520.0f;
    float preview_w = side_by_side ? (avail - 12.0f) * 0.5f : avail;
    if (preview_w < 160.0f) preview_w = 160.0f;
    float max_h = side_by_side ? 360.0f : 430.0f;
    float scale = preview_w / (float)s_w;
    if (scale > 4.0f) scale = 4.0f;
    if (scale > max_h / (float)s_h) scale = max_h / (float)s_h;
    if (scale < 0.1f) scale = 0.1f;
    ImVec2 sz((float)s_w * scale, (float)s_h * scale);
    bool white_bg = g_group_bpp_preview_bg != 0;
    ImGui::TextDisabled("Before");
    selected_bpp_draw_solid_backdrop_image(s_before, sz, white_bg);
    if (side_by_side) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("After");
    selected_bpp_draw_solid_backdrop_image(s_after, sz, white_bg);
    ImGui::EndGroup();
}

static int apply_group_bpp_reducer(const std::vector<GroupBppCandidate> &cands)
{
    int eligible = 0;
    for (size_t i = 0; i < cands.size(); i++)
        if (cands[i].can_apply) eligible++;
    if (eligible <= 0) return 0;
    int pack_count = group_bpp_count_candidate_packs(cands);
    if (pack_count <= 0) return 0;
    if (!editor_project_reserve_palettes(g_n_pals + pack_count)) return -1;

    undo_save_ex("Lower Selected BPP");
    size_t saved = 0;
    int changed = 0;
    std::vector<int> pack_to_pal((size_t)pack_count, -1);
    for (size_t ci = 0; ci < cands.size(); ci++) {
        const GroupBppCandidate *c = &cands[ci];
        if (!c->can_apply || c->img_i < 0 || c->img_i >= g_ni) continue;
        Img *im = &g_img[c->img_i];
        if (c->pack_idx < 0 || c->pack_idx >= pack_count) continue;
        int dst_pal = pack_to_pal[(size_t)c->pack_idx];
        if (dst_pal < 0) {
            char name[64];
            std::snprintf(name, sizeof name, "BPP%d_G%02d", c->new_bpp, c->pack_idx & 0xFF);
            dst_pal = editor_project_append_palette_slot(name, c->new_pal_count, c->new_pal);
            if (dst_pal < 0) return -1;
            pack_to_pal[(size_t)c->pack_idx] = dst_pal;
        }

        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t k = 0; k < n; k++) {
            Uint8 v = im->pix[k];
            if (v > 0) im->pix[k] = (Uint8)c->map[v];
        }
        im->pal_idx = dst_pal;
        for (int oi = 0; oi < g_no; oi++) {
            if (g_obj[oi].ii != c->image_idx) continue;
            if (g_group_bpp_include_shared || g_sel_flags[oi])
                g_obj[oi].fl = dst_pal;
        }
        saved += c->saved_bytes;
        changed++;
    }
    if (changed > 0) {
        sync_bdb_header_counts();
        g_need_rebuild = 1;
        g_dirty = 1;
        g_mk2_palette_sync_dirty = true;
    }
    char msg[128];
    std::snprintf(msg, sizeof msg, "Lowered %d image(s) into %d shared palette(s), est saved 0x%zX byte(s)",
                  changed, pack_count, saved);
    stage_set_toast(msg);
    return changed;
}

void draw_mk2_selected_bpp_reducer_tool(void)
{
    std::vector<GroupBppCandidate> cands;
    int selected = 0, eligible = 0;
    size_t before = 0, after = 0, saved = 0;
    build_group_bpp_candidates(cands, &selected, &eligible, &before, &after, &saved);
    int shared_palettes = group_bpp_count_candidate_packs(cands);

    ImGui::Text("Selected Object Bit-Depth Reducer");
    if (selected <= 0) {
        ImGui::TextDisabled("Select one or more object placements first.");
        return;
    }

    static const int bpp_opts[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ImGui::Text("Target:");
    ImGui::SameLine();
    for (int i = 0; i < (int)(sizeof(bpp_opts) / sizeof(bpp_opts[0])); i++) {
        if (i) ImGui::SameLine(0, 6);
        int b = bpp_opts[i];
        if (g_group_bpp_target == b)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        char label[12];
        std::snprintf(label, sizeof label, "%dbpp", b);
        if (ImGui::SmallButton(label)) g_group_bpp_target = b;
        if (g_group_bpp_target == b)
            ImGui::PopStyleColor();
    }
    ImGui::Checkbox("Include shared image uses", &g_group_bpp_include_shared);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off: skip images also used by unselected objects. On: update all uses when they share the same source palette.");
    ImGui::SameLine();
    ImGui::Checkbox("Override color count", &g_group_bpp_override);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When the selected image has too many colors for the target BPP, build the closest reduced palette instead of refusing it.");

    ImGui::Text("Preview backdrop:");
    ImGui::SameLine();
    if (g_group_bpp_preview_bg == 0)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Black")) g_group_bpp_preview_bg = 0;
    if (g_group_bpp_preview_bg == 0)
        ImGui::PopStyleColor();
    ImGui::SameLine(0, 6);
    if (g_group_bpp_preview_bg == 1)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("White")) g_group_bpp_preview_bg = 1;
    if (g_group_bpp_preview_bg == 1)
        ImGui::PopStyleColor();

    ImGui::Text("%d selected object(s), %d unique image(s), %d eligible, %d shared palette(s)",
                selected, (int)cands.size(), eligible, shared_palettes);
    ImGui::Text("Estimated image bytes: 0x%zX -> 0x%zX  save 0x%zX",
                before, after, saved);
    draw_group_bpp_preview_pair(cands);

    if (ImGui::BeginTable("selected_bpp_reduce", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupColumn("img", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("grp", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("uses", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("bpp", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("save", ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableSetupColumn("state");
        ImGui::TableHeadersRow();
        for (size_t ci = 0; ci < cands.size(); ci++) {
            GroupBppCandidate *c = &cands[ci];
            ImGui::TableNextRow();
            if (c->can_apply && c->lossy)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(140, 105, 40, 65));
            else if (c->can_apply)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(45, 105, 55, 55));
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", c->image_idx);
            ImGui::TableNextColumn();
            if (c->pack_idx >= 0) ImGui::Text("%d", c->pack_idx);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", c->selected_uses, c->total_uses);
            ImGui::TableNextColumn(); ImGui::Text("%d", c->used_colors);
            ImGui::TableNextColumn(); ImGui::Text("%d", c->old_max_idx);
            ImGui::TableNextColumn(); ImGui::Text("%d->%d", c->old_bpp, c->new_bpp);
            ImGui::TableNextColumn(); ImGui::Text("0x%zX", c->saved_bytes);
            ImGui::TableNextColumn();
            if (c->can_apply && c->lossy)
                ImGui::TextColored(ImVec4(1.0f,0.78f,0.32f,1.0f), "%s", c->reason);
            else if (c->can_apply)
                ImGui::TextColored(ImVec4(0.45f,1.0f,0.55f,1.0f), "ready");
            else
                ImGui::TextDisabled("%s", c->reason);
        }
        ImGui::EndTable();
    }

    bool can_apply = eligible > 0;
    if (!can_apply) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Lower BPP to Selection", ImVec2(-1, 0))) {
        int rc = apply_group_bpp_reducer(cands);
        if (rc < 0) stage_set_toast("Lower BPP refused: not enough palette slots");
        else if (rc == 0) stage_set_toast("No selected image can be lowered at this target");
    }
    if (!can_apply) ImGui::EndDisabled();
    bool undo_disabled = !undo_is_available();
    if (undo_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Undo Last Edit", ImVec2((ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f, 0)))
        undo_restore();
    if (undo_disabled) ImGui::EndDisabled();
    ImGui::SameLine();
    bool redo_disabled = !redo_is_available();
    if (redo_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Redo Last Edit", ImVec2(-1, 0)))
        redo_restore();
    if (redo_disabled) ImGui::EndDisabled();
}
