#ifndef VIEWER_STAGE_IO_H
#define VIEWER_STAGE_IO_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void bdd_viewer_enter_edit_layout_after_bdb_load(void);
void bdd_viewer_make_ext(const char *src, const char *ext, char *out, size_t outsz);
FILE *bdd_viewer_fopen_try(const char *path, const char *mode, char *resolved, size_t rsz);
int bdd_viewer_load_stage_for_path(const char *arg, char *bdb_path, size_t bdb_sz,
                                   char *bdd_path, size_t bdd_sz);
int bdd_viewer_roundtrip_save_stage_for_path(const char *arg, const char *out_prefix);
int bdd_viewer_undo_move_smoke_for_path(const char *arg);
int bdd_viewer_import_img_smoke_for_path(const char *img_path, const char *out_prefix);
int bdd_viewer_import_img_folder_smoke_for_path(const char *dir, const char *out_prefix);
int bdd_viewer_import_lod_smoke_for_path(const char *lod_path, const char *out_prefix);
int bdd_viewer_import_png_smoke_for_path(const char *png_path, const char *out_prefix);

#ifdef __cplusplus
}
#endif

#endif
