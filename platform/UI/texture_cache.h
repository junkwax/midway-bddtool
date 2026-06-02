#ifndef TEXTURE_CACHE_H
#define TEXTURE_CACHE_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_ni_tex;
extern SDL_Texture **g_textures;
extern SDL_Renderer *g_rend;
extern int g_need_rebuild;

void bdd_texture_cache_set_renderer(SDL_Renderer *rend);
int bdd_texture_cache_rebuild_all(void);
void bdd_texture_cache_destroy(void);
Uint64 bdd_texture_cache_signature(void);
int bdd_texture_cache_refresh_if_needed(Uint64 *signature);

#ifdef __cplusplus
}
#endif

#endif
