#ifndef MK2_ANALYSIS_H
#define MK2_ANALYSIS_H

#include "Core/bdd_core.h"
#include "Core/bdd_format.h"

#include <stddef.h>

#define MK2_LOAD2_MAX_STAGE_PALETTES BDD_CORE_MK2_LOAD2_MAX_STAGE_PALETTES
#define MK2_LOAD2_MAX_MODULES BDD_CORE_MK2_LOAD2_MAX_MODULES
#define MK2_LOAD2_MAX_BLOCKS BDD_CORE_MK2_LOAD2_MAX_BLOCKS
#define MK2_LOAD2_MAX_IMAGE_HEADERS BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS
#define MK2_LOAD2_MAX_DATA_BYTES BDD_CORE_MK2_LOAD2_MAX_DATA_BYTES
#define MK2_RUNTIME_PALETTE_SLOTS BDD_CORE_MK2_RUNTIME_PALETTE_SLOTS
#define MK2_DISPLAY_OBJECT_CAP BDD_CORE_MK2_DISPLAY_OBJECT_CAP
#define MK2_DISPLAY_OBJECT_WARN BDD_CORE_MK2_DISPLAY_OBJECT_WARN
#define MK2_DISPLAY_OBJECT_RUNTIME_RESERVE BDD_CORE_MK2_DISPLAY_OBJECT_RUNTIME_RESERVE
#define MK2_BG_DYNAMIC_PALETTE_SLOTS BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS
#define MK2_RUNTIME_WIDEST_BLOCK BDD_CORE_MK2_RUNTIME_WIDEST_BLOCK
#define MK2_STRIP_SHORT_SIDE BDD_CORE_MK2_STRIP_SHORT_SIDE
#define MK2_STRIP_RUN_MIN BDD_CORE_MK2_STRIP_RUN_MIN
#define MK2_STRIP_EXCESS_WARN BDD_CORE_MK2_STRIP_EXCESS_WARN
#define MK2_STRIP_PEAK_SHARE_PCT BDD_CORE_MK2_STRIP_PEAK_SHARE_PCT
#define MK2_BG_WIDTH_ALIGN BDD_CORE_MK2_BG_WIDTH_ALIGN

struct Mk2Diag {
    int missing_images;
    int bad_palettes;
    int palette_high_nibble;
    int runtime_palette_count;
    int runtime_palette_pressure;
    int runtime_palette16_pressure;
    int max_module_palettes;
    char max_module_palette_name[64];
    int unassigned_objects;
    int module_bound_issues;
    int old_style_bounds;
    int load2_oversize_images;
    int max_load2_block_bytes;
    int max_load2_block_bpp;
    int load2_palette_overflow;
    int load2_module_overflow;
    int load2_image_header_overflow;
    int load2_block_table_overflow;
    int load2_narrow_padded_images;
    int load2_zero_compress_disabled;
    int max_visible_objects;
    int max_visible_objects_x;
    int display_object_overflow;
    int display_object_pressure;
    int high_color_images;
    int order_issues;
    char order_issue_module[64];

    /* Blocks wider than BAKGND.ASM's widest_block scan window. LOAD2 packs
       them, bsrch1stxb never finds them, so they never draw. */
    int runtime_wide_blocks;
    int max_block_width;
    char widest_block_label[64];

    /* Module rectangles that overlap, and the blocks that sit inside more than
       one of them. LOAD2 gives each block to the FIRST module in BDB order
       that fully contains it and never revisits that choice, so the later
       module loses them -- along with its parallax plane, since the module is
       what binds a block to a plane. */
    int module_overlap_pairs;
    int module_overlap_stolen;
    char module_overlap_detail[96];

    /* Strip-chop pressure: sprites sliced into stacks of thin blocks. */
    int strip_chop_runs;          /* chop runs found */
    int strip_chop_blocks;        /* blocks those runs consume */
    int strip_chop_excess;        /* display objects re-merging would return */
    int strip_chop_peak;          /* run blocks live at the worst camera X */
    int strip_chop_longest;       /* blocks in the deepest single run */
    size_t strip_chop_rom_saved;  /* packed bytes the chopping actually buys */
    int strip_chop_pressure;

