#include "UI/assets/texture_cache.h"

#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include <cstdlib>

extern "C" {
int g_ni_tex = 0;
SDL_Texture **g_textures = nullptr;
SDL_Renderer *g_rend = nullptr;
int g_need_rebuild = 0;
}

static void destroy_texture_array(SDL_Texture **textures, int count)
{
    if (!textures) return;
    for (int i = 0; i < count; i++) {
        if (textures[i]) SDL_DestroyTexture(textures[i]);
        textures[i] = nullptr;
    }
}

static SDL_Texture *image_to_texture_pal(SDL_Renderer *rend, const Img *im, int pal_idx)
{
    if (!rend || !im || !im->pix || im->w <= 0 || im->h <= 0)
        return nullptr;

    SDL_Surface *surf = SDL_CreateRGBSurface(
        0, im->w, im->h, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return nullptr;

    Uint32 *dst = (Uint32 *)surf->pixels;
    const Uint8 *src = im->pix;
    const Uint32 *pal = (pal_idx >= 0 && pal_idx < g_n_pals)
                        ? g_pals[pal_idx] : nullptr;

    /* DMA packs pixels at the image's bpp, so stray high pixel values wrap
       into the palette instead of indexing past it (KFPITPL has single
       outlier pixels like 0x99 in 64-color images that the game shows as
       pixel & 0x3F). Approximate the bpp with the palette's bit width. */
    Uint8 mask = 0xFF;
    if (pal) {
        int count = g_pal_count[pal_idx];
        if (count < 1) count = 1;
        if (count > 256) count = 256;
        int bits = 1;
        while ((1 << bits) < count) bits++;
        mask = (Uint8)((1 << bits) - 1);
    }

    for (int i = 0; i < im->w * im->h; i++) {
        Uint8 v = pal ? (Uint8)(src[i] & mask) : src[i];
        if (v == 0) {
            dst[i] = 0u;
            continue;
        }
        dst[i] = pal ? pal[v] : (0xFF000000u | (Uint32)(v * 0x010101u));
    }

    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    return tex;
}

static SDL_Texture *image_to_texture(SDL_Renderer *rend, const Img *im)
{
    return image_to_texture_pal(rend, im, im ? im->pal_idx : -1);
}

/* Placement-palette variants: one extra texture per (image, palette) pair
   actually used by a BLKS row or BDB object whose palette differs from the
   image's default. Rebuilt lazily; flushed with the main cache. */
typedef struct {
    int img;
    int pal;
    SDL_Texture *tex;
} TexPalVariant;

static TexPalVariant *g_tex_variants = nullptr;
static int g_tex_variant_count = 0;
static int g_tex_variant_cap = 0;

static void texture_variants_clear(void)
{
    for (int i = 0; i < g_tex_variant_count; i++) {
        if (g_tex_variants[i].tex) SDL_DestroyTexture(g_tex_variants[i].tex);
    }
    free(g_tex_variants);
    g_tex_variants = nullptr;
    g_tex_variant_count = 0;
    g_tex_variant_cap = 0;
}

SDL_Texture *bdd_texture_for_placement(int img_slot, int pal_idx)
{
    if (!g_textures || img_slot < 0 || img_slot >= g_ni_tex)
        return nullptr;
    if (img_slot >= g_ni || pal_idx < 0 || pal_idx >= g_n_pals ||
        pal_idx == g_img[img_slot].pal_idx)
        return g_textures[img_slot];

    for (int i = 0; i < g_tex_variant_count; i++) {
        if (g_tex_variants[i].img == img_slot && g_tex_variants[i].pal == pal_idx)
            return g_tex_variants[i].tex ? g_tex_variants[i].tex
                                         : g_textures[img_slot];
    }

    if (g_tex_variant_count == g_tex_variant_cap) {
        int cap = g_tex_variant_cap > 0 ? g_tex_variant_cap * 2 : 32;
        TexPalVariant *grown = (TexPalVariant *)realloc(
            g_tex_variants, (size_t)cap * sizeof(TexPalVariant));
        if (!grown) return g_textures[img_slot];
        g_tex_variants = grown;
        g_tex_variant_cap = cap;
    }
    TexPalVariant *slot = &g_tex_variants[g_tex_variant_count++];
    slot->img = img_slot;
    slot->pal = pal_idx;
    slot->tex = image_to_texture_pal(g_rend, &g_img[img_slot], pal_idx);
    return slot->tex ? slot->tex : g_textures[img_slot];
}

static Uint64 texture_sig_mix_u32(Uint64 h, Uint32 v)
{
    h ^= (Uint64)v;
    h *= 1099511628211ull;
    return h;
}

void bdd_texture_cache_set_renderer(SDL_Renderer *rend)
{
    g_rend = rend;
}

int bdd_texture_cache_rebuild_all(void)
{
    int count = g_ni;
    if (count < 0) count = 0;
    SDL_Texture **newtex = (SDL_Texture **)calloc(
        (size_t)(count > 0 ? count : 1), sizeof(SDL_Texture *));
    if (!newtex) return 0;

    for (int i = 0; i < count; i++)
        newtex[i] = image_to_texture(g_rend, &g_img[i]);

    texture_variants_clear();
    destroy_texture_array(g_textures, g_ni_tex);
    free(g_textures);
    g_textures = newtex;
    g_ni_tex = count;
    return 1;
}

void bdd_texture_cache_destroy(void)
{
    texture_variants_clear();
    destroy_texture_array(g_textures, g_ni_tex);
    free(g_textures);
    g_textures = nullptr;
    g_ni_tex = 0;
}

Uint64 bdd_texture_cache_signature(void)
{
    Uint64 h = 1469598103934665603ull;
    h = texture_sig_mix_u32(h, (Uint32)g_ni);
    h = texture_sig_mix_u32(h, (Uint32)g_n_pals);
    for (int i = 0; i < g_ni; i++) {
        h = texture_sig_mix_u32(h, (Uint32)g_img[i].idx);
        h = texture_sig_mix_u32(h, (Uint32)g_img[i].pal_idx);
    }
    for (int p = 0; p < g_n_pals && p < editor_project_palette_capacity(); p++) {
        int count = g_pal_count[p];
        if (count < 0) count = 0;
        if (count > 256) count = 256;
        h = texture_sig_mix_u32(h, (Uint32)p);
        h = texture_sig_mix_u32(h, (Uint32)count);
        for (int i = 0; i < count; i++)
            h = texture_sig_mix_u32(h, g_pals[p][i]);
    }
    return h;
}

int bdd_texture_cache_refresh_if_needed(Uint64 *signature)
{
    Uint64 current = bdd_texture_cache_signature();
    if (signature && current != *signature)
        g_need_rebuild = 1;

    if (!g_need_rebuild)
        return 1;

    if (!bdd_texture_cache_rebuild_all())
        return 0;

    if (signature)
        *signature = current;
    g_need_rebuild = 0;
    return 1;
}
