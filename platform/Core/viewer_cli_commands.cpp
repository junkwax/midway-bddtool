#include "Core/viewer_cli_commands.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "Core/viewer_stage_io.h"
#include "Core/viewer_test_levels.h"
#include "UI/tools/mk2_runtime_actor_tool.h"

#include <cstdio>
#include <cstring>

static bool cli_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            char a = h[i];
            char b = needle[i];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static bool cli_equals_ci(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool runtime_smoke_stage_contains(const char *needle)
{
    return cli_contains_ci(g_name, needle) ||
           cli_contains_ci(g_bdb_path, needle) ||
           cli_contains_ci(g_bdd_path, needle) ||
           cli_contains_ci(g_stage_internal_name, needle);
}

static const char *runtime_smoke_expected_floor(void)
{
    if (runtime_smoke_stage_contains("FOREST")) return "FL_FORST";
    if (runtime_smoke_stage_contains("BATTLE")) return "FL_BATTL";
    if (runtime_smoke_stage_contains("TOWER2") ||
        runtime_smoke_stage_contains("TWGCLOUD"))
        return "FL_TOW";
    return NULL;
}

static const char *runtime_smoke_expected_actor(void)
{
    if (runtime_smoke_stage_contains("FOREST")) return "forest_tree_face";
    if (runtime_smoke_stage_contains("TOWER2") ||
        runtime_smoke_stage_contains("TWGCLOUD"))
        return "FLAME";
    return NULL;
}

static const char *runtime_smoke_forbidden_actor(void)
{
    if (runtime_smoke_stage_contains("FOREST")) return "FLAME";
    if (runtime_smoke_stage_contains("TOWER2") ||
        runtime_smoke_stage_contains("TWGCLOUD"))
        return "forest_tree_face";
    return NULL;
}

static bool runtime_smoke_float_mismatch(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d > 0.001f;
}

struct RuntimeSmokeModuleProjection {
    const char *module;
    int ox;
    int oy;
    float scroll;
};

static const RuntimeSmokeModuleProjection *runtime_smoke_expected_modules(int *out_count)
{
    static const RuntimeSmokeModuleProjection tower[] = {
        { "PLANE4", 0x16d, -0x1f, 0.6875f },
        { "PLANE5", 0,     -0x21, 0.625f  },
    };
    static const RuntimeSmokeModuleProjection forest[] = {
        { "wood1", 0,   -0x36, 1.0f    },
        { "wood2", 135, -0x1a, 0.75f   },
        { "wood4", 111,  0x14, 0.625f  },
        { "wood5", 215,  0x21, 0.5f    },
        { "wood6", 281,  0x3a, 0.3125f },
        { "wood7", 378,  0x52, 0.0f    },
    };

    if (out_count) *out_count = 0;
    if (runtime_smoke_stage_contains("TOWER2") ||
        runtime_smoke_stage_contains("TWGCLOUD")) {
        if (out_count) *out_count = (int)(sizeof tower / sizeof tower[0]);
        return tower;
    }
    if (runtime_smoke_stage_contains("FOREST")) {
        if (out_count) *out_count = (int)(sizeof forest / sizeof forest[0]);
        return forest;
    }
    return NULL;
}

static bool runtime_smoke_find_module(const char *name, BddCoreModule *out_module)
{
    if (!name || !out_module) return false;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        BddCoreModule module = {};
        if (!bdd_core_parse_module_line(g_bdb_modules[m], &module))
            continue;
        if (cli_equals_ci(module.name, name)) {
            *out_module = module;
            return true;
        }
    }
    return false;
}

static int runtime_smoke_find_object_in_module(const BddCoreModule *module)
{
    if (!module) return -1;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im || im->w <= 0 || im->h <= 0)
            continue;
        if (g_obj[i].depth >= module->x1 &&
            g_obj[i].sy >= module->y1 &&
            g_obj[i].depth + im->w - 1 <= module->x2 &&
            g_obj[i].sy + im->h - 1 <= module->y2)
            return i;
    }
    return -1;
}

