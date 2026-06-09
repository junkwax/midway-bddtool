#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "imgui.h"

#include <limits.h>

void draw_minimap(void)
{
    if (!g_show_minimap || !g_have_bdb || g_no == 0) return;
    set_left_panel_default(ImGui::GetIO().DisplaySize.y - 190.0f, 180.0f, 140.0f);
    if (!ImGui::Begin("Minimap", &g_show_minimap)) return;

    int wx_min, wx_max, wy_min, wy_max;
    bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    if (wx_min == INT_MAX) { ImGui::TextUnformatted("No objects"); ImGui::End(); return; }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float sx = avail.x / (float)(wx_max - wx_min + 1);
    float sy = avail.y / (float)(wy_max - wy_min + 1);
    float scale = sx < sy ? sx : sy;
    if (scale < 0.01f) scale = 0.01f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y),
                      IM_COL32(10, 10, 18, 200));

    for (int i = 0; i < g_no; i++) {
        if (g_obj_hidden[i]) continue;
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;
        float ox = origin.x + (o->depth - wx_min) * scale;
        float oy = origin.y + (o->sy - wy_min) * scale;
        float ow = im->w * scale;
        float oh = im->h * scale;
        if (ow < 1.0f) ow = 1.0f;
        if (oh < 1.0f) oh = 1.0f;
        ImU32 col = (i == g_hl_obj) ? IM_COL32(255, 255, 100, 220)
                   : g_sel_flags[i] ? IM_COL32(100, 200, 255, 200)
                   : g_obj_lock[i]  ? IM_COL32(100, 100, 100, 120)
                   :                  IM_COL32(200, 200, 220, 160);
        dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh), col);
    }

    float vx = origin.x + (g_view_x - wx_min) * scale;
    float vy = origin.y + (g_view_y - wy_min) * scale;
    float vw = ImGui::GetIO().DisplaySize.x / g_zoom * scale;
    float vh = ImGui::GetIO().DisplaySize.y / g_zoom * scale;
    dl->AddRect(ImVec2(vx, vy), ImVec2(vx + vw, vy + vh),
                IM_COL32(255, 255, 255, 200), 0, 0, 1.5f);

    ImGui::InvisibleButton("##minimap", avail);
    if (ImGui::IsItemActive()) {
        ImVec2 mp = ImGui::GetMousePos();
        float cx = (mp.x - origin.x) / scale + wx_min;
        float cy = (mp.y - origin.y) / scale + wy_min;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        int center_x = 0, center_y = 0;
        bdd_screen_to_world((int)(ds.x * 0.5f), (int)(ds.y * 0.5f),
                            0, 0, g_zoom, &center_x, &center_y);
        g_view_x = (int)cx - center_x;
        g_view_y = (int)cy - center_y;
        g_view_changed = 1;
    }

    ImGui::End();
}
