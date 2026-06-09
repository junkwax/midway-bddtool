#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <algorithm>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef _WIN32
#define image_list_strcasecmp _stricmp
#else
#include <strings.h>
#define image_list_strcasecmp strcasecmp
#endif

static void image_list_draw_details_tooltip(const Img *im, const ImageModuleInfo *module_info,
                                            int use_count)
{
    if (!im) return;
    ImGui::BeginTooltip();
    if (im->label[0])
        ImGui::TextUnformatted(im->label);
    else
        ImGui::Text(g_simple_mode ? "Image %d" : "Image 0x%02X", im->idx);
    ImGui::Separator();
    ImGui::Text("%dx%d   pal %d   %dbpp", im->w, im->h, im->pal_idx, mk2_bpp_for_image(im));
    ImGui::Text("%d object%s reference this image", use_count, use_count == 1 ? "" : "s");
    if (module_info) {
        char mod_badge[96];
        image_module_badge_label(module_info, mod_badge, sizeof mod_badge);
        ImGui::Text("Module: %s", mod_badge);
    }
    if (im->source[0])
        ImGui::TextDisabled("source: %s", im->source);
    if (im->anix || im->aniy || im->anix2 || im->aniy2)
        ImGui::TextDisabled("anipoint: %d,%d  alt: %d,%d,%d",
                            im->anix, im->aniy, im->anix2, im->aniy2, im->aniz2);
    if (im->frm || im->opals || im->pttblnum)
        ImGui::TextDisabled("frm=%d  opals=%d  pttbl=%d",
                            im->frm, im->opals, im->pttblnum);
    if (runtime_actor_image_is_preview_import(im))
        ImGui::TextDisabled("Runtime source: selects existing uses; art edits are locked.");
    else
        ImGui::TextDisabled("Click preview/name to arm placement. Right-click for actions.");
    ImGui::EndTooltip();
}

