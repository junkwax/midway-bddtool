#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "compat.h"
#include "Core/img_format.h"
#include "imgui.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "undo_manager.h"

#include <algorithm>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#include <shlobj.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#endif

static void path_join(char *out, size_t outsz, const char *dir, const char *file)
{
    if (!dir || !dir[0]) {
        snprintf(out, outsz, "%s", file ? file : "");
        return;
    }

    size_t len = strlen(dir);
    if (dir[len - 1] == '\\' || dir[len - 1] == '/')
        snprintf(out, outsz, "%s%s", dir, file ? file : "");
    else
        snprintf(out, outsz, "%s%c%s", dir, PATH_SEP_CHAR, file ? file : "");
}

static const char *path_basename_ptr(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash;
    if (!sep || (backslash && backslash > sep)) sep = backslash;
    return sep ? sep + 1 : path;
}

static bool path_has_ext_ci(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    return plen >= elen && strcasecmp(path + plen - elen, ext) == 0;
}

static bool ensure_directory(const char *path)
{
    if (!path || !path[0]) return false;
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

static bool copy_file_overwrite(const char *src, const char *dst)
{
    if (!src || !dst || !src[0] || !dst[0]) return false;
#ifdef _WIN32
    return CopyFileA(src, dst, FALSE) != 0;
#else
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[8192];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    if (ferror(in)) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    return ok;
#endif
}

static unsigned short rgb555_from_rgba(int r, int g, int b)
{
    int r5 = (r * 31 + 127) / 255;
    int g5 = (g * 31 + 127) / 255;
    int b5 = (b * 31 + 127) / 255;
    if (r5 > 31) r5 = 31;
    if (g5 > 31) g5 = 31;
    if (b5 > 31) b5 = 31;
    return (unsigned short)((r5 << 10) | (g5 << 5) | b5);
}

static Uint32 rgb555_to_argb(unsigned short c)
{
    int r = (c >> 10) & 31;
    int g = (c >> 5) & 31;
    int b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

static int rgb555_distance(unsigned short a, unsigned short b)
{
    int ar = (a >> 10) & 31, ag = (a >> 5) & 31, ab = a & 31;
    int br = (b >> 10) & 31, bg = (b >> 5) & 31, bb = b & 31;
    int dr = ar - br, dg = ag - bg, db = ab - bb;
    return dr * dr + dg * dg + db * db;
}

static int img_import_fallback_palette(const char *path)
{
    char base[64];
    img_basename_no_ext_upper(path, base, sizeof base);
    char name[64];
    snprintf(name, sizeof name, "%.56sIMP", base[0] ? base : "IMG");
    Uint32 colors[256] = {};
    colors[0] = 0;
    for (int i = 1; i < 256; i++)
        colors[i] = 0xFF000000u | ((Uint32)i << 16) | ((Uint32)i << 8) | (Uint32)i;
    int pi = editor_project_append_palette_slot(name, 256, colors);
    return pi >= 0 ? pi : ((g_n_pals > 0) ? 0 : -1);
}

int import_img_file_filtered(const char *path, bool save_undo,
                             const unsigned char *selected, int selected_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        stage_set_toast("IMG import failed: cannot open file");
        return 0;
    }
    long file_sz = img_file_size_for_import(f);
    ImgLibHeaderDisk hdr;
    if (file_sz < (long)sizeof hdr || fread(&hdr, 1, sizeof hdr, f) != sizeof hdr) {
        fclose(f);
        stage_set_toast("IMG import failed: bad header");
        return 0;
    }

    if (hdr.temp != 0xABCD || hdr.version < 0x0500 || hdr.imgcnt == 0 ||
        hdr.oset >= (unsigned int)file_sz) {
        fclose(f);
        stage_set_toast("IMG import failed: unsupported IMG");
        return 0;
    }

    int disk_pal_count = (hdr.palcnt > IMG_NUM_DEFAULT_PALS)
                       ? (int)hdr.palcnt - IMG_NUM_DEFAULT_PALS : 0;
    long rec_end = (long)hdr.oset
                 + (long)hdr.imgcnt * (long)sizeof(ImgImageDisk)
                 + (long)disk_pal_count * (long)sizeof(ImgPaletteDisk);
    if (rec_end > file_sz) {
        fclose(f);
        stage_set_toast("IMG import failed: too large");
        return 0;
    }

    std::vector<char> needed_pals((size_t)disk_pal_count, 0);
    int selected_imgs = 0;
    int needed_pal_count = 0;
    bool fallback_needed = false;
    for (int i = 0; i < (int)hdr.imgcnt; i++) {
        bool want = !selected || (i < selected_len && selected[i]);
        if (!want) continue;
        ImgImageDisk id;
        long img_rec = (long)hdr.oset + (long)i * (long)sizeof id;
        if (fseek(f, img_rec, SEEK_SET) != 0 || fread(&id, 1, sizeof id, f) != sizeof id)
            continue;
        int w = (id.w < 3) ? 3 : (int)id.w;
        int h = (int)id.h;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
            continue;
        selected_imgs++;
        int pal_offset = (int)id.palnum - IMG_NUM_DEFAULT_PALS;
        if (pal_offset >= 0 && pal_offset < disk_pal_count) {
            if (!needed_pals[(size_t)pal_offset]) {
                needed_pals[(size_t)pal_offset] = 1;
                needed_pal_count++;
            }
        } else {
            fallback_needed = true;
        }
    }
    if (selected && selected_imgs == 0) {
        fclose(f);
        stage_set_toast("IMG import: no sprites selected");
        return 0;
    }
    int fallback_extra = (fallback_needed && needed_pal_count == 0) ? 1 : 0;
    if (!editor_project_reserve_images(g_ni + selected_imgs) ||
        !editor_project_reserve_palettes(g_n_pals + needed_pal_count + fallback_extra)) {
        fclose(f);
        stage_set_toast("IMG import failed: too large");
        return 0;
    }

    if (save_undo) undo_save_ex("Import IMG");

    char base[64];
    img_basename_no_ext_upper(path, base, sizeof base);

    int old_pals = g_n_pals;
    int old_images = g_ni;
    int pal_base = g_n_pals;
    struct ImgImportPixelMap {
        unsigned char values[256];
    };
    std::vector<int> pal_map((size_t)disk_pal_count, -1);
    std::vector<ImgImportPixelMap> pix_map((size_t)disk_pal_count);
    for (int p = 0; p < disk_pal_count; p++)
        for (int i = 0; i < 256; i++)
            pix_map[(size_t)p].values[i] = (unsigned char)i;
    int imported_pals = 0;
    int shifted_pals = 0;
    long pal_rec = (long)hdr.oset + (long)hdr.imgcnt * (long)sizeof(ImgImageDisk);
    for (int p = 0; p < disk_pal_count; p++) {
        if (!needed_pals.empty() && !needed_pals[(size_t)p])
            continue;
        ImgPaletteDisk pd;
        if (fseek(f, pal_rec + (long)p * (long)sizeof pd, SEEK_SET) != 0 ||
            fread(&pd, 1, sizeof pd, f) != sizeof pd) {
            continue;
        }
        int count = (int)pd.numc;
        if (count < 1) count = 1;
        if (count > 256) count = 256;
        if ((long)pd.oset < 0 || (long)pd.oset + (long)count * 2L > file_sz)
            continue;

        char pal_name[64];
        img_raw_name_to_upper(pd.name, 10, base, pal_name, sizeof pal_name);
        bool preserve_visible_zero = (!g_img_import_index0_transparent && count < 256);
        if (preserve_visible_zero) shifted_pals++;
        Uint32 colors[256] = {};
        if (fseek(f, (long)pd.oset, SEEK_SET) == 0) {
            for (int i = 0; i < count; i++) {
                unsigned char b[2] = {0, 0};
                if (fread(b, 1, 2, f) != 2) break;
                unsigned short word = (unsigned short)(b[0] | (b[1] << 8));
                if (preserve_visible_zero) {
                    int dst_i = i + 1;
                    colors[dst_i] = img_pal_word_to_argb_opaque(word);
                    pix_map[(size_t)p].values[i] = (unsigned char)dst_i;
                } else {
                    colors[i] = img_pal_word_to_argb(word, i);
                    pix_map[(size_t)p].values[i] = (unsigned char)i;
                }
            }
        }
        int pi = editor_project_append_palette_slot(
            pal_name,
            preserve_visible_zero ? count + 1 : count,
            colors);
        if (pi < 0)
            continue;
        pal_map[(size_t)p] = pi;
        imported_pals++;
    }

    int fallback_pal = (g_n_pals > pal_base) ? pal_base : img_import_fallback_palette(path);
    int max_idx = 0;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;

    int imported_imgs = 0;
    int skipped_imgs = 0;
    int visible_zero_remaps = 0;
    for (int i = 0; i < (int)hdr.imgcnt; i++) {
        if (selected && (i >= selected_len || !selected[i]))
            continue;
        ImgImageDisk id;
        long img_rec = (long)hdr.oset + (long)i * (long)sizeof id;
        if (fseek(f, img_rec, SEEK_SET) != 0 || fread(&id, 1, sizeof id, f) != sizeof id) {
            skipped_imgs++;
            continue;
        }
        int w = (id.w < 3) ? 3 : (int)id.w;
        int h = (int)id.h;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            skipped_imgs++;
            continue;
        }
        int pal_offset = (int)id.palnum - IMG_NUM_DEFAULT_PALS;
        int pal_idx = (pal_offset >= 0 && pal_offset < disk_pal_count && pal_map[(size_t)pal_offset] >= 0)
                    ? pal_map[(size_t)pal_offset] : fallback_pal;
        const unsigned char *map = (pal_offset >= 0 && pal_offset < disk_pal_count && pal_map[(size_t)pal_offset] >= 0)
                                 ? pix_map[(size_t)pal_offset].values : NULL;

        unsigned char *pix = (unsigned char *)malloc((size_t)w * (size_t)h);
        if (!pix) {
            skipped_imgs++;
            continue;
        }
        if (!img_decode_pixels(f, file_sz, &id, w, h, pix, map, &visible_zero_remaps)) {
            free(pix);
            skipped_imgs++;
            continue;
        }

        Img *im = editor_project_append_image_slot();
        if (!im) {
            free(pix);
            skipped_imgs++;
            continue;
        }
        im->idx = max_idx + imported_imgs + 1;
        im->w = w;
        im->h = h;
        im->flags = id.flags & 1;
        im->pal_idx = pal_idx;
        im->anix = img_s16(id.anix);
        im->aniy = img_s16(id.aniy);
        im->anix2 = img_s16(id.anix2);
        im->aniy2 = img_s16(id.aniy2);
        im->aniz2 = img_s16(id.aniz2);
        im->frm = img_s16(id.frm);
        im->pttblnum = img_s16(id.pttblnum);
        im->opals = img_s16(id.opals);
        img_raw_name_to_upper(id.name, 16, base, im->label, sizeof im->label);
        snprintf(im->source, sizeof im->source, "%s", base);
        im->pix = pix;
        imported_imgs++;
    }

    fclose(f);

    if (imported_imgs <= 0) {
        editor_project_truncate_images(old_images);
        editor_project_truncate_palettes(old_pals);
        stage_set_toast("IMG import found no sprites");
        return 0;
    }

    g_last_import_img = g_ni - 1;
    g_show_images = true;
    g_dirty = 1;
    g_need_rebuild = 1;
    g_mk2_palette_sync_dirty = true;
    int opt_trim_images = 0, opt_trim_pixels = 0, opt_pals = 0;
    if (g_import_optimize_after_import) {
        optimize_image_range_for_space(old_images, g_ni, false,
                                       &opt_trim_images, &opt_trim_pixels, &opt_pals);
        if (opt_trim_images > 0 || opt_pals > 0)
            g_mk2_palette_sync_dirty = true;
    }
    char toast[128];
    if (visible_zero_remaps > 0 && g_img_import_index0_transparent) {
        snprintf(toast, sizeof toast,
                 "Imported IMG: %d sprites, kept %d zero px transparent, trimmed %d",
                 imported_imgs, visible_zero_remaps, opt_trim_pixels);
    } else if (visible_zero_remaps > 0) {
        snprintf(toast, sizeof toast,
                 "Imported IMG: %d sprites, preserved %d zero px as art, trimmed %d",
                 imported_imgs, visible_zero_remaps, opt_trim_pixels);
    } else {
        snprintf(toast, sizeof toast,
                 "Imported IMG: %d sprites, %d palettes, trimmed %d",
                 imported_imgs, imported_pals, opt_trim_pixels);
    }
    stage_set_toast(toast);
    fprintf(stderr, "img: imported %s (%d sprites, %d palettes, %d shifted palettes, %d zero-index pixels, %d skipped, %d trim px, %d compacted palettes, zero_is_transparent=%d)\n",
            path, imported_imgs, imported_pals, shifted_pals, visible_zero_remaps, skipped_imgs,
            opt_trim_pixels, opt_pals, g_img_import_index0_transparent ? 1 : 0);
    if (save_undo && g_mk2_palette_prompt_after_img_import)
        mk2_palette_sync_request_prompt("IMG import added BDD palettes");
    return imported_imgs;
}