static int runtime_smoke_check_module_projection(const char *path,
                                                 int *out_checked)
{
    int errors = 0;
    int checked = 0;
    int expect_count = 0;
    const RuntimeSmokeModuleProjection *expect =
        runtime_smoke_expected_modules(&expect_count);

    if (!expect || expect_count <= 0) {
        if (out_checked) *out_checked = 0;
        return 0;
    }

    if (!g_runtime_layout_view) {
        fprintf(stderr,
                "runtime-preview-smoke: %s expected runtime layout view for module projection\n",
                path);
        errors++;
    }

    for (int i = 0; i < expect_count; i++) {
        BddCoreModule module = {};
        if (!runtime_smoke_find_module(expect[i].module, &module)) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s missing expected module %s\n",
                    path, expect[i].module);
            errors++;
            continue;
        }

        int obj_i = runtime_smoke_find_object_in_module(&module);
        if (obj_i < 0) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s no object found inside module %s\n",
                    path, expect[i].module);
            errors++;
            continue;
        }

        int game_x = 0;
        int game_y = 0;
        bdd_object_game_origin(obj_i, &game_x, &game_y);
        float game_scroll = bdd_object_game_scroll_factor(obj_i);
        int expected_x = expect[i].ox + (g_obj[obj_i].depth - module.x1);
        int expected_y = expect[i].oy + (g_obj[obj_i].sy - module.y1);
        if (game_x != expected_x || game_y != expected_y ||
            runtime_smoke_float_mismatch(game_scroll, expect[i].scroll)) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s module %s projection mismatch obj=%d got=(%d,%d %.4f) expected=(%d,%d %.4f)\n",
                    path, expect[i].module, obj_i,
                    game_x, game_y, game_scroll,
                    expected_x, expected_y, expect[i].scroll);
            errors++;
            continue;
        }
        checked++;
    }

    if (out_checked) *out_checked = checked;
    return errors;
}

static int runtime_smoke_check_floors(const char *path,
                                      const char *expected_floor,
                                      int *out_floor_guides,
                                      int *out_floor_aligned)
{
    int errors = 0;
    int floor_guides = 0;
    int floor_aligned = 0;
    int found_expected = expected_floor ? 0 : 1;

    tower_runtime_guides_init_once();
    for (int i = 0; i < tower_runtime_guide_count(); i++) {
        const RuntimeExtraGuide *e = &g_tower_runtime_guides[i];
        if (!cli_contains_ci(e->asset, "FL_") &&
            !cli_contains_ci(e->label, "floor"))
            continue;

        floor_guides++;
        if (expected_floor && cli_equals_ci(e->asset, expected_floor)) {
            found_expected = 1;
        } else if (expected_floor) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s unexpected floor guide %s, expected %s\n",
                    path, e->asset, expected_floor);
            errors++;
        }

        int obj_i = runtime_guide_existing_object_for_index(i);
        if (obj_i < 0) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s missing baked floor object for %s\n",
                    path, e->asset);
            errors++;
            continue;
        }

        Img *im = img_find(g_obj[obj_i].ii);
        int expected_x = e->x;
        int expected_y = e->y;
        int expected_w = e->w;
        int expected_h = e->h;
        runtime_guide_visual_rect(e, im, &expected_x, &expected_y,
                                  &expected_w, &expected_h);
        if (!im || g_obj[obj_i].depth != expected_x || g_obj[obj_i].sy != expected_y) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s floor %s source mismatch obj=%d got=(%d,%d) expected=(%d,%d)\n",
                    path, e->asset, obj_i,
                    g_obj[obj_i].depth, g_obj[obj_i].sy,
                    expected_x, expected_y);
            errors++;
            continue;
        }
        if (im->w != e->w || im->h != e->h) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s floor %s size mismatch image=%dx%d expected=%dx%d\n",
                    path, e->asset, im->w, im->h, e->w, e->h);
            errors++;
            continue;
        }

        int game_x = 0;
        int game_y = 0;
        bdd_object_game_origin(obj_i, &game_x, &game_y);
        float game_scroll = bdd_object_game_scroll_factor(obj_i);
        if (expected_floor && !cli_equals_ci(expected_floor, "FL_BATTL") &&
            (game_x != expected_x || game_y != expected_y ||
             runtime_smoke_float_mismatch(game_scroll, e->scroll))) {
            fprintf(stderr,
                    "runtime-preview-smoke: %s floor %s game mismatch obj=%d got=(%d,%d %.4f) expected=(%d,%d %.4f)\n",
                    path, e->asset, obj_i, game_x, game_y, game_scroll,
                    expected_x, expected_y, e->scroll);
            errors++;
            continue;
        }
        floor_aligned++;
    }

    if (expected_floor && !found_expected) {
        fprintf(stderr,
                "runtime-preview-smoke: %s expected floor guide %s was not loaded\n",
                path, expected_floor);
        errors++;
    }
    if (expected_floor && floor_guides <= 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s no runtime floor guides loaded\n",
                path);
        errors++;
    }

    if (out_floor_guides) *out_floor_guides = floor_guides;
    if (out_floor_aligned) *out_floor_aligned = floor_aligned;
    return errors;
}

