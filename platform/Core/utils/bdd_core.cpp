#include "Core/bdd_core.h"

#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

uint32_t bdd_core_rgb555_to_argb(uint16_t c)
{
    int r = (c >> 10) & 31;
    int g = (c >> 5) & 31;
    int b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint8_t bdd_core_rgb8_to_5(uint8_t v)
{
    int q = ((int)v * 31 + 127) / 255;
    return (uint8_t)(q > 31 ? 31 : q);
}

uint16_t bdd_core_argb_to_rgb555(uint32_t c)
{
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >> 8) & 0xFF);
    uint8_t b = (uint8_t)(c & 0xFF);
    return (uint16_t)((bdd_core_rgb8_to_5(r) << 10) |
                      (bdd_core_rgb8_to_5(g) << 5) |
                       bdd_core_rgb8_to_5(b));
}

static void bdd_core_set_error(std::string &error, const char *fmt, ...) {
    if (!fmt) {
        error.clear();
        return;
    }
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    error = buf;
}

void bdd_core_save_result_init(BddCoreSaveResult *result)
{
    if (!result) return;
    result->error.clear();
    result->ferror_errno = 0;
    result->fclose_errno = 0;
}

static void bdd_core_set_bdb_error(BddCoreBdb *bdb, const char *fmt, const char *path) {
    if (!bdb) return;
    bdd_core_set_error(bdb->error, fmt, path);
}

static void bdd_core_strip_newline(char *s) {
    if (!s) return;
    s[strcspn(s, "\r\n")] = '\0';
}
static void bdd_core_strip_newline(std::string &s)
{
    size_t pos = s.find_first_of("\r\n");
    if (pos != std::string::npos) s.erase(pos);
}

static int bdd_core_parse_object_line(const char *line, int order, BddCoreObject *obj)
{
    char a[16], b[16], c[16], d[16], e[16];
    if (sscanf(line, "%15s %15s %15s %15s %15s", a, b, c, d, e) < 5)
        return 0;
    memset(obj, 0, sizeof *obj);
    obj->wx = (int)strtol(a, nullptr, 16);
    obj->depth = atoi(b);
    obj->sy = atoi(c);
    obj->ii = (int)strtol(d, nullptr, 16);
    obj->fl = atoi(e);
    obj->order = order;
    return 1;
}

int bdd_core_parse_module_line(const char *line, BddCoreModule *out)
{
    if (!line || !out) return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->line, sizeof out->line, "%s", line);
    bdd_core_strip_newline(out->line);
    out->parsed = sscanf(out->line, "%63s %d %d %d %d",
                         out->name, &out->x1, &out->x2, &out->y1, &out->y2) >= 5;
    return out->parsed;
}

int bdd_core_load_bdb(const char *path, BddCoreBdb *out)
{
    if (!out) return 0;
    out->init();
    if (path) out->path = path;

    FILE *f = fopen(path, "r");
    if (!f) {
        bdd_core_set_bdb_error(out, "cannot open BDB: %s", path);
        return 0;
    }

    char ln[256];
    if (!fgets(ln, sizeof ln, f)) {
        fclose(f);
        bdd_core_set_bdb_error(out, "empty BDB: %s", path);
        return 0;
    }

    out->header = ln;
    bdd_core_strip_newline(out->header);

    int modules = 0;
    int objects = -1;
    out->header_field_count = sscanf(
        ln, "%63s %d %d %d %d %d %d",
        out->name,
        &out->world_w,
        &out->world_h,
        &out->max_depth,
        &modules,
        &out->palette_count_field,
        &objects);
    if (out->header_field_count < 1)
        out->name[0] = '\0';
    if (out->header_field_count < 4)
        out->max_depth = 255;
    if (modules < 0) modules = 0;
    if (objects < -1) objects = -1;
    out->module_count_field = modules;
    out->object_count_field = objects;

    for (int m = 0; m < modules; m++) {
        if (!fgets(ln, sizeof ln, f)) break;
        BddCoreModule mod;
        bdd_core_parse_module_line(ln, &mod);
        out->modules.push_back(mod);
    }

    if (objects >= 0) {
        for (int rec = 0; rec < objects; rec++) {
            if (!fgets(ln, sizeof ln, f)) break;
            BddCoreObject obj;
            if (!bdd_core_parse_object_line(ln, rec, &obj)) continue;
            out->objects.push_back(obj);
        }
    } else {
        int rec = 0;
        while (fgets(ln, sizeof ln, f)) {
            BddCoreObject obj;
            if (!bdd_core_parse_object_line(ln, rec, &obj)) continue;
            out->objects.push_back(obj);
            rec++;
        }
    }

    fclose(f);
    return 1;
}

