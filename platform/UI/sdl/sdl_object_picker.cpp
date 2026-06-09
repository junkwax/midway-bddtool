#include "UI/sdl_object_picker.h"

#include "Core/editor_project_storage.h"
#include "UI/sdl_bitmap_font.h"
#include "UI/texture_cache.h"
#include "bg_editor_globals.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

enum {
    PICKER_W = 170,
    PICKER_ITEM_H = 70,
    PICKER_THUMB = 54
};

static int g_picker_open = 0;
static int g_picker_scroll = 0;
int g_place_img = -1;

static SDL_Texture **g_pick_label = NULL;
static int g_pick_label_capacity = 0;
static int g_pick_labels_built = 0;

int bdd_object_picker_width(void)
{
    return PICKER_W;
}

int bdd_object_picker_item_height(void)
{
    return PICKER_ITEM_H;
}

int bdd_object_picker_is_open(void)
{
    return g_picker_open;
}

void bdd_object_picker_toggle(void)
{
    g_picker_open ^= 1;
}

void bdd_object_picker_close(void)
{
    g_picker_open = 0;
}

void bdd_object_picker_cancel_place(void)
{
    g_place_img = -1;
}

void bdd_object_picker_free_labels(void)
{
    for (int i = 0; i < g_pick_label_capacity; i++) {
        if (g_pick_label[i]) {
            SDL_DestroyTexture(g_pick_label[i]);
            g_pick_label[i] = NULL;
        }
    }
    free(g_pick_label);
    g_pick_label = NULL;
    g_pick_label_capacity = 0;
    g_pick_labels_built = 0;
}

static int picker_ensure_label_storage(void)
{
    int image_cap = editor_project_image_capacity();
    if (image_cap <= 0) return 0;
    if (g_pick_label && g_pick_label_capacity == image_cap) return 1;
    bdd_object_picker_free_labels();
    g_pick_label = (SDL_Texture **)calloc((size_t)image_cap, sizeof g_pick_label[0]);
    if (!g_pick_label) return 0;
    g_pick_label_capacity = image_cap;
    return 1;
}