static int runtime_smoke_check_actors(const char *path,
                                      const char *expected_actor,
                                      const char *forbidden_actor,
                                      int *out_actors,
                                      int *out_frames,
                                      int *out_missing)
{
    int errors = 0;
    int actors = runtime_actor_count();
    int frames = runtime_actor_total_frame_count();
    int missing = runtime_actor_missing_frame_count();

    if (expected_actor && actors <= 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s expected runtime actors, got none\n",
                path);
        errors++;
    }
    if (expected_actor && runtime_actor_name_contains_count(expected_actor) <= 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s missing expected actor name containing %s\n",
                path, expected_actor);
        errors++;
    }
    if (forbidden_actor && runtime_actor_name_contains_count(forbidden_actor) > 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s leaked actor name containing %s\n",
                path, forbidden_actor);
        errors++;
    }
    if (missing > 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s has %d runtime actor frame(s) without a loaded image\n",
                path, missing);
        errors++;
    }
    if (expected_actor && frames <= 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s expected runtime actor frames, got none\n",
                path);
        errors++;
    }

    if (out_actors) *out_actors = actors;
    if (out_frames) *out_frames = frames;
    if (out_missing) *out_missing = missing;
    return errors;
}

static int runtime_smoke_check_start_camera(const char *path,
                                            int *out_x, int *out_y)
{
    int expected_x = 0;
    int expected_y = 0;
    if (out_x) *out_x = g_scroll_pos;
    if (out_y) *out_y = g_game_view_y;
    if (!bdd_get_stage_start_camera(&expected_x, &expected_y))
        return 0;

    if (out_x) *out_x = expected_x;
    if (out_y) *out_y = expected_y;
    if (g_scroll_pos != expected_x || g_game_view_y != expected_y) {
        fprintf(stderr,
                "runtime-preview-smoke: %s start camera mismatch got=(%d,%d) expected=(%d,%d)\n",
                path, g_scroll_pos, g_game_view_y, expected_x, expected_y);
        return 1;
    }
    return 0;
}

static int runtime_preview_smoke_one_stage(const char *path)
{
    char bdb_path[512] = "";
    char bdd_path[512] = "";
    int floor_guides = 0;
    int floor_aligned = 0;
    int actors = 0;
    int frames = 0;
    int missing = 0;
    int modules = 0;
    int start_x = 0;
    int start_y = 0;
    int errors = 0;

    runtime_guides_clear_session();
    if (!bdd_viewer_load_stage_for_path(path, bdb_path, sizeof bdb_path,
                                        bdd_path, sizeof bdd_path))
        return 1;
    runtime_actor_autoload_for_stage();

    const char *expected_floor = runtime_smoke_expected_floor();
    const char *expected_actor = runtime_smoke_expected_actor();
    const char *forbidden_actor = runtime_smoke_forbidden_actor();

    errors += runtime_smoke_check_floors(path, expected_floor,
                                        &floor_guides, &floor_aligned);
    errors += runtime_smoke_check_actors(path, expected_actor, forbidden_actor,
                                        &actors, &frames, &missing);
    errors += runtime_smoke_check_module_projection(path, &modules);
    errors += runtime_smoke_check_start_camera(path, &start_x, &start_y);

    Mk2Diag diag;
    mk2_collect_diag(&diag);
    int hard = mk2_diag_hard_issues(&diag);
    if (hard > 0) {
        fprintf(stderr,
                "runtime-preview-smoke: %s has %d hard MK2 build diagnostic issue(s)\n",
                path, hard);
        errors++;
    }

    fprintf(stderr,
            "runtime-preview-smoke: stage=%s path=%s floor=%s floor_guides=%d floor_aligned=%d actors=%d frames=%d missing_frames=%d modules=%d start=(%d,%d) preview_images=%d hard=%d objects=%d images=%d palettes=%d\n",
            g_name[0] ? g_name : "(unknown)", path,
            expected_floor ? expected_floor : "(none)",
            floor_guides, floor_aligned,
            actors, frames, missing,
            modules, start_x, start_y,
            runtime_actor_preview_import_count(), hard,
            g_no, g_ni, g_n_pals);
    return errors ? 1 : 0;
}

