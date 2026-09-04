#include "UI/actions/module_runtime_promote.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/world_module_utils.h"
#include "UI/view/toast_notifications.h"

#include <cstdio>
#include <cstring>

bool module_runtime_info(int module_idx, ModuleRuntimeInfo *out)
{
    ModuleRuntimeInfo info;
    std::memset(&info, 0, sizeof info);

    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (module_idx >= 0 && module_idx < g_bdb_num_modules &&
        parse_module_bounds(module_idx, info.name, &x1, &x2, &y1, &y2) &&
        info.name[0]) {
        info.valid = true;
        info.builder_x = x1;
        info.builder_y = y1;

        int ox = 0, oy = 0;
        if (bdd_module_runtime_placement(module_idx, &ox, &oy, NULL, NULL)) {
            info.placed = true;
            info.runtime_x = ox;
            info.runtime_y = oy;
            info.drift_x = ox - x1;
            info.drift_y = oy - y1;
        }
    }

    if (out) *out = info;
    return info.valid;
}

bool module_runtime_in_sync(int module_idx)
{
    ModuleRuntimeInfo info;
    if (!module_runtime_info(module_idx, &info) || !info.placed)
        return false;
    return info.drift_x == 0 && info.drift_y == 0;
}

bool module_promote_position_to_runtime(int module_idx)
{
    ModuleRuntimeInfo info;
    char msg[192];

    if (!module_runtime_info(module_idx, &info)) {
        stage_set_toast("No module to promote");
        return false;
    }

    if (info.placed && info.drift_x == 0 && info.drift_y == 0) {
        snprintf(msg, sizeof msg, "%s runtime placement already matches %d,%d",
                 info.name, info.builder_x, info.builder_y);
        stage_set_toast(msg);
        return true;
    }

    bool ok;
    if (info.placed)
        ok = stage_bgnd_set_module_offset(info.name, info.builder_x, info.builder_y);
    else
        ok = stage_bgnd_create_module_placement(info.name, info.builder_x, info.builder_y);

    if (!ok) {
        /* stage_bgnd_* already put the reason in g_stage_start_status. */
        snprintf(msg, sizeof msg, "Could not promote %s -- see Modules > Runtime", info.name);
        stage_set_toast(msg);
        return false;
    }

    bdd_invalidate_stage_module_cache();
    g_view_changed = 1;
    snprintf(msg, sizeof msg, "%s %s runtime placement %d,%d",
             info.name, info.placed ? "moved to" : "placed at",
             info.builder_x, info.builder_y);
    stage_set_toast(msg);
    return true;
}

int module_runtime_promote_pending(int *out_drifted, int *out_not_placed)
{
    int drifted = 0;
    int not_placed = 0;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        ModuleRuntimeInfo info;
        if (!module_runtime_info(m, &info)) continue;
        if (!info.placed) not_placed++;
        else if (info.drift_x != 0 || info.drift_y != 0) drifted++;
    }

    if (out_drifted) *out_drifted = drifted;
    if (out_not_placed) *out_not_placed = not_placed;
    return drifted + not_placed;
}

int module_promote_all_positions_to_runtime(bool include_unplaced,
                                            int *out_created, int *out_updated,
                                            int *out_failed)
{
    int created = 0, updated = 0, failed = 0;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        ModuleRuntimeInfo info;
        if (!module_runtime_info(m, &info)) continue;
        if (!info.placed && !include_unplaced) continue;
        if (info.placed && info.drift_x == 0 && info.drift_y == 0) continue;

        bool was_placed = info.placed;
        bool ok = was_placed
            ? stage_bgnd_set_module_offset(info.name, info.builder_x, info.builder_y)
            : stage_bgnd_create_module_placement(info.name, info.builder_x, info.builder_y);
        if (!ok) {
            failed++;
            continue;
        }
        /* Each write rewrites BGND.ASM, so the parsed plane table has to be
           dropped before the next module is read back or the loop keeps seeing
           pre-edit offsets and re-promotes work it already did. */
        bdd_invalidate_stage_module_cache();
        if (was_placed) updated++;
        else created++;
    }

    int changed = created + updated;
    if (changed > 0)
        g_view_changed = 1;

    char msg[192];
    if (changed == 0 && failed == 0)
        snprintf(msg, sizeof msg, "Runtime placements already match the builder grid");
    else if (failed > 0)
        snprintf(msg, sizeof msg, "Promoted %d module(s) (%d new, %d moved); %d failed",
                 changed, created, updated, failed);
    else
        snprintf(msg, sizeof msg, "Promoted %d module(s) to runtime (%d new, %d moved)",
                 changed, created, updated);
    stage_set_toast(msg);

    if (out_created) *out_created = created;
    if (out_updated) *out_updated = updated;
    if (out_failed) *out_failed = failed;
    return changed;
}
