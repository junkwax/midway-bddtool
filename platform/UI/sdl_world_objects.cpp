#include "UI/sdl_world_objects.h"

#include "Core/image_lookup.h"
#include "UI/sdl_selection_rect.h"
#include "UI/texture_cache.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"

void bdd_world_objects_draw(SDL_Renderer *rend,
                            int view_x, int view_y, int zoom, int ww, int wh,
                            int sel_rect_active, int sel_rx1, int sel_ry1,
                            int sel_rx2, int sel_ry2)
{
    if (!g_show_objects) return;

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
            for (int i = 0; i < g_no; i++) {
                if (g_obj_hidden[i]) continue;
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

        for (int i = 0; i < g_no; i++) {
            if (g_obj_hidden[i]) continue;
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
            SDL_RenderCopyEx(rend, g_textures[ti], NULL, &dst, 0.0, NULL, flip);

            if (g_show_borders && !g_game_view) {
                SDL_SetRenderDrawColor(rend, 60, 60, 90, 180);
                SDL_RenderDrawRect(rend, &dst);
            }
        }

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
        if (him && !g_obj_hidden[g_hover_obj]) {
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
        if (!g_sel_flags[si]) continue;
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
