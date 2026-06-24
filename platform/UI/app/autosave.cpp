#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/path_utils.h"
#include "Core/stage_paths.h"
#include "Core/viewer_save.h"
#include "UI/app/autosave.h"

#include <cstdio>
#include <cstring>

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
    char bak[768];
    if (g_have_bdb && bddtool_backup_path(bak, sizeof bak, g_bdb_path, ".autobak", "autosave"))
        bdb_save(bak);
    if (g_ni > 0) {
        char old[512];
        memcpy(old, g_bdd_path, sizeof old);
        if (bddtool_backup_path(bak, sizeof bak, g_bdd_path, ".autobak", "autosave")) {
            snprintf(g_bdd_path, sizeof g_bdd_path, "%s", bak);
            bdd_save();
        }
        memcpy(g_bdd_path, old, sizeof g_bdd_path);
    }
    fprintf(stderr, "auto-save: backed up to backups\\autosave\\*.autobak\n");
}
