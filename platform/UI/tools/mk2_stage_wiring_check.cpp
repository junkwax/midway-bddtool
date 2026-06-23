#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/img_format.h"
#include "Core/world_module_utils.h"
#include "undo_manager.h"

#include "imgui.h"

#include <stdio.h>
#include <string.h>
#include <vector>

/* One row: status, name, detail text, and an optional one-click Fix action.
 * Mirrors the Stage Readiness Gate's row format but adds the inline Fix
 * button the gate doesn't have, for checks that have a safe automatic fix. */
static void draw_check_row(const char *name, bool pass, const char *detail,
                           void (*fix)(void), const char *fix_label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(pass ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                       pass ? "OK" : "FIX");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", detail);
    ImGui::TableNextColumn();
    if (!pass && fix) {
        ImGui::PushID(name);
        if (ImGui::SmallButton(fix_label ? fix_label : "Fix"))
            fix();
        ImGui::PopID();
    }
}

static void fix_grow_world_to_fit(void)
{
    char nm[64] = ""; int ww = 0, wh = 0, md = 255, nmods = 0, npals = 0, nobj = 0;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &ww, &wh, &md, &nmods, &npals, &nobj) < 7)
        return;

    int ox0 = 0, ox1 = 0, oy0 = 0, oy1 = 0;
    bdd_get_world_bounds(&ox0, &ox1, &oy0, &oy1);
    int max_x = ox1, max_y = oy1;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        if (!parse_module_bounds(m, NULL, &mx1, &mx2, &my1, &my2)) continue;
        if (mx2 > max_x) max_x = mx2;
        if (my2 > max_y) max_y = my2;
    }
    int new_w = max_x > ww ? max_x : ww;
    int new_h = max_y > wh ? max_y : wh;
    if (new_w < 400) new_w = 400;
    if (new_h < 254) new_h = 254;
    if (new_w == ww && new_h == wh) return;

    undo_save_ex("Grow World To Fit");
    snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
             nm, new_w, new_h, md, g_bdb_num_modules, g_n_pals, g_no);
    g_dirty = 1;
    stage_set_toast("Grew world to fit objects and modules");
}

static void fix_open_palette_grouper(void)
{
    mk2_workflow_show_optimize_section();
    stage_set_toast("Open Smart Palette Grouper below to merge palettes down to 16");
}

static void fix_delete_unused_images(void)
{
    int n = delete_unused_images_impl(false, "Delete Unused Images");
    char msg[64]; snprintf(msg, sizeof msg, "Deleted %d unused image(s)", n);
    stage_set_toast(msg);
}

static void fix_remove_unused_palettes(void)
{
    int n = remove_unused_palettes_impl(true);
    char msg[64]; snprintf(msg, sizeof msg, "Removed %d unused palette(s)", n);
    stage_set_toast(msg);
}

static void fix_rename_duplicate_modules(void)
{
    undo_save_ex("Rename Duplicate Modules");
    int renamed = 0;
    for (int m = 1; m < g_bdb_num_modules; m++) {
        char mn[64] = ""; int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, mn, &x1, &x2, &y1, &y2) || !mn[0]) continue;
        if (!module_name_in_use(mn, m)) continue;  /* unique among the others already */
        char base[64]; snprintf(base, sizeof base, "%s", mn);
        char unique[64];
        module_generate_unique_name(unique, sizeof unique, base);
        char line[256];
        snprintf(line, sizeof line, "%s %d %d %d %d", unique, x1, x2, y1, y2);
        if (editor_project_set_module_line(m, line)) renamed++;
    }
    sync_bdb_header_counts();
    g_dirty = 1;
    char msg[64]; snprintf(msg, sizeof msg, "Renamed %d duplicate module name(s)", renamed);
    stage_set_toast(msg);
}

static void fix_rename_to_filename(void)
{
    if (!g_bdb_path[0]) return;
    char nm[64] = "";
    img_basename_no_ext_upper(g_bdb_path, nm, sizeof nm);
    if (!nm[0]) return;

    char dummy[64] = ""; int ww = 0, wh = 0, md = 255, nmods = 0, npals = 0, nobj = 0;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               dummy, &ww, &wh, &md, &nmods, &npals, &nobj) < 7)
        return;

    undo_save_ex("Rename Stage");
    snprintf(g_name, sizeof g_name, "%s", nm);
    snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
             nm, ww, wh, md, g_bdb_num_modules, g_n_pals, g_no);
    g_dirty = 1;
    char msg[96]; snprintf(msg, sizeof msg, "Renamed stage to %s", nm);
    stage_set_toast(msg);
}

