#include "UI/sdl_drop_file.h"

#include "bg_editor.h"
#include "UI/sdl_stage_open.h"

#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void bdd_sdl_handle_drop_file(const char *path,
                              int hover_x, int hover_y,
                              int zoom,
                              char *bdb_path, size_t bdb_sz,
                              char *bdd_path, size_t bdd_sz,
                              int fallback_w, int fallback_h,
                              int *wx_min, int *wx_max,
                              int *wy_min, int *wy_max,
                              int *view_x, int *view_y,
                              Uint64 *texture_sig)
{
    size_t len;
    const char *ext;

    if (!path || !path[0])
        return;

    len = strlen(path);
    ext = (len >= 4) ? path + len - 4 : "";
    if (strcasecmp(ext, ".png") == 0) {
        bg_editor_import_png(path);
        if (view_x && view_y && zoom > 0) {
            int wx = 0, wy = 0;
            bdd_screen_to_world(hover_x, hover_y, *view_x, *view_y, zoom, &wx, &wy);
            bg_editor_place_last_import(wx, wy);
        }
        return;
    }
    if (strcasecmp(ext, ".img") == 0) {
        bg_editor_import_img(path);
        return;
    }
    if (strcasecmp(ext, ".lod") == 0) {
        bg_editor_import_lod(path);
        return;
    }

    bdd_sdl_open_stage_file(path,
                            bdb_path, bdb_sz,
                            bdd_path, bdd_sz,
                            fallback_w, fallback_h,
                            wx_min, wx_max, wy_min, wy_max,
                            view_x, view_y,
                            texture_sig);
}
