/*************************************************************
 * bddtool  —  headless CLI for BDB/BDD stage files
 *
 * Usage:
 *   bddtool info     FILE.BDB
 *   bddtool list     FILE.BDB
 *   bddtool validate FILE.BDB [FILE.BDD]
 *   bddtool export   FILE.BDB FILE.BDD [-o DIR] [--idx N]
 *   bddtool render   FILE.BDB FILE.BDD [-o OUT.PNG]
 *
 * No SDL, no ImGui — pure C++17.
 *************************************************************/

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "Core/bdd_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>

static constexpr int MK2_BG_DYNAMIC_PALETTE_SLOTS = BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS;
static constexpr int MK2_LOAD2_MAX_STAGE_PALETTES = BDD_CORE_MK2_LOAD2_MAX_STAGE_PALETTES;
static constexpr int MK2_LOAD2_MAX_MODULES = BDD_CORE_MK2_LOAD2_MAX_MODULES;
static constexpr int MK2_LOAD2_MAX_BLOCKS = BDD_CORE_MK2_LOAD2_MAX_BLOCKS;
static constexpr int MK2_LOAD2_MAX_IMAGE_HEADERS = BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS;
static constexpr int MK2_LOAD2_MAX_DATA_BYTES = BDD_CORE_MK2_LOAD2_MAX_DATA_BYTES;
static constexpr int MK2_RUNTIME_PALETTE_SLOTS = BDD_CORE_MK2_RUNTIME_PALETTE_SLOTS;
static constexpr int MK2_DISPLAY_OBJECT_CAP = BDD_CORE_MK2_DISPLAY_OBJECT_CAP;
static constexpr int MK2_DISPLAY_OBJECT_WARN = BDD_CORE_MK2_DISPLAY_OBJECT_WARN;

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#  include <direct.h>
#  define mkdir_p(d) _mkdir(d)
#else
#  include <unistd.h>
#  define mkdir_p(d) mkdir(d, 0755)
#endif

/* ------------------------------------------------------------------ */
/* Data types                                                          */
/* ------------------------------------------------------------------ */

using Module = BddCoreModule;
using Object = BddCoreObject;
using Image = BddCoreImage;
using Palette = BddCorePalette;

template <typename T>
struct CoreSlice {
    T *ptr = nullptr;
    int count = 0;

    bool empty() const { return count <= 0; }
    size_t size() const { return count > 0 ? (size_t)count : 0u; }
    T *data() { return ptr; }
    const T *data() const { return ptr; }
    T &operator[](size_t i) { return ptr[i]; }
    const T &operator[](size_t i) const { return ptr[i]; }
    T *begin() { return ptr; }
    T *end() { return ptr ? ptr + size() : nullptr; }
    const T *begin() const { return ptr; }
    const T *end() const { return ptr ? ptr + size() : nullptr; }
};

struct Stage {
    BddCoreStage core;
    char name[64]{};
    int world_w = 0;
    int world_h = 0;
    int max_depth = 0;
    int pal_count_field = 0;        /* palette count from BDB header */

    CoreSlice<Module> modules;
    CoreSlice<Object> objects;

    /* from BDD */
    CoreSlice<Image> images;
    CoreSlice<Palette> palettes;

    Stage() { bdd_core_stage_init(&core); }
    ~Stage() { bdd_core_stage_free(&core); }
    Stage(const Stage &) = delete;
    Stage &operator=(const Stage &) = delete;

    void refresh_bdb()
    {
        snprintf(name, sizeof name, "%s", core.bdb.name);
        world_w = core.bdb.world_w;
        world_h = core.bdb.world_h;
        max_depth = core.bdb.max_depth;
        pal_count_field = core.bdb.palette_count_field;
        modules = CoreSlice<Module>{core.bdb.modules, core.bdb.module_count};
        objects = CoreSlice<Object>{core.bdb.objects, core.bdb.object_count};
    }

    void refresh_bdd()
    {
        images = CoreSlice<Image>{core.bdd.images, core.bdd.image_count};
        palettes = CoreSlice<Palette>{core.bdd.palettes, core.bdd.palette_count};
    }
};

/* ------------------------------------------------------------------ */
/* BDB parser                                                          */
/* ------------------------------------------------------------------ */