int bdd_core_rect_fits_module(int x, int y, int w, int h,
                              const BddCoreModule *module)
{
    if (!module || !module->parsed) return 0;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    return x >= module->x1 && y >= module->y1 &&
           x2 <= module->x2 && y2 <= module->y2;
}

int bdd_core_object_fits_module(const BddCoreObject *object,
                                const BddCoreModule *module,
                                int image_w,
                                int image_h)
{
    if (!object) return 0;
    return bdd_core_rect_fits_module(object->depth, object->sy,
                                     image_w, image_h, module);
}

int bdd_core_object_module_slop(const BddCoreObject *object,
                                const BddCoreModule *module,
                                int image_w,
                                int image_h)
{
    if (!object || !module || !module->parsed) return 0;
    if (image_w <= 0) image_w = 1;
    if (image_h <= 0) image_h = 1;
    int x2 = object->depth + image_w - 1;
    int y2 = object->sy + image_h - 1;
    int dx1 = module->x1 - object->depth;
    int dx2 = x2 - module->x2;
    int dy1 = module->y1 - object->sy;
    int dy2 = y2 - module->y2;
    int dx = dx1 > dx2 ? dx1 : dx2;
    int dy = dy1 > dy2 ? dy1 : dy2;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    return dx + dy;
}

int bdd_core_find_fitting_module(const BddCoreModule *modules,
                                 int module_count,
                                 int x,
                                 int y,
                                 int w,
                                 int h,
                                 BddCoreModule *out_module)
{
    if (!modules || module_count <= 0) return -1;
    for (int m = 0; m < module_count; m++) {
        if (bdd_core_rect_fits_module(x, y, w, h, &modules[m])) {
            if (out_module)
                *out_module = modules[m];
            return m;
        }
    }
    return -1;
}

int bdd_core_find_fitting_module_in_lines(const char module_lines[][256],
                                          int module_count,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          BddCoreModule *out_module)
{
    if (!module_lines || module_count <= 0) return -1;
    for (int m = 0; m < module_count; m++) {
        BddCoreModule module;
        if (!bdd_core_parse_module_line(module_lines[m], &module))
            continue;
        if (bdd_core_rect_fits_module(x, y, w, h, &module)) {
            if (out_module)
                *out_module = module;
            return m;
        }
    }
    return -1;
}

int bdd_core_image_max_pixel(const uint8_t *pix, int w, int h)
{
    if (!pix || w <= 0 || h <= 0) return 0;
    int max_px = 0;
    size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        if (pix[i] > max_px)
            max_px = pix[i];
    }
    return max_px;
}

int bdd_core_load2_bpp_for_max_pixel(int max_px)
{
    int colors = max_px + 1;
    int bpp = 1;
    while (bpp < 8 && (1 << bpp) < colors)
        bpp++;
    return bpp;
}

size_t bdd_core_load2_estimated_block_bytes(const uint8_t *pix,
                                            int w,
                                            int h,
                                            int *out_bpp)
{
    if (!pix || w <= 0 || h <= 0) {
        if (out_bpp) *out_bpp = 0;
        return 0;
    }
    int bpp = bdd_core_load2_bpp_for_max_pixel(bdd_core_image_max_pixel(pix, w, h));
    size_t bits = (size_t)w * (size_t)h * (size_t)bpp;
    size_t bytes = (bits + 7u) / 8u;
    if (out_bpp) *out_bpp = bpp;
    return bytes > (size_t)INT_MAX ? (size_t)INT_MAX : bytes;
}