int bdd_viewer_run_cli_command(int argc, char **argv, int *exit_code)
{
    char bdb_path[512] = "";
    char bdd_path[512] = "";

    if (!exit_code) return 0;
    *exit_code = 0;

    if (argc >= 2 && strcmp(argv[1], "--write-checker-test") == 0) {
        const char *prefix = (argc >= 3) ? argv[2] : "CHECKER";
        *exit_code = bdd_write_checker_test_level(prefix) ? 0 : 1;
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--write-bg-proof") == 0) {
        const char *prefix = (argc >= 3) ? argv[2] : "BGPROF";
        *exit_code = bdd_write_bg_proof_level(prefix) ? 0 : 1;
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--check-open-mode") == 0) {
        const char *expected = (argc >= 4) ? argv[3] : NULL;
        const char *mode;
        if (argc < 3) {
            fprintf(stderr, "usage: bddview --check-open-mode FILE.BDB|FILE.BDD [stage-edit|game-preview|image-grid]\n");
            *exit_code = 1;
            return 1;
        }
        if (!bdd_viewer_load_stage_for_path(argv[2], bdb_path, sizeof bdb_path, bdd_path, sizeof bdd_path)) {
            *exit_code = 1;
            return 1;
        }
        mode = g_game_view ? "game-preview" : (g_have_bdb ? "stage-edit" : "image-grid");
        printf("open-mode=%s layout=%s bdb=%s bdd=%s objects=%d images=%d camera_x=%d camera_y=%d\n",
               mode, g_runtime_layout_view ? "runtime" : "source",
               g_have_bdb ? bdb_path : "(none)", bdd_path, g_no, g_ni,
               g_scroll_pos, g_game_view_y);
        if (expected && strcmp(expected, mode) != 0) {
            fprintf(stderr, "expected open-mode=%s, got %s\n", expected, mode);
            *exit_code = 2;
            return 1;
        }
        *exit_code = 0;
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--runtime-preview-smoke") == 0) {
        int rc = 0;
        if (argc < 3) {
            fprintf(stderr, "usage: bddview --runtime-preview-smoke FILE.BDB|FILE.BDD [...]\n");
            *exit_code = 1;
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            if (runtime_preview_smoke_one_stage(argv[i]) != 0)
                rc = 1;
        }
        if (rc == 0)
            fprintf(stderr, "runtime-preview-smoke=ok stages=%d\n", argc - 2);
        *exit_code = rc;
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--roundtrip-save") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: bddview --roundtrip-save FILE.BDB|FILE.BDD OUT_PREFIX\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_roundtrip_save_stage_for_path(argv[2], argv[3]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--undo-move-smoke") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: bddview --undo-move-smoke FILE.BDB|FILE.BDD\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_undo_move_smoke_for_path(argv[2]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--import-img-smoke") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: bddview --import-img-smoke FILE.IMG OUT_PREFIX\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_import_img_smoke_for_path(argv[2], argv[3]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--import-png-smoke") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: bddview --import-png-smoke FILE.PNG OUT_PREFIX\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_import_png_smoke_for_path(argv[2], argv[3]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--import-img-folder-smoke") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: bddview --import-img-folder-smoke DIR OUT_PREFIX\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_import_img_folder_smoke_for_path(argv[2], argv[3]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--import-lod-smoke") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: bddview --import-lod-smoke FILE.LOD OUT_PREFIX\n");
            *exit_code = 1;
            return 1;
        }
        *exit_code = bdd_viewer_import_lod_smoke_for_path(argv[2], argv[3]);
        return 1;
    }

    return 0;
}
