#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/path_utils.h"
#include "Core/project_header.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

/* Backup original to bddtool's local backups/pre-save directory before overwriting. */
static void backup_to_tmp(const char *filepath)
{
    if (!filepath || !filepath[0]) return;
    struct stat st{};
    if (stat(filepath, &st) != 0) {
        int err = errno;
        if (err != ENOENT)
            bdd_save_logf("pre-save backup skipped: cannot stat source=\"%s\" errno=%d (%s)",
                          filepath, err, strerror(err));
        return;
    }

    char bakpath[520];
    if (!bddtool_backup_path(bakpath, sizeof bakpath, filepath, ".bak", "pre-save")) {
        bdd_save_logf("pre-save backup failed: cannot create local backup path source=\"%s\"",
                      filepath);
        return;
    }
    FILE *src = fopen(filepath, "rb");
    if (!src) {
        int err = errno;
        bdd_save_logf("pre-save backup failed: cannot open source=\"%s\" backup=\"%s\" errno=%d (%s)",
                      filepath, bakpath, err, strerror(err));
        return;
    }
    FILE *dst = fopen(bakpath, "wb");
    if (!dst) {
        int err = errno;
        bdd_save_logf("pre-save backup failed: cannot open backup=\"%s\" source=\"%s\" errno=%d (%s)",
                      bakpath, filepath, err, strerror(err));
        fclose(src);
        return;
    }

    bool ok = true;
    int err = 0;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            ok = false;
            err = errno;
            break;
        }
    }
    if (ferror(src) && ok) {
        ok = false;
        err = errno;
    }
    if (fclose(src) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (fclose(dst) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (!ok) {
        bdd_save_logf("pre-save backup failed while copying: source=\"%s\" backup=\"%s\" errno=%d (%s)",
                      filepath, bakpath, err, strerror(err));
        remove(bakpath);
        return;
    }
    fprintf(stderr, "backup: %s -> %s\n", filepath, bakpath);
}

static void derive_path_with_ext(const char *src, const char *ext, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!src || !src[0]) return;
    snprintf(out, outsz, "%s", src);
    char *slash1 = strrchr(out, '\\');
    char *slash2 = strrchr(out, '/');
    char *base = slash1 > slash2 ? slash1 : slash2;
    base = base ? base + 1 : out;
    char *dot = strrchr(base, '.');
    if (!dot) {
        size_t len = strlen(out);
        snprintf(out + len, outsz > len ? outsz - len : 0, "%s", ext);
    } else {
        snprintf(dot, outsz - (size_t)(dot - out), "%s", ext);
    }
}

static int project_has_bdb_save_data(void)
{
    return g_have_bdb || g_no > 0 || g_bdb_header[0] || g_bdb_num_modules > 0;
}

void set_project_save_paths_from_any(const char *path)
{
    if (!path || !path[0]) return;
    size_t len = strlen(path);
    const char *ext = (len >= 4) ? path + len - 4 : "";
    if (strcasecmp(ext, ".bdd") == 0) {
        snprintf(g_bdd_path, sizeof g_bdd_path, "%s", path);
        if (project_has_bdb_save_data())
            derive_path_with_ext(path, ".BDB", g_bdb_path, sizeof g_bdb_path);
        else
            g_bdb_path[0] = '\0';
    } else {
        snprintf(g_bdb_path, sizeof g_bdb_path, "%s", path);
        derive_path_with_ext(path, ".BDD", g_bdd_path, sizeof g_bdd_path);
    }
}

static void ensure_companion_save_paths(void)
{
    if (g_bdb_path[0] && !g_bdd_path[0])
        derive_path_with_ext(g_bdb_path, ".BDD", g_bdd_path, sizeof g_bdd_path);
    if (project_has_bdb_save_data() && g_bdd_path[0] && !g_bdb_path[0])
        derive_path_with_ext(g_bdd_path, ".BDB", g_bdb_path, sizeof g_bdb_path);
}

