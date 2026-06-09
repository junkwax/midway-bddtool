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

static SDL_Texture *image_to_texture(SDL_Renderer *rend, const Img *im)
{
    if (!rend || !im || !im->pix || im->w <= 0 || im->h <= 0)
        return nullptr;

    SDL_Surface *surf = SDL_CreateRGBSurface(
        0, im->w, im->h, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return nullptr;

    Uint32 *dst = (Uint32 *)surf->pixels;
    const Uint8 *src = im->pix;
    const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                        ? g_pals[im->pal_idx] : nullptr;

    for (int i = 0; i < im->w * im->h; i++) {
        Uint8 v = src[i];
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

    destroy_texture_array(g_textures, g_ni_tex);
    free(g_textures);
    g_textures = newtex;
    g_ni_tex = count;
    return 1;
}

void bdd_texture_cache_destroy(void)
{
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
