#include "bg_editor.h"
#include "bg_editor_globals.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

void draw_world_context_overlay(void)
{
        /* world view hover image preview */
        if (!g_preview_mode && g_have_bdb && !ImGui::IsAnyItemHovered() && !ImGui::IsPopupOpen("")) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            int wx = 0, wy = 0;
            bdd_screen_to_world((int)mp.x, (int)mp.y, g_view_x, g_view_y, g_zoom, &wx, &wy);
            for (int hi = g_no - 1; hi >= 0; hi--) {
                if (g_obj_hidden[hi]) continue;
                Img *him = img_find(g_obj[hi].ii);
                if (!him) continue;
                if (wx < g_obj[hi].depth || wx >= g_obj[hi].depth + him->w) continue;
                if (wy < g_obj[hi].sy || wy >= g_obj[hi].sy + him->h) continue;
                int ht_idx = (int)(him - g_img);
                if (SDL_Texture *tex = editor_texture_at(ht_idx)) {
                    ImGui::BeginTooltip();
                    float hts = 96.0f / (float)(him->w > him->h ? him->w : him->h);
                    if (hts > 2.0f) hts = 2.0f;
                    draw_editor_texture_transparent(tex, him->w * hts, him->h * hts);
                    ImGui::Text("obj[%d]  ii=0x%02X  %dx%d  pal=%d  wx=0x%04X",
                        hi, g_obj[hi].ii, him->w, him->h, g_obj[hi].fl, g_obj[hi].wx);
                    ImGui::EndTooltip();
                }
                break;
            }
        }
        /* world view right-click context menu */
        static int s_world_ctx_obj = -1;
        static ImVec2 s_world_ctx_pos = ImVec2(0, 0);
        if (g_ctx_obj >= 0 && g_ctx_obj < g_no) {
            s_world_ctx_obj = g_ctx_obj;
            if (!g_sel_flags[s_world_ctx_obj]) {
                editor_project_clear_selection();
                g_sel_flags[s_world_ctx_obj] = 1;
            }
            g_hl_obj = s_world_ctx_obj;
            ImVec2 mp = ImGui::GetIO().MousePos;
            s_world_ctx_pos = ImVec2(mp.x + 4, mp.y + 4);
            ImGui::OpenPopup("world_ctx");
            g_ctx_obj = -1;
        }
        if (s_world_ctx_obj >= g_no)
            s_world_ctx_obj = -1;
        if (s_world_ctx_obj >= 0)
            ImGui::SetNextWindowPos(s_world_ctx_pos, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("world_ctx")) {
                int active = (s_world_ctx_obj >= 0 && s_world_ctx_obj < g_no) ? s_world_ctx_obj : -1;
                bool has_obj = active >= 0;
                bool ctx_valid = has_obj;
                if (has_obj) {
                    ImGui::Text("Object %d  (ii=0x%04X)", active, g_obj[active].ii);
                    int sel_n = selected_count();
                    if (sel_n > 1) ImGui::TextDisabled("%d selected", sel_n);
                } else {
                    ImGui::TextDisabled("No object selected");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Edit Properties", NULL, false, has_obj)) {
                    open_object_properties(active);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_obj))
                    duplicate_object_menu_targets(active);
                if (ImGui::MenuItem("Delete", "Del", false, has_obj)) {
                    delete_object_menu_targets(active);
                    s_world_ctx_obj = -1;
                    ctx_valid = false;
                    ImGui::CloseCurrentPopup();
                }
                if (ctx_valid && ImGui::MenuItem("H-Flip", "X", false, has_obj))
                    flip_object_menu_targets(active, true);
                if (ctx_valid && ImGui::MenuItem("V-Flip", "Y", false, has_obj))
                    flip_object_menu_targets(active, false);
                ImGui::Separator();
                if (ctx_valid && ImGui::MenuItem("Bring to Front", nullptr, false, has_obj)) {
                    reorder_object_menu_targets(active, true);
                    active = g_hl_obj;
                    s_world_ctx_obj = active;
                }
                if (ctx_valid && ImGui::MenuItem("Send to Back", nullptr, false, has_obj)) {
                    reorder_object_menu_targets(active, false);
                    active = g_hl_obj;
                    s_world_ctx_obj = active;
                }
                ImGui::Separator();
                if (ctx_valid && ImGui::MenuItem("Edit Block", NULL, false, has_obj))
                    edit_block_for_object(active);
                if (ctx_valid && ImGui::MenuItem("Resize Sprite...", NULL, false, has_obj)) {
                    Img *rim = img_find(g_obj[active].ii);
                    if (rim) open_sprite_resize((int)(rim - g_img), true);
                }
                if (ctx_valid && ImGui::MenuItem("Split Object...", NULL, false, has_obj)) {
                    open_split_object_dialog(active);
                    ImGui::CloseCurrentPopup();
                }
                if (ctx_valid && ImGui::MenuItem("Lower Selected Bit Depth...", NULL, false, selected_count() > 0)) {
                    g_show_group_bpp_reducer = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ctx_valid && ImGui::MenuItem("Center View", NULL, false, has_obj))
                    center_view_on_object(active);
                if (ctx_valid && ImGui::MenuItem("Select All with Same Image", NULL, false, has_obj)) {
                    select_all_with_image_ii(g_obj[active].ii);
                    g_hl_obj = active;
                }
                if (ctx_valid && ImGui::MenuItem("Select All in Layer", NULL, false, has_obj)) {
                    select_all_in_layer_byte((g_obj[active].wx >> 8) & 0xFF);
                    g_hl_obj = active;
                }
                if (ctx_valid && ImGui::MenuItem("Export Image as PNG...", NULL, false, has_obj))
                    export_object_image_png_dialog(active);
                ImGui::Separator();
                /* P13: wrap selection in new region */
                if (ctx_valid && ImGui::MenuItem("Wrap selection in Region", NULL, false,
                                                 g_simple_mode && selected_count() > 0))
                    wrap_selected_objects_in_region();
                ImGui::Separator();
                /* Layer assignment submenu */
                if (ctx_valid && ImGui::BeginMenu(g_simple_mode ? "Assign Layer" : "Assign Layer (wx)", has_obj)) {
                    static const struct { int byte; const char *name; float sf; } wlayers[] = {
                        {0x32,"Sky / far back",0.2f},{0x3C,"Mid distance",0.5f},
                        {0x40,"Floor / play",1.0f},{0x41,"Floor alt",1.0f},
                        {0x43,"Near foreground",1.2f},{0x46,"Front foreground",1.5f}
                    };
                    int cur_lyr = (g_obj[active].wx >> 8) & 0xFF;
                    for (int li = 0; li < 6; li++) {
                        bool is_cur = (cur_lyr == wlayers[li].byte);
                        char lbl[64];
                        if (g_simple_mode)
                            snprintf(lbl, sizeof lbl, "%s  (%.1fx)", wlayers[li].name, wlayers[li].sf);
                        else
                            snprintf(lbl, sizeof lbl, "%s  (0x%02X, %.1fx)", wlayers[li].name, wlayers[li].byte, wlayers[li].sf);
                        if (is_cur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.85f,0.2f,1.0f));
                        if (ImGui::MenuItem(lbl, is_cur ? "(current)" : nullptr))
                            assign_layer_to_object_targets(active, wlayers[li].byte);
                        if (is_cur) ImGui::PopStyleColor();
                    }
                    ImGui::EndMenu();
                }
                ImGui::TextDisabled("Drag to move; Alt+drag to clone");
                ImGui::EndPopup();
        }
    
    
}
