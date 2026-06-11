#include "UI/sdl/sdl_world_objects.h"

#include "Core/image_lookup.h"
#include "UI/sdl/sdl_selection_rect.h"
#include "UI/assets/texture_cache.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"

#include <algorithm>
#include <climits>
#include <vector>

static void bdd_build_object_draw_order(std::vector<int> &order)
{
    order.clear();
    order.reserve((size_t)(g_no > 0 ? g_no : 0));
    for (int i = 0; i < g_no; i++)
        order.push_back(i);

    if (!g_runtime_layout_view)
        return;

    std::stable_sort(order.begin(), order.end(), [](int a, int b) {
        int ra = bdd_object_runtime_draw_rank(a);
        int rb = bdd_object_runtime_draw_rank(b);
        if (ra != rb) return ra < rb;
        int oa = (a >= 0 && a < g_no) ? g_obj[a].order : a;
        int ob = (b >= 0 && b < g_no) ? g_obj[b].order : b;
        if (oa != ob) return oa < ob;
        return a < b;
    });
}

/* Render the loaded stage's background straight from the game's *BLKS block
   tables (BGNDTBL.ASM) instead of the reconstructed BDB objects: each block is
   drawn at its module-local placement + the plane's baklst offset, with the
   plane's parallax scroll -- exactly as BGND draws it. Planes are walked in
   dlists draw-rank order; only ranks in [rank_lo, rank_hi) are drawn so the
   caller can interleave the floor at its -1/floor_code slot. */
static void bdd_block_background_draw(SDL_Renderer *rend, int clip_x, int clip_y,
                                      int zoom, int game_scroll,
                                      int rank_lo, int rank_hi)
{
    int n = bdd_stage_plane_count();
    if (n <= 0) return;

    /* Plane indices sorted by draw rank (far first); small n, simple insertion. */
    int order[BDD_STAGE_ACTOR_MAX];
    int count = 0;
    for (int i = 0; i < n && count < (int)(sizeof order / sizeof order[0]); i++) {
        int rank = 0;
        if (!bdd_stage_plane_info(i, NULL, 0, NULL, NULL, NULL, &rank)) continue;
        if (rank < rank_lo || rank >= rank_hi) continue;
        int pos = count++;
        order[pos] = i;
        while (pos > 0) {
            int ra = 0, rb = 0;
            bdd_stage_plane_info(order[pos - 1], NULL, 0, NULL, NULL, NULL, &ra);
            bdd_stage_plane_info(order[pos], NULL, 0, NULL, NULL, NULL, &rb);
            if (ra <= rb) break;
            int t = order[pos - 1]; order[pos - 1] = order[pos]; order[pos] = t;
            pos--;
        }
    }

    static BddBgndBlock blocks[512];
    for (int oi = 0; oi < count; oi++) {
        char mod[32];
        int ox = 0, oy = 0;
        float scroll = 1.0f;
        if (!bdd_stage_plane_info(order[oi], mod, sizeof mod, &ox, &oy, &scroll, NULL))
            continue;
        int nb = bdd_stage_module_blocks(mod, blocks, (int)(sizeof blocks / sizeof blocks[0]));
        for (int b = 0; b < nb; b++) {
            int hdr = blocks[b].hdr;
            if (hdr < 0 || hdr >= g_ni) continue;
            if (!g_textures || !g_textures[hdr]) continue;
            Img *im = &g_img[hdr];
            if (im->w <= 0 || im->h <= 0) continue;
            int world_x = ox + blocks[b].x;
            int world_y = oy + blocks[b].y;
            SDL_Rect dst = {
                clip_x + (world_x - (int)(game_scroll * scroll)) * zoom,
                clip_y + (world_y - g_game_view_y) * zoom,
                im->w * zoom, im->h * zoom
            };
            /* MK2 block flip bits (see mk2_analysis: wx |= 0x10/0x20). */
            SDL_RendererFlip flip = SDL_FLIP_NONE;
            if (blocks[b].flags & 0x0010) flip = (SDL_RendererFlip)(flip | SDL_FLIP_HORIZONTAL);
            if (blocks[b].flags & 0x0020) flip = (SDL_RendererFlip)(flip | SDL_FLIP_VERTICAL);
            SDL_RenderCopyEx(rend, g_textures[hdr], NULL, &dst, 0.0, NULL, flip);
        }
    }
}

