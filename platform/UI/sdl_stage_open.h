#ifndef SDL_STAGE_OPEN_H
#define SDL_STAGE_OPEN_H

#include <SDL_stdinc.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int bdd_sdl_open_stage_file(const char *path,
                            char *bdb_path, size_t bdb_sz,
                            char *bdd_path, size_t bdd_sz,
                            int fallback_w, int fallback_h,
                            int *wx_min, int *wx_max,
                            int *wy_min, int *wy_max,
                            int *view_x, int *view_y,
                            Uint64 *texture_sig);

#ifdef __cplusplus
}
#endif

#endif