int import_img_file(const char *path, bool save_undo)
{
    return import_img_file_filtered(path, save_undo, NULL, 0);
}

int batch_import_png(const char *dir)
{
    bool undo_done = false;
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof pattern, "%s\\*.png", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        stage_set_toast("No PNG files found");
        return 0;
    }
    int count = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!undo_done) { undo_save_ex("Batch Import PNG"); undo_done = true; }
        char full[512];
        path_join(full, sizeof full, dir, fd.cFileName);
        import_png(full, false);
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (count > 0) fprintf(stderr, "batch: imported %d PNG files from %s\n", count, dir);
    return count;
#else
    DIR *d = opendir(dir);
    if (!d) {
        stage_set_toast("No PNG files found");
        return 0;
    }
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (!path_has_ext_ci(name, ".png")) continue;
        if (!undo_done) { undo_save_ex("Batch Import PNG"); undo_done = true; }
        char full[1024];
        path_join(full, sizeof full, dir, name);
        import_png(full, false);
        count++;
    }
    closedir(d);
    if (count > 0) fprintf(stderr, "batch: imported %d PNG files from %s\n", count, dir);
    return count;
#endif
}

int batch_import_img(const char *dir)
{
    if (!dir || !dir[0]) return 0;
    bool undo_done = false;
    int libraries = 0;
    int sprites = 0;
    int failed = 0;
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof pattern, "%s\\*.img", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (!undo_done) { undo_save_ex("Batch Import IMG"); undo_done = true; }
            char full[512];
            path_join(full, sizeof full, dir, fd.cFileName);
            int n = import_img_file(full, false);
            if (n > 0) { libraries++; sprites += n; }
            else failed++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *name = ent->d_name;
            if (!path_has_ext_ci(name, ".img")) continue;
            if (!undo_done) { undo_save_ex("Batch Import IMG"); undo_done = true; }
            char full[1024];
            path_join(full, sizeof full, dir, name);
            int n = import_img_file(full, false);
            if (n > 0) { libraries++; sprites += n; }
            else failed++;
        }
        closedir(d);
    }
