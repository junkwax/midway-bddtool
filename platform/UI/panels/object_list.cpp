#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int  g_sort_col = -1; /* -1 = default layer order */
static bool g_sort_rev = false;

static void sort_objects(void)
{
    if (g_sort_col < 0 || g_sort_col > 6) return;
    std::vector<Obj> rows((size_t)g_no);
    std::vector<int> sel((size_t)g_no);
    std::vector<int> lock((size_t)g_no);
    std::vector<int> hidden((size_t)g_no);
    std::vector<int> old_index((size_t)g_no);
    for (int i = 0; i < g_no; i++) {
        rows[(size_t)i] = g_obj[i];
        sel[(size_t)i] = g_sel_flags[i];
        lock[(size_t)i] = g_obj_lock[i];
        hidden[(size_t)i] = g_obj_hidden[i];
        old_index[(size_t)i] = i;
    }
    for (int i = 1; i < g_no; i++) {
        Obj tmp = rows[(size_t)i];
        int tmp_sel = sel[(size_t)i];
        int tmp_lock = lock[(size_t)i];
        int tmp_hidden = hidden[(size_t)i];
        int tmp_old_index = old_index[(size_t)i];
        int j = i - 1;
        while (j >= 0) {
            Obj *a = &rows[(size_t)j];
            int ca = 0, cb = 0;
            auto val = [](Obj *o, int c) -> int {
                switch (c) {
                    case 0: return o->order;
                    case 1: return o->ii;
                    case 2: return o->wx;
                    case 3: return o->fl;
                    case 4: return o->depth;
                    case 5: return o->sy;
                    case 6: return (o->hfl ? 2 : 0) | (o->vfl ? 1 : 0);
                } return 0;
            };
            ca = val(a, g_sort_col); cb = val(&tmp, g_sort_col);
            bool swap = g_sort_rev ? (ca < cb) : (ca > cb);
            if (swap) {
                rows[(size_t)(j + 1)] = rows[(size_t)j];
                sel[(size_t)(j + 1)] = sel[(size_t)j];
                lock[(size_t)(j + 1)] = lock[(size_t)j];
                hidden[(size_t)(j + 1)] = hidden[(size_t)j];
                old_index[(size_t)(j + 1)] = old_index[(size_t)j];
                j--;
            } else break;
        }
        rows[(size_t)(j + 1)] = tmp;
        sel[(size_t)(j + 1)] = tmp_sel;
        lock[(size_t)(j + 1)] = tmp_lock;
        hidden[(size_t)(j + 1)] = tmp_hidden;
        old_index[(size_t)(j + 1)] = tmp_old_index;
    }
    int new_hl = -1;
    for (int i = 0; i < g_no; i++)
        if (old_index[(size_t)i] == g_hl_obj) new_hl = i;
    editor_project_replace_object_rows(rows.data(), g_no, g_no,
                                       lock.data(), hidden.data(), sel.data());
    if (new_hl >= 0) g_hl_obj = new_hl;
}