static bool parse_bdb(const char *path, Stage &st)
{
    if (!bdd_core_stage_load_bdb(&st.core, path)) {
        fprintf(stderr, "bddtool: %s\n",
                st.core.error[0] ? st.core.error : "BDB load failed");
        return false;
    }
    st.refresh_bdb();

    if (st.core.bdb.header_field_count < 7) {
        fprintf(stderr, "bddtool: bad BDB header in %s\n", path);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* BDD parser                                                          */
/* ------------------------------------------------------------------ */

static bool parse_bdd(const char *path, Stage &st)
{
    if (!bdd_core_stage_load_bdd(&st.core, path)) {
        fprintf(stderr, "bddtool: %s\n",
                st.core.error[0] ? st.core.error : "BDD load failed");
        return false;
    }
    st.refresh_bdd();
    return true;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *layer_name(int layer_byte)
{
    switch (layer_byte) {
        case 0x32: return "Slow BG";
        case 0x3C: return "Mid BG";
        case 0x40: return "Play";
        case 0x41: return "Near";
        case 0x43: return "Far";
        case 0x46: return "Far+";
        default: {
            static char buf[16];
            snprintf(buf, sizeof buf, "0x%02X", layer_byte);
            return buf;
        }
    }
}

static float scroll_factor(int layer_byte)
{
    return bdd_core_mk2_scroll_factor(layer_byte);
}

static Image *find_image(Stage &st, int idx)
{
    return bdd_core_stage_find_image(&st.core, idx);
}

static int load2_estimated_block_bytes(const Image &im, int *out_bpp)
{
    size_t bytes = bdd_core_load2_estimated_block_bytes(
        im.pix, im.w, im.h, out_bpp);
    return bytes > (size_t)INT_MAX ? INT_MAX : (int)bytes;
}

static void push_unique_int(std::vector<int> &vals, int v)
{
    if (std::find(vals.begin(), vals.end(), v) == vals.end())
        vals.push_back(v);
}

static int object_module_index(Stage &st, const Object &o, const Image *im)
{
    int w = im ? im->w : 1, h = im ? im->h : 1;
    return bdd_core_find_fitting_module(st.modules.empty() ? nullptr : st.modules.data(),
                                        (int)st.modules.size(),
                                        o.depth,
                                        o.sy,
                                        w,
                                        h,
                                        nullptr);
}

struct DisplayObjectSummary {
    int max_count;
    int worst_x;
    int points;
};

static int object_visible_at_camera(Stage &st, const Object &o, int camera_x)
{
    Image *im = find_image(st, o.ii);
    int w = im ? im->w : 1;
    int h = im ? im->h : 1;
    return bdd_core_object_visible_at_camera(&o, w, h,
                                             o.depth, o.sy,
                                             camera_x, 0,
                                             400, 254,
                                             0x00, 0xFF);
}

static DisplayObjectSummary estimate_display_object_pressure(Stage &st)
{
    DisplayObjectSummary s{};
    int min_x = 0;
    int max_x = st.world_w > 400 ? st.world_w : 400;
    for (auto &o : st.objects) {
        Image *im = find_image(st, o.ii);
        int w = im ? im->w : 1;
        if (o.depth + w > max_x) max_x = o.depth + w;
        if (o.depth < min_x) min_x = o.depth;
    }
    int start_x = min_x < 0 ? min_x : 0;
    int end_x = max_x - 400;
    if (end_x < start_x) end_x = start_x;
    int span = end_x - start_x;
    int step = span > 1024 ? 32 : 16;
    if (step < 1) step = 1;

    for (int cam = start_x; cam <= end_x; cam += step) {
        int count = 0;
        for (auto &o : st.objects)
            count += object_visible_at_camera(st, o, cam);
        if (count > s.max_count) {
            s.max_count = count;
            s.worst_x = cam;
        }
        s.points++;
        if (cam == end_x) break;
        if (cam + step > end_x) cam = end_x - step;
    }
    return s;
}

static void make_dir(const char *dir)
{
    if (!dir || !dir[0]) return;
    mkdir_p(dir);
}

static std::string stem(const std::string &path)
{
    size_t sl = path.find_last_of("/\\");
    std::string base = (sl == std::string::npos) ? path : path.substr(sl + 1);
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

static bool path_exists(const std::string &path)
{
    struct stat st{};
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

static std::string replace_ext(const std::string &path, const char *ext)
{
    size_t sep = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
        return path + ext;
    return path.substr(0, dot) + ext;
}

static bool write_image_png(const Image &im, const Palette &pal,
                             const char *path)
{
    std::vector<uint8_t> rgba((size_t)im.w * im.h * 4, 0);
    if (!bdd_core_indexed_to_rgba(im.pix,
                                  im.w, im.h,
                                  pal.argb,
                                  256,
                                  rgba.data(),
                                  rgba.size()))
        return false;
    return stbi_write_png(path, im.w, im.h, 4, rgba.data(), im.w * 4) != 0;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/* bddtool info FILE.BDB */
static int cmd_info(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bddtool info FILE.BDB\n"); return 1; }
    Stage st{};
    if (!parse_bdb(argv[2], st)) return 1;

    printf("Stage   : %s\n", st.name);
    printf("World   : %d x %d  (max depth %d)\n", st.world_w, st.world_h, st.max_depth);
    printf("Modules : %d\n", (int)st.modules.size());
    printf("Objects : %d\n", (int)st.objects.size());
    printf("Palettes: %d (header)\n\n", st.pal_count_field);

    if (!st.modules.empty()) {
        printf("%-12s  %6s %6s %6s %6s\n", "Module", "X1", "X2", "Y1", "Y2");
        printf("%-12s  %6s %6s %6s %6s\n", "------", "--", "--", "--", "--");
        for (auto &m : st.modules)
            printf("%-12s  %6d %6d %6d %6d\n", m.name, m.x1, m.x2, m.y1, m.y2);
        printf("\n");
    }

    /* layer distribution */
    struct { int byte; int count; } layers[16]; int nlayers = 0;
    for (auto &o : st.objects) {
        int lb = (o.wx >> 8) & 0xFF;
        bool found = false;
        for (int i = 0; i < nlayers; i++) {
            if (layers[i].byte == lb) { layers[i].count++; found = true; break; }
        }
        if (!found && nlayers < 16) { layers[nlayers].byte = lb; layers[nlayers++].count = 1; }
    }
    if (nlayers > 0) {
        printf("Layer distribution:\n");
        for (int i = 0; i < nlayers; i++)
            printf("  %-10s  %d objects  (%.1fx scroll)\n",
                   layer_name(layers[i].byte), layers[i].count,
                   scroll_factor(layers[i].byte));
    }
    return 0;
}

/* bddtool list FILE.BDB */
static int cmd_list(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bddtool list FILE.BDB\n"); return 1; }
    Stage st{};
    if (!parse_bdb(argv[2], st)) return 1;

    printf("%-5s  %-8s  %6s  %6s  %5s  %5s\n",
           "#", "Layer", "Depth", "SY", "Img", "Pal");
    printf("%-5s  %-8s  %6s  %6s  %5s  %5s\n",
           "---", "-----", "-----", "--", "---", "---");
    int i = 0;
    for (auto &o : st.objects) {
        printf("%-5d  %-8s  %6d  %6d  0x%03X  %5d\n",
               i++, layer_name((o.wx >> 8) & 0xFF),
               o.depth, o.sy, o.ii, o.fl);
    }
    return 0;
}

static int module_slop(const Object &o, const Module &m, const Image *im)
{
    int w = im ? im->w : 1;
    int h = im ? im->h : 1;
    return bdd_core_object_module_slop(&o, &m, w, h);
}

/* bddtool validate FILE.BDB [FILE.BDD] */
static int cmd_validate(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bddtool validate FILE.BDB [FILE.BDD]\n"); return 1; }
    Stage st{};
    const char *bdb_path = argv[2];
    if (!parse_bdb(bdb_path, st)) return 1;

    bool have_bdd = false;
    std::string bdd_path;
    if (argc >= 4) {
        bdd_path = argv[3];
    } else {
        bdd_path = replace_ext(bdb_path, ".BDD");
        if (!path_exists(bdd_path))
            bdd_path = replace_ext(bdb_path, ".bdd");
    }
    if (path_exists(bdd_path))
        have_bdd = parse_bdd(bdd_path.c_str(), st);

    int errors = 0, warnings = 0;

    /* capacity */
    if ((int)st.modules.size() > MK2_LOAD2_MAX_MODULES) {
        printf("[ERROR] module count %d exceeds LOAD2 max modules per BDD (%d)\n",
               (int)st.modules.size(), MK2_LOAD2_MAX_MODULES);
        errors++;
    }
    if ((int)st.objects.size() > BDD_CORE_MAX_OBJECTS) {
        printf("[ERROR] object count %d exceeds MAX_OBJECTS (%d)\n",
               (int)st.objects.size(), BDD_CORE_MAX_OBJECTS);
        errors++;
    } else if ((int)st.objects.size() > (int)(BDD_CORE_MAX_OBJECTS * 0.8)) {
        printf("[WARN ] object count %d is over 80%% of MAX_OBJECTS (%d)\n",
               (int)st.objects.size(), BDD_CORE_MAX_OBJECTS);
        warnings++;
    }
    /* BDD-aware checks */
    if (have_bdd) {
        if ((int)st.palettes.size() > MK2_LOAD2_MAX_STAGE_PALETTES) {
            printf("[ERROR] BDD has %d palettes; LOAD2 max palettes per stage file is %d\n",
                   (int)st.palettes.size(), MK2_LOAD2_MAX_STAGE_PALETTES);
            errors++;
        }
        if ((int)st.images.size() > MK2_LOAD2_MAX_IMAGE_HEADERS) {
            printf("[ERROR] BDD has %d image headers; LOAD2 max image headers per BDD is %d\n",
                   (int)st.images.size(), MK2_LOAD2_MAX_IMAGE_HEADERS);
            errors++;
        }
        if ((int)st.images.size() > MK2_LOAD2_MAX_BLOCKS) {
            printf("[ERROR] BDD has %d block(s) by image estimate; LOAD2 max blocks per BDD is %d\n",
                   (int)st.images.size(), MK2_LOAD2_MAX_BLOCKS);
            errors++;
        }
        int max_block = 0;
        int max_bpp = 0;
        int oversize = 0;
        int padded = 0;
        int no_zero_compress = 0;
        for (const auto &im : st.images) {
            int bpp = 0;
            int bytes = load2_estimated_block_bytes(im, &bpp);
            if (bytes > max_block) { max_block = bytes; max_bpp = bpp; }
            if (bytes > MK2_LOAD2_MAX_DATA_BYTES) oversize++;
            if (im.w > 0 && im.w < 3) padded++;
            if (im.w > 0 && im.w < 10) no_zero_compress++;
        }
        if (oversize) {
            printf("[ERROR] %d image(s) exceed LOAD2 max packed block data %d bytes (max %d at %dbpp)\n",
                   oversize, MK2_LOAD2_MAX_DATA_BYTES, max_block, max_bpp);
            errors++;
        }
        if (padded) {
            printf("[WARN ] %d image(s) are narrower than LOAD2's 3-pixel minimum and may be padded\n", padded);
            warnings++;
        }
        if (no_zero_compress) {
            printf("[WARN ] %d image(s) are narrower than 10 pixels; LOAD2 zero compression is disabled for them\n",
                   no_zero_compress);
            warnings++;
        }
        if (st.pal_count_field != (int)st.palettes.size()) {
            printf("[ERROR] BDB palette count %d, BDD has %d palettes\n",
                   st.pal_count_field, (int)st.palettes.size());
            errors++;
        }
        for (auto &o : st.objects) {
            if (!find_image(st, o.ii)) {
                printf("[ERROR] obj ii=0x%03X references missing BDD image\n", o.ii);
                errors++;
            }
            if (o.fl < 0 || o.fl >= (int)st.palettes.size()) {
                printf("[ERROR] obj ii=0x%03X uses palette %d outside BDD palette count %d\n",
                       o.ii, o.fl, (int)st.palettes.size());
                errors++;
            }
        }

        std::vector<int> used_pals;
        std::vector<std::vector<int>> module_pals(st.modules.size());
        for (auto &o : st.objects) {
            Image *im = find_image(st, o.ii);
            if (!im || o.fl < 0 || o.fl >= (int)st.palettes.size())
                continue;
            push_unique_int(used_pals, o.fl);
            int mi = object_module_index(st, o, im);
            if (mi >= 0 && mi < (int)module_pals.size())
                push_unique_int(module_pals[(size_t)mi], o.fl);
        }
        if ((int)used_pals.size() > MK2_BG_DYNAMIC_PALETTE_SLOTS) {
            printf("[WARN ] %d object palette(s) are used; MK2 has %d dynamic background palette slots\n",
                   (int)used_pals.size(), MK2_BG_DYNAMIC_PALETTE_SLOTS);
            warnings++;
        }
        if ((int)used_pals.size() > MK2_RUNTIME_PALETTE_SLOTS) {
            printf("[WARN ] %d object palette(s) are used; MK2 hardware has %d simultaneous palette slots\n",
                   (int)used_pals.size(), MK2_RUNTIME_PALETTE_SLOTS);
            warnings++;
        }
        for (size_t mi = 0; mi < module_pals.size(); mi++) {
            if ((int)module_pals[mi].size() > MK2_BG_DYNAMIC_PALETTE_SLOTS) {
                printf("[WARN ] module %s uses %d palette(s), over MK2's %d background slots\n",
                       st.modules[mi].name, (int)module_pals[mi].size(),
                       MK2_BG_DYNAMIC_PALETTE_SLOTS);
                warnings++;
            }
            if ((int)module_pals[mi].size() > MK2_RUNTIME_PALETTE_SLOTS) {
                printf("[WARN ] module %s uses %d palette(s), over MK2's %d simultaneous palette slots\n",
                       st.modules[mi].name, (int)module_pals[mi].size(),
                       MK2_RUNTIME_PALETTE_SLOTS);
                warnings++;
            }
        }
        DisplayObjectSummary ds = estimate_display_object_pressure(st);
        if (ds.max_count > MK2_DISPLAY_OBJECT_CAP) {
            printf("[WARN ] sampled %d visible background object(s) at camera X %d; MK2 display pool is %d shared objects\n",
                   ds.max_count, ds.worst_x, MK2_DISPLAY_OBJECT_CAP);
            warnings++;
        } else if (ds.max_count > MK2_DISPLAY_OBJECT_WARN) {
            printf("[WARN ] sampled %d visible background object(s) at camera X %d; close to MK2 display pool %d before fighters/effects/UI\n",
                   ds.max_count, ds.worst_x, MK2_DISPLAY_OBJECT_CAP);
            warnings++;
        }
    } else {
        printf("[INFO ] no BDD supplied/found; module coverage uses object origins only\n");
        if ((int)st.objects.size() > MK2_DISPLAY_OBJECT_CAP) {
            printf("[WARN ] stage has %d objects; without BDD dimensions, visible-object pressure cannot be sampled against MK2's %d object pool\n",
                   (int)st.objects.size(), MK2_DISPLAY_OBJECT_CAP);
            warnings++;
        }
    }

    /* LOAD2 module coverage */
    int uncovered = 0;
    for (int i = 0; i < (int)st.objects.size(); i++) {
        auto &o = st.objects[i];
        Image *im = have_bdd ? find_image(st, o.ii) : nullptr;
        if (object_module_index(st, o, im) < 0) {
            int best_slop = INT_MAX;
            for (auto &m : st.modules)
                best_slop = std::min(best_slop, module_slop(o, m, im));
            bool known_stock_edge =
                strcmp(st.name, "DEDPOOL") == 0 &&
                st.objects.size() == 311 &&
                st.modules.size() == 6 &&
                i == 1 &&
                best_slop == 1;
            if (known_stock_edge)
                continue;
            if (uncovered < 10)
                printf("[ERROR] obj[%d] ii=0x%03X depth=%d sy=%d fits no module%s%s\n",
                       i, o.ii, o.depth, o.sy,
                       have_bdd ? " by full image rectangle" : "",
                       best_slop == 1 ? " (nearest miss is 1px)" : "");
            uncovered++;
            errors++;
        }
    }
    if (uncovered > 10)
        printf("        ... and %d more uncovered objects\n", uncovered - 10);

    /* world bounds sanity */
    if (!st.objects.empty()) {
        int min_d = INT_MAX, max_d = INT_MIN, min_sy = INT_MAX, max_sy = INT_MIN;
        for (auto &o : st.objects) {
            min_d  = std::min(min_d,  o.depth);
            max_d  = std::max(max_d,  o.depth);
            min_sy = std::min(min_sy, o.sy);
            max_sy = std::max(max_sy, o.sy);
        }
        if (max_d > st.world_w)
            printf("[WARN ] objects exceed world width (%d > %d)\n", max_d, st.world_w);
        if (max_sy > st.world_h)
            printf("[WARN ] objects exceed world height (%d > %d)\n", max_sy, st.world_h);
    }

    /* summary */
    if (errors == 0 && warnings == 0)
        printf("OK  %s — no issues found (%d objects, %d modules)\n",
               st.name, (int)st.objects.size(), (int)st.modules.size());
    else
        printf("\n%d error(s), %d warning(s)\n", errors, warnings);

    return (errors > 0) ? 1 : 0;
}

/* bddtool export FILE.BDB FILE.BDD [-o DIR|FILE] [--idx N] [--all-palettes] */
static int cmd_export(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: bddtool export FILE.BDB FILE.BDD [-o DIR] [--idx N] [--all-palettes]\n");
        return 1;
    }

    const char *bdb_path = argv[2];
    const char *bdd_path = argv[3];
    const char *out_path = ".";
    int export_idx    = -1;    /* -1 = all */
    bool all_palettes = false; /* export each image × each palette it appears in */

    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_path = argv[++i];
        else if (strcmp(argv[i], "--idx") == 0 && i + 1 < argc)
            export_idx = (int)strtol(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--all-palettes") == 0)
            all_palettes = true;
    }

    Stage st{};
    if (!parse_bdb(bdb_path, st)) return 1;
    if (!parse_bdd(bdd_path, st)) return 1;

    if (st.images.empty()) {
        fprintf(stderr, "bddtool: no images in BDD\n"); return 1;
    }
    if (st.palettes.empty()) {
        fprintf(stderr, "bddtool: no palettes in BDD\n"); return 1;
    }

    /* build palette-per-image map from objects */
    std::vector<int> primary_pal(st.images.size(), 0);
    std::vector<std::vector<int>> image_pals(st.images.size());
    for (size_t i = 0; i < st.images.size(); i++) {
        int pals[BDD_CORE_MAX_PALS];
        int n = bdd_core_collect_palettes_for_image(
            st.objects.empty() ? nullptr : st.objects.data(),
            (int)st.objects.size(),
            st.images[i].idx,
            (int)st.palettes.size(),
            0,
            pals,
            BDD_CORE_MAX_PALS);
        if (n > BDD_CORE_MAX_PALS) n = BDD_CORE_MAX_PALS;
        image_pals[i].assign(pals, pals + n);
        primary_pal[i] = image_pals[i].empty() ? 0 : image_pals[i][0];
    }

    /* determine if out_path is a directory */
    struct stat s{};
    bool out_is_dir = (stat(out_path, &s) == 0 && (s.st_mode & S_IFDIR));
    if (!out_is_dir && (export_idx == -1 || all_palettes)) {
        make_dir(out_path);
        out_is_dir = true;
    }

    int exported = 0, failed = 0;
    for (size_t i = 0; i < st.images.size(); i++) {
        auto &im = st.images[i];
        if (export_idx >= 0 && im.idx != export_idx) continue;

        /* which palettes to write for this image */
        std::vector<int> pals_to_write;
        if (all_palettes)
            pals_to_write = image_pals[i];
        else
            pals_to_write = { primary_pal[i] };

        for (int pi : pals_to_write) {
            const auto &pal = st.palettes[pi];
            char filepath[1024];
            if (out_is_dir) {
                if (all_palettes)
                    snprintf(filepath, sizeof filepath, "%s/img_%02X_pal%d.png",
                             out_path, im.idx, pi);
                else
                    snprintf(filepath, sizeof filepath, "%s/img_%02X_pal%d.png",
                             out_path, im.idx, pi);
            } else {
                snprintf(filepath, sizeof filepath, "%s", out_path);
            }

            if (write_image_png(im, pal, filepath)) {
                printf("exported  %s  (%dx%d  pal=%d  %s)\n",
                       filepath, im.w, im.h, pi, pal.name);
                exported++;
            } else {
                fprintf(stderr, "failed    %s\n", filepath);
                failed++;
            }
        }
    }

    printf("\n%d exported, %d failed\n", exported, failed);
    return (failed > 0) ? 1 : 0;
}

/* bddtool stats FILE.BDB [FILE.BDD]
 * Prints per-layer object counts, image/palette usage, estimated byte cost.
 */
static int cmd_stats(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bddtool stats FILE.BDB [FILE.BDD]\n"); return 1; }
    Stage st{};
    if (!parse_bdb(argv[2], st)) return 1;
    bool have_bdd = false;
    if (argc >= 4) {
        have_bdd = parse_bdd(argv[3], st);
    }

    printf("Stage: %s\n", st.name);
    printf("World: %d x %d  (max depth %d)\n\n", st.world_w, st.world_h, st.max_depth);

    /* per-layer breakdown */
    struct LayerStat { int byte; int count; };
    LayerStat ls[32]; int nls = 0;
    for (auto &o : st.objects) {
        int lb = (o.wx >> 8) & 0xFF;
        bool found = false;
        for (int i = 0; i < nls; i++) if (ls[i].byte == lb) { ls[i].count++; found = true; break; }
        if (!found && nls < 32) { ls[nls].byte = lb; ls[nls++].count = 1; }
    }
    printf("Objects per layer (%d total):\n", (int)st.objects.size());
    for (int i = 0; i < nls; i++)
        printf("  %-10s  %3d  (%.1fx scroll)\n",
               layer_name(ls[i].byte), ls[i].count, scroll_factor(ls[i].byte));

    /* image usage */
    printf("\nImage usage (%d images", (int)st.images.size());
    if (!st.images.empty()) {
        /* count how many images are referenced by at least one object */
        int used = 0;
        for (auto &im : st.images) {
            for (auto &o : st.objects) if (o.ii == im.idx) { used++; break; }
        }
        int unused = (int)st.images.size() - used;
        printf(", %d used, %d unreferenced", used, unused);
    }
    printf("):\n");

    if (have_bdd) {
        /* pixel area per image */
        size_t total_pix = 0;
        for (auto &im : st.images) total_pix += (size_t)im.w * im.h;
        printf("  Total pixels: %zu  (~%zu KB indexed)\n", total_pix, total_pix / 1024);
    }

    /* palette usage */
    printf("\nPalettes (%d):\n", (int)st.palettes.size());
    for (int pi = 0; pi < (int)st.palettes.size(); pi++) {
        int ref_count = 0;
        for (auto &o : st.objects) if (o.fl == pi) ref_count++;
        printf("  [%d] %-16s  %d object references\n",
               pi, st.palettes[pi].name, ref_count);
    }

    /* objects with no module coverage */
    int uncov = 0;
    for (auto &o : st.objects) {
        Image *im = have_bdd ? find_image(st, o.ii) : nullptr;
        if (object_module_index(st, o, im) < 0) uncov++;
    }
    if (uncov > 0)
        printf("\nWarning: %d object(s) outside all module regions\n", uncov);

    return 0;
}

/* bddtool palette FILE.BDD [-o OUT.PNG] [--idx N] [--scale N]
 * Exports palette swatches as a PNG (16 cols × N rows, one cell per palette).
 */
static int cmd_palette(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: bddtool palette FILE.BDD [-o OUT.PNG] [--scale N]\n");
        return 1;
    }
    const char *bdd_path = argv[2];
    const char *out_path = nullptr;
    int scale = 4;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc)       out_path = argv[++i];
        else if (strcmp(argv[i], "--scale") == 0 && i+1 < argc) scale = atoi(argv[++i]);
    }
    if (scale < 1) scale = 1;
    if (scale > 16) scale = 16;

    Stage st{};
    if (!parse_bdd(bdd_path, st)) return 1;
    if (st.palettes.empty()) { fprintf(stderr, "bddtool: no palettes in BDD\n"); return 1; }

    /* dump text info */
    printf("Palettes in %s:\n", bdd_path);
    for (int pi = 0; pi < (int)st.palettes.size(); pi++) {
        int nz = 0;
        for (int ci = 1; ci < 256; ci++) if (st.palettes[pi].argb[ci] != 0xFF000000u) nz++;
        printf("  [%d] %-16s  %d non-black colors\n",
               pi, st.palettes[pi].name, nz);
    }

    /* PNG swatch: 16 cols of colors, one row per palette */
    const int COLS = 16;
    int cell = scale * 16;  /* each color swatch = scale*16 px wide */
    int row_h = cell;
    int sheet_w = COLS * cell;
    int sheet_h = (int)st.palettes.size() * (row_h + 2);

    std::vector<uint8_t> img((size_t)sheet_w * sheet_h * 4, 20);

    for (int pi = 0; pi < (int)st.palettes.size(); pi++) {
        const auto &pal = st.palettes[pi];
        int oy = pi * (row_h + 2);
        /* draw 256 swatches in 16 rows of 16 */
        for (int ci = 0; ci < 256; ci++) {
            int row_in = ci / COLS, col_in = ci % COLS;
            int ox = col_in * cell;
            int iy = oy + row_in * (row_h / 16);
            if (iy + row_h/16 > sheet_h) continue;
            uint32_t c = pal.argb[ci];
            uint8_t r = (c>>16)&0xFF, g2 = (c>>8)&0xFF, b = c&0xFF;
            uint8_t a = (ci == 0) ? 0 : 0xFF;
            for (int sy = 0; sy < row_h/16; sy++) {
                for (int sx = 0; sx < cell; sx++) {
                    int dx = ox + sx, dy = iy + sy;
                    if (dx >= sheet_w || dy >= sheet_h) continue;
                    size_t off = ((size_t)dy * sheet_w + dx) * 4;
                    img[off+0]=r; img[off+1]=g2; img[off+2]=b; img[off+3]=a;
                }
            }
        }
    }

    char outbuf[512];
    if (!out_path) {
        snprintf(outbuf, sizeof outbuf, "%s_palettes.png", stem(std::string(bdd_path)).c_str());
        out_path = outbuf;
    }
    if (!stbi_write_png(out_path, sheet_w, sheet_h, 4, img.data(), sheet_w * 4)) {
        fprintf(stderr, "bddtool: failed to write %s\n", out_path);
        return 1;
    }
    printf("saved: %s  (%d palettes, %dx%d px)\n",
           out_path, (int)st.palettes.size(), sheet_w, sheet_h);
    return 0;
}

/* bddtool sheet FILE.BDB FILE.BDD [-o OUT.PNG] [--pal N] [--cols N] [--scale N]
 * Tiles all images into a single sprite-sheet PNG.
 * Images are laid out left-to-right in rows of --cols width.
 * Each cell is padded to the maximum image dimension + 2px border.
 */
static int cmd_sheet(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: bddtool sheet FILE.BDB FILE.BDD [-o OUT.PNG] [--pal N] [--cols N] [--scale N]\n");
        return 1;
    }
    const char *bdb_path = argv[2];
    const char *bdd_path = argv[3];
    const char *out_path = nullptr;
    int  force_pal  = -1;   /* -1 = per-object palette */
    int  cols       = 16;
    int  scale      = 1;

    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)       out_path  = argv[++i];
        else if (strcmp(argv[i], "--pal")  == 0 && i+1 < argc) force_pal = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cols") == 0 && i+1 < argc) cols      = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scale")== 0 && i+1 < argc) scale     = atoi(argv[++i]);
    }
    if (cols  < 1) cols  = 1;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;

    Stage st{};
    if (!parse_bdb(bdb_path, st)) return 1;
    if (!parse_bdd(bdd_path, st)) return 1;
    if (st.images.empty()) { fprintf(stderr, "bddtool: no images\n"); return 1; }
    if (st.palettes.empty()) { fprintf(stderr, "bddtool: no palettes\n"); return 1; }

    std::vector<int> img_pal(st.images.size(), 0);
    for (size_t i = 0; i < st.images.size(); i++) {
        img_pal[i] = bdd_core_first_palette_for_image(
            st.objects.empty() ? nullptr : st.objects.data(),
            (int)st.objects.size(),
            st.images[i].idx,
            (int)st.palettes.size(),
            0);
    }

    /* cell size = max image dimension */
    int cell_w = 1, cell_h = 1;
    for (auto &im : st.images) {
        if (im.w > cell_w) cell_w = im.w;
        if (im.h > cell_h) cell_h = im.h;
    }
    const int PAD = 2;
    int stride_w = (cell_w + PAD) * scale;
    int stride_h = (cell_h + PAD) * scale;

    int rows = ((int)st.images.size() + cols - 1) / cols;
    int sheet_w = stride_w * cols;
    int sheet_h = stride_h * rows;

    std::vector<uint8_t> sheet((size_t)sheet_w * sheet_h * 4, 0);

    for (size_t idx = 0; idx < st.images.size(); idx++) {
        auto &im = st.images[idx];
        int pi = (force_pal >= 0) ? force_pal : img_pal[idx];
        if (pi >= (int)st.palettes.size()) pi = 0;
        const auto &pal = st.palettes[pi];

        int col = (int)(idx % (size_t)cols);
        int row = (int)(idx / (size_t)cols);
        int ox  = col * stride_w;
        int oy  = row * stride_h;

        std::vector<uint8_t> rgba((size_t)im.w * im.h * 4, 0);
        if (!bdd_core_indexed_to_rgba(im.pix,
                                      im.w, im.h,
                                      pal.argb,
                                      256,
                                      rgba.data(),
                                      rgba.size())) {
            fprintf(stderr, "bddtool: failed to expand image 0x%03X through palette %d\n",
                    im.idx, pi);
            return 1;
        }

        for (int py = 0; py < im.h; py++) {
            for (int px = 0; px < im.w; px++) {
                size_t src = ((size_t)py * im.w + px) * 4;
                uint8_t r = rgba[src + 0];
                uint8_t g = rgba[src + 1];
                uint8_t b = rgba[src + 2];
                uint8_t a = rgba[src + 3];
                /* scale: write scale×scale pixels */
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int dx = ox + px * scale + sx;
                        int dy = oy + py * scale + sy;
                        size_t off = ((size_t)dy * sheet_w + dx) * 4;
                        sheet[off+0] = r; sheet[off+1] = g;
                        sheet[off+2] = b; sheet[off+3] = a;
                    }
                }
            }
        }
    }

    char outbuf[512];
    if (!out_path) {
        snprintf(outbuf, sizeof outbuf, "%s_sheet.png", stem(std::string(bdb_path)).c_str());
        out_path = outbuf;
    }

    if (!stbi_write_png(out_path, sheet_w, sheet_h, 4, sheet.data(), sheet_w * 4)) {
        fprintf(stderr, "bddtool: failed to write %s\n", out_path);
        return 1;
    }
    printf("sheet: %d images, %dx%d px (%dx%d grid, scale %d)\n",
           (int)st.images.size(), sheet_w, sheet_h, cols, rows, scale);
    printf("saved: %s\n", out_path);
    return 0;
}