#endif
    char toast[128];
    if (sprites > 0) {
        snprintf(toast, sizeof toast, "Imported IMG folder: %d files, %d sprites",
                 libraries, sprites);
        stage_set_toast(toast);
        fprintf(stderr, "batch-img: imported %d IMG files, %d sprites from %s (%d failed)\n",
                libraries, sprites, dir, failed);
        if (g_mk2_palette_prompt_after_img_import)
            mk2_palette_sync_request_prompt("IMG folder import added BDD palettes");
    } else {
        stage_set_toast("No IMG sprites imported");
    }
    return sprites;
}

static bool file_exists_for_import(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool path_is_absolute_for_import(const char *path)
{
    if (!path || !path[0]) return false;
#ifdef _WIN32
    if ((path[0] == '\\' || path[0] == '/') ||
        (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
         path[1] == ':'))
        return true;
    return false;
#else
    return path[0] == '/';
#endif
}

static void path_dirname_for_import(const char *path, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s", path ? path : "");
    char *sep = strrchr(out, '\\');
    char *sep2 = strrchr(out, '/');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
    if (sep) *sep = '\0';
    else snprintf(out, outsz, ".");
}

static void trim_import_token(char *token)
{
    if (!token) return;
    char *start = token;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' ||
           *start == '"' || *start == '\'')
        start++;
    if (start != token)
        memmove(token, start, strlen(start) + 1);

    size_t len = strlen(token);
    while (len > 0) {
        char c = token[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' ||
            c == '\'' || c == ',' || c == ';' || c == ')' || c == ']') {
            token[--len] = '\0';
            continue;
        }
        break;
    }
}