void draw_obj_list_contents(void)
{
    g_hover_obj = -1;
    static int s_last_auto_scroll_obj = -2;

    if (g_no == 0) {
        ImGui::TextUnformatted("No objects loaded.");
        return;
    }

    ImGui::Text("Layers:"); ImGui::SameLine();
    if (ImGui::SmallButton("All")) { g_obj_filter[0] = '\0'; } ImGui::SameLine();

    int dyn_layer_vals[256], dyn_layer_n = 0;
    for (int i = 0; i < g_no; i++) {
        int lv = (g_obj[i].wx >> 8) & 0xFF;
        int dup = 0;
        for (int j = 0; j < dyn_layer_n; j++) if (dyn_layer_vals[j] == lv) { dup = 1; break; }
        if (!dup && dyn_layer_n < 256) dyn_layer_vals[dyn_layer_n++] = lv;
    }
    for (int a = 0; a < dyn_layer_n - 1; a++) for (int b = a + 1; b < dyn_layer_n; b++)
        if (dyn_layer_vals[a] > dyn_layer_vals[b]) { int t = dyn_layer_vals[a]; dyn_layer_vals[a] = dyn_layer_vals[b]; dyn_layer_vals[b] = t; }

    for (int li = 0; li < dyn_layer_n; li++) {
        char layer_short[32];
        if (g_simple_mode)
            snprintf(layer_short, sizeof layer_short, "%s", layer_friendly_name(dyn_layer_vals[li]));
        else
            snprintf(layer_short, sizeof layer_short, "%02X", dyn_layer_vals[li]);
        if (ImGui::SmallButton(layer_short)) {
            snprintf(g_obj_filter, sizeof g_obj_filter, "wx%02X", dyn_layer_vals[li]);
        }
        ImGui::SameLine();
    }

    const char *sort_labels_adv[] = {"# ","ii","wx","fl","Z ","sy","fp"};
    const char *sort_labels_sim[] = {"# ","Img","Layer","Pal","X ","Y ","Flip"};
    const char **sort_labels = g_simple_mode ? sort_labels_sim : sort_labels_adv;
    ImGui::Text("Sort:"); ImGui::SameLine();
    for (int c = 0; c < 7; c++) {
        if (c > 0) ImGui::SameLine(0, 2);
        char lbl[16]; snprintf(lbl, sizeof lbl, "%s%s", sort_labels[c],
                                c == g_sort_col ? (g_sort_rev ? " ^" : " v") : "");
        if (ImGui::SmallButton(lbl)) {
            if (g_sort_col == c) { g_sort_rev = !g_sort_rev; }
            else { g_sort_col = c; g_sort_rev = false; }
            undo_save_ex("Sort Objects");
            sort_objects();
        }
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##filter", g_simple_mode ? "filter by layer, image..." : "ii=hex  p=pal  z=depth  wx=layer",
                             g_obj_filter, sizeof g_obj_filter);
    int filter_ii   = -1;
    int filter_fl   = -1;
    int filter_z    = -1;
    int filter_wx   = -1;
    if (g_obj_filter[0]) {
        char *end = NULL;
        if (g_obj_filter[0] == 'p' || g_obj_filter[0] == 'P')
            filter_fl = (int)strtol(g_obj_filter + 1, &end, 10);
        else if (g_obj_filter[0] == 'z' || g_obj_filter[0] == 'Z')
            filter_z  = (int)strtol(g_obj_filter + 1, &end, 10);
        else if (strncmp(g_obj_filter, "wx", 2) == 0)
            filter_wx = (int)strtol(g_obj_filter + 2, &end, 16);
        else
            filter_ii  = (int)strtol(g_obj_filter, &end, 16);
    }

    if (ImGui::BeginTable("obj_tbl", 10, ImGuiTableFlags_BordersV
            | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody))
    {
        float scale = ImGui::GetFontSize() / 13.0f;
        ImGui::TableSetupColumn("#",                             ImGuiTableColumnFlags_WidthFixed, 28.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Img"   : "ii", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Layer" : "wx", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Pal"   : "fl", ImGuiTableColumnFlags_WidthFixed, 30.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "X"     : "Z",  ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Y"     : "sy", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Size"  : "sz", ImGuiTableColumnFlags_WidthFixed, 42.0f * scale);
        ImGui::TableSetupColumn(g_simple_mode ? "Flip"  : "flp", ImGuiTableColumnFlags_WidthFixed, 30.0f * scale);
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 22.0f * scale);  /* lock */
        ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, 22.0f * scale);  /* hide */
        ImGui::TableHeadersRow();

        for (int i = 0; i < g_no; i++) {
            Obj *o = &g_obj[i];

            if (filter_ii >= 0 && o->ii != filter_ii) continue;
            if (filter_fl >= 0 && o->fl != filter_fl) continue;
            if (filter_z  >= 0 && o->depth != filter_z) continue;
            if (filter_wx >= 0 && ((o->wx >> 8) & 0xFF) != filter_wx) continue;
            ImGui::TableNextRow();
            if (i == g_hl_obj)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(35, 125, 165, 95));
            if (g_budget_relief_highlight_img_ii == o->ii)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(120, 92, 35, 120));
            ImGui::TableNextColumn();
            char label[32];
            snprintf(label, sizeof label, "%d", i);
            if (ImGui::Selectable(label, g_sel_flags[i] != 0, ImGuiSelectableFlags_SpanAllColumns)) {
                if (ImGui::GetIO().KeyCtrl) {
                    toggle_object_selection(i);
                } else if (ImGui::GetIO().KeyShift && g_hl_obj >= 0) {
                    int lo = g_hl_obj < i ? g_hl_obj : i;
                    int hi = g_hl_obj < i ? i : g_hl_obj;
                    for (int r = lo; r <= hi; r++) g_sel_flags[r] = 1;
                    g_hl_obj = i;
                } else {
                    editor_project_clear_selection();
                    g_sel_flags[i] = 1;
                    g_hl_obj = i;
                }
            }
            if (i == g_hl_obj && s_last_auto_scroll_obj != g_hl_obj)
                ImGui::SetScrollHereY(0.45f);
            if (ImGui::IsItemHovered()) g_hover_obj = i;
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("OBJ_REORDER", &i, sizeof(int));
                ImGui::Text("Move obj %d", i);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("OBJ_REORDER")) {
                    int src = *(int*)pl->Data;
                    if (src >= 0 && src < g_no && src != i) {
                        undo_save_ex("Reorder Objects");
                        move_object_to_index(src, i);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(1) && i >= 0) {
                g_hl_obj = i;
                if (!g_sel_flags[i]) {
                    editor_project_clear_selection();
                    g_sel_flags[i] = 1;
                }
                ImGui::OpenPopup("obj_ctx");
            }
            ImGui::TableNextColumn();
            if (g_simple_mode) ImGui::Text("%d", o->ii); else ImGui::Text("%04X", o->ii);
            ImGui::TableNextColumn();
            if (g_simple_mode) ImGui::Text("%s", layer_friendly_name((o->wx >> 8) & 0xFF));
            else ImGui::Text("%04X", o->wx);
            ImGui::TableNextColumn(); ImGui::Text("%d", o->fl);
            ImGui::TableNextColumn(); ImGui::Text("%d", o->depth);
            ImGui::TableNextColumn(); ImGui::Text("%d", o->sy);
            ImGui::TableNextColumn();
            {
                Img *szim = img_find(o->ii);
                if (szim) ImGui::Text("%dx%d", szim->w, szim->h);
            }
            ImGui::TableNextColumn(); ImGui::Text("%s%s", o->hfl ? "H" : "", o->vfl ? "V" : "");
            ImGui::TableNextColumn();
            bool lk = (g_obj_lock[i] != 0);
            if (ImGui::SmallButton(lk ? "L" : "-")) g_obj_lock[i] ^= 1;
            ImGui::TableNextColumn();
            bool hd = (g_obj_hidden[i] != 0);
            if (ImGui::SmallButton(hd ? "H" : "-")) g_obj_hidden[i] ^= 1;
        }
        ImGui::EndTable();
    }
    s_last_auto_scroll_obj = g_hl_obj;

    if (ImGui::BeginPopup("obj_ctx")) {
        int active = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_hl_obj : -1;
        bool has_obj = active >= 0;
        int sel_n = selected_count();
        if (has_obj) {
            ImGui::Text("Object %d  (ii=0x%04X)", active, g_obj[active].ii);
            if (sel_n > 1) ImGui::TextDisabled("%d selected", sel_n);
        } else {
            ImGui::TextDisabled("No object selected");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_obj))
            duplicate_object_menu_targets(active);
        if (ImGui::MenuItem("Delete", "Del", false, has_obj))
            delete_object_menu_targets(active);
        if (ImGui::MenuItem("H-Flip", "X", false, has_obj))
            flip_object_menu_targets(active, true);
        if (ImGui::MenuItem("V-Flip", "Y", false, has_obj))
            flip_object_menu_targets(active, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Resize Sprite...", NULL, false, has_obj)) {
            Img *rim = img_find(g_obj[active].ii);
            if (rim) open_sprite_resize((int)(rim - g_img), true);
        }
        if (ImGui::MenuItem("Center View", NULL, false, has_obj))
            center_view_on_object(active);
        if (ImGui::BeginMenu("Assign Palette", has_obj && g_n_pals > 0)) {
            for (int pi = 0; pi < g_n_pals; pi++) {
                char lbl[80];
                snprintf(lbl, sizeof lbl, "Pal %d: %s", pi, g_pal_name[pi]);
                bool is_cur = has_obj && g_obj[active].fl == pi;
                if (ImGui::MenuItem(lbl, is_cur ? "(current)" : NULL))
                    assign_palette_to_object_targets(active, pi);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Select All in Layer", NULL, false, has_obj)) {
            select_all_in_layer_byte((g_obj[active].wx >> 8) & 0xFF);
            g_hl_obj = active;
        }
        if (ImGui::MenuItem("Select All with Same Image", NULL, false, has_obj)) {
            select_all_with_image_ii(g_obj[active].ii);
            g_hl_obj = active;
        }
        if (ImGui::BeginMenu("Assign to Module", has_obj && g_bdb_num_modules > 0)) {
            for (int mi = 0; mi < g_bdb_num_modules; mi++) {
                char mn[64] = "";
                int x1, x2, y1, y2;
                if (sscanf(g_bdb_modules[mi], "%63s %d %d %d %d", mn, &x1, &x2, &y1, &y2) < 5)
                    continue;
                if (ImGui::MenuItem(mn))
                    assign_module_to_object_targets(active, mi);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (g_simple_mode) {
        ImGui::Separator();
        char cap_lbl[48];
        int object_cap = editor_project_object_capacity();
        snprintf(cap_lbl, sizeof cap_lbl, "Sprites  %d / %d", g_no, object_cap);
        ImGui::ProgressBar(object_cap > 0 ? (float)g_no / (float)object_cap : 0.0f, ImVec2(-1, 0), cap_lbl);
    }
}

void draw_obj_list(void)
{
    right_panel_set_next(RIGHT_PANEL_OBJECTS);
    bool open = ImGui::Begin("Objects", NULL, ImGuiWindowFlags_HorizontalScrollbar);
    right_panel_after_begin(RIGHT_PANEL_OBJECTS);
    if (open)
        draw_obj_list_contents();
    ImGui::End();
}