float bdd_core_mk2_scroll_factor(int layer_byte)
{
    switch (layer_byte) {
        case 0x32: return 0.20f;
        case 0x3C: return 0.50f;
        case 0x40:
        case 0x41: return 1.00f;
        case 0x43: return 1.20f;
        case 0x46: return 1.50f;
        default:   return (layer_byte < 0x40) ? 0.50f : 1.20f;
    }
}

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
                                      int max_layer)
{
    if (!object || image_w <= 0 || image_h <= 0 || view_w <= 0 || view_h <= 0)
        return 0;
    int layer = (object->wx >> 8) & 0xFF;
    if (layer < min_layer || layer > max_layer)
        return 0;
    float sf = bdd_core_mk2_scroll_factor(layer);
    int sx = origin_x - (int)(camera_x * sf);
    int sy = origin_y - camera_y;
    return sx < view_w && sx + image_w > 0 &&
           sy < view_h && sy + image_h > 0;
}

static int bdd_core_normalize_palette_index(int pal_idx,
                                            int palette_count,
                                            int fallback_palette)
{
    if (palette_count > 0 && (pal_idx < 0 || pal_idx >= palette_count))
        return fallback_palette;
    return pal_idx;
}

int bdd_core_first_palette_for_image(const BddCoreObject *objects,
                                     int object_count,
                                     int image_idx,
                                     int palette_count,
                                     int fallback_palette)
{
    if (!objects || object_count <= 0)
        return fallback_palette;
    for (int i = 0; i < object_count; i++) {
        if (objects[i].ii != image_idx)
            continue;
        return bdd_core_normalize_palette_index(objects[i].fl,
                                                palette_count,
                                                fallback_palette);
    }
    return fallback_palette;
}

int bdd_core_collect_palettes_for_image(const BddCoreObject *objects,
                                        int object_count,
                                        int image_idx,
                                        int palette_count,
                                        int fallback_palette,
                                        int *out_palettes,
                                        int out_palette_cap)
{
    int count = 0;
    int seen_values[BDD_CORE_MAX_PALS + 1];
    int seen_count = 0;
    if (!objects || object_count <= 0) {
        if (out_palettes && out_palette_cap > 0)
            out_palettes[0] = fallback_palette;
        return 1;
    }
    for (int i = 0; i < object_count; i++) {
        if (objects[i].ii != image_idx)
            continue;
        int pal = bdd_core_normalize_palette_index(objects[i].fl,
                                                   palette_count,
                                                   fallback_palette);
        int duplicate = 0;
        for (int j = 0; j < seen_count; j++) {
            if (seen_values[j] == pal) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate)
            continue;
        if (seen_count < (int)(sizeof seen_values / sizeof seen_values[0]))
            seen_values[seen_count++] = pal;
        if (out_palettes && count < out_palette_cap)
            out_palettes[count] = pal;
        count++;
    }
    if (count == 0) {
        if (out_palettes && out_palette_cap > 0)
            out_palettes[0] = fallback_palette;
        return 1;
    }
    return count;
}

int bdd_core_indexed_to_rgba(const uint8_t *pix,
                             int w,
                             int h,
                             const uint32_t *palette_argb,
                             int palette_count,
                             uint8_t *rgba,
                             size_t rgba_size)
{
    if (!pix || !palette_argb || !rgba || w <= 0 || h <= 0)
        return 0;
    size_t pixels = (size_t)w * (size_t)h;
    if (w != 0 && pixels / (size_t)w != (size_t)h)
        return 0;
    if (pixels > SIZE_MAX / 4u || rgba_size < pixels * 4u)
        return 0;
    if (palette_count <= 0)
        palette_count = 256;

    for (size_t i = 0; i < pixels; i++) {
        uint8_t v = pix[i];
        uint32_t c = (v == 0 || (int)v >= palette_count) ? 0u : palette_argb[v];
        size_t off = i * 4u;
        rgba[off + 0] = (uint8_t)((c >> 16) & 0xFF);
        rgba[off + 1] = (uint8_t)((c >> 8) & 0xFF);
        rgba[off + 2] = (uint8_t)(c & 0xFF);
        rgba[off + 3] = (v == 0) ? 0 : 0xFF;
    }
    return 1;
}

