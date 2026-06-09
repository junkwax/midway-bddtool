#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#include "bdd_format.h"

#include <stddef.h>

struct ImageModuleInfo {
    int primary_module;
    int bucket;
    int use_count;
    int group_count;
    bool mixed;
    bool outside;
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
void assign_selected_layer(int wx_layer);

#endif
