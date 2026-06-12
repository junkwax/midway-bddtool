#include "Core/viewer_save.h"

#include "Core/app_diagnostics.h"
#include "Core/bdd_core.h"
#include "Core/bdd_metadata.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "UI/tools/mk2_runtime_actor_tool.h"
#include "UI/view/toast_notifications.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int file_exists_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int file_copy(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out = NULL;
    int ok = 1;
    int err = 0;
    if (!in) {
        err = errno;
        bdd_save_logf("file copy failed: cannot open source=\"%s\" dest=\"%s\" errno=%d (%s)",
                      src ? src : "", dst ? dst : "", err, strerror(err));
        return 0;
    }
    out = fopen(dst, "wb");
    if (!out) {
        err = errno;
        bdd_save_logf("file copy failed: cannot open dest=\"%s\" source=\"%s\" errno=%d (%s)",
                      dst ? dst : "", src ? src : "", err, strerror(err));
        fclose(in);
        return 0;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            err = errno;
            break;
        }
    }
    if (ferror(in) && ok) {
        ok = 0;
        err = errno;
    }
    if (fclose(in) != 0 && ok) {
        ok = 0;
        err = errno;
    }
    if (fclose(out) != 0 && ok) {
        ok = 0;
        err = errno;
    }
    if (!ok) {
        bdd_save_logf("file copy failed: source=\"%s\" dest=\"%s\" errno=%d (%s)",
                      src ? src : "", dst ? dst : "", err, strerror(err));
        remove(dst);
        return 0;
    }
    return 1;
}

static void make_save_temp_path(const char *path, char *out, size_t outsz)
{
#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(out, outsz, "%s.tmp.%lu", path ? path : "bddview-save", pid);
}

static void make_save_backup_path(const char *path, char *out, size_t outsz)
{
    char dir[512] = "";
    const char *src = (path && path[0]) ? path : "bddview-save";
    const char *fname = src;
    const char *root = g_bdd_path[0] ? g_bdd_path : src;

    if (!out || outsz == 0) return;

    for (const char *p = src; *p; p++)
        if (*p == '\\' || *p == '/') fname = p + 1;

    snprintf(dir, sizeof dir, "%s", root);
    char *sep = strrchr(dir, '\\');
    char *slash = strrchr(dir, '/');
    if (!sep || (slash && slash > sep)) sep = slash;
    if (sep) {
        sep[1] = '\0';
        snprintf(out, outsz, "%s%s.BAK", dir, fname);
    } else {
        snprintf(out, outsz, "%s.BAK", src);
    }
}

static int save_target_is_readonly(const char *path)
{
#ifdef _WIN32
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    return (attrs & FILE_ATTRIBUTE_READONLY) != 0;
#else
    (void)path;
    return 0;
#endif
}

static int replace_saved_file_with_backup(const char *path, const char *tmp)
{
    char bak[560];
    int had_original;
    make_save_backup_path(path, bak, sizeof bak);
    had_original = file_exists_readable(path);

    if (had_original && save_target_is_readonly(path)) {
        fprintf(stderr, "save: target is read-only: %s\n", path);
        bdd_save_logf("save replace blocked: target is read-only target=\"%s\" temp=\"%s\"",
                      path ? path : "", tmp ? tmp : "");
        remove(tmp);
        return 0;
    }

    if (had_original && !file_copy(path, bak)) {
        fprintf(stderr, "save: could not create backup %s\n", bak);
        bdd_save_logf("save backup failed: source=\"%s\" backup=\"%s\" temp=\"%s\"",
                      path ? path : "", bak, tmp ? tmp : "");
        remove(tmp);
        return 0;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        DWORD err = GetLastError();
        fprintf(stderr, "save: replace failed for %s (winerr=%lu)\n", path, (unsigned long)err);
        bdd_save_logf("save replace failed: target=\"%s\" temp=\"%s\" backup=\"%s\" winerr=%lu",
                      path ? path : "", tmp ? tmp : "", had_original ? bak : "",
                      (unsigned long)err);
        if (had_original && !file_copy(bak, path))
            bdd_save_logf("save rollback failed: backup=\"%s\" target=\"%s\"", bak, path ? path : "");
        DeleteFileA(tmp);
        return 0;
    }
#else
    if (rename(tmp, path) != 0) {
        int err = errno;
        fprintf(stderr, "save: replace failed for %s\n", path);
        bdd_save_logf("save replace failed: target=\"%s\" temp=\"%s\" backup=\"%s\" errno=%d (%s)",
                      path ? path : "", tmp ? tmp : "", had_original ? bak : "",
                      err, strerror(err));
        if (had_original && !file_copy(bak, path))
            bdd_save_logf("save rollback failed: backup=\"%s\" target=\"%s\"", bak, path ? path : "");
        remove(tmp);
        return 0;
    }
#endif
    if (had_original)
        fprintf(stderr, "save: backup %s\n", bak);
    return 1;
}

