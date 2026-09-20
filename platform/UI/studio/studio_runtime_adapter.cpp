#include "UI/studio/studio_app.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/viewer_stage_io.h"
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace studio {
// Migration boundary: reuse the existing assembly interpretation once on open.
// The studio's live state and renderer never use the legacy object arrays or BLKS tables.
void read_runtime_defaults(Document &document) {
    if (document.has_layout() || !document.notice().empty() || !document.state().has_bdb ||
        document.path().empty())
        return;
    namespace fs = std::filesystem;
    const fs::path input = fs::u8path(document.path());
    fs::path draft = input.parent_path() / (document.state().name + ".BGND.ASM");
    fs::path root = input.parent_path().parent_path();
    // No machine-specific fallback to an unrelated checkout. A stage must carry a draft
    // or live under a tree with its own runtime source.
    if (!fs::exists(draft) && !fs::exists(root / "src" / "BGND.ASM") &&
        !fs::exists(root / "src-refactor" / "src" / "BGND.ASM"))
        return;
    g_pref_autoload_runtime_extras = false;
    g_runtime_autoload_pref_loaded = true;
    g_stage_start_bgnd_path[0] = '\0';
    g_stage_internal_name[0] = '\0';
    std::snprintf(g_stage_mk2_root, sizeof g_stage_mk2_root, "%s", root.u8string().c_str());
    char bdb[512] = {}, bdd[512] = {};
    if (!bdd_viewer_load_stage_for_path(document.path().c_str(), bdb, sizeof bdb, bdd, sizeof bdd))
        return;
    bdd_invalidate_stage_module_cache();
    int x = 0, y = 0, ground = 230;
    bdd_get_stage_start_camera(&x, &y);
    bdd_get_stage_ground_y(&ground);
    std::vector<Plane> planes;
    for (int i = 0; i < bdd_stage_plane_count(); i++) {
        Plane p;
        float scroll = 1;
        if (!bdd_stage_plane_info(i, p.source.name, sizeof p.source.name, &p.x, &p.y, &scroll,
                                  &p.rank))
            continue;
        size_t n = std::strlen(p.source.name);
        if (n >= 4 && std::strcmp(p.source.name + n - 4, "BMOD") == 0)
            p.source.name[n - 4] = '\0';
        // Normalize each runtime scroll origin into the studio's common stage coordinates.
        // runtime camera projection: origin + (camera - start) * scroll.
        int scroll_origin = x;
        bdd_stage_plane_scroll_origin(i, &scroll_origin);
        p.x += (int)std::round(x * scroll) - scroll_origin;
        // LOAD2 normalizes blocks to their tight artwork bounds, not the authored rectangle.
        const auto &state = document.state();
        for (size_t plane = 0; plane < state.planes.size(); plane++) {
            if (std::strcmp(state.planes[plane].source.name, p.source.name) != 0)
                continue;
            int minx = 0, miny = 0;
            bool found = false;
            for (const auto &object : state.objects)
                if (object.plane == (int)plane) {
                    if (!found) {
                        minx = object.object.depth;
                        miny = object.object.sy;
                        found = true;
                    } else {
                        minx = std::min(minx, object.object.depth);
                        miny = std::min(miny, object.object.sy);
                    }
                }
            if (found) {
                p.x -= minx - state.planes[plane].source.x1;
                p.y -= miny - state.planes[plane].source.y1;
            }
        }
        p.scroll = scroll;
        p.bound = true;
        planes.push_back(p);
    }
    document.seed_runtime(planes, x, y, ground);
}
} // namespace studio
