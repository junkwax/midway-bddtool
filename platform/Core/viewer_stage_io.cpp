#include "Core/viewer_stage_io.h"

#include "bg_editor.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/viewer_load.h"
#include "Core/viewer_save.h"
#include "undo_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void bdd_viewer_enter_edit_layout_after_bdb_load(void)
{
    if (!g_have_bdb || g_no <= 0) {
        g_game_view = 0;
        return;
    }
    bg_editor_autoload_lod_assets();
    g_game_view = 0;
    g_split_view = 0;
    g_runtime_layout_view = 1;
    bdd_center_game_preview_camera();
}

void bdd_viewer_make_ext(const char *src, const char *ext, char *out, size_t outsz)
{
    strncpy(out, src, outsz - 1);
    out[outsz - 1] = '\0';
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    char *backslash = strrchr(out, '\\');
    char *sep = slash > backslash ? slash : backslash;
    if (!dot || (sep && dot < sep)) dot = out + strlen(out);
    strncpy(dot, ext, outsz - (size_t)(dot - out) - 1);
}

FILE *bdd_viewer_fopen_try(const char *path, const char *mode, char *resolved, size_t rsz)
{
    FILE *f = fopen(path, mode);
    if (f) {
        snprintf(resolved, rsz, "%s", path);
        return f;
    }

#ifndef _WIN32
    char up[512];
    strncpy(up, path, sizeof up - 1);
    char *dot = strrchr(up, '.');
    if (dot) {
        for (char *p = dot + 1; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
        f = fopen(up, mode);
        if (f) {
            snprintf(resolved, rsz, "%s", up);
            return f;
        }
    }
#endif
    return NULL;
}

int bdd_viewer_load_stage_for_path(const char *arg, char *bdb_path, size_t bdb_sz,
                                   char *bdd_path, size_t bdd_sz)
{
    const char *ext;
    size_t alen;

    if (!arg || !arg[0]) return 0;

    editor_project_reset_loaded_stage();

    alen = strlen(arg);
    ext = (alen >= 4) ? (arg + alen - 4) : "";

    if (strcasecmp(ext, ".bdb") == 0) {
        bdd_viewer_make_ext(arg, ".bdb", bdb_path, bdb_sz);
        bdd_viewer_make_ext(arg, ".bdd", bdd_path, bdd_sz);
    } else if (strcasecmp(ext, ".bdd") == 0) {
        bdd_viewer_make_ext(arg, ".bdd", bdd_path, bdd_sz);
        bdd_viewer_make_ext(arg, ".bdb", bdb_path, bdb_sz);
    } else {
        fprintf(stderr, "Unknown extension: expected .BDB or .BDD\n");
        return 0;
    }

    {
        char resolved[512] = "";
        FILE *tf = bdd_viewer_fopen_try(bdd_path, "rb", resolved, sizeof resolved);
        if (!tf) {
            fprintf(stderr, "Cannot find BDD file: %s\n", bdd_path);
            return 0;
        }
        fclose(tf);
        snprintf(bdd_path, bdd_sz, "%s", resolved);
    }
    if (!bdd_load(bdd_path)) return 0;

    {
        char resolved[512] = "";
        FILE *tf = bdd_viewer_fopen_try(bdb_path, "r", resolved, sizeof resolved);
        if (tf) {
            fclose(tf);
            snprintf(bdb_path, bdb_sz, "%s", resolved);
            g_have_bdb = bdb_load(bdb_path);
        } else {
            fprintf(stderr, "BDB not found - showing image grid\n");
        }
    }
    bdd_viewer_enter_edit_layout_after_bdb_load();
    return 1;
}

int bdd_viewer_roundtrip_save_stage_for_path(const char *arg, const char *out_prefix)
{
    char in_bdb[512] = "", in_bdd[512] = "";
    char out_bdb[512] = "", out_bdd[512] = "";
    char check_bdb[512] = "", check_bdd[512] = "";

    if (!arg || !arg[0] || !out_prefix || !out_prefix[0]) {
        fprintf(stderr, "usage: bddview --roundtrip-save FILE.BDB|FILE.BDD OUT_PREFIX\n");
        return 1;
    }

    if (!bdd_viewer_load_stage_for_path(arg, in_bdb, sizeof in_bdb, in_bdd, sizeof in_bdd))
        return 1;
    if (!g_have_bdb) {
        fprintf(stderr, "roundtrip: input has no BDB placement data: %s\n", arg);
        return 1;
    }

    bdd_viewer_make_ext(out_prefix, ".BDB", out_bdb, sizeof out_bdb);
    bdd_viewer_make_ext(out_prefix, ".BDD", out_bdd, sizeof out_bdd);
    snprintf(g_bdb_path, sizeof g_bdb_path, "%s", out_bdb);
    snprintf(g_bdd_path, sizeof g_bdd_path, "%s", out_bdd);

    if (!bdb_save(out_bdb))
        return 1;
    if (!bdd_save())
        return 1;

    if (!bdd_viewer_load_stage_for_path(out_bdb, check_bdb, sizeof check_bdb,
                                        check_bdd, sizeof check_bdd))
        return 1;
    if (!g_have_bdb) {
        fprintf(stderr, "roundtrip: saved BDB did not reload: %s\n", out_bdb);
        return 1;
    }

    printf("roundtrip=ok source_bdb=%s source_bdd=%s out_bdb=%s out_bdd=%s objects=%d images=%d palettes=%d modules=%d\n",
           in_bdb, in_bdd, check_bdb, check_bdd,
           g_no, g_ni, g_n_pals, g_bdb_num_modules);
    return 0;
}

int bdd_viewer_undo_move_smoke_for_path(const char *arg)
{
    char in_bdb[512] = "", in_bdd[512] = "";
    int object_cap;
    int *before_depth = NULL;
    int *before_sy = NULL;
    unsigned char *mask = NULL;
    Obj *before_objects = NULL;
    char *before_modules = NULL;
    unsigned char *module_mask = NULL;
    Uint8 *before_pixels = NULL;
    int old_depth, old_sy, new_depth, new_sy;
    int mask_old_depth = 0, mask_old_sy = 0, mask_new_depth = 0, mask_new_sy = 0;
    int mask_tested = 0;
    int record_tested = 0;
    int module_tested = 0;
    int palette_tested = 0;
    int palette_raw_tested = 0;
    int image_index_tested = 0;
    int image_pixels_tested = 0;
    Obj old_obj = {};
    Obj new_obj = {};
    int module_cap = 0;
    char old_line[256] = "";
    char new_line[256] = "";
    Uint32 before_colors[256];
    Uint16 before_rgb555[256];
    Uint16 check_rgb555[256];
    char before_name[64] = "";
    int before_count = 0;
    Uint32 old_color = 0;
    Uint32 new_color = 0;
    int old_idx = 0;
    int new_idx = -1;
    int pixel_count = 0;
    Uint8 old_pixel = 0;
    Uint8 new_pixel = 0;
    int rc = 1;

    if (!arg || !arg[0]) {
        fprintf(stderr, "usage: bddview --undo-move-smoke FILE.BDB|FILE.BDD\n");
        return 1;
    }
    if (!bdd_viewer_load_stage_for_path(arg, in_bdb, sizeof in_bdb, in_bdd, sizeof in_bdd))
        return 1;
    if (!g_have_bdb || g_no <= 0) {
        fprintf(stderr, "undo-move-smoke: input has no movable objects: %s\n", arg);
        return 1;
    }

    undo_manager_init();
    object_cap = editor_project_object_capacity();
    before_depth = (int *)calloc((size_t)object_cap, sizeof(int));
    before_sy = (int *)calloc((size_t)object_cap, sizeof(int));
    before_objects = (Obj *)calloc((size_t)object_cap, sizeof(Obj));
    if (!before_depth || !before_sy || !before_objects)
        goto done;

    editor_project_clear_selection();
    g_sel_flags[0] = 1;
    old_depth = g_obj[0].depth;
    old_sy = g_obj[0].sy;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        before_depth[i] = g_obj[i].depth;
        before_sy[i] = g_obj[i].sy;
    }

    g_obj[0].depth += 17;
    g_obj[0].sy += 9;
    new_depth = g_obj[0].depth;
    new_sy = g_obj[0].sy;
    if (undo_save_object_position_delta_for_selection(before_depth, before_sy,
                                                      object_cap, "Move") != 1) {
        fprintf(stderr, "undo-move-smoke: command delta was not recorded\n");
        goto done;
    }
    if (!undo_is_available()) {
        fprintf(stderr, "undo-move-smoke: undo not available after delta save\n");
        goto done;
    }

    undo_restore();
    if (g_obj[0].depth != old_depth || g_obj[0].sy != old_sy) {
        fprintf(stderr, "undo-move-smoke: undo mismatch got=(%d,%d) expected=(%d,%d)\n",
                g_obj[0].depth, g_obj[0].sy, old_depth, old_sy);
        goto done;
    }
    if (!redo_is_available()) {
        fprintf(stderr, "undo-move-smoke: redo not available after undo\n");
        goto done;
    }

    redo_restore();
    if (g_obj[0].depth != new_depth || g_obj[0].sy != new_sy) {
        fprintf(stderr, "undo-move-smoke: redo mismatch got=(%d,%d) expected=(%d,%d)\n",
                g_obj[0].depth, g_obj[0].sy, new_depth, new_sy);
        goto done;
    }

    if (g_no > 1) {
        mask = (unsigned char *)calloc((size_t)object_cap, sizeof(unsigned char));
        if (!mask)
            goto done;

        editor_project_clear_selection();
        g_sel_flags[0] = 1;
        mask[1] = 1;
        for (int i = 0; i < g_no && i < object_cap; i++) {
            before_depth[i] = g_obj[i].depth;
            before_sy[i] = g_obj[i].sy;
        }
        mask_old_depth = g_obj[1].depth;
        mask_old_sy = g_obj[1].sy;
        g_obj[1].depth -= 5;
        g_obj[1].sy += 11;
        mask_new_depth = g_obj[1].depth;
        mask_new_sy = g_obj[1].sy;
        if (undo_save_object_position_delta_for_mask(before_depth, before_sy,
                                                     mask, object_cap, "Nudge") != 1) {
            fprintf(stderr, "undo-move-smoke: masked command delta was not recorded\n");
            goto done;
        }
        undo_restore();
        if (g_obj[1].depth != mask_old_depth || g_obj[1].sy != mask_old_sy) {
            fprintf(stderr, "undo-move-smoke: masked undo mismatch got=(%d,%d) expected=(%d,%d)\n",
                    g_obj[1].depth, g_obj[1].sy, mask_old_depth, mask_old_sy);
            goto done;
        }
        redo_restore();
        if (g_obj[1].depth != mask_new_depth || g_obj[1].sy != mask_new_sy) {
            fprintf(stderr, "undo-move-smoke: masked redo mismatch got=(%d,%d) expected=(%d,%d)\n",
                    g_obj[1].depth, g_obj[1].sy, mask_new_depth, mask_new_sy);
            goto done;
        }
        mask_tested = 1;
    }

    if (!mask) {
        mask = (unsigned char *)calloc((size_t)object_cap, sizeof(unsigned char));
        if (!mask)
            goto done;
    }
    memset(mask, 0, (size_t)object_cap);
    mask[0] = 1;
    for (int i = 0; i < g_no && i < object_cap; i++)
        before_objects[i] = g_obj[i];
    old_obj = g_obj[0];
    g_obj[0].wx ^= 0x0100;
    g_obj[0].fl = (g_n_pals > 1) ? ((g_obj[0].fl + 1) % g_n_pals) : g_obj[0].fl;
    g_obj[0].hfl = g_obj[0].hfl ? 0 : 1;
    if (undo_save_object_record_delta_for_mask(before_objects, mask,
                                               object_cap, "Edit Object") != 1) {
        fprintf(stderr, "undo-move-smoke: object record delta was not recorded\n");
        goto done;
    }
    new_obj = g_obj[0];
    undo_restore();
    if (memcmp(&g_obj[0], &old_obj, sizeof(Obj)) != 0) {
        fprintf(stderr, "undo-move-smoke: object record undo mismatch\n");
        goto done;
    }
    redo_restore();
    if (memcmp(&g_obj[0], &new_obj, sizeof(Obj)) != 0) {
        fprintf(stderr, "undo-move-smoke: object record redo mismatch\n");
        goto done;
    }
    record_tested = 1;

    module_cap = editor_project_module_capacity();
    if (g_bdb_num_modules > 0 && module_cap > 0) {
        before_modules = (char *)calloc((size_t)module_cap, 256u);
        module_mask = (unsigned char *)calloc((size_t)module_cap, sizeof(unsigned char));
        if (!before_modules || !module_mask)
            goto done;

        for (int i = 0; i < g_bdb_num_modules && i < module_cap; i++)
            memcpy(before_modules + ((size_t)i * 256u), g_bdb_modules[i], 256);
        module_mask[0] = 1;
        snprintf(old_line, sizeof old_line, "%s", g_bdb_modules[0]);
        snprintf(new_line, sizeof new_line, "%s", g_bdb_modules[0]);
        strncat(new_line, " ", sizeof new_line - strlen(new_line) - 1);
        snprintf(g_bdb_modules[0], 256, "%s", new_line);
        if (undo_save_module_line_delta_for_mask(before_modules, module_mask,
                                                 module_cap, "Edit Module") != 1) {
            fprintf(stderr, "undo-move-smoke: module line delta was not recorded\n");
            goto done;
        }
        undo_restore();
        if (strncmp(g_bdb_modules[0], old_line, 256) != 0) {
            fprintf(stderr, "undo-move-smoke: module undo mismatch\n");
            goto done;
        }
        redo_restore();
        if (strncmp(g_bdb_modules[0], new_line, 256) != 0) {
            fprintf(stderr, "undo-move-smoke: module redo mismatch\n");
            goto done;
        }
        module_tested = 1;
    }

    if (g_n_pals > 0 && g_pal_count[0] > 0) {
        before_count = g_pal_count[0];
        if (before_count > 256) before_count = 256;
        memcpy(before_colors, g_pals[0], sizeof before_colors);
        snprintf(before_name, sizeof before_name, "%s", g_pal_name[0]);
        for (int i = 0; i < before_count; i++)
            before_rgb555[i] = (Uint16)((0x1234u + (unsigned)i * 37u) & 0x7FFFu);
        memset(check_rgb555, 0, sizeof check_rgb555);
        if (!editor_project_set_palette_rgb555_cache(0, before_rgb555, before_count) ||
            !editor_project_get_palette_rgb555_cache(0, check_rgb555, 256) ||
            memcmp(before_rgb555, check_rgb555,
                   (size_t)before_count * sizeof before_rgb555[0]) != 0) {
            fprintf(stderr, "undo-move-smoke: palette raw cache setup mismatch\n");
            goto done;
        }
        old_color = g_pals[0][0];
        new_color = (old_color & 0xFF000000u) | ((old_color ^ 0x00332113u) & 0x00FFFFFFu);
        if (new_color == old_color)
            new_color = (old_color & 0xFF000000u) | 0x00010203u;
        if (!editor_project_set_palette_color(0, 0, new_color) ||
            undo_save_palette_slot_delta(0, before_colors, before_count, before_name,
                                         before_rgb555, 1, before_count,
                                         "Edit Palette") != 1) {
            fprintf(stderr, "undo-move-smoke: palette delta was not recorded\n");
            goto done;
        }
        if (editor_project_get_palette_rgb555_cache(0, check_rgb555, 256)) {
            fprintf(stderr, "undo-move-smoke: palette edit did not invalidate raw cache\n");
            goto done;
        }
        undo_restore();
        memset(check_rgb555, 0, sizeof check_rgb555);
        if (g_pal_count[0] != before_count ||
            strncmp(g_pal_name[0], before_name, 64) != 0 ||
            memcmp(g_pals[0], before_colors, sizeof before_colors) != 0 ||
            !editor_project_get_palette_rgb555_cache(0, check_rgb555, 256) ||
            memcmp(before_rgb555, check_rgb555,
                   (size_t)before_count * sizeof before_rgb555[0]) != 0) {
            fprintf(stderr, "undo-move-smoke: palette undo mismatch\n");
            goto done;
        }
        redo_restore();
        if (g_pals[0][0] != new_color ||
            editor_project_get_palette_rgb555_cache(0, check_rgb555, 256)) {
            fprintf(stderr, "undo-move-smoke: palette redo mismatch\n");
            goto done;
        }
        palette_tested = 1;
        palette_raw_tested = 1;
    }

    if (g_ni > 0) {
        old_idx = g_img[0].idx;
        new_idx = -1;
        for (int candidate = 0; candidate <= 0xFFFF; candidate++) {
            int used = 0;
            if (candidate == old_idx) continue;
            for (int i = 1; i < g_ni; i++) {
                if (g_img[i].idx == candidate) {
                    used = 1;
                    break;
                }
            }
            if (!used) {
                new_idx = candidate;
                break;
            }
        }
        if (new_idx >= 0) {
            g_img[0].idx = new_idx;
            if (undo_save_image_index_delta(0, old_idx, "Edit Image Index") != 1) {
                fprintf(stderr, "undo-move-smoke: image index delta was not recorded\n");
                goto done;
            }
            undo_restore();
            if (g_img[0].idx != old_idx) {
                fprintf(stderr, "undo-move-smoke: image index undo mismatch\n");
                goto done;
            }
            redo_restore();
            if (g_img[0].idx != new_idx) {
                fprintf(stderr, "undo-move-smoke: image index redo mismatch\n");
                goto done;
            }
            image_index_tested = 1;
        }
    }

    if (g_ni > 0 && g_img[0].pix && g_img[0].w > 0 && g_img[0].h > 0 &&
        g_img[0].w <= 0x3fffffff / g_img[0].h) {
        pixel_count = g_img[0].w * g_img[0].h;
        old_pixel = g_img[0].pix[0];
        new_pixel = (Uint8)(old_pixel ^ 1u);
        before_pixels = (Uint8 *)malloc((size_t)pixel_count * sizeof before_pixels[0]);
        if (!before_pixels)
            goto done;
        memcpy(before_pixels, g_img[0].pix, (size_t)pixel_count * sizeof before_pixels[0]);
        g_img[0].pix[0] = new_pixel;
        if (undo_save_image_pixels_delta(0, g_img[0].w, g_img[0].h,
                                         before_pixels, "Edit Pixels") != 1) {
            fprintf(stderr, "undo-move-smoke: image pixel delta was not recorded\n");
            goto done;
        }
        undo_restore();
        if (g_img[0].pix[0] != old_pixel) {
            fprintf(stderr, "undo-move-smoke: image pixel undo mismatch\n");
            goto done;
        }
        redo_restore();
        if (g_img[0].pix[0] != new_pixel) {
            fprintf(stderr, "undo-move-smoke: image pixel redo mismatch\n");
            goto done;
        }
        image_pixels_tested = 1;
    }

    printf("undo-move-smoke=ok object=0 before=(%d,%d) after=(%d,%d) mask=%s record=%s module=%s palette=%s raw=%s image_index=%s image_pixels=%s label=%s\n",
           old_depth, old_sy, new_depth, new_sy, mask_tested ? "ok" : "skipped",
           record_tested ? "ok" : "skipped",
           module_tested ? "ok" : "skipped",
           palette_tested ? "ok" : "skipped",
           palette_raw_tested ? "ok" : "skipped",
           image_index_tested ? "ok" : "skipped",
           image_pixels_tested ? "ok" : "skipped",
           undo_get_history_label(0));
    rc = 0;

done:
    free(before_pixels);
    free(module_mask);
    free(before_modules);
    free(mask);
    free(before_objects);
    free(before_depth);
    free(before_sy);
    undo_manager_shutdown();
    return rc;
}