int bdb_save(const char *path)
{
    char tmp[560];
    const char **module_lines = NULL;
    BddCoreObject *objects = NULL;
    BddCoreSaveResult save_result;
    int module_cap;
    int object_cap;
    int ok = 0;
    if (!editor_project_storage_init()) {
        bdd_save_logf("BDB save failed: project storage allocation failed");
        return 0;
    }
    module_cap = editor_project_module_capacity();
    object_cap = editor_project_object_capacity();
    if (!path || !path[0]) {
        fprintf(stderr, "bdb: no path to save\n");
        bdd_save_logf("BDB save failed: no path");
        return 0;
    }
    if (module_cap <= 0 || object_cap <= 0 ||
        g_bdb_num_modules > module_cap || g_no > object_cap) {
        bdd_save_logf("BDB save failed: project counts exceed storage capacity (objects=%d/%d modules=%d/%d)",
                      g_no, object_cap, g_bdb_num_modules, module_cap);
        return 0;
    }
    module_lines = (const char **)calloc((size_t)module_cap, sizeof(*module_lines));
    objects = (BddCoreObject *)calloc((size_t)object_cap, sizeof(*objects));
    if (!module_lines || !objects) {
        bdd_save_logf("BDB save failed: temporary save allocation failed");
        goto bdb_save_done;
    }
    make_save_temp_path(path, tmp, sizeof tmp);

    {
        char nm[64] = "";
        int ww = 0, wh = 0, md = 255, old_nm = 0, old_np = 0, old_no = 0;
        if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                   nm, &ww, &wh, &md, &old_nm, &old_np, &old_no) >= 7) {
            snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
                     nm, ww, wh, md, g_bdb_num_modules, g_n_pals, g_no);
        }
    }

    for (int m = 0; m < g_bdb_num_modules; m++)
        module_lines[m] = g_bdb_modules[m];
    for (int i = 0; i < g_no; i++) {
        objects[i].wx = g_obj[i].wx;
        objects[i].depth = g_obj[i].depth;
        objects[i].sy = g_obj[i].sy;
        objects[i].ii = g_obj[i].ii;
        objects[i].fl = g_obj[i].fl;
        objects[i].order = g_obj[i].order;
    }

    if (!bdd_core_save_bdb(tmp, g_bdb_header, module_lines, g_bdb_num_modules,
                           objects, g_no, &save_result)) {
        fprintf(stderr, "bdb: failed while writing %s\n", tmp);
        bdd_save_logf("BDB save failed: temp=\"%s\" target=\"%s\" error=\"%s\" objects=%d modules=%d ferror_errno=%d (%s) fclose_errno=%d (%s)",
                      tmp, path, !save_result.error.empty() ? save_result.error.c_str() : "unknown",
                      g_no, g_bdb_num_modules,
                      save_result.ferror_errno,
                      save_result.ferror_errno ? strerror(save_result.ferror_errno) : "none",
                      save_result.fclose_errno,
                      save_result.fclose_errno ? strerror(save_result.fclose_errno) : "none");
        remove(tmp);
        goto bdb_save_done;
    }
    if (!replace_saved_file_with_backup(path, tmp))
        goto bdb_save_done;
    fprintf(stderr, "bdb: saved %d objects to %s\n", g_no, path);
    ok = 1;

bdb_save_done:
    free(objects);
    free(module_lines);
    return ok;
}

