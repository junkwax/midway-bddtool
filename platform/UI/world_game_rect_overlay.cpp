#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "imgui.h"

void draw_world_game_rect(void)
{
    if (!g_runtime_layout_view || g_game_view || !g_have_bdb || g_no == 0) return;

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    BddScreenRect rect;
    bdd_world_rect_screen_rect(g_scroll_pos, g_game_view_y,
                               g_scroll_pos + 400, g_game_view_y + 254,
                               g_view_x, g_view_y, g_zoom,
                               (int)ds.x, (int)ds.y, &rect);
    float sx = (float)rect.x;
    float sy = (float)rect.y;
    float sw = (float)rect.w;
    float sh = (float)rect.h;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh),
                      IM_COL32(255, 200, 60, 14));
    dl->AddRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh),
                IM_COL32(255, 190, 40, 180), 0.0f, 0, 2.0f);

    const char *lbl = "Game Screen (400x254)";
    ImVec2 ts = ImGui::CalcTextSize(lbl);
    float lx = sx + 4.0f;
    float ly = sy + 2.0f;
    dl->AddRectFilled(ImVec2(lx - 2, ly - 1),
                      ImVec2(lx + ts.x + 2, ly + ts.y + 1),
                      IM_COL32(0, 0, 0, 140), 2.0f);
    dl->AddText(ImVec2(lx, ly), IM_COL32(255, 200, 60, 220), lbl);
}