static void reset_import_smoke_state(void)
{
    editor_project_reset_loaded_stage();
}

static int import_asset_smoke_for_path(const char *src_path, const char *out_prefix,
                                       const char *kind, const char *usage,
                                       int (*import_fn)(const char *))
{
    char out_bdd[512] = "";
    int imported;
    int saved_images;
    int saved_pals;
    int saved_labels = 0;
    int saved_lod_refs = 0;

    if (!src_path || !src_path[0] || !out_prefix || !out_prefix[0]) {
        fprintf(stderr, "%s\n", usage);
        return 1;
    }

    reset_import_smoke_state();

    bdd_viewer_make_ext(out_prefix, ".BDD", out_bdd, sizeof out_bdd);
    snprintf(g_bdd_path, sizeof g_bdd_path, "%s", out_bdd);

    imported = import_fn(src_path);
    if (imported <= 0)
        return 1;
    saved_images = g_ni;
    saved_pals = g_n_pals;
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].label[0]) saved_labels++;
        if (g_img[i].lod_ref) saved_lod_refs++;
    }
    if (!bdd_save())
        return 1;

    img_free();
    if (!bdd_load(out_bdd))
        return 1;
    if (g_ni != saved_images || g_n_pals != saved_pals) {
        fprintf(stderr, "%s-import: reload mismatch images=%d/%d palettes=%d/%d\n",
                kind, g_ni, saved_images, g_n_pals, saved_pals);
        return 1;
    }
    {
        int loaded_labels = 0;
        int loaded_lod_refs = 0;
        for (int i = 0; i < g_ni; i++) {
            if (g_img[i].label[0]) loaded_labels++;
            if (g_img[i].lod_ref) loaded_lod_refs++;
        }
        if (loaded_labels != saved_labels || loaded_lod_refs != saved_lod_refs) {
            fprintf(stderr, "%s-import: metadata reload mismatch labels=%d/%d lod_refs=%d/%d\n",
                    kind, loaded_labels, saved_labels, loaded_lod_refs, saved_lod_refs);
            return 1;
        }
    }

    fprintf(stderr, "%s-import=ok source=%s out_bdd=%s imported=%d images=%d palettes=%d labels=%d lod_refs=%d\n",
            kind, src_path, out_bdd, imported, g_ni, g_n_pals, saved_labels, saved_lod_refs);
    return 0;
}

