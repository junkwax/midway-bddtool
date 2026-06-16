#include "UI/panels/MenuBarPanel.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_commands.h"
#include "UI/sdl/sdl_object_picker.h"
#include "UI/actions/object_position_undo.h"
#include "imgui.h"
#include "undo_manager.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vector>

static int menu_bar_animation_metadata_count(void)
{
    int count = 0;
    for (int i = 0; i < g_ni; i++) {
        const Img *im = &g_img[i];
        if (runtime_actor_image_is_preview_import(im))
            continue;
        if (im->frm || im->opals || im->pttblnum ||
            im->anix || im->aniy || im->anix2 || im->aniy2 || im->aniz2)
            count++;
    }
    return count;
}

static void menu_bar_diag_tooltip(const Mk2Diag *d, int hard, int cautions)
{
    if (!d) return;
    ImGui::BeginTooltip();
    ImGui::Text("Loaded: %d objects, %d images, %d palettes", g_no, g_ni, g_n_pals);
    ImGui::Text("Runtime palettes: %d / %d", d->runtime_palette_count, MK2_RUNTIME_PALETTE_SLOTS);
    if (hard == 0 && cautions == 0) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "No build-blocking MK2 warnings detected.");
    } else {
        ImGui::Separator();
        if (d->missing_images) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "%d missing image reference(s)", d->missing_images);
        if (d->bad_palettes) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "%d bad palette reference(s)", d->bad_palettes);
        if (d->load2_oversize_images) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "%d LOAD2 oversize image(s)", d->load2_oversize_images);
        if (d->load2_palette_overflow) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "LOAD2 palette overflow +%d", d->load2_palette_overflow);
        if (d->load2_module_overflow) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "LOAD2 module overflow +%d", d->load2_module_overflow);
        if (d->load2_image_header_overflow) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "LOAD2 image header overflow +%d", d->load2_image_header_overflow);
        if (d->load2_block_table_overflow) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "LOAD2 block table overflow +%d", d->load2_block_table_overflow);
        if (d->palette_high_nibble) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "%d object(s) use palette >= 16", d->palette_high_nibble);
        if (d->runtime_palette16_pressure) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "Runtime palette pressure: %d used", d->runtime_palette_count);
        else if (d->runtime_palette_pressure) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "Dynamic palette pressure: %d used", d->runtime_palette_count);
        if (d->display_object_overflow) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "Display object overflow +%d at X %d", d->display_object_overflow, d->max_visible_objects_x);
        else if (d->display_object_pressure) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "Display object pressure: %d at X %d", d->max_visible_objects, d->max_visible_objects_x);
        if (d->high_color_images) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "%d image(s) use color index >= 64", d->high_color_images);
        if (d->unassigned_objects) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "%d object(s) outside modules", d->unassigned_objects);
        if (d->module_bound_issues) ImGui::TextColored(ImVec4(1,0.45f,0.30f,1), "%d module bound issue(s)", d->module_bound_issues);
        if (d->order_issues) ImGui::TextColored(ImVec4(1,0.75f,0.30f,1), "%d object order issue(s)", d->order_issues);
    }
    ImGui::EndTooltip();
}

static void draw_menu_bar_stage_info(void)
{
    if (!g_name[0] && g_ni <= 0) return;

    char info[256] = "";
    if (g_name[0])
        snprintf(info, sizeof info, "%s%s  |  %d objects  %d images  %d palettes",
                 g_dirty ? "* " : "", g_name, g_no, g_ni, g_n_pals);
    else
        snprintf(info, sizeof info, "%s%d images  %d palettes  (no BDB)",
                 g_dirty ? "* " : "", g_ni, g_n_pals);

    Mk2Diag d;
    mk2_collect_diag(&d);
    int hard = mk2_diag_hard_issues(&d);
    int cautions = mk2_diag_cautions(&d);
    int anim_meta = menu_bar_animation_metadata_count();

    char build[48];
    if (hard > 0)
        snprintf(build, sizeof build, "BUILD ERROR:%d", hard);
    else if (cautions > 0)
        snprintf(build, sizeof build, "BUILD WARN:%d", cautions);
    else
        snprintf(build, sizeof build, "BUILD PASS");

    char pal[32];
    snprintf(pal, sizeof pal, "PAL %d/%d", d.runtime_palette_count, MK2_RUNTIME_PALETTE_SLOTS);
    char anim[32] = "";
    if (anim_meta > 0)
        snprintf(anim, sizeof anim, "ANIM %d", anim_meta);

    const float gap = 10.0f;
    float total_w = ImGui::CalcTextSize(info).x + gap + ImGui::CalcTextSize(build).x +
                    gap + ImGui::CalcTextSize(pal).x;
    if (anim[0]) total_w += gap + ImGui::CalcTextSize(anim).x;

    float right = ImGui::GetWindowContentRegionMax().x;
    float target = right - total_w;
    if (target > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(target);

    if (g_dirty) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.8f,0.2f,1));
    ImGui::Text("%s", info);
    if (g_dirty) ImGui::PopStyleColor();

    ImGui::SameLine(0, gap);
    ImVec4 build_col = hard > 0 ? ImVec4(1.0f, 0.34f, 0.22f, 1.0f) :
                       (cautions > 0 ? ImVec4(1.0f, 0.78f, 0.30f, 1.0f) :
                                        ImVec4(0.45f, 0.95f, 0.55f, 1.0f));
    ImGui::TextColored(build_col, "%s", build);
    if (ImGui::IsItemHovered())
        menu_bar_diag_tooltip(&d, hard, cautions);

    ImGui::SameLine(0, gap);
    ImVec4 pal_col = d.runtime_palette16_pressure ? ImVec4(1.0f,0.34f,0.22f,1.0f) :
                     (d.runtime_palette_pressure ? ImVec4(1.0f,0.78f,0.30f,1.0f) :
                                                   ImVec4(0.60f,0.84f,1.0f,1.0f));
    ImGui::TextColored(pal_col, "%s", pal);
    if (ImGui::IsItemHovered())
        menu_bar_diag_tooltip(&d, hard, cautions);

    if (anim[0]) {
        ImGui::SameLine(0, gap);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", anim);
    }
}

