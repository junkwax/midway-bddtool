#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>

void draw_module_bounds_overlay(void)
{
    if (!g_show_module_bounds || !g_have_bdb || g_bdb_num_modules <= 0 ||
        g_preview_mode || g_game_view)
        return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;

    int selected_mod = -1;
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        Img *him = img_find(g_obj[g_hl_obj].ii);
        if (him) selected_mod = assign_module(g_obj[g_hl_obj].depth,
                                              g_obj[g_hl_obj].sy,
                                              him->w, him->h);
    }

    for (int m = 0; m < g_bdb_num_modules; m++) {
        char name[64] = "";
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!parse_module_bounds(m, name, &x1, &x2, &y1, &y2)) continue;
        if (x2 < x1 || y2 < y1) continue;

        BddScreenRect rect;
        if (!bdd_world_rect_screen_rect(x1, y1, x2 + 1, y2 + 1,
                                        g_view_x, g_view_y, g_zoom,
                                        (int)ds.x, (int)ds.y, &rect))
            continue;
        float sx1 = (float)rect.x;
        float sy1 = (float)rect.y;
        float sx2 = (float)(rect.x + rect.w);
        float sy2 = (float)(rect.y + rect.h);

        int pals = 0, layers = 0, first = -1;
        int objects = module_collect_stats(m, &pals, &layers, &first);
        bool selected = (m == selected_mod);
        ImU32 line_col = selected ? IM_COL32(255, 230, 90, 235) :
                         (pals > MK2_RUNTIME_PALETTE_SLOTS ? IM_COL32(255, 90, 90, 200) :
                          objects == 0 ? IM_COL32(115, 125, 145, 120) :
                                         IM_COL32(130, 170, 255, 170));
        ImU32 fill_col = selected ? IM_COL32(255, 220, 60, 26) :
                         (pals > MK2_RUNTIME_PALETTE_SLOTS ? IM_COL32(255, 70, 70, 18) :
                                         IM_COL32(80, 130, 255, 14));
        ImVec2 p0(sx1, sy1);
        ImVec2 p1(sx2, sy2);
        dl->AddRectFilled(p0, p1, fill_col);
        dl->AddRect(p0, p1, line_col, 0.0f, 0, selected ? 2.5f : 1.5f);

        char label[128];
        snprintf(label, sizeof label, "%d %s  obj:%d pal:%d", m, name, objects, pals);
        ImVec2 ts = ImGui::CalcTextSize(label);
        if (sx2 - sx1 > ts.x + 10.0f && sy2 - sy1 > ts.y + 6.0f) {
            ImVec2 lp(sx1 + 5.0f, sy1 + 4.0f);
            dl->AddRectFilled(ImVec2(lp.x - 3.0f, lp.y - 2.0f),
                              ImVec2(lp.x + ts.x + 3.0f, lp.y + ts.y + 2.0f),
                              IM_COL32(4, 7, 12, 190), 3.0f);
            dl->AddText(lp, selected ? IM_COL32(255, 245, 180, 245)
                                     : IM_COL32(220, 235, 255, 230), label);
        }
    }
}
