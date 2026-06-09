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
            if (image_is_imported_asset(&g_img[ii])) {
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

    float win_w = ImGui::GetContentRegionAvail().x;
    int cols = (int)(win_w / 92.0f);
    if (cols < 3) cols = 3;
    if (cols > 6) cols = 6;
    float thumb_max = (win_w - (cols + 1) * 6.0f) / cols;
    if (thumb_max < 30.0f) thumb_max = 30.0f;
    if (thumb_max > 64.0f) thumb_max = 64.0f;

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
            ImGui::SetTooltip("Reorders image records only; object image IDs stay the same.");
        ImGui::SameLine();
    }
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
        ImGui::SetTooltip("Renumbers BDD image IDs to match current list order.");

    int shown_images = 0;
    for (int i = 0; i < g_ni; i++)
        if (image_passes_list_filter(&g_img[i], img_filter, img_search, search_idx))
            shown_images++;
    ImGui::SeparatorText("Assets");
    if (shown_images != g_ni)
        ImGui::TextDisabled("Showing %d of %d image(s)", shown_images, g_ni);

    int col_pos = 0;
    int last_module_bucket = INT_MIN;
    for (int order_pos = 0; order_pos < (int)image_order.size(); order_pos++) {
        int i = image_order[(size_t)order_pos];
        Img *im = &g_img[i];
        if (!image_passes_list_filter(im, img_filter, img_search, search_idx)) continue;
        if (img_sort == 9) {
            int bucket = image_modules[(size_t)i].bucket;
            if (bucket != last_module_bucket) {
                char group_lbl[96];
                image_module_group_label(bucket, group_lbl, sizeof group_lbl);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", group_lbl);
                last_module_bucket = bucket;
                col_pos = 0;
            }
        }
        if (col_pos % cols != 0) ImGui::SameLine();
        col_pos++;

        ImGui::BeginGroup();
        ImGui::PushID(i);
        SDL_Texture *tex = editor_texture_at(i);
        if (tex) {
            float sc = thumb_max / (float)(im->w > im->h ? im->w : im->h);
            if (sc > 1.25f) sc = 1.25f;
            ImVec2 im_sz(im->w * sc, im->h * sc);
            ImVec2 im_min = ImGui::GetCursorScreenPos();
            draw_editor_texture_transparent(tex, im_sz.x, im_sz.y);
            if (ImGui::IsItemHovered()) {
                g_hover_img_ii = im->idx;
                char tip[256];
                if (im->label[0])
                    snprintf(tip, sizeof tip, "%s\nsrc=%s  anipoint=(%d,%d)\nClick to arm one placement  |  Right-click for options",
                             im->label, im->source[0] ? im->source : "BDD", im->anix, im->aniy);
                else
                    snprintf(tip, sizeof tip, "0x%02X\nClick to arm one placement  |  Right-click for options", im->idx);
                ImGui::SetTooltip("%s", tip);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                g_place_tool_img = i;
                g_cur_tool = 1;
            }
            char ctx_id[32]; snprintf(ctx_id, sizeof ctx_id, "imgctx%d", i);
            if (ImGui::BeginPopupContextItem(ctx_id)) {
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
                if (im->lod_ref) ImGui::TextColored(ImVec4(0.5f,0.9f,1.0f,1.0f), "Imported or referenced by LOD");
                ImGui::Separator();
                if (ImGui::MenuItem("Arm Place Tool")) {
                    g_place_tool_img = i; g_cur_tool = 1;
                }
                if (ImGui::MenuItem("Add to Center of View") && g_no < editor_project_object_capacity()) {
                    add_image_to_view_center(i);
                }
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
                ImGui::Separator();
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
                ImGui::Separator();
                if (ImGui::MenuItem("Resize Sprite...")) {
                    open_sprite_resize(i, false);
                }
                if (ImGui::MenuItem("Select All Uses")) {
                    select_all_with_image_ii(im->idx);
                }
                ImGui::EndPopup();
            }
            int sel_ii = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_obj[g_hl_obj].ii : -1;
            if (sel_ii == im->idx) {
                ImVec2 im_max(im_min.x + im_sz.x, im_min.y + im_sz.y);
                ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                    IM_COL32(0, 255, 128, 255), 0, 0, 3.0f);
            }
            if (g_budget_relief_highlight_img_ii == im->idx) {
                ImVec2 im_max(im_min.x + im_sz.x, im_min.y + im_sz.y);
                ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                    IM_COL32(255, 190, 70, 255), 0, 0, 3.0f);
            }
            if (g_place_tool_img == i) {
                ImVec2 im_max(im_min.x + im_sz.x, im_min.y + im_sz.y);
                ImGui::GetWindowDrawList()->AddRect(im_min, im_max,
                    IM_COL32(0, 220, 255, 255), 0, 0, 2.0f);
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("IMG_REORDER", &i, sizeof(int));
                ImGui::Text("Move img %d", i);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("IMG_REORDER")) {
                    int src = *(int*)pl->Data;
                    if (src >= 0 && src < g_ni && src != i) {
                        undo_save();
                        if (editor_project_swap_image_slots(src, i)) {
                            g_need_rebuild = 1;
                            g_dirty = 1;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        int use_count = image_modules[(size_t)i].use_count;

        char idx_lbl[20];
        if (g_simple_mode)
            snprintf(idx_lbl, sizeof idx_lbl, "Img %d", im->idx);
        else
            snprintf(idx_lbl, sizeof idx_lbl, "0x%02X", im->idx);
        if (ImGui::SmallButton(idx_lbl)) {
            snprintf(g_img_edit_buf, sizeof g_img_edit_buf, "%X", im->idx);
            g_img_edit_idx = i;
        }
        ImGui::SameLine();
        /* Size and palette share one line; full source/anipoint detail lives in
           the hover tooltip and right-click menu to keep each card compact. */
        if (g_simple_mode)
            ImGui::Text("%dx%d  palette %d", im->w, im->h, im->pal_idx);
        else
            ImGui::Text("%dx%d  pal=%d", im->w, im->h, im->pal_idx);
        if (im->label[0]) {
            if (im->lod_ref)
                ImGui::TextColored(ImVec4(0.55f,0.9f,1.0f,1.0f), "LOD %s", im->label);
            else
                ImGui::TextWrapped("%s", im->label);
        } else if (im->source[0]) {
            ImGui::TextDisabled("%s", im->source);
        }
        if (use_count == 0)
            ImGui::TextColored(ImVec4(1.0f,0.5f,0.2f,1.0f), "unused");
        else
            ImGui::TextColored(ImVec4(0.5f,0.9f,0.5f,1.0f), "x%d", use_count);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d object%s reference this image", use_count, use_count == 1 ? "" : "s");
        {
            char mod_badge[96];
            image_module_badge_label(&image_modules[(size_t)i], mod_badge, sizeof mod_badge);
            ImVec4 mod_col = ImVec4(0.65f, 0.75f, 0.85f, 1.0f);
            if (use_count == 0)
                mod_col = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
            else if (image_modules[(size_t)i].outside && image_modules[(size_t)i].primary_module < 0)
                mod_col = ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
            else if (image_modules[(size_t)i].mixed)
                mod_col = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(mod_col, "%s", mod_badge);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%d placement%s; sort by Module to group images by LOAD2 module.",
                                  use_count, use_count == 1 ? "" : "s");
            }
        }
        bool delete_this_image = false;
        if (ImGui::SmallButton("+")) {
            add_image_to_view_center(i);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add object to world view");
        ImGui::SameLine();
        if (ImGui::SmallButton("Size")) {
            open_sprite_resize(i, false);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resize this sprite image");
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
            ImGui::OpenPopup("img_actions");
        if (ImGui::BeginPopup("img_actions")) {
            if (ImGui::MenuItem("Replace from PNG...")) {
                char path[512] = "";
                if (file_dialog_open("Replace from PNG",
                    "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path))
                {
                    undo_save();
                    reimport_image(i, path);
                }
            }
            if (ImGui::MenuItem("Add chopped to center", NULL, false, g_no < editor_project_object_capacity())) {
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                int x = 0, y = 0;
                bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                                    g_view_x, g_view_y, g_zoom, &x, &y);
                int pal = (im->pal_idx >= 0) ? im->pal_idx : 0;
                chop_image_to_map(i, x, y, 0x4100, pal, false, false, -1, true);
            }
            if (ImGui::MenuItem("Export as TGA"))
                export_image_tga(im);
            if (ImGui::MenuItem("Export as PNG"))
                export_image_png(im);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete image"))
                delete_this_image = true;
            ImGui::EndPopup();
        }
        if (delete_this_image) {
            undo_save();
            editor_project_delete_image_slot(i);
            g_need_rebuild = 1;
            ImGui::PopID();
            ImGui::EndGroup();
            break;
        }
        ImGui::PopID();
        ImGui::EndGroup();
    }

    if (g_simple_mode) {
        ImGui::Separator();
        char img_cap_lbl[48];
        const int image_cap = editor_project_image_capacity();
        snprintf(img_cap_lbl, sizeof img_cap_lbl, "Images  %d / %d", g_ni, image_cap);
        ImGui::ProgressBar(image_cap > 0 ? (float)g_ni / (float)image_cap : 0.0f, ImVec2(-1, 0), img_cap_lbl);
    }

    ImGui::End();
}