static bool resolve_lod_img_path(const char *lod_dir, const char *token,
                                 char *out, size_t outsz)
{
    char clean[512];
    snprintf(clean, sizeof clean, "%s", token ? token : "");
    trim_import_token(clean);
    out[0] = '\0';
    if (!clean[0]) return false;

    if (path_is_absolute_for_import(clean) && file_exists_for_import(clean)) {
        snprintf(out, outsz, "%s", clean);
        return true;
    }
    if (!path_is_absolute_for_import(clean) && file_exists_for_import(clean)) {
        snprintf(out, outsz, "%s", clean);
        return true;
    }

    const char *base = path_basename_ptr(clean);
    const char *rel = path_is_absolute_for_import(clean) ? base : clean;
    if (lod_dir && lod_dir[0]) {
        path_join(out, outsz, lod_dir, rel);
        if (file_exists_for_import(out)) return true;
        if (base != rel) {
            path_join(out, outsz, lod_dir, base);
            if (file_exists_for_import(out)) return true;
        }

        char parent[512];
        path_dirname_for_import(lod_dir, parent, sizeof parent);
        if (parent[0] && strcasecmp(parent, lod_dir) != 0) {
            path_join(out, outsz, parent, rel);
            if (file_exists_for_import(out)) return true;
            if (base != rel) {
                path_join(out, outsz, parent, base);
                if (file_exists_for_import(out)) return true;
            }
        }
    }

    const char *envs[] = { "IMGDIR", "MK2_IMGDIR", "MIDWAY_IMGDIR" };
    for (int i = 0; i < 3; i++) {
        const char *dir = getenv(envs[i]);
        if (!dir || !dir[0]) continue;
        path_join(out, outsz, dir, rel);
        if (file_exists_for_import(out)) return true;
        if (base != rel) {
            path_join(out, outsz, dir, base);
            if (file_exists_for_import(out)) return true;
        }
    }

    out[0] = '\0';
    return false;
}

struct ImportedImgRange {
    std::string path;
    int start;
    int end;
};

static int find_imported_range(const std::vector<ImportedImgRange> &ranges, const char *path)
{
    for (size_t i = 0; i < ranges.size(); i++)
        if (strcasecmp(ranges[i].path.c_str(), path) == 0)
            return (int)i;
    return -1;
}

static void uppercase_ascii_inplace(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
}

static void lod_mark_label_in_range(int start, int end, const char *raw_label, int *matched)
{
    char label[64];
    snprintf(label, sizeof label, "%s", raw_label ? raw_label : "");
    trim_import_token(label);
    uppercase_ascii_inplace(label);
    if (!label[0]) return;
    for (int i = start; i < end && i < g_ni; i++) {
        if (strcasecmp(g_img[i].label, label) != 0)
            continue;
        g_img[i].lod_ref = 1;
        if (matched) (*matched)++;
        return;
    }
}

static void lod_basename_no_ext_upper(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    uppercase_ascii_inplace(out);
}

static bool lod_text_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            unsigned char a = (unsigned char)h[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static void lod_tag_imported_range_core(int start, int end, const char *lod_path)
{
    if (start < 0 || end <= start || !lod_path || !lod_path[0]) return;
    char lod_base[64];
    lod_basename_no_ext_upper(lod_path, lod_base, sizeof lod_base);
    if (!lod_base[0]) return;

    for (int i = start; i < end && i < g_ni; i++) {
        g_img[i].lod_ref = 1;
        if (g_img[i].source[0] && lod_text_contains_ci(g_img[i].source, lod_base))
            continue;

        char old_source[64];
        snprintf(old_source, sizeof old_source, "%s", g_img[i].source);
        if (old_source[0])
            snprintf(g_img[i].source, sizeof g_img[i].source, "%.28s/%.28s",
                     old_source, lod_base);
        else
            snprintf(g_img[i].source, sizeof g_img[i].source, "%.56s", lod_base);
    }
}

static int lod_mark_reference_line(int start, int end, const char *refs, int *matched)
{
    if (start < 0 || end <= start || !refs) return 0;
    int labels = 0;
    const char *p = refs;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char label[64];
        size_t o = 0;
        while (*p && *p != ',' && *p != '\r' && *p != '\n' && o + 1 < sizeof label)
            label[o++] = *p++;
        label[o] = '\0';
        trim_import_token(label);
        if (label[0]) {
            labels++;
            lod_mark_label_in_range(start, end, label, matched);
        }
        while (*p && *p != ',') p++;
    }
    return labels;
}

