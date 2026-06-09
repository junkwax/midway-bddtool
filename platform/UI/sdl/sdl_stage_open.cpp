#include "UI/sdl/sdl_stage_open.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/viewer_stage_io.h"
#include "UI/sdl/sdl_object_picker.h"
#include "UI/assets/texture_cache.h"

#include <climits>
#include <cstdio>

int bdd_sdl_open_stage_file(const char *path,
                            char *bdb_path, size_t bdb_sz,
                            char *bdd_path, size_t bdd_sz,
                            int fallback_w, int fallback_h,
                            int *wx_min, int *wx_max,
                            int *wy_min, int *wy_max,
                            int *view_x, int *view_y,
                            Uint64 *texture_sig)
{
    int x0 = 0, x1 = fallback_w, y0 = 0, y1 = fallback_h;

    if (!path || !path[0] || !bdb_path || !bdd_path)
        return 0;

    bdd_object_picker_free_labels();
    bdd_object_picker_close();
    bdd_object_picker_cancel_place();
    bdd_texture_cache_destroy();

    if (!bdd_viewer_load_stage_for_path(path, bdb_path, bdb_sz, bdd_path, bdd_sz))
        return 0;
    runtime_actor_autoload_for_stage();

    if (g_runtime_layout_view)
        bdd_get_runtime_layout_bounds(&x0, &x1, &y0, &y1);
    else
        bdd_get_world_bounds(&x0, &x1, &y0, &y1);
    if (x0 == INT_MAX) {
        x0 = 0;
        x1 = fallback_w;
        y0 = 0;
        y1 = fallback_h;
    }

    if (!bdd_texture_cache_rebuild_all()) {
        fprintf(stderr, "out of memory\n");
        return 0;
    }

    if (wx_min) *wx_min = x0;
    if (wx_max) *wx_max = x1;
    if (wy_min) *wy_min = y0;
    if (wy_max) *wy_max = y1;
    if (view_x) *view_x = x0;
    if (view_y) *view_y = y0;
    if (texture_sig) *texture_sig = bdd_texture_cache_signature();
    return 1;
}