static void picker_build_labels(SDL_Renderer *rend)
{
    if (!picker_ensure_label_storage()) return;
    for (int i = 0; i < g_ni && i < g_pick_label_capacity; i++) {
        if (g_pick_label[i]) continue;
        Img *im = &g_img[i];
        char l1[24], l2[20];
        snprintf(l1, sizeof l1, "ii=0x%02X", im->idx);
        snprintf(l2, sizeof l2, "%d x %d", im->w, im->h);
        int sw = (int)(strlen(l1) > strlen(l2) ? strlen(l1) : strlen(l2)) * 8;
        SDL_Surface *s = SDL_CreateRGBSurface(0, sw, 19, 32,
            0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
        if (!s) continue;
        SDL_FillRect(s, NULL, 0);
        bdd_sdl_font_draw_str(s, 0, 0, l1, SDL_MapRGBA(s->format, 210, 220, 255, 255));
        bdd_sdl_font_draw_str(s, 0, 11, l2, SDL_MapRGBA(s->format, 150, 200, 150, 255));
        g_pick_label[i] = SDL_CreateTextureFromSurface(rend, s);
        SDL_FreeSurface(s);
        if (g_pick_label[i])
            SDL_SetTextureBlendMode(g_pick_label[i], SDL_BLENDMODE_BLEND);
    }
    g_pick_labels_built = 1;
}

void bdd_object_picker_select_at_y(int y)
{
    int idx = g_picker_scroll + y / PICKER_ITEM_H;
    if (idx < 0 || idx >= g_ni) return;
    g_place_img = idx;
    g_picker_open = 0;
}

void bdd_object_picker_scroll(int wheel_y, int wh)
{
    int visible = wh / PICKER_ITEM_H;
    int max_scroll = g_ni - visible;
    if (max_scroll < 0) max_scroll = 0;

    if (wheel_y < 0 && g_picker_scroll < max_scroll) g_picker_scroll++;
    if (wheel_y > 0 && g_picker_scroll > 0) g_picker_scroll--;
}

void bdd_object_picker_draw(SDL_Renderer *rend, int ww, int wh, int mx, int my)
{
    if (!g_pick_labels_built) picker_build_labels(rend);

    int px = ww - PICKER_W;
    int pad = 6;

    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = { px, 0, PICKER_W, wh };
    SDL_SetRenderDrawColor(rend, 14, 14, 26, 242);
    SDL_RenderFillRect(rend, &bg);
    SDL_SetRenderDrawColor(rend, 80, 80, 150, 255);
    SDL_RenderDrawLine(rend, px, 0, px, wh);

    for (int i = g_picker_scroll; i < g_ni; i++) {
        int iy = (i - g_picker_scroll) * PICKER_ITEM_H;
        if (iy >= wh) break;

        Img *im = &g_img[i];

        if (mx >= px && my >= iy && my < iy + PICKER_ITEM_H) {
            SDL_Rect hi = { px + 1, iy, PICKER_W - 1, PICKER_ITEM_H - 1 };
            SDL_SetRenderDrawColor(rend, 55, 55, 110, 200);
            SDL_RenderFillRect(rend, &hi);
        }

        if (g_textures && g_textures[i]) {
            float sc = (float)PICKER_THUMB / (float)(im->w > im->h ? im->w : im->h);
            if (sc > 1.0f) sc = 1.0f;
            int tw = (int)(im->w * sc), th = (int)(im->h * sc);
            SDL_Rect dst = {
                px + pad + (PICKER_THUMB - tw) / 2,
                iy + (PICKER_ITEM_H - th) / 2,
                tw, th
            };
            SDL_RenderCopy(rend, g_textures[i], NULL, &dst);
        }

        if (g_pick_label && i < g_pick_label_capacity && g_pick_label[i]) {
            int lx = px + pad + PICKER_THUMB + 6;
            SDL_Rect ldst = { lx, iy + (PICKER_ITEM_H - 19) / 2,
                              PICKER_W - (lx - px) - 4, 19 };
            SDL_RenderCopy(rend, g_pick_label[i], NULL, &ldst);
        }

        SDL_SetRenderDrawColor(rend, 35, 35, 55, 255);
        SDL_RenderDrawLine(rend, px, iy + PICKER_ITEM_H - 1,
                           px + PICKER_W, iy + PICKER_ITEM_H - 1);
    }

    SDL_SetRenderDrawColor(rend, 140, 140, 210, 255);
    if (g_picker_scroll > 0) {
        int ax = px + PICKER_W - 10;
        SDL_RenderDrawLine(rend, ax - 5, 14, ax, 6);
        SDL_RenderDrawLine(rend, ax, 6, ax + 5, 14);
    }
    if (g_picker_scroll + wh / PICKER_ITEM_H < g_ni - 1) {
        int ax = px + PICKER_W - 10;
        SDL_RenderDrawLine(rend, ax - 5, wh - 14, ax, wh - 6);
        SDL_RenderDrawLine(rend, ax, wh - 6, ax + 5, wh - 14);
    }
}

void bdd_object_picker_draw_placement_ghost(SDL_Renderer *rend, int x, int y, int zoom)
{
    if (g_place_img < 0 || g_place_img >= g_ni) return;
    if (!g_textures || !g_textures[g_place_img]) return;

    Img *im = &g_img[g_place_img];
    SDL_Rect dst = { x, y, im->w * zoom, im->h * zoom };
    SDL_SetTextureAlphaMod(g_textures[g_place_img], 160);
    SDL_RenderCopy(rend, g_textures[g_place_img], NULL, &dst);
    SDL_SetTextureAlphaMod(g_textures[g_place_img], 255);
}