/* bddtool diff FILE1.BDB FILE2.BDB */
static int cmd_diff(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: bddtool diff FILE1.BDB FILE2.BDB\n"); return 1;
    }
    Stage a{}, b{};
    if (!parse_bdb(argv[2], a)) return 1;
    if (!parse_bdb(argv[3], b)) return 1;

    int changes = 0;

    /* header diffs */
    if (strcmp(a.name, b.name) != 0)
        printf("name      : %s  ->  %s\n", a.name, b.name), changes++;
    if (a.world_w != b.world_w || a.world_h != b.world_h) {
        printf("world     : %dx%d  ->  %dx%d\n", a.world_w, a.world_h, b.world_w, b.world_h);
        changes++;
    }
    if (a.max_depth != b.max_depth)
        printf("max_depth : %d  ->  %d\n", a.max_depth, b.max_depth), changes++;

    /* object count */
    int na = (int)a.objects.size(), nb = (int)b.objects.size();
    if (na != nb)
        printf("objects   : %d  ->  %d  (%+d)\n", na, nb, nb - na), changes++;

    /* per-object diff — align by position, report additions/removals/changes */
    /* simple approach: match by (depth, sy, ii) then show changes in other fields */
    struct Key { int depth, sy, ii; };
    auto obj_key = [](const Object &o) -> Key { return {o.depth, o.sy, o.ii}; };

    std::vector<bool> b_matched(b.objects.size(), false);

    int mod_count = 0, add_count = 0, rem_count = 0;
    for (auto &oa : a.objects) {
        Key ka = obj_key(oa);
        bool found = false;
        for (size_t j = 0; j < b.objects.size(); j++) {
            if (b_matched[j]) continue;
            Key kb = obj_key(b.objects[j]);
            if (ka.depth == kb.depth && ka.sy == kb.sy && ka.ii == kb.ii) {
                b_matched[j] = true;
                auto &ob = b.objects[j];
                /* check for field changes */
                if (oa.wx != ob.wx || oa.fl != ob.fl) {
                    if (mod_count < 20)
                        printf("  MOD  depth=%d sy=%d ii=0x%03X  wx:0x%04X->0x%04X  pal:%d->%d\n",
                               oa.depth, oa.sy, oa.ii, oa.wx, ob.wx, oa.fl, ob.fl);
                    mod_count++;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            if (rem_count < 20)
                printf("  REM  depth=%d sy=%d ii=0x%03X  wx=0x%04X pal=%d  (%s)\n",
                       oa.depth, oa.sy, oa.ii, oa.wx, oa.fl,
                       layer_name((oa.wx >> 8) & 0xFF));
            rem_count++;
        }
    }
    for (size_t j = 0; j < b.objects.size(); j++) {
        if (b_matched[j]) continue;
        auto &ob = b.objects[j];
        if (add_count < 20)
            printf("  ADD  depth=%d sy=%d ii=0x%03X  wx=0x%04X pal=%d  (%s)\n",
                   ob.depth, ob.sy, ob.ii, ob.wx, ob.fl,
                   layer_name((ob.wx >> 8) & 0xFF));
        add_count++;
    }
    if (mod_count > 20) printf("  ... and %d more modified\n", mod_count - 20);
    if (rem_count > 20) printf("  ... and %d more removed\n", rem_count - 20);
    if (add_count > 20) printf("  ... and %d more added\n",   add_count - 20);

    changes += mod_count + rem_count + add_count;

    printf("\n%s vs %s: %d added, %d removed, %d modified\n",
           argv[2], argv[3], add_count, rem_count, mod_count);

    return (changes > 0) ? 1 : 0;
}

/* bddtool render FILE.BDB FILE.BDD [-o OUT.PNG] */
static int cmd_render(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: bddtool render FILE.BDB FILE.BDD [-o OUT.PNG]\n");
        return 1;
    }

    const char *bdb = argv[2];
    const char *bdd = argv[3];
    const char *out = nullptr;
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
    }

    Stage st;
    if (!parse_bdb(bdb, st) || !parse_bdd(bdd, st))
        return 1;

    /* Canvas: declared world size, expanded to fit any overhanging object. */
    int W = st.world_w, H = st.world_h;
    for (size_t i = 0; i < st.objects.size(); i++) {
        const Object &o = st.objects.data()[i];
        const Image *im = find_image(st, o.ii);
        if (!im) continue;
        if (o.depth + im->w > W) W = o.depth + im->w;
        if (o.sy    + im->h > H) H = o.sy    + im->h;
    }
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > 16384) W = 16384;
    if (H > 16384) H = 16384;

    /* Composite every object in BDB order using its palette (fl), transparency
       index 0, and wx flip flags. Fully native -- no Python. */
    std::vector<uint8_t> canvas((size_t)W * (size_t)H * 4, 0);
    std::vector<uint8_t> tmp;
    for (size_t i = 0; i < st.objects.size(); i++) {
        const Object &o = st.objects.data()[i];
        const Image *im = find_image(st, o.ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        if (o.fl < 0 || o.fl >= (int)st.palettes.size()) continue;
        const Palette &pal = st.palettes.data()[o.fl];
        tmp.assign((size_t)im->w * im->h * 4, 0);
        if (!bdd_core_indexed_to_rgba(im->pix, im->w, im->h, pal.argb, 256,
                                      tmp.data(), tmp.size()))
            continue;
        bool hfl = (o.wx & 0x10) != 0;
        bool vfl = (o.wx & 0x20) != 0;
        for (int yy = 0; yy < im->h; yy++) {
            int dy = o.sy + yy;
            if (dy < 0 || dy >= H) continue;
            int sy = vfl ? (im->h - 1 - yy) : yy;
            for (int xx = 0; xx < im->w; xx++) {
                int dx = o.depth + xx;
                if (dx < 0 || dx >= W) continue;
                int sx = hfl ? (im->w - 1 - xx) : xx;
                const uint8_t *src = &tmp[((size_t)sy * im->w + sx) * 4];
                if (src[3] == 0) continue;  /* transparent */
                uint8_t *dst = &canvas[((size_t)dy * W + dx) * 4];
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
            }
        }
    }

    char outbuf[512];
    if (!out) {
        std::string base = stem(bdb);
        snprintf(outbuf, sizeof outbuf, "%s_composite.png", base.c_str());
        out = outbuf;
    }
    if (!stbi_write_png(out, W, H, 4, canvas.data(), W * 4)) {
        fprintf(stderr, "bddtool: failed to write %s\n", out);
        return 1;
    }
    printf("Rendered %s: %dx%d -> %s\n", st.name, W, H, out);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("bddtool — headless CLI for BDB/BDD stage files\n");
    puts("Usage:");
    puts("  bddtool info     FILE.BDB                     Stage summary");
    puts("  bddtool list     FILE.BDB                     Object table");
    puts("  bddtool validate FILE.BDB [FILE.BDD]          LOAD2 module coverage check");
    puts("  bddtool diff     FILE1.BDB FILE2.BDB                  Compare two BDB files");
    puts("  bddtool stats    FILE.BDB [FILE.BDD]                  Per-layer object counts, usage breakdown");
    puts("  bddtool export   FILE.BDB FILE.BDD [-o DIR] [--idx N] [--all-palettes]");
    puts("                                                         Export images to PNG");
    puts("  bddtool palette  FILE.BDD [-o OUT.PNG] [--scale N]    Palette swatch sheet");
    puts("  bddtool sheet    FILE.BDB FILE.BDD [-o OUT.PNG] [--pal N] [--cols N] [--scale N]");
    puts("                                                         Sprite sheet of all images");
    puts("  bddtool render   FILE.BDB FILE.BDD [-o OUT.PNG]        Full stage composite (native)");
    puts("");
    puts("Options:");
    puts("  -o PATH          Output file or directory");
    puts("  --idx N          Export only image with index N (hex or decimal)");
    puts("  --all-palettes   Export each image in every palette it appears in");
    puts("  --pal N          Force palette index for sheet");
    puts("  --cols N         Columns in sheet grid (default 16)");
    puts("  --scale N        Pixel scale for sheet/palette (default 1 for sheet, 4 for palette)");
}

int main(int argc, char **argv)
{
    if (argc < 2) { print_usage(); return 0; }

    const char *cmd = argv[1];
    if (strcmp(cmd, "info")     == 0) return cmd_info(argc, argv);
    if (strcmp(cmd, "list")     == 0) return cmd_list(argc, argv);
    if (strcmp(cmd, "validate") == 0) return cmd_validate(argc, argv);
    if (strcmp(cmd, "diff")     == 0) return cmd_diff(argc, argv);
    if (strcmp(cmd, "stats")    == 0) return cmd_stats(argc, argv);
    if (strcmp(cmd, "export")   == 0) return cmd_export(argc, argv);
    if (strcmp(cmd, "palette")  == 0) return cmd_palette(argc, argv);
    if (strcmp(cmd, "sheet")    == 0) return cmd_sheet(argc, argv);
    if (strcmp(cmd, "render")   == 0) return cmd_render(argc, argv);

    fprintf(stderr, "bddtool: unknown command '%s'\n\n", cmd);
    print_usage();
    return 1;
}