static void bdd_core_stage_set_error(BddCoreStage *stage, const char *message) {
    if (!stage) return;
    stage->error = message ? message : "";
}

int bdd_core_stage_load_bdb(BddCoreStage *stage, const char *path)
{
    if (!stage) return 0;
    stage->bdb.init();
    stage->has_bdb = 0;
    stage->error.clear();
    if (!bdd_core_load_bdb(path, &stage->bdb)) {
        bdd_core_stage_set_error(stage, stage->bdb.error.c_str());
        return 0;
    }
    stage->has_bdb = 1;
    return 1;
}

int bdd_core_stage_load_bdd(BddCoreStage *stage, const char *path)
{
    if (!stage) return 0;
    stage->bdd.init();
    stage->has_bdd = 0;
    stage->error.clear();
    if (!bdd_core_load_bdd(path, &stage->bdd)) {
        bdd_core_stage_set_error(stage, stage->bdd.error.c_str());
        return 0;
    }
    stage->has_bdd = 1;
    return 1;
}

int bdd_core_load_stage(const char *bdb_path,
                        const char *bdd_path,
                        int require_bdd,
                        BddCoreStage *out)
{
    if (!out) return 0;
    out->init();
    if (!bdd_core_stage_load_bdb(out, bdb_path))
        return 0;
    if (bdd_path && bdd_path[0]) {
        if (!bdd_core_stage_load_bdd(out, bdd_path)) {
            if (require_bdd) {
                
                std::string msg = out->error;
                out->init();
                bdd_core_stage_set_error(out, msg.c_str());
                return 0;
            }
        }
    } else if (require_bdd) {
        out->init();
        bdd_core_stage_set_error(out, "missing BDD path");
        return 0;
    }
    return 1;
}

BddCoreImage *bdd_core_stage_find_image(BddCoreStage *stage, int image_idx)
{
    if (!stage || !stage->has_bdd) return nullptr;
    for (int i = 0; i < (int)stage->bdd.images.size(); i++) {
        if (stage->bdd.images[i].idx == image_idx)
            return &stage->bdd.images[i];
    }
    return nullptr;
}

BddCorePalette *bdd_core_stage_find_palette(BddCoreStage *stage, int palette_idx)
{
    if (!stage || !stage->has_bdd ||
        palette_idx < 0 || palette_idx >= (int)stage->bdd.palettes.size())
        return nullptr;
    return &stage->bdd.palettes[palette_idx];
}

static int bdd_core_read_text_line(FILE *f, char *out, size_t outsz)
{
    if (!out || outsz == 0) return 0;
    out[0] = '\0';
    int c;
    while ((c = fgetc(f)) != EOF && (c == '\r' || c == '\n')) {}
    if (c == EOF) return 0;

    size_t n = 0;
    out[n++] = (char)c;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (c == '\r') continue;
        if (n + 1 < outsz)
            out[n++] = (char)c;
    }
    out[n] = '\0';
    return 1;
}

