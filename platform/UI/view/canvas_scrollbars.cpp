#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "UI/view/canvas_scrollbars.h"
#include "UI/app/editor_lifecycle.h"
#include "UI/view/welcome_screen.h"
#include "imgui.h"

#include <limits.h>

static int canvas_clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool canvas_point_in_rect(ImVec2 p, ImVec2 a, ImVec2 b)
{
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
}

static void image_grid_content_size_px(int wrap_w, int zoom, int *out_w, int *out_h)
{
    int pad = 8 * zoom;
    int cx = 0, cy = 0, row_h = 0, max_x = 0;
    if (wrap_w < 1) wrap_w = 1;
    if (pad < 1) pad = 1;

    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].w <= 0 || g_img[i].h <= 0) continue;
        int tw = g_img[i].w * zoom;
        int th = g_img[i].h * zoom;
        if (cx > 0 && cx + tw + pad > wrap_w) {
            cx = 0;
            cy += row_h + pad;
            row_h = 0;
        }
        if (row_h < th) row_h = th;
        if (max_x < cx + tw) max_x = cx + tw;
        cx += tw + pad;
    }

    if (out_w) *out_w = max_x + pad;
    if (out_h) *out_h = cy + row_h + pad;
}

void draw_canvas_scrollbars(void)
{
    static bool s_drag_h = false;
    static bool s_drag_v = false;
    static float s_drag_h_offset = 0.0f;
    static float s_drag_v_offset = 0.0f;

    g_canvas_scrollbar_mouse_capture = false;
    if (g_preview_mode || g_game_view || welcome_visible() || g_zoom <= 0 || (g_no <= 0 && g_ni <= 0)) {
        s_drag_h = false;
        s_drag_v = false;
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 ds = io.DisplaySize;
    const float bar = 12.0f;
    const float left = 6.0f;
    const float top = editor_canvas_top_y();
    const float status_h = (!g_simple_mode && g_have_bdb) ? 70.0f : 22.0f;
    float right = ds.x - 6.0f;
    float panel_left = right_panel_canvas_right_limit();
    if (panel_left > 0.0f && panel_left < right)
        right = panel_left - 8.0f;
    float bottom = ds.y - status_h - 6.0f;

    float h_track_x = left;
    float h_track_y = bottom - bar;
    float h_track_w = right - left - bar - 4.0f;
    float v_track_x = right - bar;
    float v_track_y = top;
    float v_track_h = bottom - top - bar - 4.0f;
    if (h_track_w < 96.0f || v_track_h < 96.0f) {
        s_drag_h = false;
        s_drag_v = false;
        return;
    }

    int content_x0 = 0, content_x1 = 0, content_y0 = 0, content_y1 = 0;
    int visible_w = 1, visible_h = 1;
    if (g_have_bdb && g_no > 0) {
        bdd_get_editor_layout_bounds(&content_x0, &content_x1, &content_y0, &content_y1);
        if (content_x0 == INT_MAX || content_x1 == INT_MIN ||
            content_y0 == INT_MAX || content_y1 == INT_MIN) {
            content_x0 = 0; content_x1 = 1280;
            content_y0 = 0; content_y1 = 720;
        }
        content_x0 -= 500;
        content_x1 += 500;
        content_y0 -= 500;
        content_y1 += 500;
        visible_w = (int)(h_track_w / (float)g_zoom);
        visible_h = (int)(v_track_h / (float)g_zoom);
    } else {
        int content_w_px = 0, content_h_px = 0;
        image_grid_content_size_px((int)h_track_w, g_zoom, &content_w_px, &content_h_px);
        content_x0 = 0;
        content_y0 = 0;
        content_x1 = content_w_px;
        content_y1 = content_h_px;
        visible_w = (int)h_track_w;
        visible_h = (int)v_track_h;
    }
    if (visible_w < 1) visible_w = 1;
    if (visible_h < 1) visible_h = 1;

    int span_w = content_x1 - content_x0;
    int span_h = content_y1 - content_y0;
    bool can_scroll_x = span_w > visible_w;
    bool can_scroll_y = span_h > visible_h;

    int min_x = content_x0;
    int min_y = content_y0;
    int max_x = can_scroll_x ? (content_x1 - visible_w) : g_view_x;
    int max_y = can_scroll_y ? (content_y1 - visible_h) : g_view_y;
    int old_view_x = g_view_x;
    int old_view_y = g_view_y;
    if (can_scroll_x) g_view_x = canvas_clamp_int(g_view_x, min_x, max_x);
    if (can_scroll_y) g_view_y = canvas_clamp_int(g_view_y, min_y, max_y);

    float h_handle_w = can_scroll_x ? h_track_w * ((float)visible_w / (float)span_w) : h_track_w;
    float v_handle_h = can_scroll_y ? v_track_h * ((float)visible_h / (float)span_h) : v_track_h;
    if (h_handle_w < 32.0f) h_handle_w = 32.0f;
    if (v_handle_h < 32.0f) v_handle_h = 32.0f;
    if (h_handle_w > h_track_w) h_handle_w = h_track_w;
    if (v_handle_h > v_track_h) v_handle_h = v_track_h;

    float h_free = h_track_w - h_handle_w;
    float v_free = v_track_h - v_handle_h;
    float hx = h_track_x;
    float vy = v_track_y;
    if (can_scroll_x && max_x > min_x)
        hx += ((float)(g_view_x - min_x) / (float)(max_x - min_x)) * h_free;
    if (can_scroll_y && max_y > min_y)
        vy += ((float)(g_view_y - min_y) / (float)(max_y - min_y)) * v_free;

    ImVec2 mouse = io.MousePos;
    ImVec2 h0(h_track_x, h_track_y), h1(h_track_x + h_track_w, h_track_y + bar);
    ImVec2 v0(v_track_x, v_track_y), v1(v_track_x + bar, v_track_y + v_track_h);
    ImVec2 hh0(hx, h_track_y), hh1(hx + h_handle_w, h_track_y + bar);
    ImVec2 vh0(v_track_x, vy), vh1(v_track_x + bar, vy + v_handle_h);
    /* don't grab the scrollbar when a panel overlaps the track */
    bool over_win = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    bool hover_h = !over_win && canvas_point_in_rect(mouse, h0, h1);
    bool hover_v = !over_win && canvas_point_in_rect(mouse, v0, v1);

    if (ImGui::IsMouseClicked(0)) {
        if (hover_h && can_scroll_x) {
            if (canvas_point_in_rect(mouse, hh0, hh1))
                s_drag_h_offset = mouse.x - hx;
            else
                s_drag_h_offset = h_handle_w * 0.5f;
            s_drag_h = true;
            s_drag_v = false;
        } else if (hover_v && can_scroll_y) {
            if (canvas_point_in_rect(mouse, vh0, vh1))
                s_drag_v_offset = mouse.y - vy;
            else
                s_drag_v_offset = v_handle_h * 0.5f;
            s_drag_v = true;
            s_drag_h = false;
        }
    }
    if (!ImGui::IsMouseDown(0)) {
        s_drag_h = false;
        s_drag_v = false;
    }

    bool changed = (g_view_x != old_view_x || g_view_y != old_view_y);
    if (s_drag_h && can_scroll_x && h_free > 0.0f && max_x > min_x) {
        float t = (mouse.x - s_drag_h_offset - h_track_x) / h_free;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int nv = min_x + (int)(t * (float)(max_x - min_x) + 0.5f);
        if (g_view_x != nv) { g_view_x = nv; changed = true; }
    }
    if (s_drag_v && can_scroll_y && v_free > 0.0f && max_y > min_y) {
        float t = (mouse.y - s_drag_v_offset - v_track_y) / v_free;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int nv = min_y + (int)(t * (float)(max_y - min_y) + 0.5f);
        if (g_view_y != nv) { g_view_y = nv; changed = true; }
    }
    if (changed)
        g_view_changed = 1;

    g_canvas_scrollbar_mouse_capture = hover_h || hover_v || s_drag_h || s_drag_v;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImU32 track_col = IM_COL32(25, 27, 34, 210);
    ImU32 track_hover_col = IM_COL32(35, 39, 50, 230);
    ImU32 handle_col = IM_COL32(88, 108, 140, 230);
    ImU32 handle_hover_col = IM_COL32(120, 150, 196, 245);
    dl->AddRectFilled(h0, h1, hover_h ? track_hover_col : track_col, 3.0f);
    dl->AddRectFilled(v0, v1, hover_v ? track_hover_col : track_col, 3.0f);
    dl->AddRectFilled(hh0, hh1, (hover_h || s_drag_h) ? handle_hover_col : handle_col, 3.0f);
    dl->AddRectFilled(vh0, vh1, (hover_v || s_drag_v) ? handle_hover_col : handle_col, 3.0f);
}
