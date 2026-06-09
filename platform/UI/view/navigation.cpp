#include "bg_editor.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "UI/view/navigation.h"

#include <imgui.h>
#include <climits>

void zoom_to_fit(void)
{
    if (!g_have_bdb || g_no == 0) return;

    int wx_min, wx_max, wy_min, wy_max;
    bdd_get_editor_layout_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    if (wx_min == INT_MAX) return;

    int pad_x = (g_grid_sx > 0 ? g_grid_sx : 64) * 3;
    int pad_y = (g_grid_sy > 0 ? g_grid_sy : 32) * 3;
    int bw = wx_max - wx_min + pad_x * 2;
    int bh = wy_max - wy_min + pad_y * 2;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int zx = (int)(ds.x / bw);
    int zy = (int)(ds.y / bh);
    g_zoom = (zx < zy) ? zx : zy;
    if (g_zoom < 1) g_zoom = 1;
    if (g_zoom > 8) g_zoom = 8;

    g_view_x = wx_min - pad_x - (int)(ds.x / g_zoom - bw) / 2;
    g_view_y = wy_min - pad_y - (int)(ds.y / g_zoom - bh) / 2;

    g_view_changed = 1;
}

void zoom_to_selection(void)
{
    if (!g_have_bdb || g_no == 0) return;
    int wx_min = INT_MAX, wx_max = INT_MIN, wy_min = INT_MAX, wy_max = INT_MIN;
    int found = 0;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Obj *o = &g_obj[i]; Img *im = img_find(o->ii);
        int ox = o->depth, oy = o->sy;
        if (!im) continue; found = 1;
        bdd_object_editor_origin(i, &ox, &oy);
        if (ox      < wx_min) wx_min = ox;
        if (ox+im->w > wx_max) wx_max = ox+im->w;
        if (oy      < wy_min) wy_min = oy;
        if (oy+im->h > wy_max) wy_max = oy+im->h;
    }
    if (!found) { zoom_to_fit(); return; }
    int pad_x = (g_grid_sx > 0 ? g_grid_sx : 64) * 3;
    int pad_y = (g_grid_sy > 0 ? g_grid_sy : 32) * 3;
    int bw = wx_max - wx_min + pad_x * 2, bh = wy_max - wy_min + pad_y * 2;
    if (bw < 1) bw = 1; if (bh < 1) bh = 1;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int zx = (int)(ds.x / bw), zy = (int)(ds.y / bh);
    g_zoom = (zx < zy) ? zx : zy;
    if (g_zoom < 1) g_zoom = 1; if (g_zoom > 8) g_zoom = 8;
    g_view_x = wx_min - pad_x - (int)(ds.x / g_zoom - bw) / 2;
    g_view_y = wy_min - pad_y - (int)(ds.y / g_zoom - bh) / 2;
    g_view_changed = 1;
}

void fit_game_preview_zoom_to_window(void)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int zx = (int)(ds.x * 0.68f / 400.0f);
    float top = (float)bg_editor_canvas_top_px();
    float avail_y = ds.y - top - 156.0f;
    if (avail_y < 254.0f) avail_y = 254.0f;
    int zy = (int)(avail_y / 254.0f);
    g_zoom = (zx < zy) ? zx : zy;
    if (g_zoom < 1) g_zoom = 1;
    if (g_zoom > 8) g_zoom = 8;
}

void focus_editor_on_game_preview_screen(void)
{
    if (!g_have_bdb || g_no <= 0) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int view_w = (int)(ds.x / (float)(g_zoom > 0 ? g_zoom : 1));
    int view_h = (int)(ds.y / (float)(g_zoom > 0 ? g_zoom : 1));
    if (view_w < 400) view_w = 400;
    if (view_h < 254) view_h = 254;
    g_view_x = g_scroll_pos - (view_w - 400) / 2;
    g_view_y = g_game_view_y - (view_h - 254) / 2;
    g_view_changed = 1;
}

void route_to_game_preview_screen(bool recenter_camera, bool fit_zoom)
{
    if (!g_have_bdb || g_no <= 0) return;
    if (recenter_camera)
        bdd_center_game_preview_camera();
    if (fit_zoom)
        fit_game_preview_zoom_to_window();
    focus_editor_on_game_preview_screen();
}

