#include "bg_editor.h"
#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>
static void enter_edit_layout_after_stage_load(void)
{
    if (!g_have_bdb || g_no <= 0) {
        g_game_view = 0;
        return;
    }
    runtime_guides_clear_session();
    g_game_view = 0;
    g_split_view = 0;
    g_runtime_layout_view = 1;
    g_show_images = true;
    g_show_obj_properties = true;
    g_show_modules = false;
    if (g_hl_obj < 0 && g_no > 0) {
        editor_project_clear_selection();
        g_hl_obj = 0;
        g_sel_flags[0] = 1;
    }
    bdd_center_game_preview_camera();
    g_anim_playing = false;
    g_anim_v_sweep = false;
}

void open_stage_path_now(const char *path)
{
    if (!path || !path[0]) return;
    doc_add();
    char bdd[512] = {0}, bdb[512] = {0};
    derive_stage_pair_paths(path, bdd, sizeof bdd, bdb, sizeof bdb);

    FILE *f = fopen(bdd, "rb");
    if (!f) {
        snprintf(bdd, sizeof bdd, "%s", path);
        f = fopen(bdd, "rb");
    }
    if (f) { fclose(f); bdd_load(bdd); }

    f = fopen(bdb, "r");
    if (f) { fclose(f); g_have_bdb = bdb_load(bdb); }

    if (g_ni == 0 && g_no == 0) {
        f = fopen(path, "rb");
        if (f) { fclose(f); bdd_load(path); }
    }

    enter_edit_layout_after_stage_load();
    g_need_rebuild = 1;
    if (g_cur_doc >= 0) doc_save(g_cur_doc);
    recent_add(path);
    recent_save();
}

void open_mk2_tool(int tool)
{
    g_simple_mode = false;
    g_show_mk2_workflow = true;
    g_mk2_focus_tool = tool;
    switch (tool) {
    case 1:
        g_mk2_workflow_section = 2;
        g_show_mk2_stage_kit = true;
        break;
    case 2:
    case 3:
        g_mk2_workflow_section = 1;
        break;
    case 4:
    case 5:
    case 6:
        g_mk2_workflow_section = 3;
        break;
    case 7:
    case 8:
    case 9:
        g_mk2_workflow_section = 4;
        break;
    case 10:
        g_mk2_workflow_section = 0;
        break;
    default:
        g_mk2_workflow_section = g_have_bdb ? 3 : 0;
        break;
    }
    settings_save();
}

