#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/path_utils.h"
#include "Core/stage_paths.h"
#include "Core/viewer_save.h"
#include "UI/autosave.h"

#include <cstdio>
#include <cstring>

static void auto_save_backup_base_dir(char *out, size_t outsz, const char *fallback_path)
{
    if (!out || outsz == 0) return;
    const char *root = g_bdd_path[0] ? g_bdd_path : fallback_path;
    snprintf(out, outsz, "%s", root ? root : "");
    char *sep = strrchr(out, '\\');
    char *slash = strrchr(out, '/');
    if (!sep || (slash && slash > sep)) sep = slash;
    if (sep) sep[1] = '\0';
    else out[0] = '\0';
}

void run_auto_save_tick(void)
{
    if (!(g_dirty && (g_have_bdb || g_ni > 0) && g_bdb_path[0] && g_pref_autosave_s > 0)) {
        g_auto_save_tick = 0;
        return;
    }

    g_auto_save_tick++;
    if (g_auto_save_tick <= g_pref_autosave_s * 60)
        return;

    g_auto_save_tick = 0;
    char bakdir[520], bakname[256], bak[768];
    auto_save_backup_base_dir(bakdir, sizeof bakdir, g_bdb_path);
    snprintf(bakname, sizeof bakname, "%s.autobak", path_basename_ptr(g_bdb_path));
    path_join(bak, sizeof bak, bakdir, bakname);
    if (g_have_bdb) bdb_save(bak);
    snprintf(bakname, sizeof bakname, "%s.autobak", path_basename_ptr(g_bdd_path));
    path_join(bak, sizeof bak, bakdir, bakname);
    if (g_ni > 0) {
        char old[512];
        memcpy(old, g_bdd_path, sizeof old);
        snprintf(g_bdd_path, sizeof g_bdd_path, "%s", bak);
        bdd_save();
        memcpy(g_bdd_path, old, sizeof g_bdd_path);
    }
    fprintf(stderr, "auto-save: backed up to *.autobak\n");
}