static void image_list_draw_action_menu(int i, bool *delete_this_image)
{
    if (i < 0 || i >= g_ni) return;
    Img *im = &g_img[i];
    bool runtime_locked = runtime_actor_image_is_preview_import(im);

    if (im->label[0])
        ImGui::Text("%s", im->label);
    ImGui::Text(g_simple_mode ? "Image %d  (%dx%d)" : "Image 0x%02X  (%dx%d)",
                im->idx, im->w, im->h);
    if (im->source[0]) ImGui::TextDisabled("source: %s", im->source);
    if (im->anix || im->aniy || im->anix2 || im->aniy2)
        ImGui::TextDisabled("anipoint: %d,%d  alt: %d,%d,%d",
                            im->anix, im->aniy, im->anix2, im->aniy2, im->aniz2);
    if (im->frm || im->opals || im->pttblnum)
        ImGui::TextDisabled("frm=%d  opals=%d  pttbl=%d",
                            im->frm, im->opals, im->pttblnum);
    if (im->lod_ref)
        ImGui::TextColored(ImVec4(0.5f,0.9f,1.0f,1.0f), "Imported or referenced by LOD");
    if (runtime_locked)
        ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f),
                           "Runtime preview source: art is read-only");
    ImGui::Separator();

    if (runtime_locked) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Arm Place Tool")) {
        g_place_tool_img = i;
        g_cur_tool = 1;
    }
    if (ImGui::MenuItem("Add to Center of View") && g_no < editor_project_object_capacity())
        add_image_to_view_center(i);
    if (ImGui::MenuItem("Set as Static Background", NULL, false,
                        g_no < editor_project_object_capacity() || image_use_count(im->idx) > 0)) {
        if (mk2_set_image_as_static_background(i)) {
            char msg[160];
            snprintf(msg, sizeof msg, "Set %s as static background",
                     im->label[0] ? im->label : "image");
            stage_set_toast(msg);
        } else {
            stage_set_toast("Could not set static background");
        }
    }
    if (ImGui::MenuItem("Add Chopped to Center") && g_no < editor_project_object_capacity()) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        int x = 0, y = 0;
        bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                            g_view_x, g_view_y, g_zoom, &x, &y);
        int pal = (im->pal_idx >= 0) ? im->pal_idx : 0;
        chop_image_to_map(i, x, y, 0x4100, pal, false, false, -1, true);
    }
    if (runtime_locked) ImGui::EndDisabled();

    ImGui::Separator();
    if (runtime_locked) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Clear Edge Matte")) {
        int changed = clear_image_edge_matte(i, false, true);
        char msg[128];
        snprintf(msg, sizeof msg, changed > 0 ? "Cleared %d matte pixel(s)" : "No edge matte found", changed);
        stage_set_toast(msg);
    }
    if (ImGui::MenuItem("Clear Black Matte")) {
        int changed = clear_image_edge_matte(i, true, true);
        char msg[128];
        snprintf(msg, sizeof msg, changed > 0 ? "Cleared %d black matte pixel(s)" : "No black matte found", changed);
        stage_set_toast(msg);
    }
    if (runtime_locked) ImGui::EndDisabled();

    ImGui::Separator();
    if (runtime_locked) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Edit Image ID...")) {
        snprintf(g_img_edit_buf, sizeof g_img_edit_buf, "%X", im->idx);
        g_img_edit_idx = i;
    }
    if (ImGui::MenuItem("Resize Sprite..."))
        open_sprite_resize(i, false);
    if (ImGui::MenuItem("Replace from PNG...")) {
        char path[512] = "";
        if (file_dialog_open("Replace from PNG",
            "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path))
        {
            undo_save();
            reimport_image(i, path);
        }
    }
    if (runtime_locked) ImGui::EndDisabled();
    if (ImGui::MenuItem("Select All Uses"))
        select_all_with_image_ii(im->idx);
    if (ImGui::MenuItem("Export as TGA"))
        export_image_tga(im);
    if (ImGui::MenuItem("Export as PNG"))
        export_image_png(im);
    ImGui::Separator();
    if (runtime_locked) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Delete image") && delete_this_image)
        *delete_this_image = true;
    if (runtime_locked) ImGui::EndDisabled();
}

static void image_list_select_asset(int i)
{
    if (i < 0 || i >= g_ni) return;
    Img *im = &g_img[i];

    g_place_tool_img = i;
    if (!runtime_actor_image_is_preview_import(im))
        g_cur_tool = 1;
    if (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
        g_sel_pal = im->pal_idx;

    editor_project_clear_selection();
    g_hl_obj = -1;

    bool select_all_uses = ImGui::GetIO().KeyCtrl;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].ii != im->idx) continue;
        g_sel_flags[oi] = 1;
        if (g_hl_obj < 0)
            g_hl_obj = oi;
        if (!select_all_uses)
            break;
    }
}

