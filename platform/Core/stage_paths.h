#ifndef STAGE_PATHS_H
#define STAGE_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char g_stage_dir[512];

#ifdef __cplusplus
}
#endif

void resolve_stage_file(char *out, size_t outsz, const char *path);
void derive_stage_pair_paths(const char *path,
                             char *bdd, size_t bddsz,
                             char *bdb, size_t bdbsz);
const char *path_basename_ptr(const char *path);

#endif