    /* BGNDTBL.ASM records vs the BDB they were generated from. LOAD2 bakes each
       module's shrunk size and block count into <module>BMOD, and the runtime
       believes that record, not the BDB -- so editing a stage without a full
       rebuild leaves the game drawing the old module. */
    int bgndtbl_modules_checked;
    int bgndtbl_stale_modules;
    char bgndtbl_stale_detail[192];

    /* Placed sprites whose width is odd. LOAD2's background emitter scans row
       headers on an even stride but copies payload from the tight odd rows, so
       the art shears progressively down the sprite. No shipped stage has one. */
    int odd_width_images;
    char odd_width_label[64];
};

struct Mk2Budget {
    size_t raw_image_bytes;
    size_t estimated_payload;
    int max_image_pixels;
    int oversized_images;
    int high_color_images;
    int palette_entries;
    int unused_images;
};

struct PanCoverageSummary {
    float full;
    float top;
    float floor;
    int worst_x;
    int points;
};

struct DisplayObjectSummary {
    int max_count;
    int worst_x;
    int points;
};

int mk2_diag_hard_issues(const Mk2Diag *d);
int mk2_diag_cautions(const Mk2Diag *d);
void mk2_collect_diag(Mk2Diag *d);
int mk2_create_default_module(void);
int mk2_first_unassigned_object(void);
int mk2_select_unassigned_objects(void);
int mk2_isolate_unassigned_objects(void);
int mk2_include_unassigned_objects_in_modules(void);
int mk2_include_object_in_nearest_module(int obj_idx);
int mk2_fit_module_bounds_to_objects(void);
int mk2_sort_objects_x_major(void);
int image_use_count(int ii);
int first_unused_image_index(void);
int mk2_max_object_order(void);
int mk2_find_first_fit_for_image(const Img *im, int *out_x, int *out_y);
int mk2_find_center_fit_for_image(const Img *im, int *out_x, int *out_y);
void mk2_delete_object_preserve_order(int idx);
int mk2_delete_unassigned_objects(void);
void mk2_toast_outside_delete_result(int removed);
int mk2_add_object_for_image(int img_i, int x, int y, int layer, int pal, int hfl, int vfl,
                             bool save_undo = true);
int mk2_set_image_as_static_background(int img_i);
int mk2_enable_unused_asset(int img_i);
int mk2_disable_selected_assets_keep_images(void);
int object_module_index(const Obj *o, const Img *im);
int object_pixel_at_world(const Obj *o, const Img *im, int wx, int wy);
int mk2_select_objects_by_image(int ii);
int mk2_bpp_for_image(const Img *im);
int mk2_bpp_for_max_index(int max_px);
size_t mk2_estimate_image_bytes_for_bpp(const Img *im, int bpp);
size_t mk2_estimate_image_bytes(const Img *im);
Mk2Budget mk2_collect_budget(void);
float mk2_screen_band_coverage(int camera_x, int y0, int y1);
DisplayObjectSummary mk2_compute_display_object_summary(void);
int mk2_select_visible_objects_at_camera(int camera_x);
int mk2_select_visible_objects_at_camera_layer(int camera_x, int layer);
int mk2_visible_object_counts_by_layer_at_camera(int camera_x,
                                                 int *layers,
                                                 int *counts,
                                                 int max_layers);
unsigned int image_pixel_hash(const Img *im, bool hflip);
bool image_pixels_match(const Img *a, const Img *b, bool mirror);
bool mk2_has_drawable_stage(void);
PanCoverageSummary mk2_compute_pan_summary(void);
size_t mk2_estimate_duplicate_savings(void);
void mk2_readiness_report(char *out, size_t outsz);
/* Runtime-silent problems worth a word at save time: blocks the engine
   will skip, blocks the wrong module will claim, and X-order inversions
   that break the per-module binary search. Returns the issue count and
   fills a one-line summary; 0 means the save is clean on all three. */
int mk2_runtime_integrity_summary(char *out, size_t outsz);
bool find_first_duplicate_pair(int *keep_i, int *replace_i, bool *mirror);
int apply_safe_dedup(int keep_i, int replace_i, bool mirror);

#endif