void MenuBarPanel::render()
{
    const int object_cap = editor_project_object_capacity();
    const int module_cap = editor_project_module_capacity();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...", NULL))
                editor_emit_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
            if (ImGui::MenuItem("New Simple MK2 Level...", NULL))
                editor_emit_unsaved_action(UNSAVED_ACTION_NEW_SIMPLE_MK2);
            if (ImGui::MenuItem("New Full-Screen Proof Level", NULL))
                editor_emit_unsaved_action(UNSAVED_ACTION_NEW_BG_PROOF);
            if (ImGui::MenuItem("New Checker Test Level", NULL))
                editor_emit_unsaved_action(UNSAVED_ACTION_NEW_CHECKER);
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                char path[512] = "";
                if (file_dialog_open("Open BDB/BDD",
                    "Midway Background Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0",
                    path, sizeof path))
                {
                    editor_emit_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, path);
                }
            }
            if (g_recent_count > 0 && ImGui::BeginMenu("Open Recent")) {
                for (int i = 0; i < g_recent_count; i++) {
                    if (ImGui::MenuItem(g_recent_files[i])) {
                        editor_emit_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, g_recent_files[i]);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Import")) {
                if (ImGui::MenuItem("TGA Image...", "Ctrl+L")) {
                    char path[512] = "";
                    if (file_dialog_open("Import TGA", "TGA Files\0*.TGA;*.tga\0All Files\0*.*\0", path, sizeof path)) {
                        int old_ni = g_ni;
                        if (bdd_import_tga(path) && g_ni > old_ni) {
                            bdd_object_picker_free_labels();
                            g_need_rebuild = 1;
                            g_show_images = true;
                            g_dirty = 1;
                        }
                    }
                }
                ImGui::MenuItem("Force PNG Imports To 8bpp", NULL, &g_png_import_force_8bpp);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Keeps PNG imports LOAD2-visible as 8bpp by moving low-color art into high palette indexes.");
                ImGui::MenuItem("Optimize Imports For Space", NULL, &g_import_optimize_after_import);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Losslessly trims transparent borders and compacts palettes after PNG/IMG import.");
                ImGui::MenuItem("IMG Index 0 Is Transparent", NULL, &g_img_import_index0_transparent);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Recommended for MK2 IMG sprites. Keeps pixel/palette index 0 transparent instead of importing it as visible black.");
                if (ImGui::MenuItem("PNG Image...", NULL)) {
                    char path[512] = "";
                    if (file_dialog_open("Import PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path))
                        import_png(path);
                }
                if (ImGui::MenuItem("IMG Sprites...", NULL)) {
                    char path[512] = "";
                    if (file_dialog_open("Import IMG Sprites", "Midway IMG Files\0*.IMG;*.img\0All Files\0*.*\0", path, sizeof path))
                        open_img_import_picker(path);
                }
                if (ImGui::MenuItem("IMG Folder...", NULL)) {
                    char sel[1024] = {0};
                    if (folder_dialog_open("Select folder with IMG files", sel, sizeof sel))
                        batch_import_img(sel);
                }
                if (ImGui::MenuItem("IMG LOD...", NULL)) {
                    char path[512] = "";
                    if (file_dialog_open("Import IMG LOD", "LOAD2 LOD Files\0*.LOD;*.lod\0All Files\0*.*\0", path, sizeof path))
                        import_lod_file(path);
                }
                if (ImGui::MenuItem("Batch PNG from Folder...", NULL)) {
                    char sel[1024] = {0};
                    if (folder_dialog_open("Select folder with PNG files", sel, sizeof sel))
                        batch_import_png(sel);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Export")) {
                if (ImGui::MenuItem("Composite PNG...", NULL, false, g_have_bdb && g_no > 0))
                    export_composite_png();
                if (ImGui::MenuItem("Viewport as PNG...", NULL, false, g_have_bdb && g_no > 0))
                    export_viewport_png();
                if (ImGui::MenuItem("MK2 Assembly Skeleton...", NULL, false, g_have_bdb && g_no > 0))
                    export_mk2_assembly();
                if (ImGui::MenuItem("All Images as TGA...", NULL, false, g_ni > 0)) {
                    for (int ei = 0; ei < g_ni; ei++) {
                        export_image_tga(&g_img[ei]);
                    }
                }
                if (ImGui::MenuItem("All Images as PNG...", NULL, false, g_ni > 0)) {
                    for (int ei = 0; ei < g_ni; ei++) {
                        export_image_png(&g_img[ei]);
                    }
                }
                if (ImGui::MenuItem("Sprite Sheet as PNG...", NULL, false, g_ni > 0))
                    export_sprite_sheet_png();
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Validate...", NULL, false, g_have_bdb || g_ni > 0))
                editor_emit_show_verify();
            ImGui::Separator();
            if (ImGui::MenuItem("Tile Fill...", NULL, false, g_have_bdb && g_ni > 0))
                g_show_tile = true;
            if (!g_simple_mode) {
                if (ImGui::MenuItem("MK2 Stage Kit...", NULL)) {
                    g_show_mk2_workflow = true;
                    g_show_mk2_stage_kit = true;
                }
                if (ImGui::MenuItem("Sync MK2 Runtime Palettes...", NULL, false, g_n_pals > 0))
                    mk2_palette_sync_request_prompt("Manual runtime palette sync", true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(g_simple_mode ? "Save" : "Save All", "Ctrl+S",
                                false, (g_have_bdb || g_no > 0 || g_ni > 0) &&
                                       (g_bdb_path[0] || g_bdd_path[0]))) {
                editor_emit_save_all();
            }
            if (!g_simple_mode) {
                if (ImGui::MenuItem("Save BDB + BDD", NULL, false,
                                    (g_have_bdb || g_no > 0 || g_ni > 0) &&
                                    (g_bdb_path[0] || g_bdd_path[0])))
                    editor_emit_save_all();
            }
            if (ImGui::MenuItem("Save As...", NULL, false, g_have_bdb || g_no > 0 || g_ni > 0)) {
                char path[512] = "";
                if (file_dialog_save("Save As", "Midway Background Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0", path, sizeof path)) {
                    set_project_save_paths_from_any(path);
                    ensure_bdb_header_for_save();
                    editor_emit_save_all();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Package...", NULL, false, g_have_bdb && g_bdb_path[0]))
                stage_export_bundle();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences..."))
                editor_emit_show_preferences();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))
                editor_emit_unsaved_action(UNSAVED_ACTION_CLOSE_APP);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, (bool)(undo_is_available())))
                undo_restore();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, (bool)redo_is_available()))
                redo_restore();
            ImGui::Separator();
            bool has_sel = false;
            for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) { has_sel = true; break; }
            if (ImGui::MenuItem("Cut",            "Ctrl+X", false, has_sel && g_no > 0)) {
                copy_selected_objects_to_clipboard();
                if (g_clip_count > 0)
                    delete_object_targets_preserve_order(-1, "Cut");
            }
            if (ImGui::MenuItem("Copy",           "Ctrl+C", false, has_sel && g_no > 0))
                copy_selected_objects_to_clipboard();
            if (ImGui::MenuItem("Paste",          "Ctrl+V", false, g_clip_count > 0 && g_no + g_clip_count <= object_cap))
                paste_clipboard_objects(32, 16);
            if (ImGui::MenuItem("Duplicate",      "Ctrl+D", false, has_sel && g_no > 0)) {
                undo_save_ex("Duplicate");
                int max_order = 0;
                for (int i = 0; i < g_no; i++) if (g_obj[i].order > max_order) max_order = g_obj[i].order;
                int added = 0;
                int original_no = g_no;
                for (int si = 0; si < original_no && g_no < object_cap; si++) {
                    if (!g_sel_flags[si]) continue;
                    Obj src = g_obj[si];
                    Obj *dst = editor_project_append_object_slot();
                    if (!dst) break;
                    *dst = src;
                    dst->depth += 16; dst->sy += 8;
                    dst->order  = max_order + 1 + added;
                    added++;
                }
                if (added) g_need_rebuild = 1;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("H-Flip Selection", "X", false, has_sel)) {
                flip_object_targets_mirrored(-1, true, "H-Flip");
            }
            if (ImGui::MenuItem("V-Flip Selection", "Y", false, has_sel)) {
                flip_object_targets_mirrored(-1, false, "V-Flip");
            }
            /* Align submenu — uses lead object (g_hl_obj) as anchor */
            bool can_align = has_sel && g_hl_obj >= 0 && g_hl_obj < g_no;
            if (ImGui::BeginMenu("Align Selection", can_align)) {
                Img *anc = img_find(g_obj[g_hl_obj].ii);
                int anc_w = anc ? anc->w : 0, anc_h = anc ? anc->h : 0;
                auto do_align = [&](int axis) {
                    /* axis: 0=left 1=right 2=top 3=bottom 4=centerH 5=centerV */
                    ObjectPositionUndoCapture undo;
                    if (!object_position_undo_capture_selected(&undo)) return;
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i] || i == g_hl_obj) continue;
                        Img *oi = img_find(g_obj[i].ii);
                        int ow = oi ? oi->w : 0, oh = oi ? oi->h : 0;
                        if      (axis == 0) g_obj[i].depth = g_obj[g_hl_obj].depth;
                        else if (axis == 1) g_obj[i].depth = g_obj[g_hl_obj].depth + anc_w - ow;
                        else if (axis == 2) g_obj[i].sy    = g_obj[g_hl_obj].sy;
                        else if (axis == 3) g_obj[i].sy    = g_obj[g_hl_obj].sy + anc_h - oh;
                        else if (axis == 4) g_obj[i].depth = g_obj[g_hl_obj].depth + anc_w/2 - ow/2;
                        else if (axis == 5) g_obj[i].sy    = g_obj[g_hl_obj].sy   + anc_h/2 - oh/2;
                    }
                    if (object_position_undo_commit(&undo, "Align") > 0) {
                        g_need_rebuild = 1;
                        g_view_changed = 1;
                    }
                };
                if (ImGui::MenuItem("Align Left"))           do_align(0);
                if (ImGui::MenuItem("Align Right"))          do_align(1);
                if (ImGui::MenuItem("Align Top"))            do_align(2);
                if (ImGui::MenuItem("Align Bottom"))         do_align(3);
                if (ImGui::MenuItem("Center Horizontally"))  do_align(4);
                if (ImGui::MenuItem("Center Vertically"))    do_align(5);
                ImGui::EndMenu();
            }
            /* Distribute submenu — requires ≥3 selected */
            int sel_dist_n = selected_count();
            bool can_dist = has_sel && sel_dist_n >= 3;
            if (ImGui::BeginMenu("Distribute Selection", can_dist)) {
                auto do_distribute = [&](bool horiz) {
                    ObjectPositionUndoCapture undo;
                    if (!object_position_undo_capture_selected(&undo)) return;
                    /* collect + sort selected by leading edge */
                    std::vector<int> s_di;
                    s_di.reserve((size_t)g_no);
                    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) s_di.push_back(i);
                    int dn = (int)s_di.size();
                    for (int a = 1; a < dn; a++) {
                        int tmp = s_di[a], j = a - 1;
                        int tv = horiz ? g_obj[tmp].depth : g_obj[tmp].sy;
                        while (j >= 0 && (horiz ? g_obj[s_di[j]].depth : g_obj[s_di[j]].sy) > tv) {
                            s_di[j+1] = s_di[j]; j--;
                        }
                        s_di[j+1] = tmp;
                    }
                    /* outer extent of first and last objects */
                    Img *im_last = img_find(g_obj[s_di[dn-1]].ii);
                    int span_start = horiz ? g_obj[s_di[0]].depth : g_obj[s_di[0]].sy;
                    int span_end   = horiz ? g_obj[s_di[dn-1]].depth + (im_last ? im_last->w : 0)
                                           : g_obj[s_di[dn-1]].sy    + (im_last ? im_last->h : 0);
                    int total_ext = 0;
                    for (int k = 0; k < dn; k++) {
                        Img *im = img_find(g_obj[s_di[k]].ii);
                        total_ext += horiz ? (im ? im->w : 0) : (im ? im->h : 0);
                    }
                    int gap = (dn > 1) ? (span_end - span_start - total_ext) / (dn - 1) : 0;
                    int cur = span_start;
                    for (int k = 0; k < dn; k++) {
                        Img *im = img_find(g_obj[s_di[k]].ii);
                        int ext = horiz ? (im ? im->w : 0) : (im ? im->h : 0);
                        if (horiz) g_obj[s_di[k]].depth = cur;
                        else       g_obj[s_di[k]].sy    = cur;
                        cur += ext + gap;
                    }
                    if (object_position_undo_commit(&undo, horiz ? "Distribute H" : "Distribute V") > 0)
                        g_need_rebuild = 1;
                };
                if (ImGui::MenuItem("Distribute Horizontally",
                                    NULL, false, sel_dist_n >= 3)) do_distribute(true);
                if (ImGui::MenuItem("Distribute Vertically",
                                    NULL, false, sel_dist_n >= 3)) do_distribute(false);
                ImGui::EndMenu();
            }
            /* Center on Stage submenu */
            {
                int stg_w = 1024, stg_h = 256;
                if (g_bdb_header[0]) {
                    char _nm[64]; int _d, _nm2, _np, _no;
                    sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                           _nm, &stg_w, &stg_h, &_d, &_nm2, &_np, &_no);
                }
                bool can_ctr = has_sel && g_have_bdb;
                if (ImGui::BeginMenu("Center on Stage", can_ctr)) {
                    auto do_center = [&](bool horiz, bool vert) {
                        ObjectPositionUndoCapture undo;
                        if (!object_position_undo_capture_selected(&undo)) return;
                        int sl = INT_MAX, sr = INT_MIN, st = INT_MAX, sb = INT_MIN;
                        for (int i = 0; i < g_no; i++) {
                            if (!g_sel_flags[i]) continue;
                            Img *im = img_find(g_obj[i].ii);
                            int r = g_obj[i].depth + (im ? im->w : 0);
                            int b = g_obj[i].sy    + (im ? im->h : 0);
                            if (g_obj[i].depth < sl) sl = g_obj[i].depth;
                            if (r > sr) sr = r;
                            if (g_obj[i].sy < st) st = g_obj[i].sy;
                            if (b > sb) sb = b;
                        }
                        int dx = horiz ? ((stg_w - (sr - sl)) / 2 - sl) : 0;
                        int dy = vert  ? ((stg_h - (sb - st)) / 2 - st) : 0;
                        for (int i = 0; i < g_no; i++) {
                            if (!g_sel_flags[i]) continue;
                            if (horiz) g_obj[i].depth += dx;
                            if (vert)  g_obj[i].sy    += dy;
                        }
                        if (object_position_undo_commit(&undo, "Center on Stage") > 0)
                            g_need_rebuild = 1;
                    };
                    if (ImGui::MenuItem("Center Horizontally")) do_center(true,  false);
                    if (ImGui::MenuItem("Center Vertically"))   do_center(false, true);
                    if (ImGui::MenuItem("Center Both"))         do_center(true,  true);
                    ImGui::TextDisabled("Stage: %dx%d", stg_w, stg_h);
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All",     "Ctrl+A", false, g_have_bdb && g_no > 0)) {
                int sel_n = 0;
                for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_n++;
                int val = (sel_n == g_no) ? 0 : 1;
                for (int i = 0; i < g_no; i++) g_sel_flags[i] = val;
            }
            if (ImGui::MenuItem("Invert Selection","Ctrl+I", false, g_have_bdb && g_no > 0))
                for (int i = 0; i < g_no; i++) g_sel_flags[i] ^= 1;
            if (ImGui::MenuItem("Deselect All",   "Escape",  false, has_sel))
                editor_project_clear_selection();
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Unused Images", NULL, false, g_ni > 0)) {
                delete_unused_images_impl(false, "Delete Unused Images");
            }
            if (ImGui::MenuItem("Delete Imported Unused Images", NULL, false, g_ni > 0)) {
                delete_unused_images_impl(true, "Delete Imported Unused Images");
            }
            if (ImGui::MenuItem("Delete Unused Palettes", NULL, false, g_n_pals > 0)) {
                int removed = remove_unused_palettes_impl(true);
                char msg[96];
                snprintf(msg, sizeof msg, removed ? "Deleted %d unused palette(s)" : "No unused palettes", removed);
                stage_set_toast(msg);
            }
            bool can_compact_ids = g_ni > 0 && !runtime_actor_preview_imports_loaded();
            if (ImGui::MenuItem("Compact Image Indices", NULL, false, can_compact_ids)) {
                /* Renumber image indices to 0, 1, 2, … and fix all object references */
                undo_save_ex("Compact Indices");
                for (int i = 0; i < g_ni; i++) {
                    int old_idx = g_img[i].idx;
                    if (old_idx == i) continue;
                    g_img[i].idx = i;
                    for (int oi = 0; oi < g_no; oi++)
                        if (g_obj[oi].ii == old_idx) g_obj[oi].ii = i;
                }
                g_need_rebuild = 1; g_dirty = 1;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object")) {
            int active_obj = active_object_index();
            int sel_n = selected_count();
            bool has_obj = active_obj >= 0 && active_obj < g_no;
            if (has_obj) {
                ImGui::Text("Object %d  (ii=0x%04X)", active_obj, g_obj[active_obj].ii);
                if (sel_n > 1) ImGui::TextDisabled("%d selected", sel_n);
            } else {
                ImGui::TextDisabled("No object selected");
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Edit Properties", NULL, false, has_obj))
                open_object_properties(active_obj);
            if (ImGui::MenuItem("Edit Block", NULL, false, has_obj))
                edit_block_for_object(active_obj);
            if (ImGui::MenuItem("Resize Sprite...", NULL, false, has_obj)) {
                Img *rim = img_find(g_obj[active_obj].ii);
                if (rim) open_sprite_resize((int)(rim - g_img), true);
            }
            if (ImGui::MenuItem("Resize Selected Sprites...", NULL, false, sel_n > 1))
                open_group_sprite_resize();
            if (ImGui::MenuItem("Split Object...", NULL, false, has_obj))
                open_split_object_dialog(active_obj);
            if (ImGui::MenuItem("Lower Selected Bit Depth...", NULL, false, selected_count() > 0))
                g_show_group_bpp_reducer = true;
            if (ImGui::MenuItem("Center View", NULL, false, has_obj))
                center_view_on_object(active_obj);
            if (ImGui::MenuItem("Export Image as PNG...", NULL, false, has_obj))
                export_object_image_png_dialog(active_obj);

            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_obj && g_no < object_cap))
                duplicate_object_menu_targets(active_obj);
            if (ImGui::MenuItem("Delete", "Del", false, has_obj))
                delete_object_menu_targets(active_obj);
            if (ImGui::MenuItem("H-Flip", "X", false, has_obj))
                flip_object_menu_targets(active_obj, true);
            if (ImGui::MenuItem("V-Flip", "Y", false, has_obj))
                flip_object_menu_targets(active_obj, false);

            ImGui::Separator();
            if (ImGui::MenuItem("Bring to Front", NULL, false, has_obj))
                reorder_object_menu_targets(active_obj, true);
            if (ImGui::MenuItem("Send to Back", NULL, false, has_obj))
                reorder_object_menu_targets(active_obj, false);

            ImGui::Separator();
            if (ImGui::MenuItem("Select All with Same Image", NULL, false, has_obj)) {
                select_all_with_image_ii(g_obj[active_obj].ii);
                g_hl_obj = active_obj;
            }
            if (ImGui::MenuItem("Select All in Layer", NULL, false, has_obj)) {
                select_all_in_layer_byte((g_obj[active_obj].wx >> 8) & 0xFF);
                g_hl_obj = active_obj;
            }
            if (ImGui::MenuItem("Wrap Selection in Region", NULL, false,
                                g_simple_mode && selected_count() > 0 && g_bdb_num_modules < module_cap))
                wrap_selected_objects_in_region();

            ImGui::Separator();
            if (ImGui::BeginMenu(g_simple_mode ? "Assign Layer" : "Assign Layer (wx)", has_obj)) {
                static const struct { int byte; const char *name; float scroll; } layers[] = {
                    {0x32, "Sky / far back", 0.2f}, {0x3C, "Mid distance", 0.5f},
                    {0x40, "Floor / play", 1.0f},   {0x41, "Floor alt", 1.0f},
                    {0x43, "Near foreground", 1.2f},{0x46, "Front foreground", 1.5f}
                };
                int cur_layer = has_obj ? ((g_obj[active_obj].wx >> 8) & 0xFF) : -1;
                for (int li = 0; li < 6; li++) {
                    char label[80];
                    if (g_simple_mode)
                        snprintf(label, sizeof label, "%s  (%.1fx)", layers[li].name, layers[li].scroll);
                    else
                        snprintf(label, sizeof label, "%s  (0x%02X, %.1fx)",
                                 layers[li].name, layers[li].byte, layers[li].scroll);
                    bool is_cur = cur_layer == layers[li].byte;
                    if (ImGui::MenuItem(label, is_cur ? "(current)" : NULL))
                        assign_layer_to_object_targets(active_obj, layers[li].byte);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Assign Palette", has_obj && g_n_pals > 0)) {
                for (int pi = 0; pi < g_n_pals; pi++) {
                    char label[96];
                    snprintf(label, sizeof label, "Pal %d: %s", pi, g_pal_name[pi]);
                    bool is_cur = has_obj && g_obj[active_obj].fl == pi;
                    if (ImGui::MenuItem(label, is_cur ? "(current)" : NULL))
                        assign_palette_to_object_targets(active_obj, pi);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Assign to Module", has_obj && g_bdb_num_modules > 0)) {
                for (int mi = 0; mi < g_bdb_num_modules; mi++) {
                    char name[64] = "";
                    int x1, x2, y1, y2;
                    if (sscanf(g_bdb_modules[mi], "%63s %d %d %d %d", name, &x1, &x2, &y1, &y2) < 5)
                        continue;
                    if (ImGui::MenuItem(name))
                        assign_module_to_object_targets(active_obj, mi);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Image")) {
            int active_img = active_menu_image_index();
            int active_obj = active_object_index();
            bool has_img = active_img >= 0 && active_img < g_ni;
            if (has_img) {
                Img *im = &g_img[active_img];
                if (im->label[0]) ImGui::Text("%s", im->label);
                ImGui::Text(g_simple_mode ? "Image %d  (%dx%d)" : "Image 0x%02X  (%dx%d)",
                            im->idx, im->w, im->h);
                if (im->source[0]) ImGui::TextDisabled("%s", im->source);
            } else {
                ImGui::TextDisabled("No active image");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show Image List", NULL, g_show_images))
                g_show_images = !g_show_images;
            if (ImGui::MenuItem("Arm Place Tool", NULL, false, has_img)) {
                g_place_tool_img = active_img;
                g_cur_tool = 1;
                g_hint_place = false;
            }
            if (ImGui::MenuItem("Add to Center of View", NULL, false, has_img && g_no < object_cap))
                add_image_to_view_center(active_img);
            if (ImGui::MenuItem("Select All Uses", NULL, false, has_img))
                select_all_with_image_ii(g_img[active_img].idx);
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Block", NULL, false, has_img)) {
                g_block_edit_img = active_img;
                g_block_edit_zoom = 8;
                g_block_edit_col = 0;
                g_block_edit_open = true;
            }
            if (ImGui::MenuItem("Export Current as PNG", NULL, false, has_img)) {
                if (active_obj >= 0 && active_obj < g_no)
                    export_object_image_png_dialog(active_obj);
                else
                    export_image_png(&g_img[active_img]);
            }
            if (ImGui::MenuItem("Export Current as TGA", NULL, false, has_img))
                export_image_tga(&g_img[active_img]);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Select Tool", "S", g_cur_tool == 0))
                g_cur_tool = 0;
            if (ImGui::MenuItem("Place Tool", "P", g_cur_tool == 1, g_ni > 0)) {
                g_cur_tool = 1;
                g_hint_place = false;
            }
            if (ImGui::MenuItem("Pan Tool", "H", g_cur_tool == 2))
                g_cur_tool = 2;
            if (ImGui::MenuItem("Zoom Tool", "Z", g_cur_tool == 3))
                g_cur_tool = 3;
            if (ImGui::MenuItem("Brush Tool", "B", g_cur_tool == 4, g_ni > 0))
                g_cur_tool = 4;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid",         "Shift+T", &g_show_grid);
            ImGui::MenuItem("Borders",      "Shift+B", &g_show_borders);
            ImGui::MenuItem("Module Bounds", NULL, &g_show_module_bounds);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show BDB/LOAD2 module rectangles in the source world view.");
            ImGui::MenuItem("All Objects",  "Shift+O", &g_show_objects);
            ImGui::MenuItem("Object Info Overlay", "O", &g_show_labels);
            ImGui::MenuItem("Layer Colors",  NULL, &g_layer_tint);
            ImGui::MenuItem("Alignment Doctor", NULL, &g_show_alignment_doctor);
            if (ImGui::MenuItem("Grid Settings..."))
                g_show_grid_set = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Background Color..."))
                g_show_bg_picker = true;
            if (ImGui::MenuItem("Reference Image..."))
                g_show_ref_settings = true;
            ImGui::Separator();
            ImGui::MenuItem("Grid Snap",    NULL, &g_grid_snap);
            ImGui::MenuItem("Visible Pixel Snap", NULL, &g_snap_visible_pixels);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Smart edge snapping uses non-transparent BDD pixels instead of the full LOAD2 rectangle.");
            ImGui::Separator();
            ImGui::MenuItem("Preview Mode", "F11", &g_preview_mode);
            ImGui::Separator();
            if (ImGui::MenuItem("Game Preview", NULL, g_game_view != 0, g_have_bdb && g_no > 0)) {
                g_game_view ^= 1;
                if (g_game_view) {
                    route_to_game_preview_screen(true, true);
                    g_gv_needs_autozoom = true;
                } else {
                    focus_editor_on_game_preview_screen();
                }
            }
            if (ImGui::MenuItem("Runtime Layout", NULL, g_runtime_layout_view != 0, g_have_bdb && g_no > 0)) {
                g_runtime_layout_view ^= 1;
                g_split_view = 0;
                route_to_game_preview_screen(true, g_game_view != 0);
                if (g_game_view)
                    g_gv_needs_autozoom = true;
            }
            ImGui::MenuItem("Palette Animation", NULL, &g_show_pal_anim);
            ImGui::Separator();
            ImGui::MenuItem("Minimap",        NULL, &g_show_minimap);
            ImGui::MenuItem("Layers",         NULL, &g_show_layers);
            ImGui::MenuItem("Image List",     NULL, &g_show_images);
            ImGui::MenuItem(g_simple_mode ? "Regions" : "Modules", NULL, &g_show_modules);
            ImGui::MenuItem("Object Properties", NULL, &g_show_obj_properties);
            ImGui::MenuItem("History",        NULL, &g_show_undo_history);
            ImGui::MenuItem("Level Stats",    NULL, &g_show_level_stats);
            ImGui::MenuItem("Selected BPP Reducer", NULL, &g_show_group_bpp_reducer);
            ImGui::MenuItem("Bit Depth Preview", NULL, &g_show_bpp_preview);
            ImGui::MenuItem("Garbage Collect",   NULL, &g_show_gc);
            ImGui::MenuItem("Checkpoints",       NULL, &g_show_checkpoints);
            if (!g_simple_mode) {
                ImGui::MenuItem("MK2 Workflow", NULL, &g_show_mk2_workflow);
                if (ImGui::MenuItem("MK2 Stage Kit", NULL, false))
                    editor_emit_open_mk2_tool(1);
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Zoom to Fit", "Ctrl+0", false, g_have_bdb && g_no > 0))
                zoom_to_fit();
            if (ImGui::MenuItem("Zoom to Selection", "Ctrl+Shift+Z", false, g_have_bdb && g_no > 0))
                zoom_to_selection();
            ImGui::Separator();
            if (ImGui::MenuItem("Snap Panels to Rails")) {
                g_dock_right_panels_next = true;
            }
            if (ImGui::MenuItem("Reset Window Layout")) {
                remove("bddview_layout.ini");
                remove("bddview_layout.ver");
                remove("bddview_right_panels.cfg");
                g_dock_right_panels_next = true;
            }
            ImGui::Separator();
            bool adv = !g_simple_mode;
            if (ImGui::MenuItem("Advanced Mode", NULL, &adv)) {
                g_simple_mode = !adv;
                if (g_simple_mode) {
                    g_show_mk2_workflow  = false;
                    g_show_mk2_stage_kit = false;
                }
                settings_save();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("MK2")) {
            if (ImGui::MenuItem("Simple Four-Image Level...", NULL, false))
                editor_emit_unsaved_action(UNSAVED_ACTION_NEW_SIMPLE_MK2);
            ImGui::Separator();
            if (ImGui::MenuItem("Workflow", NULL, false))
                editor_emit_open_mk2_tool(0);
            if (ImGui::MenuItem("Stage Kit", NULL, false))
                editor_emit_open_mk2_tool(1);
            if (ImGui::MenuItem("Level Start Helper", NULL, false))
                editor_emit_open_mk2_tool(10);
            ImGui::Separator();
            if (ImGui::MenuItem("Import Existing Stage", NULL, false))
                editor_emit_open_mk2_tool(2);
            if (ImGui::MenuItem("Clustered PNG Stage Import", NULL, false))
                editor_emit_open_mk2_tool(3);
            ImGui::Separator();
            if (ImGui::MenuItem("Authoring Tools", NULL, false, g_have_bdb && g_no > 0))
                editor_emit_open_mk2_tool(4);
            if (ImGui::MenuItem("Stage Readiness Gate", NULL, false, g_have_bdb && g_no > 0))
                editor_emit_open_mk2_tool(5);
            if (ImGui::MenuItem("Finish-Line Gate", NULL, false))
                editor_emit_open_mk2_tool(6);
            if (ImGui::MenuItem("LOAD2 / Runtime Preview", NULL, false))
                editor_emit_open_mk2_tool(7);
            if (ImGui::MenuItem("Live MAME Preview Gate", NULL, false, g_have_bdb && g_no > 0))
                editor_emit_open_mk2_tool(8);
            if (ImGui::MenuItem("ROM Preview Diff", NULL, false, g_have_bdb && g_no > 0))
                editor_emit_open_mk2_tool(9);
            if (ImGui::MenuItem("Runtime Animation Actors", NULL, false, g_have_bdb && g_no > 0))
                editor_emit_open_mk2_tool(11);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard Shortcuts", "F1"))
                g_show_help = true;
            if (ImGui::MenuItem("About"))
                g_about_open = true;
            ImGui::EndMenu();
        }
        draw_menu_bar_stage_info();
        ImGui::EndMainMenuBar();
    }
}
