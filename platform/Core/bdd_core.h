#ifndef BDD_CORE_H
#define BDD_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <vector>
#include <string>



enum {
    BDD_CORE_MAX_IMAGES = 2048,
    BDD_CORE_MAX_OBJECTS = 8192,
    BDD_CORE_MAX_MODULES = 256,
    BDD_CORE_MAX_PALS = 256,
    BDD_CORE_MK2_LOAD2_MAX_STAGE_PALETTES = 256,
    BDD_CORE_MK2_LOAD2_MAX_MODULES = 100,
    BDD_CORE_MK2_LOAD2_MAX_BLOCKS = 2500,
    BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS = 400,
    BDD_CORE_MK2_LOAD2_MAX_DATA_BYTES = 65500,
    BDD_CORE_MK2_RUNTIME_PALETTE_SLOTS = 16,
    BDD_CORE_MK2_DISPLAY_OBJECT_CAP = 358,
    BDD_CORE_MK2_DISPLAY_OBJECT_WARN = 300,
    BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS = 35
};

struct BddCoreModule {
    char line[256];
    char name[64];
    int x1, x2, y1, y2;
    int parsed;
};

struct BddCoreObject {
    int wx;
    int depth;
    int sy;
    int ii;
    int fl;
    int order;
};

struct BddCoreBdb {
    std::string path;
    std::string header;
    char name[64] = {0};
    int world_w = 0;
    int world_h = 0;
    int max_depth = 255;
    int module_count_field = 0;
    int palette_count_field = 0;
    int object_count_field = -1;
    int header_field_count = 0;
    std::vector<BddCoreModule> modules;
    std::vector<BddCoreObject> objects;
    std::string error;

    void init() {
        path.clear();
        header.clear();
        memset(name, 0, sizeof(name));
        world_w = world_h = 0;
        max_depth = 255;
        module_count_field = palette_count_field = 0;
        object_count_field = -1;
        header_field_count = 0;
        modules.clear();
        objects.clear();
        error.clear();
    }
};

struct BddCoreImage {
    int idx;
    int w, h;
    int flags;
    std::vector<uint8_t> pix;
};

struct BddCorePalette {
    char name[64];
    int count;
    uint32_t argb[256];
    uint16_t rgb555[256];
};

struct BddCoreBdd {
    std::string path;
    std::vector<BddCoreImage> images;
    std::vector<BddCorePalette> palettes;
    std::string error;

    void init() {
        path.clear();
        images.clear();
        palettes.clear();
        error.clear();
    }
};

struct BddCoreStage {
    BddCoreBdb bdb;
    BddCoreBdd bdd;
    int has_bdb = 0;
    int has_bdd = 0;
    std::string error;

    void init() {
        bdb.init();
        bdd.init();
        has_bdb = 0;
        has_bdd = 0;
        error.clear();
    }
};

struct BddCoreSaveResult {
    std::string error;
    int ferror_errno = 0;
    int fclose_errno = 0;
};

int bdd_core_load_bdb(const char *path, BddCoreBdb *out);
int bdd_core_parse_module_line(const char *line, BddCoreModule *out);
int bdd_core_load_bdd(const char *path, BddCoreBdd *out);
int bdd_core_stage_load_bdb(BddCoreStage *stage, const char *path);
int bdd_core_stage_load_bdd(BddCoreStage *stage, const char *path);
int bdd_core_load_stage(const char *bdb_path,
                        const char *bdd_path,
                        int require_bdd,
                        BddCoreStage *out);
BddCoreImage *bdd_core_stage_find_image(BddCoreStage *stage, int image_idx);
BddCorePalette *bdd_core_stage_find_palette(BddCoreStage *stage, int palette_idx);
uint32_t bdd_core_rgb555_to_argb(uint16_t c);
uint16_t bdd_core_argb_to_rgb555(uint32_t c);
int bdd_core_rect_fits_module(int x, int y, int w, int h,
                              const BddCoreModule *module);
int bdd_core_object_fits_module(const BddCoreObject *object,
                                const BddCoreModule *module,
                                int image_w,
                                int image_h);
int bdd_core_object_module_slop(const BddCoreObject *object,
                                const BddCoreModule *module,
                                int image_w,
                                int image_h);
int bdd_core_find_fitting_module(const BddCoreModule *modules,
                                 int module_count,
                                 int x,
                                 int y,
                                 int w,
                                 int h,
                                 BddCoreModule *out_module);
int bdd_core_find_fitting_module_in_lines(const char module_lines[][256],
                                          int module_count,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          BddCoreModule *out_module);
int bdd_core_image_max_pixel(const uint8_t *pix, int w, int h);
int bdd_core_load2_bpp_for_max_pixel(int max_px);
size_t bdd_core_load2_estimated_block_bytes(const uint8_t *pix,
                                            int w,
                                            int h,
                                            int *out_bpp);
float bdd_core_mk2_scroll_factor(int layer_byte);
int bdd_core_object_visible_at_camera(const BddCoreObject *object,
                                      int image_w,
                                      int image_h,
                                      int origin_x,
                                      int origin_y,
                                      int camera_x,
                                      int camera_y,
                                      int view_w,
                                      int view_h,
                                      int min_layer,
                                      int max_layer);
int bdd_core_first_palette_for_image(const BddCoreObject *objects,
                                     int object_count,
                                     int image_idx,
                                     int palette_count,
                                     int fallback_palette);
int bdd_core_collect_palettes_for_image(const BddCoreObject *objects,
                                        int object_count,
                                        int image_idx,
                                        int palette_count,
                                        int fallback_palette,
                                        int *out_palettes,
                                        int out_palette_cap);
int bdd_core_indexed_to_rgba(const uint8_t *pix,
                             int w,
                             int h,
                             const uint32_t *palette_argb,
                             int palette_count,
                             uint8_t *rgba,
                             size_t rgba_size);
void bdd_core_save_result_init(BddCoreSaveResult *result);
int bdd_core_save_bdb(const char *path,
                      const char *header,
                      const char *const *module_lines,
                      int module_count,
                      const BddCoreObject *objects,
                      int object_count,
                      BddCoreSaveResult *result);
int bdd_core_save_bdd(const char *path,
                      const BddCoreImage *images,
                      int image_count,
                      const BddCorePalette *palettes,
                      int palette_count,
                      BddCoreSaveResult *result);



#endif
