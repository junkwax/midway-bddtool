#ifndef IMPORT_EXPORT_H
#define IMPORT_EXPORT_H

#include "Core/bdd_format.h"

void import_png(const char *path, bool save_undo = true);
int import_img_file(const char *path, bool save_undo = true);
int import_img_file_filtered(const char *path, bool save_undo,
                             const unsigned char *selected, int selected_len);
int batch_import_img(const char *dir);
int import_lod_file(const char *path, bool save_undo = true);
int batch_import_png(const char *dir);

void export_composite_png(void);
/* Renders the stage composite straight to dest_path (no file dialog); shared by
   Export Package and Share Stage. */
bool export_composite_to(const char *dest_path);
bool copy_file_overwrite(const char *src, const char *dst);
void export_image_tga(Img *im);
void export_image_png(Img *im);
void export_sprite_sheet_png(void);
void stage_export_bundle(void);

#endif