int bdd_core_load_bdd(const char *path, BddCoreBdd *out)
{
    if (!out) return 0;
    out->init();
    if (path) out->path = path;

    FILE *f = fopen(path, "rb");
    if (!f) {
        bdd_core_set_error(out->error, "cannot open BDD: %s", path);
        return 0;
    }

    char ln[128];
    if (!bdd_core_read_text_line(f, ln, sizeof ln)) {
        fclose(f);
        bdd_core_set_error(out->error, "empty BDD: %s", path);
        return 0;
    }

    int expected_images = atoi(ln);
    if (expected_images < 0) {
        fclose(f);
        bdd_core_set_error(out->error, "invalid BDD image count: %s", path);
        return 0;
    }

    bool ok = true;
    for (int rec = 0; rec < expected_images; rec++) {
        if (!bdd_core_read_text_line(f, ln, sizeof ln)) {
            bdd_core_set_error(out->error, "truncated before image header %d", rec);
            ok = false;
            break;
        }

        char a[16], b[16], c[16], d[16];
        if (sscanf(ln, "%15s %15s %15s %15s", a, b, c, d) < 4) {
            bdd_core_set_error(out->error, "bad image header %d: %.80s", rec, ln);
            ok = false;
            break;
        }

        BddCoreImage im{};
        im.idx = (int)strtol(a, nullptr, 16);
        im.w = atoi(b);
        im.h = atoi(c);
        im.flags = atoi(d);
        if (im.w <= 0 || im.h <= 0 || im.w > 4096 || im.h > 4096) {
            bdd_core_set_error(out->error,
                     "invalid image dimensions %dx%d at idx=0x%X", im.w, im.h, im.idx);
            ok = false;
            break;
        }

        size_t pixel_count = (size_t)im.w * (size_t)im.h;
        try {
            im.pix.resize(pixel_count);
        } catch (const std::bad_alloc &) {
            bdd_core_set_error(out->error, "out of memory while reading image pixels");
            ok = false;
            break;
        }
        if (fread(im.pix.data(), 1, pixel_count, f) != pixel_count) {
            bdd_core_set_error(out->error, "truncated at image idx=0x%X", im.idx);
            ok = false;
            break;
        }
        out->images.push_back(im);
    }

    if (!ok) {
        fclose(f);
        return 0;
    }

    while (bdd_core_read_text_line(f, ln, sizeof ln)) {
        if (!ln[0]) continue;
        BddCorePalette pal{};
        int cnt = 0;
        if (sscanf(ln, "%63s %d", pal.name, &cnt) != 2) continue;
        if (cnt <= 0 || cnt > 256) {
            bdd_core_set_error(out->error, "invalid palette count %d for %.63s", cnt, pal.name);
            ok = false;
            break;
        }
        pal.count = cnt;
        for (int i = 0; i < 256; i++) {
            pal.argb[i] = 0xFF000000u;
            pal.rgb555[i] = 0;
        }
        for (int i = 0; i < cnt; i++) {
            uint8_t lo, hi;
            if (fread(&lo, 1, 1, f) != 1 || fread(&hi, 1, 1, f) != 1) {
                bdd_core_set_error(out->error, "truncated palette %.63s", pal.name);
                ok = false;
                break;
            }
            uint16_t rgb = (uint16_t)(lo | (hi << 8));
            pal.rgb555[i] = rgb;
            pal.argb[i] = (i == 0) ? 0u : bdd_core_rgb555_to_argb(rgb);
        }
        if (!ok) break;
        out->palettes.push_back(pal);
    }

    fclose(f);
    /* Success means parsing completed cleanly, not "found at least one image" --
     * a freshly-created, never-populated project legitimately has zero of both. */
    return ok;
}

static int bdd_core_raw_rgb555_matches_argb(int color_index, uint16_t rgb, uint32_t argb)
{
    uint32_t decoded = (color_index == 0) ? 0u : bdd_core_rgb555_to_argb(rgb);
    return decoded == argb;
}

static void bdd_core_set_save_message(BddCoreSaveResult *result, const char *message)
{
    if (!result) return;
    result->error = message ? message : "";
}