void draw_mk2_play_readiness_checklist(void)
{
    ImGui::Text("Play Readiness Checklist");
    ImGui::TextDisabled("Checks beyond LOAD2/ROM budget: hardware palette limit, world bounds,\n"
                        "unused art, and whether BGND.ASM even knows this stage exists.");

    if (!g_have_bdb || g_no == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "No drawable BDB/BDD loaded.");
        return;
    }

    Mk2Diag d;
    mk2_collect_diag(&d);
    Mk2Budget b = mk2_collect_budget();

    bool wired = bdd_stage_plane_count() > 0;

    int ox0 = 0, ox1 = 0, oy0 = 0, oy1 = 0;
    bdd_get_world_bounds(&ox0, &ox1, &oy0, &oy1);
    char nm[64] = ""; int ww = 0, wh = 0, md = 255, nmods = 0, npals = 0, nobj = 0;
    sscanf(g_bdb_header, "%63s %d %d %d %d %d %d", nm, &ww, &wh, &md, &nmods, &npals, &nobj);
    int max_x = ox1, max_y = oy1;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        if (!parse_module_bounds(m, NULL, &mx1, &mx2, &my1, &my2)) continue;
        if (mx2 > max_x) max_x = mx2;
        if (my2 > max_y) max_y = my2;
    }
    bool bounds_ok = max_x <= ww && max_y <= wh;

    int unused_pals = 0;
    {
        std::vector<unsigned char> used((size_t)(g_n_pals > 0 ? g_n_pals : 1), 0);
        for (int i = 0; i < g_ni; i++)
            if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals) used[(size_t)g_img[i].pal_idx] = 1;
        for (int i = 0; i < g_no; i++)
            if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals) used[(size_t)g_obj[i].fl] = 1;
        for (int i = 0; i < g_n_pals; i++) if (!used[(size_t)i]) unused_pals++;
    }

    bool name_is_template_default = false;
    for (int i = 0; i < g_num_templates; i++)
        if (strcmp(g_name, g_templates[i].name) == 0) { name_is_template_default = true; break; }

    int dup_modules = 0;
    char first_dup[64] = "";
    for (int m = 1; m < g_bdb_num_modules; m++) {
        char mn[64] = "";
        if (sscanf(g_bdb_modules[m], "%63s", mn) != 1 || !mn[0]) continue;
        if (module_name_in_use(mn, m)) {
            dup_modules++;
            if (!first_dup[0]) snprintf(first_dup, sizeof first_dup, "%s", mn);
        }
    }

    if (ImGui::BeginTable("play_readiness", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("check", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("detail");
        ImGui::TableSetupColumn("fix", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        char detail[200];

        snprintf(detail, sizeof detail,
                 wired ? "Found a matching <stage>_mod plane table in BGND.ASM."
                       : "No <stage>_mod block in BGND.ASM references \"%s\" -- the game has no way to load this stage yet. This needs a manual BGND.ASM/MKSEL.ASM edit.",
                 g_name);
        draw_check_row("BGND.ASM wiring", wired, detail, nullptr, nullptr);

        snprintf(detail, sizeof detail, "%d distinct palette(s) used; MK2 hardware holds %d simultaneously.",
                 d.runtime_palette_count, MK2_RUNTIME_PALETTE_SLOTS);
        draw_check_row("Palette budget", d.runtime_palette16_pressure == 0, detail,
                      fix_open_palette_grouper, "Merge...");

        snprintf(detail, sizeof detail,
                 "Objects/modules reach (%d, %d); world is declared %d x %d.",
                 max_x, max_y, ww, wh);
        draw_check_row("World bounds", bounds_ok, detail, fix_grow_world_to_fit, "Grow world");

        snprintf(detail, sizeof detail,
                 dup_modules > 0
                     ? "%d module(s) share a name with another (e.g. \"%s\") -- breaks module-drag and BGND.ASM BMOD matching for one of them."
                     : "Every module has a unique name.",
                 dup_modules, first_dup);
        draw_check_row("Unique module names", dup_modules == 0, detail,
                      fix_rename_duplicate_modules, "Rename");

        snprintf(detail, sizeof detail, "%d of %d images are never placed by an object.",
                 b.unused_images, g_ni);
        draw_check_row("Unused images", b.unused_images == 0, detail,
                      fix_delete_unused_images, "Delete");

        snprintf(detail, sizeof detail, "%d of %d palettes are never referenced by an image or object.",
                 unused_pals, g_n_pals);
        draw_check_row("Unused palettes", unused_pals == 0, detail,
                      fix_remove_unused_palettes, "Remove");

        snprintf(detail, sizeof detail,
                 name_is_template_default
                     ? "Stage name \"%s\" is still a template default, not renamed for this stage."
                     : "Stage name \"%s\" looks like it was set deliberately.",
                 g_name);
        draw_check_row("Stage name", !name_is_template_default, detail,
                      fix_rename_to_filename, "Rename");

        ImGui::EndTable();
    }
}