void draw_image_list(void)
{
    right_panel_set_next(RIGHT_PANEL_IMAGES);
    bool open = ImGui::Begin("Images", NULL);
    right_panel_after_begin(RIGHT_PANEL_IMAGES);
    if (!open) {
        ImGui::End();
        return;
    }

    if (g_ni == 0) {
        ImGui::TextUnformatted("No images loaded.");
        ImGui::End();
        return;
    }
    g_hover_img_ii = -1;

    int unused_imgs = 0, unused_pals = 0, unused_imported_imgs = 0, unused_imported_pixels = 0;
    const int pal_cap = editor_project_palette_capacity();
    std::vector<int> pal_used((size_t)(pal_cap > 0 ? pal_cap : 0), 0);
    for (int oi = 0; oi < g_no; oi++) {
        for (int ii = 0; ii < g_ni; ii++) {
            if (g_img[ii].idx != g_obj[oi].ii) continue;
            if (g_img[ii].pal_idx >= 0 && g_img[ii].pal_idx < pal_cap)
                pal_used[(size_t)g_img[ii].pal_idx] = 1;
            break;
        }
    }
    for (int ii = 0; ii < g_ni; ii++) {
        int used = 0;
        for (int oi = 0; oi < g_no; oi++)
            if (g_obj[oi].ii == g_img[ii].idx) { used = 1; break; }
        if (!used) {
            unused_imgs++;
            if (image_is_imported_asset(&g_img[ii]) &&
                !runtime_actor_image_is_preview_import(&g_img[ii])) {
                unused_imported_imgs++;
                unused_imported_pixels += g_img[ii].w * g_img[ii].h;
            }
        }
    }
    for (int pi = 0; pi < g_n_pals && pi < pal_cap; pi++)
        if (!pal_used[(size_t)pi]) unused_pals++;
    if (unused_imgs > 0 || unused_pals > 0)
        ImGui::TextColored(ImVec4(1,0.6f,0.2f,1), "%d unused images, %d unused palettes",
                          unused_imgs, unused_pals);

    /* Bulk maintenance tools live in a collapsible section so the panel leads
       with its filters and asset grid instead of a wall of buttons. */
    if (ImGui::CollapsingHeader("Cleanup & Tools")) {
    if (unused_imported_imgs > 0) {
        if (ImGui::SmallButton("Delete Imported Unused")) {
            delete_unused_images_impl(true, "Delete Imported Unused Images");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d imported unused image(s), %d raw pixels",
                              unused_imported_imgs, unused_imported_pixels);
        ImGui::SameLine();
    }
    if (unused_imgs > 0) {
        if (ImGui::SmallButton("Delete All Unused")) {
            delete_unused_images_impl(false, "Delete Unused Images");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deletes every image with zero object references.");
    }
    if (ImGui::SmallButton("Optimize All")) {
        int ti = 0, tp = 0, cp = 0;
        optimize_image_range_for_space(0, g_ni, true, &ti, &tp, &cp);
        char msg[160];
        snprintf(msg, sizeof msg, "Optimized %d image(s), saved %d px, compacted %d palette(s)",
                 ti, tp, cp);
        stage_set_toast(msg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lossless trim + shared palette compaction across loaded images.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear IMG Black Mattes")) {
        bool undo_done = false;
        int fixed_images = 0;
        int fixed_pixels = 0;
        for (int i = 0; i < g_ni; i++) {
            if (!image_is_imported_asset(&g_img[i])) continue;
            if (runtime_actor_image_is_preview_import(&g_img[i])) continue;
            int changed = clear_image_edge_matte(i, true, !undo_done);
            if (changed > 0) {
                undo_done = true;
                fixed_images++;
                fixed_pixels += changed;
            }
        }
        char msg[160];
        snprintf(msg, sizeof msg, "Cleared black matte from %d image(s), %d px",
                 fixed_images, fixed_pixels);
        stage_set_toast(msg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Repairs older IMG imports whose transparent index 0 was shifted into visible black.");
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("Chop W", &g_chop_tile_w);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("Chop H", &g_chop_tile_h);
    ImGui::SameLine();
    ImGui::Checkbox("Trim tiles", &g_chop_trim_tiles);
    }  /* end Cleanup & Tools */

    static int  img_filter = 0;
    static char img_search[64] = "";
    ImGui::Text("Show:"); ImGui::SameLine();
    /* Highlight the active filter so it's obvious which view is selected. */
    auto filter_button = [&](const char *label, int value) {
        bool active = (img_filter == value);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::SmallButton(label))
            img_filter = value;
        if (active)
            ImGui::PopStyleColor();
    };
    filter_button("All", 0);    ImGui::SameLine();
    filter_button("Used", 1);   ImGui::SameLine();
    filter_button("Unused", 2); ImGui::SameLine();
    filter_button("LOD", 3);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##imgsearch", img_search, sizeof img_search);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter by index, IMG frame name, source IMG/LOD, or palette");

    int search_idx = -1;
    if (img_search[0]) {
        char *end = nullptr;
        long sv = strtol(img_search, &end, 0);
        if (end && end != img_search && *end == '\0') search_idx = (int)sv;
    }

    int matching_imported_pixels = 0;
    int matching_imported_uses = 0;
    int matching_imported = collect_matching_imported_images(img_filter, img_search, search_idx,
                                                             NULL,
                                                             &matching_imported_pixels,
                                                             &matching_imported_uses);
    bool scoped_import_filter = img_search[0] || img_filter == 3;
    if (matching_imported > 0) {
        ImGui::TextDisabled("%d matching imported image(s), %d placement(s), %d px",
                            matching_imported, matching_imported_uses, matching_imported_pixels);
        if (ImGui::SmallButton("Select Matching Uses")) {
            int selected = select_matching_imported_image_uses(img_filter, img_search, search_idx);
            char msg[128];
            snprintf(msg, sizeof msg, "Selected %d imported placement(s)", selected);
            stage_set_toast(msg);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Selects object placements using imported images in the current image filter/search.");
        ImGui::SameLine();
        if (!scoped_import_filter) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Delete Matching Imported")) {
            delete_matching_imported_images_and_uses(img_filter, img_search, search_idx);
        }
        if (!scoped_import_filter) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(scoped_import_filter
                ? "Deletes matching imported images and every object placement using them. Original IMG/LOD files stay on disk."
                : "Type a source/name search, such as MK7MIL or CLOUDS, or switch to LOD before bulk deleting.");
    }

    static int img_sort = 0;
    static bool img_sort_desc = false;
    const char *sort_labels[] = {
        "File Order", "Index", "Width", "Height", "Palette", "Use Count",
        "Source", "Label", "Transfer", "Module"
    };
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("Sort", sort_labels[img_sort])) {
        for (int si = 0; si < 10; si++)
            if (ImGui::Selectable(sort_labels[si], img_sort == si))
                img_sort = si;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(img_sort_desc ? "Desc" : "Asc"))
        img_sort_desc = !img_sort_desc;

    if (g_last_import_img >= 0 && g_last_import_img < g_ni) {
        Img *last = &g_img[g_last_import_img];
        ImGui::Separator();
        if (last->label[0])
            ImGui::Text("Last import %s  0x%02X  %dx%d", last->label, last->idx, last->w, last->h);
        else
            ImGui::Text("Last import 0x%02X  %dx%d", last->idx, last->w, last->h);
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) {
            g_place_tool_img = g_last_import_img;
            g_cur_tool = 1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arm one placement; Brush repeats");
        ImGui::SameLine();
        if (ImGui::SmallButton("Place")) {
            add_image_to_view_center(g_last_import_img);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add object to world view");
        ImGui::Separator();
    }

    std::vector<ImageModuleInfo> image_modules;
    image_modules.reserve((size_t)g_ni);
    for (int i = 0; i < g_ni; i++)
        image_modules.push_back(image_module_info(&g_img[i]));

    std::vector<int> image_order;
    image_order.reserve((size_t)g_ni);
    for (int i = 0; i < g_ni; i++)
        image_order.push_back(i);
    if (img_sort != 0) {
        std::stable_sort(image_order.begin(), image_order.end(), [&](int a, int b) {
            int av = 0, bv = 0;
            switch (img_sort) {
                case 1: av = g_img[a].idx; bv = g_img[b].idx; break;
                case 2: av = g_img[a].w; bv = g_img[b].w; break;
                case 3: av = g_img[a].h; bv = g_img[b].h; break;
                case 4: av = g_img[a].pal_idx; bv = g_img[b].pal_idx; break;
                case 5: av = image_use_count(g_img[a].idx); bv = image_use_count(g_img[b].idx); break;
                case 6: {
                    int cmp = image_list_strcasecmp(g_img[a].source, g_img[b].source);
                    if (cmp != 0) return img_sort_desc ? (cmp > 0) : (cmp < 0);
                    av = g_img[a].idx; bv = g_img[b].idx;
                    break;
                }
                case 7: {
                    int cmp = image_list_strcasecmp(g_img[a].label, g_img[b].label);
                    if (cmp != 0) return img_sort_desc ? (cmp > 0) : (cmp < 0);
                    av = g_img[a].idx; bv = g_img[b].idx;
                    break;
                }
                case 8: {
                    int au = image_use_count(g_img[a].idx) > 0 ? 0 : 1;
                    int bu = image_use_count(g_img[b].idx) > 0 ? 0 : 1;
                    if (au != bu) return au < bu;
                    int cmp = image_list_strcasecmp(g_img[a].source, g_img[b].source);
                    if (cmp != 0) return cmp < 0;
                    av = g_img[a].pal_idx; bv = g_img[b].pal_idx;
                    if (av != bv) return av < bv;
                    cmp = image_list_strcasecmp(g_img[a].label, g_img[b].label);
                    if (cmp != 0) return cmp < 0;
                    av = g_img[a].idx; bv = g_img[b].idx;
                    break;
                }
                case 9: {
                    const ImageModuleInfo &am = image_modules[(size_t)a];
                    const ImageModuleInfo &bm = image_modules[(size_t)b];
                    if (am.bucket != bm.bucket)
                        return img_sort_desc ? (am.bucket > bm.bucket) : (am.bucket < bm.bucket);
                    if (am.mixed != bm.mixed)
                        return img_sort_desc ? (am.mixed && !bm.mixed) : (!am.mixed && bm.mixed);
                    av = g_img[a].idx; bv = g_img[b].idx;
                    break;
                }
                default: av = a; bv = b; break;
            }
            if (av != bv)
                return img_sort_desc ? (av > bv) : (av < bv);
            return g_img[a].idx < g_img[b].idx;
        });
    }

    if (img_sort != 0) {
        bool runtime_preview_loaded = runtime_actor_preview_imports_loaded();
        if (runtime_preview_loaded) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Apply Sort to BDD Order")) {
            undo_save_ex("Sort Images");
            const int image_cap = editor_project_image_capacity();
            std::vector<int> old_to_new((size_t)(image_cap > 0 ? image_cap : 0));
            for (int i = 0; i < image_cap; i++) old_to_new[(size_t)i] = i;
            for (int pos = 0; pos < (int)image_order.size(); pos++) {
                old_to_new[(size_t)image_order[(size_t)pos]] = pos;
            }
            if (editor_project_reorder_images(image_order.data(), (int)image_order.size())) {
                if (g_place_tool_img >= 0 && g_place_tool_img < image_cap)
                    g_place_tool_img = old_to_new[(size_t)g_place_tool_img];
                if (g_last_import_img >= 0 && g_last_import_img < image_cap)
                    g_last_import_img = old_to_new[(size_t)g_last_import_img];
                g_need_rebuild = 1;
                g_dirty = 1;
                stage_set_toast("Image order sorted for BDD save");
            } else {
                stage_set_toast("Image sort failed");
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(runtime_preview_loaded
                ? "Discard runtime preview imports before changing BDD image order."
                : "Reorders image records only; object image IDs stay the same.");
        if (runtime_preview_loaded) ImGui::EndDisabled();
        ImGui::SameLine();
    }
    bool runtime_preview_loaded_for_ids = runtime_actor_preview_imports_loaded();
    if (runtime_preview_loaded_for_ids) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Compact Image IDs")) {
        undo_save_ex("Compact Image Indices");
        for (int i = 0; i < g_ni; i++) {
            int old_idx = g_img[i].idx;
            if (old_idx == i) continue;
            g_img[i].idx = i;
            for (int oi = 0; oi < g_no; oi++)
                if (g_obj[oi].ii == old_idx) g_obj[oi].ii = i;
        }
        g_need_rebuild = 1;
        g_dirty = 1;
        stage_set_toast("Image IDs compacted");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(runtime_preview_loaded_for_ids
            ? "Discard runtime preview imports before compacting image IDs."
            : "Renumbers BDD image IDs to match current list order.");
    if (runtime_preview_loaded_for_ids) ImGui::EndDisabled();

    int shown_images = 0;
    for (int i = 0; i < g_ni; i++)
        if (image_passes_list_filter(&g_img[i], img_filter, img_search, search_idx))
            shown_images++;
    ImGui::SeparatorText("Assets");
    if (shown_images != g_ni)
        ImGui::TextDisabled("Showing %d of %d image(s)", shown_images, g_ni);

    float table_h = ImGui::GetContentRegionAvail().y;
    if (g_simple_mode) table_h -= 34.0f;
    if (table_h < 150.0f) table_h = 150.0f;
    ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_SizingStretchProp;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
    if (ImGui::BeginTable("image_asset_table", 3, table_flags, ImVec2(0, table_h))) {
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int last_module_bucket = INT_MIN;
        int sel_ii = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_obj[g_hl_obj].ii : -1;
        for (int order_pos = 0; order_pos < (int)image_order.size(); order_pos++) {
            int i = image_order[(size_t)order_pos];
            Img *im = &g_img[i];
            if (!image_passes_list_filter(im, img_filter, img_search, search_idx)) continue;
            const ImageModuleInfo *module_info = &image_modules[(size_t)i];
            int use_count = module_info->use_count;
            bool runtime_locked = runtime_actor_image_is_preview_import(im);

            if (img_sort == 9) {
                int bucket = module_info->bucket;
                if (bucket != last_module_bucket) {
                    char group_lbl[96];
                    image_module_group_label(bucket, group_lbl, sizeof group_lbl);
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 0.0f);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", group_lbl);
                    last_module_bucket = bucket;
                }
            }

            ImGui::TableNextRow(ImGuiTableRowFlags_None, 76.0f);
            if (sel_ii == im->idx)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(30, 130, 80, 70));
            else if (g_budget_relief_highlight_img_ii == im->idx)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(160, 105, 35, 70));
            else if (g_place_tool_img == i)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(35, 115, 150, 70));

            bool delete_this_image = false;
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            SDL_Texture *tex = editor_texture_at(i);
            if (tex) {
                float max_dim = (float)(im->w > im->h ? im->w : im->h);
                float sc = max_dim > 0.0f ? 42.0f / max_dim : 1.0f;
                if (sc > 1.75f) sc = 1.75f;
                if (sc < 0.05f) sc = 0.05f;
                ImVec2 im_sz(im->w * sc, im->h * sc);
                ImVec2 im_min = ImGui::GetCursorScreenPos();
                draw_editor_texture_transparent(tex, im_sz.x, im_sz.y);
                if (ImGui::IsItemHovered()) {
                    g_hover_img_ii = im->idx;
                    image_list_draw_details_tooltip(im, module_info, use_count);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    image_list_select_asset(i);
                }
                if (ImGui::BeginPopupContextItem("imgctx")) {
                    image_list_draw_action_menu(i, &delete_this_image);
                    ImGui::EndPopup();
                }
                ImVec2 im_max(im_min.x + im_sz.x, im_min.y + im_sz.y);
                if (sel_ii == im->idx)
                    ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                        IM_COL32(0, 255, 128, 255), 0, 0, 3.0f);
                if (g_budget_relief_highlight_img_ii == im->idx)
                    ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                        IM_COL32(255, 190, 70, 255), 0, 0, 3.0f);
                if (g_place_tool_img == i)
                    ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                        IM_COL32(0, 220, 255, 255), 0, 0, 2.0f);
                if (!runtime_locked && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("IMG_REORDER", &i, sizeof(int));
                    ImGui::Text("Move img %d", i);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("IMG_REORDER")) {
                        int src = *(int*)pl->Data;
                        if (src >= 0 && src < g_ni && src != i &&
                            !runtime_actor_image_is_preview_import(&g_img[src]) &&
                            !runtime_locked) {
                            undo_save();
                            if (editor_project_swap_image_slots(src, i)) {
                                g_need_rebuild = 1;
                                g_dirty = 1;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            } else {
                ImGui::Dummy(ImVec2(42.0f, 42.0f));
            }

            ImGui::TableSetColumnIndex(1);
            char asset_name[96];
            if (im->label[0])
                snprintf(asset_name, sizeof asset_name, "%s", im->label);
            else if (g_simple_mode)
                snprintf(asset_name, sizeof asset_name, "Image %d", im->idx);
            else
                snprintf(asset_name, sizeof asset_name, "Image 0x%02X", im->idx);
            ImVec4 name_col = im->lod_ref ? ImVec4(0.55f,0.9f,1.0f,1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, name_col);
            if (ImGui::Selectable(asset_name, g_place_tool_img == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                image_list_select_asset(i);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                image_list_draw_details_tooltip(im, module_info, use_count);
            if (ImGui::BeginPopupContextItem("imgrowctx")) {
                image_list_draw_action_menu(i, &delete_this_image);
                ImGui::EndPopup();
            }

            char id_line[96];
            if (g_simple_mode)
                snprintf(id_line, sizeof id_line, "id %d", im->idx);
            else
                snprintf(id_line, sizeof id_line, "id 0x%02X", im->idx);
            ImGui::TextDisabled("%s  %dx%d  pal %d  %dbpp",
                                id_line, im->w, im->h, im->pal_idx,
                                mk2_bpp_for_image(im));

            char mod_badge[96];
            image_module_badge_label(module_info, mod_badge, sizeof mod_badge);
            ImVec4 mod_col = ImVec4(0.65f, 0.75f, 0.85f, 1.0f);
            if (use_count == 0)
                mod_col = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
            else if (module_info->outside && module_info->primary_module < 0)
                mod_col = ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
            else if (module_info->mixed)
                mod_col = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(mod_col, "%s", mod_badge);
            if (im->frm || im->opals || im->pttblnum || im->anix || im->aniy)
            {
                ImGui::SameLine(0, 8);
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "anim %d", im->frm);
            }
            if (im->source[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("src %s", im->source);
                ImGui::PopStyleColor();
            }

            if (runtime_locked) ImGui::BeginDisabled();
            if (ImGui::SmallButton("+")) {
                add_image_to_view_center(i);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add object to world view");
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("S")) {
                open_sprite_resize(i, false);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resize this sprite image");
            ImGui::SameLine(0, 4);
            if (runtime_locked) ImGui::EndDisabled();
            if (ImGui::SmallButton("..."))
                ImGui::OpenPopup("img_actions");
            if (ImGui::BeginPopup("img_actions")) {
                image_list_draw_action_menu(i, &delete_this_image);
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(2);
            if (use_count == 0)
                ImGui::TextColored(ImVec4(1.0f,0.5f,0.2f,1.0f), "unused");
            else
                ImGui::TextColored(ImVec4(0.5f,0.9f,0.5f,1.0f), "x%d", use_count);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%d object placement%s", use_count,
                                  use_count == 1 ? "" : "s");
            if (delete_this_image) {
                undo_save();
                editor_project_delete_image_slot(i);
                g_need_rebuild = 1;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);

    if (g_simple_mode) {
        ImGui::Separator();
        char img_cap_lbl[48];
        const int image_cap = editor_project_image_capacity();
        snprintf(img_cap_lbl, sizeof img_cap_lbl, "Images  %d / %d", g_ni, image_cap);
        ImGui::ProgressBar(image_cap > 0 ? (float)g_ni / (float)image_cap : 0.0f, ImVec2(-1, 0), img_cap_lbl);
    }

    ImGui::End();
}