int import_lod_file(const char *path, bool save_undo)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        stage_set_toast("LOD import failed: cannot open file");
        return 0;
    }

    char lod_dir[512];
    path_dirname_for_import(path, lod_dir, sizeof lod_dir);
    std::vector<ImportedImgRange> ranges;
    bool undo_done = false;
    int libraries = 0;
    int sprites = 0;
    int missing = 0;
    int failed = 0;
    int duplicates = 0;
    int last_start = -1;
    int last_end = -1;
    int lod_labels = 0;
    int lod_matched = 0;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        comment = strchr(line, '#');
        if (comment) *comment = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "--->", 4) == 0) {
            lod_labels += lod_mark_reference_line(last_start, last_end, p + 4, &lod_matched);
            continue;
        }

        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')
                p++;
            if (!*p) break;

            char token[512] = "";
            char *dst = token;
            size_t left = sizeof token - 1;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && left > 0) {
                    *dst++ = *p++;
                    left--;
                }
                if (*p == '"') p++;
            } else {
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && left > 0) {
                    *dst++ = *p++;
                    left--;
                }
            }
            *dst = '\0';
            trim_import_token(token);
            if (!path_has_ext_ci(token, ".img"))
                continue;

            char resolved[1024];
            if (!resolve_lod_img_path(lod_dir, token, resolved, sizeof resolved)) {
                missing++;
                fprintf(stderr, "lod-img: missing %s\n", token);
                continue;
            }
            int seen_range = find_imported_range(ranges, resolved);
            if (seen_range >= 0) {
                duplicates++;
                last_start = ranges[(size_t)seen_range].start;
                last_end = ranges[(size_t)seen_range].end;
                continue;
            }
            if (save_undo && !undo_done) {
                undo_save_ex("Import IMG LOD");
                undo_done = true;
            }
            int start = g_ni;
            int n = import_img_file(resolved, false);
            if (n > 0) {
                ImportedImgRange range;
                range.path = resolved;
                range.start = start;
                range.end = g_ni;
                lod_tag_imported_range_core(range.start, range.end, path);
                ranges.push_back(range);
                last_start = range.start;
                last_end = range.end;
                libraries++;
                sprites += n;
            } else {
                last_start = -1;
                last_end = -1;
                failed++;
            }
        }
    }
    fclose(f);

    char toast[128];
    if (sprites > 0) {
        g_show_images = true;
        snprintf(toast, sizeof toast, "Imported LOD: %d IMG, %d sprites, %d labels",
                 libraries, sprites, lod_matched);
        stage_set_toast(toast);
        if (g_mk2_palette_prompt_after_img_import)
            mk2_palette_sync_request_prompt("LOD import added BDD palettes");
    } else {
        stage_set_toast("LOD import found no IMG sprites");
    }
    fprintf(stderr, "lod-img: %s -> %d IMG files, %d sprites (%d labels/%d matched, %d missing, %d failed, %d duplicate)\n",
            path, libraries, sprites, lod_matched, lod_labels, missing, failed, duplicates);
    return sprites;
}