int bdd_viewer_import_img_smoke_for_path(const char *img_path, const char *out_prefix)
{
    return import_asset_smoke_for_path(img_path, out_prefix, "img",
        "usage: bddview --import-img-smoke FILE.IMG OUT_PREFIX",
        bg_editor_import_img);
}

int bdd_viewer_import_img_folder_smoke_for_path(const char *dir, const char *out_prefix)
{
    return import_asset_smoke_for_path(dir, out_prefix, "img-folder",
        "usage: bddview --import-img-folder-smoke DIR OUT_PREFIX",
        bg_editor_import_img_folder);
}

int bdd_viewer_import_lod_smoke_for_path(const char *lod_path, const char *out_prefix)
{
    return import_asset_smoke_for_path(lod_path, out_prefix, "lod",
        "usage: bddview --import-lod-smoke FILE.LOD OUT_PREFIX",
        bg_editor_import_lod);
}

int bdd_viewer_import_png_smoke_for_path(const char *png_path, const char *out_prefix)
{
    char out_bdd[512] = "";
    int imported;
    int saved_images;
    int saved_pals;
    int saved_idx;
    int saved_w;
    int saved_h;
    int saved_pal_idx;
    int saved_pal_count;
    size_t pixel_count;
    Uint8 *saved_pix = NULL;
    Uint32 saved_pal[256];
    Img *im;
    Img *loaded;

    if (!png_path || !png_path[0] || !out_prefix || !out_prefix[0]) {
        fprintf(stderr, "usage: bddview --import-png-smoke FILE.PNG OUT_PREFIX\n");
        return 1;
    }

    reset_import_smoke_state();

    bdd_viewer_make_ext(out_prefix, ".BDD", out_bdd, sizeof out_bdd);
    snprintf(g_bdd_path, sizeof g_bdd_path, "%s", out_bdd);

    imported = bg_editor_import_png_headless(png_path);
    if (imported <= 0 || g_ni <= 0) {
        fprintf(stderr, "png-import: no image imported from %s\n", png_path);
        return 1;
    }

    im = &g_img[g_ni - 1];
    saved_idx = im->idx;
    saved_w = im->w;
    saved_h = im->h;
    saved_pal_idx = im->pal_idx;
    if (!im->pix || saved_w <= 0 || saved_h <= 0 ||
        saved_pal_idx < 0 || saved_pal_idx >= g_n_pals) {
        fprintf(stderr, "png-import: imported image is incomplete\n");
        return 1;
    }

    pixel_count = (size_t)saved_w * (size_t)saved_h;
    saved_pix = (Uint8 *)malloc(pixel_count);
    if (!saved_pix) {
        fprintf(stderr, "png-import: out of memory\n");
        return 1;
    }
    memcpy(saved_pix, im->pix, pixel_count);
    saved_pal_count = g_pal_count[saved_pal_idx];
    memcpy(saved_pal, g_pals[saved_pal_idx], sizeof saved_pal);
    saved_images = g_ni;
    saved_pals = g_n_pals;

    if (!bdd_save()) {
        free(saved_pix);
        return 1;
    }

    img_free();
    if (!bdd_load(out_bdd)) {
        free(saved_pix);
        return 1;
    }

    loaded = img_find(saved_idx);
    if (!loaded || loaded->w != saved_w || loaded->h != saved_h ||
        !loaded->pix || memcmp(loaded->pix, saved_pix, pixel_count) != 0) {
        fprintf(stderr, "png-import: pixel reload mismatch for image 0x%X\n", saved_idx);
        free(saved_pix);
        return 1;
    }
    free(saved_pix);

    if (g_ni != saved_images || g_n_pals != saved_pals) {
        fprintf(stderr, "png-import: reload mismatch images=%d/%d palettes=%d/%d\n",
                g_ni, saved_images, g_n_pals, saved_pals);
        return 1;
    }
    if (saved_pal_idx >= g_n_pals ||
        g_pal_count[saved_pal_idx] != saved_pal_count ||
        memcmp(g_pals[saved_pal_idx], saved_pal,
               (size_t)saved_pal_count * sizeof saved_pal[0]) != 0) {
        fprintf(stderr, "png-import: palette reload mismatch for palette %d\n",
                saved_pal_idx);
        return 1;
    }

    printf("png-import=ok source=%s out_bdd=%s imported=%d image=0x%X size=%dx%d palette=%d colors=%d images=%d palettes=%d\n",
           png_path, out_bdd, imported, saved_idx, saved_w, saved_h,
           saved_pal_idx, saved_pal_count, g_ni, g_n_pals);
    return 0;
}