int bdd_core_save_bdb(const char *path,
                      const char *header,
                      const char *const *module_lines,
                      int module_count,
                      const BddCoreObject *objects,
                      int object_count,
                      BddCoreSaveResult *result)
{
    bdd_core_save_result_init(result);
    if (!path || !path[0]) {
        bdd_core_set_save_message(result, "no BDB path");
        return 0;
    }
    if (!header) {
        bdd_core_set_save_message(result, "missing BDB header");
        return 0;
    }
    if (module_count < 0 || object_count < 0 ||
        (module_count > 0 && !module_lines) ||
        (object_count > 0 && !objects)) {
        bdd_core_set_save_message(result, "invalid BDB save inputs");
        return 0;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        if (result) {
            bdd_core_set_save_message(result, (std::string("cannot open BDB for write: ") + strerror(errno)).c_str());
        }
        return 0;
    }

    int ok = 1;
    if (fprintf(f, "%s\n", header) < 0)
        ok = 0;
    for (int m = 0; m < module_count; m++) {
        const char *line = module_lines[m] ? module_lines[m] : "";
        if (fprintf(f, "%s\n", line) < 0)
            ok = 0;
    }

    const BddCoreObject **sorted = nullptr;
    if (object_count > 0) {
        sorted = (const BddCoreObject **)malloc((size_t)object_count * sizeof *sorted);
        if (!sorted) {
            fclose(f);
            bdd_core_set_save_message(result, "out of memory while sorting BDB objects");
            return 0;
        }
        for (int i = 0; i < object_count; i++)
            sorted[i] = &objects[i];
        for (int i = 1; i < object_count; i++) {
            const BddCoreObject *tmp = sorted[i];
            int j = i - 1;
            while (j >= 0 && sorted[j]->order > tmp->order) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = tmp;
        }
    }

    for (int i = 0; i < object_count; i++) {
        const BddCoreObject *o = sorted[i];
        if (fprintf(f, "%X %d %d %X %d\n", o->wx, o->depth, o->sy, o->ii, o->fl) < 0)
            ok = 0;
    }
    free(sorted);

    if (ferror(f)) {
        ok = 0;
        if (result) result->ferror_errno = errno;
    }
    if (fclose(f) != 0) {
        ok = 0;
        if (result) result->fclose_errno = errno;
    }
    if (!ok) {
        bdd_core_set_save_message(result, "failed while writing BDB");
        return 0;
    }
    return 1;
}

int bdd_core_save_bdd(const char *path,
                      const BddCoreImage *images,
                      int image_count,
                      const BddCorePalette *palettes,
                      int palette_count,
                      BddCoreSaveResult *result)
{
    bdd_core_save_result_init(result);
    if (!path || !path[0]) {
        bdd_core_set_save_message(result, "no BDD path");
        return 0;
    }
    if (image_count < 0 || palette_count < 0 ||
        (image_count > 0 && !images) ||
        (palette_count > 0 && !palettes)) {
        bdd_core_set_save_message(result, "invalid BDD save inputs");
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        if (result) {
            char buf[256];
            snprintf(buf, sizeof buf, "cannot open BDD for write: %s", strerror(errno));
            result->error = buf;
        }
        return 0;
    }

    int ok = 1;
    if (fprintf(f, "%d\n", image_count) < 0)
        ok = 0;

    for (int i = 0; i < image_count; i++) {
        const BddCoreImage *im = &images[i];
        if (im->w < 0 || im->h < 0) {
            ok = 0;
            bdd_core_set_save_message(result, "invalid BDD image dimensions");
            break;
        }
        size_t pixel_count = (size_t)im->w * (size_t)im->h;
        if (fprintf(f, "%X %d %d %d\n", im->idx, im->w, im->h, im->flags) < 0)
            ok = 0;
        if (pixel_count > 0) {
            if (im->pix.empty()) {
                ok = 0;
                bdd_core_set_save_message(result, "missing BDD image pixels");
                break;
            }
            if (fwrite(im->pix.data(), 1, pixel_count, f) != pixel_count)
                ok = 0;
        }
    }

    if (ok) {
        for (int i = 0; i < palette_count; i++) {
            const BddCorePalette *pal = &palettes[i];
            if (pal->count < 0 || pal->count > 256) {
                ok = 0;
                bdd_core_set_save_message(result, "invalid BDD palette count");
                break;
            }
            if (fprintf(f, "%s %d\n", pal->name, pal->count) < 0)
                ok = 0;
            for (int j = 0; j < pal->count; j++) {
                uint16_t v = bdd_core_argb_to_rgb555(pal->argb[j]);
                if (bdd_core_raw_rgb555_matches_argb(j, pal->rgb555[j], pal->argb[j]))
                    v = pal->rgb555[j];
                if (fwrite(&v, 2, 1, f) != 1)
                    ok = 0;
            }
        }
    }

    if (ferror(f)) {
        ok = 0;
        if (result) result->ferror_errno = errno;
    }
    if (fclose(f) != 0) {
        ok = 0;
        if (result) result->fclose_errno = errno;
    }
    if (!ok) {
        if (result && result->error.empty())
            bdd_core_set_save_message(result, "failed while writing BDD");
        return 0;
    }
    return 1;
}