void import_png(const char *path, bool save_undo)
{
    int w, h, n;
    unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba) { fprintf(stderr, "png: failed to load %s\n", path); return; }
    if (!editor_project_reserve_images(g_ni + 1) ||
        !editor_project_reserve_palettes(g_n_pals + 1)) {
        fprintf(stderr, "png: project storage reserve failed\n");
        stbi_image_free(rgba);
        return;
    }

    if (save_undo) undo_save_ex("Import PNG");
    int old_images = g_ni;

    unsigned short pal[256];
    int pal_cnt = 0;
    int raw_color_count = 0;
    int rgb555_color_count = 0;
    std::vector<Uint32> raw_colors;
    std::vector<unsigned short> rgb555_colors;
    unsigned char *idx = (unsigned char *)malloc((size_t)w * h);
    if (!idx) { stbi_image_free(rgba); return; }
    raw_colors.reserve((size_t)w * (size_t)h > 4096 ? 4096 : (size_t)w * (size_t)h);
    rgb555_colors.reserve(raw_colors.capacity());

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 4;
            int r = rgba[off], g = rgba[off+1], b = rgba[off+2], a = rgba[off+3];
            if (a < 128) { idx[y * w + x] = 0; continue; }

            unsigned short c555 = rgb555_from_rgba(r, g, b);
            raw_colors.push_back(((Uint32)(Uint8)r << 16) |
                                 ((Uint32)(Uint8)g << 8) |
                                  (Uint8)b);
            rgb555_colors.push_back(c555);
            int best = 1;
            int best_d = INT_MAX;
            for (int p = 0; p < pal_cnt; p++) {
                int d = rgb555_distance(c555, pal[p]);
                if (d < best_d) { best_d = d; best = p + 1; }
            }
            if (best_d > 0 && pal_cnt < 255) {
                pal[pal_cnt] = c555;
                best = ++pal_cnt;
            }
            idx[y * w + x] = (Uint8)best;
        }
    }
    std::sort(raw_colors.begin(), raw_colors.end());
    raw_color_count = (int)(std::unique(raw_colors.begin(), raw_colors.end()) - raw_colors.begin());
    std::sort(rgb555_colors.begin(), rgb555_colors.end());
    rgb555_color_count = (int)(std::unique(rgb555_colors.begin(), rgb555_colors.end()) - rgb555_colors.begin());

    int palette_base = (g_png_import_force_8bpp && pal_cnt > 0 && pal_cnt < 128) ? 128 : 1;
    int forced_8bpp = (palette_base == 128);
    if (forced_8bpp) {
        size_t npx = (size_t)w * (size_t)h;
        for (size_t i = 0; i < npx; i++) {
            if (idx[i])
                idx[i] = (Uint8)(palette_base + (int)idx[i] - 1);
        }
    }

    Uint32 colors[256] = {};
    for (int i = 0; i < pal_cnt; i++)
        colors[forced_8bpp ? (palette_base + i) : (i + 1)] = rgb555_to_argb(pal[i]);
    colors[0] = 0;
    const char *base = path;
    for (const char *s = path; *s; s++)
        if (*s == '/' || *s == '\\') base = s + 1;
    char pal_name[64];
    snprintf(pal_name, sizeof pal_name, "%s", base);
    char *dot = strrchr(pal_name, '.');
    if (dot) *dot = '\0';
    for (char *p = pal_name; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    int pi = editor_project_append_palette_slot(
        pal_name,
        forced_8bpp ? (palette_base + pal_cnt) : (pal_cnt + 1),
        colors);
    if (pi < 0) {
        free(idx);
        stbi_image_free(rgba);
        return;
    }

    int max_idx = 0;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx > max_idx) max_idx = g_img[i].idx;
    Img *im = editor_project_append_image_slot();
    if (!im) {
        free(idx);
        editor_project_truncate_palettes(pi);
        stbi_image_free(rgba);
        return;
    }
    im->idx    = max_idx + 1;
    im->w      = w;
    im->h      = h;
    im->flags  = 0;
    im->pal_idx = pi;
    snprintf(im->label, sizeof im->label, "%s", g_pal_name[pi]);
    snprintf(im->source, sizeof im->source, "%s", base);
    im->pix    = idx;

    stbi_image_free(rgba);
    g_last_import_img = g_ni - 1;
    int opt_trim_images = 0, opt_trim_pixels = 0, opt_pals = 0;
    if (g_import_optimize_after_import) {
        optimize_image_range_for_space(old_images, g_ni, false,
                                       &opt_trim_images, &opt_trim_pixels, &opt_pals);
    }
    fprintf(stderr,
            "png: imported %s as idx=0x%02X  pal=%d  %dx%d (%d source colors -> %d RGB555%s%s)\n",
            path, im->idx, pi, w, h, raw_color_count, rgb555_color_count,
            rgb555_color_count > pal_cnt ? ", capped to 255" : "",
            forced_8bpp ? ", forced 8bpp" : "");
    char toast[128];
    snprintf(toast, sizeof toast,
             "Imported 0x%02X %dx%d, %d->%d RGB555%s",
             im->idx, im->w, im->h, raw_color_count, rgb555_color_count,
             forced_8bpp ? ", 8bpp" : "");
    stage_set_toast(toast);
    g_show_images = true;
    g_dirty = 1;
    g_need_rebuild = 1;
}

void bg_editor_import_png(const char *path)
{
    import_png(path);
}

int bg_editor_import_png_headless(const char *path)
{
    int before = g_ni;
    import_png(path, false);
    return g_ni - before;
}

int bg_editor_import_img(const char *path)
{
    return import_img_file(path);
}

int bg_editor_import_img_folder(const char *dir)
{
    return batch_import_img(dir);
}

int bg_editor_import_lod(const char *path)
{
    return import_lod_file(path);
}

bool file_dialog_open(const char *title, const char *filter,
                      char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? true : false;
#else
    (void)filter;
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --title='%s' 2>/dev/null", title ? title : "Open");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getopenfilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

bool file_dialog_save(const char *title, const char *filter,
                      char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? true : false;
#else
    (void)filter;
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --save --confirm-overwrite --title='%s' 2>/dev/null",
             title ? title : "Save");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getsavefilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

bool folder_dialog_open(const char *title, char *out, int outsz)
{
#ifdef _WIN32
    char sel[1024] = {0};
    BROWSEINFOA bi = {0};
    bi.lpszTitle = title ? title : "Select folder";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        bool ok = SHGetPathFromIDListA(pidl, sel) ? true : false;
        CoTaskMemFree(pidl);
        if (ok) {
            snprintf(out, (size_t)outsz, "%s", sel);
            return true;
        }
    }
    out[0] = '\0';
    return false;
#else
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --directory --title='%s' 2>/dev/null",
             title ? title : "Select folder");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getexistingdirectory . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

static bool export_composite_to(const char *dest_path)
{
    if (!g_have_bdb || g_no == 0 || !dest_path) return false;
    int wx_min, wx_max, wy_min, wy_max;
    bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    if (wx_min == INT_MAX) return false;
    int w = wx_max - wx_min, h = wy_max - wy_min;
    if (w <= 0 || w > 32768 || h <= 0 || h > 32768) return false;
    unsigned char *buf = (unsigned char *)calloc((size_t)w * h, 4);
    if (!buf) return false;
    for (int i = 0; i < g_no; i++) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im || !im->pix) continue;
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;
        int ox = o->depth - wx_min, oy = o->sy - wy_min;
        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                int sx = o->hfl ? (im->w - 1 - x) : x;
                int sy = (o->vfl ? (im->h - 1 - y) : y) * im->w;
                Uint8 v = im->pix[sy + sx];
                if (!v) continue;
                int px = ox + x, py = oy + y;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                Uint32 c = pal[v];
                size_t off = ((size_t)py * w + (size_t)px) * 4;
                buf[off+0] = (c>>16)&0xFF; buf[off+1] = (c>>8)&0xFF;
                buf[off+2] = c&0xFF;       buf[off+3] = (c>>24)?((c>>24)&0xFF):0xFF;
            }
        }
    }
    int ok = stbi_write_png(dest_path, w, h, 4, buf, w * 4);
    free(buf);
    return ok != 0;
}

