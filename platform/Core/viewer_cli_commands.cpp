#include "Core/viewer_cli_commands.h"

#include "Core/editor_project_globals.h"
#include "Core/viewer_stage_io.h"
#include "Core/viewer_test_levels.h"

#include <cstdio>
#include <cstring>

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