int bdd_save(void)
{
    std::vector<BddCoreImage> images;
    std::vector<BddCorePalette> palettes;
    BddCoreSaveResult save_result;
    int image_cap;
    int pal_cap;
    int ok = 0;

    if (!editor_project_storage_init()) {
        bdd_save_logf("BDD save failed: project storage allocation failed");
        return 0;
    }
    image_cap = editor_project_image_capacity();
    pal_cap = editor_project_palette_capacity();
    if (!g_bdd_path[0]) {
        fprintf(stderr, "bdd: no path to save\n");
        bdd_save_logf("BDD save failed: no path");
        return 0;
    }
    if (image_cap <= 0 || pal_cap <= 0 || g_ni > image_cap || g_n_pals > pal_cap) {
        bdd_save_logf("BDD save failed: project counts exceed storage capacity (images=%d/%d palettes=%d/%d)",
                      g_ni, image_cap, g_n_pals, pal_cap);
        return 0;
    }
    if (runtime_actor_preview_imports_loaded()) {
        char msg[256];
        runtime_actor_preview_import_status(msg, sizeof msg);
        fprintf(stderr, "bdd: save blocked, runtime preview imports loaded: %s\n", msg);
        bdd_save_logf("BDD save blocked: %s Save Runtime Sidecar if needed, then Discard Preview IMG Imports.", msg);
        stage_set_toast("BDD save blocked: discard runtime preview sprites first");
        return 0;
    }

    try {
        images.reserve((size_t)g_ni);
        palettes.reserve((size_t)g_n_pals);
    } catch (const std::bad_alloc &) {
        bdd_save_logf("BDD save failed: temporary save allocation failed");
        goto bdd_save_done;
    }

    char tmp[560];
    make_save_temp_path(g_bdd_path, tmp, sizeof tmp);

    try {
        for (int i = 0; i < g_ni; i++) {
            Img *im = &g_img[i];
            BddCoreImage image{};
            image.idx = im->idx;
            image.w = im->w;
            image.h = im->h;
            image.flags = im->flags;
            if (im->pix) {
                image.pix.assign(im->pix, im->pix + (size_t)im->w * im->h);
            } else {
                image.pix.clear();
            }
            images.push_back(std::move(image));
        }
        for (int i = 0; i < g_n_pals; i++) {
            int copy_count = g_pal_count[i];
            if (copy_count < 0) copy_count = 0;
            if (copy_count > 256) copy_count = 256;
            BddCorePalette palette{};
            snprintf(palette.name, sizeof palette.name, "%s", g_pal_name[i]);
            palette.count = copy_count;
            for (int j = 0; j < copy_count; j++) {
                palette.argb[j] = g_pals[i][j];
            }
            editor_project_get_palette_rgb555_cache(i, palette.rgb555, copy_count);
            palettes.push_back(palette);
        }
    } catch (const std::bad_alloc &) {
        bdd_save_logf("BDD save failed: temporary save allocation failed");
        goto bdd_save_done;
    }

    if (!bdd_core_save_bdd(tmp,
                           images.empty() ? NULL : images.data(),
                           (int)images.size(),
                           palettes.empty() ? NULL : palettes.data(),
                           (int)palettes.size(),
                           &save_result)) {
        fprintf(stderr, "bdd: failed while writing %s\n", tmp);
        bdd_save_logf("BDD save failed: temp=\"%s\" target=\"%s\" error=\"%s\" images=%d palettes=%d ferror_errno=%d (%s) fclose_errno=%d (%s)",
                      tmp, g_bdd_path, !save_result.error.empty() ? save_result.error.c_str() : "unknown",
                      g_ni, g_n_pals,
                      save_result.ferror_errno,
                      save_result.ferror_errno ? strerror(save_result.ferror_errno) : "none",
                      save_result.fclose_errno,
                      save_result.fclose_errno ? strerror(save_result.fclose_errno) : "none");
        remove(tmp);
        goto bdd_save_done;
    }
    if (!replace_saved_file_with_backup(g_bdd_path, tmp))
        goto bdd_save_done;
    editor_project_save_bdd_metadata(g_bdd_path);
    fprintf(stderr, "bdd: saved %d images, %d palettes to %s\n",
            g_ni, g_n_pals, g_bdd_path);
    ok = 1;

bdd_save_done:
    return ok;
}