void export_composite_png(void)
{
    if (!g_have_bdb || g_no == 0) return;

    int wx_min, wx_max, wy_min, wy_max;
    if (g_runtime_layout_view)
        bdd_get_runtime_layout_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    else
        bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    if (wx_min == INT_MAX) return;

    int w = wx_max - wx_min;
    int h = wy_max - wy_min;
    if (w <= 0 || w > 32768 || h <= 0 || h > 32768)
        return;

    unsigned char *buf = (unsigned char *)calloc((size_t)w * h, 4);
    if (!buf) return;

    for (int i = 0; i < g_no; i++) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;

        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                          ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;

        int ox = o->depth - wx_min;
        int oy = o->sy - wy_min;

        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                int sx = o->hfl ? (im->w - 1 - x) : x;
                int sy = (o->vfl ? (im->h - 1 - y) : y) * im->w;
                Uint8 v = im->pix[sy + sx];
                if (v == 0) continue;
                int px = ox + x, py = oy + y;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                Uint32 c = pal[v];
                size_t off = ((size_t)py * w + (size_t)px) * 4;
                buf[off + 0] = (c >> 16) & 0xFF;
                buf[off + 1] = (c >>  8) & 0xFF;
                buf[off + 2] =  c        & 0xFF;
                buf[off + 3] = (c >> 24) & 0xFF;
            }
        }
    }

    char path[520];
    snprintf(path, sizeof path, "%s", g_bdb_path);
    char *dot = strrchr(path, '.');
    if (dot) *dot = '\0';
    snprintf(path + strlen(path), sizeof(path) - strlen(path), "_composite.png");

    stbi_write_png(path, w, h, 4, buf, w * 4);
    fprintf(stderr, "export: composite saved to %s (%dx%d)\n", path, w, h);
    free(buf);
}

void stage_export_bundle(void)
{
    if (!g_have_bdb) { stage_set_toast("No project loaded"); return; }
    char dir[512] = "";
    if (!folder_dialog_open("Choose export folder", dir, sizeof dir)) return;

    char out_dir[512];
    path_join(out_dir, sizeof out_dir, dir, g_name[0] ? g_name : "export");
    ensure_directory(out_dir);

    int files_written = 0;

    if (g_bdb_path[0]) {
        const char *fname = path_basename_ptr(g_bdb_path);
        char dst[512]; path_join(dst, sizeof dst, out_dir, fname);
        if (copy_file_overwrite(g_bdb_path, dst)) files_written++;
    }
    if (g_bdd_path[0]) {
        const char *fname = path_basename_ptr(g_bdd_path);
        char dst[512]; path_join(dst, sizeof dst, out_dir, fname);
        if (copy_file_overwrite(g_bdd_path, dst)) files_written++;
    }

    {
        char png[512]; path_join(png, sizeof png, out_dir, "composite.png");
        if (export_composite_to(png)) files_written++;
    }

    {
        char mf[512]; path_join(mf, sizeof mf, out_dir, "manifest.txt");
        FILE *f = fopen(mf, "w");
        if (f) {
            fprintf(f, "project: %s\n", g_name);
            fprintf(f, "images:  %d\n", g_ni);
            fprintf(f, "objects: %d\n", g_no);
            fprintf(f, "palettes:%d\n", g_n_pals);
            fprintf(f, "modules: %d\n", g_bdb_num_modules);
            if (g_bdb_path[0]) fprintf(f, "bdb: %s\n", g_bdb_path);
            if (g_bdd_path[0]) fprintf(f, "bdd: %s\n", g_bdd_path);
            fclose(f);
            files_written++;
        }
    }

    char toast[128];
    snprintf(toast, sizeof toast, "Package exported: %d file(s) -> %s", files_written, out_dir);
    stage_set_toast(toast);
}

void export_viewport_png(void)
{
    if (!g_have_bdb || g_no == 0) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int vw = (int)(ds.x / g_zoom), vh = (int)(ds.y / g_zoom);
    if (vw <= 0 || vh <= 0 || vw > 8192 || vh > 8192) return;

    unsigned char *buf = (unsigned char *)calloc((size_t)vw * vh, 4);
    if (!buf) return;

    for (int i = 0; i < g_no; i++) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im || !im->pix) continue;
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;
        int ox = o->depth - g_view_x, oy = o->sy - g_view_y;
        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                int sx = o->hfl ? (im->w - 1 - x) : x;
                int sy2 = (o->vfl ? (im->h - 1 - y) : y) * im->w;
                Uint8 v = im->pix[sy2 + sx];
                if (!v) continue;
                int px = ox + x, py = oy + y;
                if (px < 0 || px >= vw || py < 0 || py >= vh) continue;
                Uint32 c = pal[v];
                size_t off = ((size_t)py * vw + px) * 4;
                buf[off+0] = (c>>16)&0xFF; buf[off+1] = (c>>8)&0xFF;
                buf[off+2] = c&0xFF;       buf[off+3] = (c>>24)&0xFF;
            }
        }
    }

    char path[512] = "";
    if (file_dialog_save("Export Viewport",
            "PNG Files\0*.png\0All Files\0*.*\0", path, (int)sizeof path)) {
        size_t pl = strlen(path);
        if (pl < 4 || strcasecmp(path + pl - 4, ".png") != 0)
            strncat(path, ".png", sizeof path - pl - 1);
        stbi_write_png(path, vw, vh, 4, buf, vw * 4);
    }
    free(buf);
}