void ensure_bdb_header_for_save(void)
{
    if (g_bdb_header[0]) {
        g_have_bdb = 1;
        return;
    }

    char nm[64] = "UNTITLED";
    if (g_name[0]) {
        snprintf(nm, sizeof nm, "%s", g_name);
    } else {
        const char *path = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
        const char *base = path;
        for (const char *p = path; p && *p; p++)
            if (*p == '\\' || *p == '/') base = p + 1;
        if (base && base[0]) {
            snprintf(nm, sizeof nm, "%s", base);
            char *dot = strrchr(nm, '.');
            if (dot) *dot = '\0';
        }
    }
    if (!nm[0]) snprintf(nm, sizeof nm, "UNTITLED");
    nm[8] = '\0';

    int x0 = INT_MAX, x1 = INT_MIN, y0 = INT_MAX, y1 = INT_MIN;
    int ww = 1024, wh = 256;
    bdd_get_world_bounds(&x0, &x1, &y0, &y1);
    if (x0 != INT_MAX && x1 > x0) ww = x1 - x0;
    if (y0 != INT_MAX && y1 > y0) wh = y1 - y0;
    if (ww < 400) ww = 400;
    if (wh < 254) wh = 254;

    snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d 255 %d %d %d",
             nm, ww, wh, g_bdb_num_modules, g_n_pals, g_no);
    g_have_bdb = 1;
    snprintf(g_name, sizeof g_name, "%s", nm);
}

bool save_all_project(void)
{
    ensure_companion_save_paths();
    if (project_has_bdb_save_data() && !g_bdb_header[0])
        ensure_bdb_header_for_save();
    sync_bdb_header_counts();
    int saved_bdb = 0;
    int saved_bdd = 0;
    int want_bdb = (project_has_bdb_save_data() && g_bdb_path[0]);
    int want_bdd = (g_bdd_path[0] != '\0');
    bdd_clear_last_save_error();
    if (want_bdb) {
        backup_to_tmp(g_bdb_path);
        saved_bdb = bdb_save(g_bdb_path);
    }
    if (want_bdd) {
        backup_to_tmp(g_bdd_path);
        saved_bdd = bdd_save();
    }
    if ((!want_bdb || saved_bdb) && (!want_bdd || saved_bdd)) {
        g_dirty = 0;
        if (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
            doc_save(g_cur_doc);
        snprintf(g_toast_msg, sizeof g_toast_msg,
                 saved_bdb && saved_bdd ? "Saved BDB + BDD" :
                 saved_bdb ? "Saved BDB" : "Saved BDD");
        g_toast_timer = 2.0f;
        if (g_mk2_palette_sync_dirty && g_mk2_palette_prompt_after_save) {
            if (!g_mk2_palette_auto_sync_on_save ||
                !mk2_palette_sync_auto_apply_if_ready("Saved BDD palettes"))
                mk2_palette_sync_request_prompt("Saved BDD palettes");
        }
        mk2_lod_stale_check_after_save();
        return true;
    } else if (want_bdb || want_bdd) {
        char detail[1024] = "";
        snprintf(detail, sizeof detail, "%s", bdd_last_save_error());
        bdd_save_logf("Save All failed: want_bdb=%d saved_bdb=%d bdb=\"%s\" want_bdd=%d saved_bdd=%d bdd=\"%s\"",
                      want_bdb, saved_bdb, g_bdb_path,
                      want_bdd, saved_bdd, g_bdd_path);
        snprintf(g_toast_msg, sizeof g_toast_msg, "Save failed: %s%s%s; see save_errors.log",
                 want_bdb && !saved_bdb ? "BDB" : "",
                 (want_bdb && !saved_bdb && want_bdd && !saved_bdd) ? " + " : "",
                 want_bdd && !saved_bdd ? "BDD" : "");
        g_toast_timer = 3.0f;
        open_save_error_popup(detail[0] ? detail : bdd_last_save_error());
    }
    return false;
}

bool save_all_dirty_documents(void)
{
    int original = g_cur_doc;
    if (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
        doc_save(g_cur_doc);

    if (g_num_docs <= 0)
        return save_all_project();

    for (int i = 0; i < g_num_docs; i++) {
        if (!g_docs[i].loaded || !g_docs[i].dirty) continue;
        doc_restore(i);
        if (!save_all_project()) {
            if (original >= 0 && original < g_num_docs)
                doc_restore(original);
            return false;
        }
        doc_save(i);
    }

    if (original >= 0 && original < g_num_docs)
        doc_restore(original);
    return true;
}