void bdd_world_objects_draw(SDL_Renderer *rend,
                            int view_x, int view_y, int zoom, int ww, int wh,
                            int sel_rect_active, int sel_rx1, int sel_ry1,
                            int sel_rx2, int sel_ry2)
{
    if (!g_show_objects) return;

    std::vector<int> draw_order;
    bdd_build_object_draw_order(draw_order);

    int cx = 0, cy = 0, gw = ww, gh = wh;
    if (g_game_view) {
        BddScreenRect viewport;
        bdd_game_view_screen_rect(zoom, ww, wh, &viewport);
        cx = viewport.x;
        cy = viewport.y;
        gw = viewport.w;
        gh = viewport.h;
    }

    if (g_game_view && g_split_view) {
        int half_gw = gw / 2;
        for (int pass = 0; pass < 2; pass++) {
            int cur_scroll = (pass == 0) ? g_split_scroll_a : g_scroll_pos;
            SDL_Rect hclip = { cx + pass * half_gw, cy,
                               (pass == 0) ? half_gw : gw - half_gw, gh };
            SDL_RenderSetClipRect(rend, &hclip);
            SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
            SDL_RenderFillRect(rend, &hclip);
            for (size_t oi = 0; oi < draw_order.size(); oi++) {
                int i = draw_order[oi];
                if (g_obj_hidden[i] || runtime_actor_preview_hides_object(i)) continue;
                Obj *o = &g_obj[i];
                Img *im = img_find(o->ii);
                if (!im) continue;
                int ti = (int)(im - g_img);
                if (!g_textures || !g_textures[ti]) continue;
                BddScreenRect screen_rect;
                if (!bdd_object_screen_rect(i, im->w, im->h, view_x, view_y,
                                            zoom, ww, wh, cur_scroll, &screen_rect))
                    continue;
                if (screen_rect.x + screen_rect.w < hclip.x ||
                    screen_rect.x > hclip.x + hclip.w)
                    continue;
                SDL_Rect dst2 = { screen_rect.x, screen_rect.y, screen_rect.w, screen_rect.h };
                SDL_RendererFlip flip2 = SDL_FLIP_NONE;
                if (o->hfl) flip2 = (SDL_RendererFlip)(flip2 | SDL_FLIP_HORIZONTAL);
                if (o->vfl) flip2 = (SDL_RendererFlip)(flip2 | SDL_FLIP_VERTICAL);
                SDL_RenderCopyEx(rend, g_textures[ti], NULL, &dst2, 0.0, NULL, flip2);
            }
        }
        SDL_RenderSetClipRect(rend, NULL);

        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 255, 220, 0, 220);
        SDL_RenderDrawLine(rend, cx + half_gw, cy, cx + half_gw, cy + gh - 1);

        SDL_SetRenderDrawColor(rend, 10, 10, 15, 220);
        SDL_Rect top_s = { 0, 0, ww, cy };
        SDL_Rect bot_s = { 0, cy + gh, ww, wh - (cy + gh) };
        SDL_Rect lft_s = { 0, cy, cx, gh };
        SDL_Rect rgt_s = { cx + gw, cy, ww - (cx + gw), gh };
        SDL_RenderFillRect(rend, &top_s);
        SDL_RenderFillRect(rend, &bot_s);
        SDL_RenderFillRect(rend, &lft_s);
        SDL_RenderFillRect(rend, &rgt_s);
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 100);
        SDL_Rect frame_s = { cx - 1, cy - 1, gw + 2, gh + 2 };
        SDL_RenderDrawRect(rend, &frame_s);
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
    } else {
        if (g_game_view) {
            SDL_Rect clip_rect = { cx, cy, gw, gh };
            SDL_RenderSetClipRect(rend, &clip_rect);
            SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
            SDL_RenderFillRect(rend, &clip_rect);
        }

        /* Block-table background: draw the far planes (rank below the floor's
           dlists slot) before the floor/objects, exactly as BGND orders them. */
        int block_bg = g_game_view && g_block_background_render && bdd_stage_plane_count() > 0;
        int floor_rank = block_bg ? bdd_stage_floor_rank() : 0;
        if (block_bg)
            bdd_block_background_draw(rend, cx, cy, zoom, g_scroll_pos, INT_MIN, floor_rank);

        for (size_t oi = 0; oi < draw_order.size(); oi++) {
            int i = draw_order[oi];
            if (g_obj_hidden[i] || runtime_actor_preview_hides_object(i)) continue;
            /* Background-plane objects are drawn from their *BLKS block tables. */
            if (block_bg && bdd_object_in_background_plane(i)) continue;
            Obj *o = &g_obj[i];
            Img *im = img_find(o->ii);
            if (!im) continue;

            int ti = (int)(im - g_img);
            if (!g_textures || !g_textures[ti]) continue;

            BddScreenRect screen_rect;
            if (!bdd_object_screen_rect(i, im->w, im->h, view_x, view_y,
                                        zoom, ww, wh, g_scroll_pos, &screen_rect))
                continue;

            SDL_Rect dst = { screen_rect.x, screen_rect.y, screen_rect.w, screen_rect.h };
            SDL_RendererFlip flip = SDL_FLIP_NONE;
            if (o->hfl) flip = (SDL_RendererFlip)(flip | SDL_FLIP_HORIZONTAL);
            if (o->vfl) flip = (SDL_RendererFlip)(flip | SDL_FLIP_VERTICAL);

            /* The MK2 floor shears per scanline as the camera pans (skew_calla):
               line i leans by i*shear source px. Replicate by drawing the floor
               one source scanline at a time at the sheared x. */
            int floor_shear = (g_game_view && bdd_object_uses_runtime_floor_y(i))
                              ? bdd_runtime_floor_shear_per_line() : 0;
            if (floor_shear != 0 && im->h > 0) {
                for (int row = 0; row < im->h; row++) {
                    SDL_Rect ssrc = { 0, row, im->w, 1 };
                    SDL_Rect sdst = { screen_rect.x + floor_shear * row * zoom,
                                      screen_rect.y + row * zoom,
                                      im->w * zoom, zoom };
                    SDL_RenderCopyEx(rend, g_textures[ti], &ssrc, &sdst, 0.0, NULL, flip);
                }
            } else {
                SDL_RenderCopyEx(rend, g_textures[ti], NULL, &dst, 0.0, NULL, flip);
            }

            if (g_show_borders && !g_game_view) {
                SDL_SetRenderDrawColor(rend, 60, 60, 90, 180);
                SDL_RenderDrawRect(rend, &dst);
            }
        }

        /* Foreground planes (rank at/after the floor slot, e.g. the big wood1
           trees) draw over the floor, beneath the runtime actors. */
        if (block_bg)
            bdd_block_background_draw(rend, cx, cy, zoom, g_scroll_pos, floor_rank, INT_MAX);

        if (g_game_view) {
            SDL_RenderSetClipRect(rend, NULL);

            SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(rend, 10, 10, 15, 220);
            SDL_Rect top = { 0, 0, ww, cy };
            SDL_Rect bot = { 0, cy + gh, ww, wh - (cy + gh) };
            SDL_Rect lft = { 0, cy, cx, gh };
            SDL_Rect rgt = { cx + gw, cy, ww - (cx + gw), gh };
            SDL_RenderFillRect(rend, &top);
            SDL_RenderFillRect(rend, &bot);
            SDL_RenderFillRect(rend, &lft);
            SDL_RenderFillRect(rend, &rgt);

            SDL_SetRenderDrawColor(rend, 255, 255, 255, 100);
            SDL_Rect frame = { cx - 1, cy - 1, gw + 2, gh + 2 };
            SDL_RenderDrawRect(rend, &frame);
            SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
        }
    }

    if (sel_rect_active)
        bdd_selection_rect_draw(rend, sel_rx1, sel_ry1, sel_rx2, sel_ry2);

    if (g_hover_obj >= 0 && g_hover_obj < g_no) {
        Obj *ho = &g_obj[g_hover_obj];
        Img *him = img_find(ho->ii);
        if (him && !g_obj_hidden[g_hover_obj] && !runtime_actor_preview_hides_object(g_hover_obj)) {
            BddScreenRect screen_rect;
            if (bdd_object_screen_rect(g_hover_obj, him->w, him->h, view_x, view_y,
                                       zoom, ww, wh, g_scroll_pos, &screen_rect)) {
                SDL_Rect hdst = { screen_rect.x, screen_rect.y, screen_rect.w, screen_rect.h };
                SDL_SetRenderDrawColor(rend, 255, 255, 100, 60);
                SDL_RenderFillRect(rend, &hdst);
                SDL_SetRenderDrawColor(rend, 255, 255, 100, 200);
                SDL_RenderDrawRect(rend, &hdst);
            }
        }
    }

    for (int si = 0; si < g_no; si++) {
        if (!g_sel_flags[si] || runtime_actor_preview_hides_object(si)) continue;
        Obj *ho = &g_obj[si];
        Img *him = img_find(ho->ii);
        if (!him) continue;

        int layer = (ho->wx >> 8) & 0xFF;
        static const struct { int l; Uint8 r, g, b; } lc[] = {
            {0x32, 255, 180, 100}, {0x3C, 100, 200, 255}, {0x40, 100, 255, 120},
            {0x41, 200, 255, 100}, {0x43, 255, 150, 200}, {0x46, 255, 100, 100},
            {-1,   255, 200, 50}
        };
        Uint8 hr = 255, hg = 200, hb = 50;
        for (int li = 0; lc[li].l >= 0; li++) {
            if (layer == lc[li].l) {
                hr = lc[li].r; hg = lc[li].g; hb = lc[li].b;
                break;
            }
        }

        BddScreenRect screen_rect;
        if (!bdd_object_screen_rect(si, him->w, him->h, view_x, view_y,
                                    zoom, ww, wh, g_scroll_pos, &screen_rect))
            continue;
        SDL_Rect hdst = { screen_rect.x, screen_rect.y, screen_rect.w, screen_rect.h };
        SDL_SetRenderDrawColor(rend, hr, hg, hb, si == g_hl_obj ? 230 : 140);
        SDL_RenderDrawRect(rend, &hdst);
        SDL_Rect hi = { screen_rect.x + 1, screen_rect.y + 1,
                        screen_rect.w - 2, screen_rect.h - 2 };
        SDL_SetRenderDrawColor(rend, hr, hg, hb, si == g_hl_obj ? 90 : 40);
        SDL_RenderDrawRect(rend, &hi);
    }
}