void export_sprite_sheet_png(void)
{
    if (g_ni == 0) return;

    char path[512] = "";
    if (!file_dialog_save("Export Sprite Sheet",
            "PNG Files\0*.png\0All Files\0*.*\0", path, (int)sizeof path))
        return;
    size_t pl = strlen(path);
    if (pl < 4 || strcasecmp(path + pl - 4, ".png") != 0)
        strncat(path, ".png", sizeof path - pl - 1);

    const int COLS = 16;
    const int PAD  = 2;

    int cell_w = 1, cell_h = 1;
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].w > cell_w) cell_w = g_img[i].w;
        if (g_img[i].h > cell_h) cell_h = g_img[i].h;
    }
    int stride_w = cell_w + PAD;
    int stride_h = cell_h + PAD;
    int rows   = (g_ni + COLS - 1) / COLS;
    int sheet_w = stride_w * COLS;
    int sheet_h = stride_h * rows;

    unsigned char *sheet = (unsigned char *)calloc((size_t)sheet_w * sheet_h, 4);
    if (!sheet) return;

    int *img_pal = (int *)calloc((size_t)g_ni, sizeof(int));
    if (!img_pal) {
        free(sheet);
        return;
    }

    for (int o = 0; o < g_no; o++) {
        for (int i = 0; i < g_ni; i++) {
            if (g_img[i].idx == g_obj[o].ii && img_pal[i] == 0) {
                img_pal[i] = (g_obj[o].fl >= 0 && g_obj[o].fl < g_n_pals) ? g_obj[o].fl : 0;
            }
        }
    }

    for (int idx = 0; idx < g_ni; idx++) {
        Img *im = &g_img[idx];
        if (!im->pix) continue;
        int pi = (img_pal[idx] >= 0 && img_pal[idx] < g_n_pals) ? img_pal[idx] : 0;
        const Uint32 *pal = g_pals[pi];

        int col = idx % COLS;
        int row = idx / COLS;
        int ox  = col * stride_w;
        int oy  = row * stride_h;

        for (int py = 0; py < im->h; py++) {
            for (int px = 0; px < im->w; px++) {
                Uint8 v = im->pix[(size_t)py * im->w + px];
                if (!v) continue;
                Uint32 c = pal[v];
                int dx = ox + px, dy = oy + py;
                if (dx < 0 || dx >= sheet_w || dy < 0 || dy >= sheet_h) continue;
                size_t off = ((size_t)dy * sheet_w + dx) * 4;
                sheet[off+0] = (c >> 16) & 0xFF;
                sheet[off+1] = (c >>  8) & 0xFF;
                sheet[off+2] =  c        & 0xFF;
                sheet[off+3] = 0xFF;
            }
        }
    }

    stbi_write_png(path, sheet_w, sheet_h, 4, sheet, sheet_w * 4);
    free(sheet);
    free(img_pal);
}

void export_image_tga(Img *im)
{
    char path[520];
    snprintf(path, sizeof path, "img_%02X.tga", im->idx);
    const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                      ? g_pals[im->pal_idx] : NULL;
    if (pal && im->pix) {
        unsigned char hdr[18] = {0};
        hdr[1] = 1; hdr[2] = 1;
        hdr[5] = (unsigned char)(g_pal_count[im->pal_idx] & 0xFF);
        hdr[6] = (unsigned char)(g_pal_count[im->pal_idx] >> 8);
        hdr[7] = 24;
        hdr[12] = (unsigned char)(im->w & 0xFF);
        hdr[13] = (unsigned char)(im->w >> 8);
        hdr[14] = (unsigned char)(im->h & 0xFF);
        hdr[15] = (unsigned char)(im->h >> 8);
        hdr[16] = 8; hdr[17] = 0x20;
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(hdr, 1, 18, f);
            for (int ci = 0; ci < g_pal_count[im->pal_idx]; ci++) {
                Uint32 c = pal[ci];
                fputc((c >> 16) & 0xFF, f);
                fputc((c >> 8)  & 0xFF, f);
                fputc(c         & 0xFF, f);
            }
            for (int y = 0; y < im->h; y++)
                fwrite(im->pix + y * im->w, 1, im->w, f);
            fclose(f);
        }
    }
}

void export_image_png(Img *im)
{
    char path[520];
    snprintf(path, sizeof path, "img_%02X.png", im->idx);
    const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                      ? g_pals[im->pal_idx] : NULL;
    if (pal && im->pix) {
        unsigned char *rgba = (unsigned char *)malloc((size_t)im->w * im->h * 4);
        if (rgba) {
            for (int py = 0; py < im->h; py++) {
                for (int px = 0; px < im->w; px++) {
                    Uint8 v = im->pix[py * im->w + px];
                    Uint32 c = (v == 0 || !pal) ? 0 : pal[v];
                    int o = (py * im->w + px) * 4;
                    rgba[o+0] = (c >> 16) & 0xFF;
                    rgba[o+1] = (c >>  8) & 0xFF;
                    rgba[o+2] =  c        & 0xFF;
                    rgba[o+3] = (v == 0) ? 0 : 0xFF;
                }
            }
            stbi_write_png(path, im->w, im->h, 4, rgba, im->w * 4);
            free(rgba);
        }
    }
}
