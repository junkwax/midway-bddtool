#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#include "Core/bdd_format.h"

#include <stddef.h>

struct ImageModuleInfo {
    int primary_module;
    int bucket;
    int use_count;
    int group_count;
    bool mixed;
    bool outside;
};

struct ImageBppCapResult {
    int high_images;
    int changed_images;
    int changed_palettes;
    int lossy_palettes;
    int skipped_mixed_images;
    int skipped_palettes;
    int remapped_pixels;
    size_t before_bytes;
    size_t after_bytes;
};

Uint32 palette_argb_at(int pal_idx, int color_idx);
int block_match_candidate_count(int ref_obj, bool used_only);
int block_match_image_to_object_style(int img_i, int ref_obj, bool used_only,
                                      bool all_uses, float shade_weight,
                                      int *out_candidates);
int edge_candidate_index(const Img *im, int *out_count, int *out_total);
int replace_image_index_with_zero(Img *im, int idx);
int clear_image_edge_matte(int img_i, bool require_black, bool save_undo);
int image_nonzero_bounds(const Img *im, int *x1, int *y1, int *x2, int *y2);
void image_palette_usage_stats(const Img *im, int *used_count, int *max_idx);
int image_object_ref_count(int image_idx);
ImageModuleInfo image_module_info(const Img *im);
void image_module_group_label(int bucket, char *out, size_t out_sz);
void image_module_badge_label(const ImageModuleInfo *info, char *out, size_t out_sz);
int trim_image_transparent_border(int img_i, bool save_undo);
int compact_palettes_for_image_range(int start_img, int end_img, bool save_undo);
int optimize_image_range_for_space(int start_img, int end_img, bool save_undo,
                                   int *trimmed_images, int *trimmed_pixels,
                                   int *compacted_palettes);
int compress_active_image_palette(int img_i, int target, bool save_undo);
int cap_images_to_max_bpp(int max_bpp, bool allow_lossy, bool save_undo,
                          ImageBppCapResult *result);
void assign_selected_layer(int wx_layer);

#endif
