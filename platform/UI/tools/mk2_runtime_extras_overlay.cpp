#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct RuntimeOverlayBounds {
    int x1, y1, x2, y2;
    int valid;
};

static void runtime_overlay_bounds_reset(RuntimeOverlayBounds *b)
{
    if (!b) return;
    b->x1 = b->y1 = b->x2 = b->y2 = 0;
    b->valid = 0;
}

static void runtime_overlay_bounds_add_rect(RuntimeOverlayBounds *b, int x1, int y1, int x2, int y2)
{
    if (!b || x2 <= x1 || y2 <= y1) return;
    if (!b->valid) {
        b->x1 = x1; b->y1 = y1; b->x2 = x2; b->y2 = y2;
        b->valid = 1;
        return;
    }
    if (x1 < b->x1) b->x1 = x1;
    if (y1 < b->y1) b->y1 = y1;
    if (x2 > b->x2) b->x2 = x2;
    if (y2 > b->y2) b->y2 = y2;
}

static int runtime_overlay_rounded_div_nearest(int value, int step)
{
    if (step <= 1) return value;
    if (value >= 0) return (value + step / 2) / step;
    return -((-value + step / 2) / step);
}

static bool runtime_extra_text_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            unsigned char a = (unsigned char)h[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static bool runtime_extra_uses_floor_screen_y(const RuntimeExtraGuide *e)
{
    return e && (runtime_extra_text_contains_ci(e->asset, "FL_") ||
                 runtime_extra_text_contains_ci(e->label, "floor"));
}

#define RectBounds RuntimeOverlayBounds
#define bounds_reset runtime_overlay_bounds_reset
#define bounds_add_rect runtime_overlay_bounds_add_rect
#define rounded_div_nearest runtime_overlay_rounded_div_nearest
static bool runtime_extra_screen_rect(const RuntimeExtraGuide *e, ImVec2 *p0, ImVec2 *p1)
{
    if (!e || !p0 || !p1 || g_zoom <= 0) return false;
    Img *im = NULL;
    int img_i = find_img_by_label_casefold(e->asset);
    if (img_i >= 0 && img_i < g_ni) im = &g_img[img_i];
    int rx = e->x, ry = e->y, rw = e->w, rh = e->h;
    runtime_guide_visual_rect(e, im, &rx, &ry, &rw, &rh);
    if (g_game_view) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BddScreenRect viewport;
        bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);
        int screen_y = runtime_extra_uses_floor_screen_y(e)
                     ? bdd_runtime_floor_screen_y(ry)
                     : ry - g_game_view_y;
        float sx = (float)viewport.x + ((float)rx - (float)g_scroll_pos * e->scroll) * (float)g_zoom;
        float sy = (float)viewport.y + (float)screen_y * (float)g_zoom;
        *p0 = ImVec2(sx, sy);
        *p1 = ImVec2(sx + (float)rw * (float)g_zoom, sy + (float)rh * (float)g_zoom);
        return true;
    }

    float sx = ((float)rx - (float)g_view_x) * (float)g_zoom;
    float sy = ((float)ry - (float)g_view_y) * (float)g_zoom;
    *p0 = ImVec2(sx, sy);
    *p1 = ImVec2(sx + (float)rw * (float)g_zoom, sy + (float)rh * (float)g_zoom);
    return true;
}

static void runtime_extra_hit_rect(const ImVec2 *p0, const ImVec2 *p1, ImVec2 *h0, ImVec2 *h1)
{
    if (!p0 || !p1 || !h0 || !h1) return;
    const float pad = 10.0f;
    const float min_size = 24.0f;
    *h0 = ImVec2(p0->x - pad, p0->y - pad);
    *h1 = ImVec2(p1->x + pad, p1->y + pad);
    float cx = (p0->x + p1->x) * 0.5f;
    float cy = (p0->y + p1->y) * 0.5f;
    if (h1->x - h0->x < min_size) {
        h0->x = cx - min_size * 0.5f;
        h1->x = cx + min_size * 0.5f;
    }
    if (h1->y - h0->y < min_size) {
        h0->y = cy - min_size * 0.5f;
        h1->y = cy + min_size * 0.5f;
    }
}

static int runtime_guide_bounds_at(int guide_idx, RectBounds *out)
{
    if (guide_idx < 0 || guide_idx >= tower_runtime_guide_count() || !out) return 0;
    const RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    bounds_reset(out);
    bounds_add_rect(out, e->x, e->y, e->x + e->w, e->y + e->h);
    return out->valid;
}

static int runtime_editor_object_bounds_at(int obj_idx, RectBounds *out)
{
    if (obj_idx < 0 || obj_idx >= g_no || !out) return 0;
    Img *im = img_find(g_obj[obj_idx].ii);
    if (!im || im->w <= 0 || im->h <= 0) return 0;
    int ox = g_obj[obj_idx].depth;
    int oy = g_obj[obj_idx].sy;
    gv_object_origin(obj_idx, &ox, &oy);
    bounds_reset(out);
    bounds_add_rect(out, ox, oy, ox + im->w, oy + im->h);
    return out->valid;
}

static float runtime_extra_screen_x_for_world(const RuntimeExtraGuide *e, int world_x)
{
    if (g_game_view) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BddScreenRect viewport;
        bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);
        return (float)viewport.x + ((float)world_x - (float)g_scroll_pos * e->scroll) * (float)g_zoom;
    }
    return ((float)world_x - (float)g_view_x) * (float)g_zoom;
}

static float runtime_extra_screen_y_for_world(const RuntimeExtraGuide *e, int world_y)
{
    if (g_game_view) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BddScreenRect viewport;
        bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);
        int screen_y = runtime_extra_uses_floor_screen_y(e)
                     ? bdd_runtime_floor_screen_y(world_y)
                     : world_y - g_game_view_y;
        return (float)viewport.y + (float)screen_y * (float)g_zoom;
    }
    return ((float)world_y - (float)g_view_y) * (float)g_zoom;
}

static void runtime_consider_snap_pair(int moving, int target, int snap, int *best_adj, int *snap_world)
{
    if (!best_adj || !snap_world) return;
    int adj = target - moving;
    if (abs(adj) <= snap && abs(adj) < abs(*best_adj)) {
        *best_adj = adj;
        *snap_world = target;
    }
}

static void runtime_consider_snap_target(const RectBounds *moving, const RectBounds *target,
                                         int snap, int *best_ax, int *best_ay,
                                         int *snap_x, int *snap_y)
{
    if (!moving || !moving->valid || !target || !target->valid) return;
    int mcx = (moving->x1 + moving->x2) / 2;
    int mcy = (moving->y1 + moving->y2) / 2;
    int tcx = (target->x1 + target->x2) / 2;
    int tcy = (target->y1 + target->y2) / 2;
    int xpairs[7][2] = {
        { moving->x1, target->x1 }, { moving->x2, target->x2 },
        { moving->x1, target->x2 }, { moving->x2, target->x1 },
        { mcx, tcx },               { moving->x1, tcx },
        { moving->x2, tcx }
    };
    int ypairs[7][2] = {
        { moving->y1, target->y1 }, { moving->y2, target->y2 },
        { moving->y1, target->y2 }, { moving->y2, target->y1 },
        { mcy, tcy },               { moving->y1, tcy },
        { moving->y2, tcy }
    };
    for (int p = 0; p < 7; p++) {
        runtime_consider_snap_pair(xpairs[p][0], xpairs[p][1], snap, best_ax, snap_x);
        runtime_consider_snap_pair(ypairs[p][0], ypairs[p][1], snap, best_ay, snap_y);
    }
}

static void runtime_snap_drag_position(int guide_idx, int *nx, int *ny,
                                       int *snap_x, int *snap_y,
                                       bool *has_snap_x, bool *has_snap_y)
{
    if (snap_x) *snap_x = 0;
    if (snap_y) *snap_y = 0;
    if (has_snap_x) *has_snap_x = false;
    if (has_snap_y) *has_snap_y = false;
    if (guide_idx < 0 || guide_idx >= tower_runtime_guide_count() || !nx || !ny) return;
    if (ImGui::GetIO().KeyAlt) return;

    const RuntimeExtraGuide *e = &g_tower_runtime_guides[guide_idx];
    if (g_grid_snap) {
        if (g_grid_sx > 1) *nx = rounded_div_nearest(*nx, g_grid_sx) * g_grid_sx;
        if (g_grid_sy > 1) *ny = rounded_div_nearest(*ny, g_grid_sy) * g_grid_sy;
    }

    int snap = bg_editor_snap_dist();
    if (snap < 1) return;

    RectBounds moving;
    bounds_reset(&moving);
    bounds_add_rect(&moving, *nx, *ny, *nx + e->w, *ny + e->h);
    if (!moving.valid) return;

    int best_ax = snap + 1;
    int best_ay = snap + 1;
    int tx = 0;
    int ty = 0;

    for (int i = 0; i < tower_runtime_guide_count(); i++) {
        if (i == guide_idx) continue;
        RectBounds tb;
        if (runtime_guide_bounds_at(i, &tb))
            runtime_consider_snap_target(&moving, &tb, snap, &best_ax, &best_ay, &tx, &ty);
    }
    for (int i = 0; i < g_no; i++) {
        if (g_obj_hidden[i]) continue;
        RectBounds tb;
        if (runtime_editor_object_bounds_at(i, &tb))
            runtime_consider_snap_target(&moving, &tb, snap, &best_ax, &best_ay, &tx, &ty);
    }

    if (abs(best_ax) <= snap) {
        *nx += best_ax;
        if (snap_x) *snap_x = tx;
        if (has_snap_x) *has_snap_x = true;
    }
    if (abs(best_ay) <= snap) {
        *ny += best_ay;
        if (snap_y) *snap_y = ty;
        if (has_snap_y) *has_snap_y = true;
    }
}


void draw_mk2_runtime_extras_overlay(void)
{
    g_runtime_guide_mouse_capture = false;
    if (!g_runtime_extras_overlay || !g_have_bdb || !mk2_current_stage_has_known_runtime_extras())
        return;
    if (!g_show_borders) return;
    if (g_preview_mode) return;
    tower_runtime_guides_init_once();

    static bool s_drag_runtime_guide = false;
    static int  s_drag_runtime_idx = -1;
    static int  s_runtime_ctx_idx = -1;
    static ImVec2 s_drag_runtime_mouse = ImVec2(0, 0);
    static int s_drag_runtime_x = 0;
    static int s_drag_runtime_y = 0;
    static int s_snap_x = 0;
    static int s_snap_y = 0;
    static bool s_has_snap_x = false;
    static bool s_has_snap_y = false;
    static bool s_drag_runtime_saved_undo = false;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered = -1;
    for (int i = 0; i < tower_runtime_guide_count(); i++) {
        const RuntimeExtraGuide *e = &g_tower_runtime_guides[i];
        ImVec2 p0, p1;
        if (!runtime_extra_screen_rect(e, &p0, &p1)) continue;
        if (p1.x < -200.0f || p1.y < -200.0f || p0.x > ImGui::GetIO().DisplaySize.x + 200.0f ||
            p0.y > ImGui::GetIO().DisplaySize.y + 200.0f)
            continue;

        dl->AddRectFilled(p0, p1, e->color);
        ImVec2 h0, h1;
        runtime_extra_hit_rect(&p0, &p1, &h0, &h1);
        bool hot = mouse.x >= h0.x && mouse.x <= h1.x && mouse.y >= h0.y && mouse.y <= h1.y;
        if (hot) hovered = i;
        ImU32 outline = (i == g_tower_runtime_selected) ? IM_COL32(255, 255, 255, 240) :
                       (hot ? IM_COL32(255, 245, 190, 235) : IM_COL32(255, 220, 140, 210));
        if (hot || i == g_tower_runtime_selected)
            dl->AddRect(h0, h1, IM_COL32(255, 255, 255, hot ? 80 : 45), 2.0f, 0, 1.0f);
        dl->AddRect(p0, p1, outline, 0.0f, 0, (i == g_tower_runtime_selected || hot) ? 2.5f : 1.5f);
        if (g_runtime_extras_labels) {
            char label[96];
            snprintf(label, sizeof label, "%s  %s", e->label, e->source);
            ImVec2 ts = ImGui::CalcTextSize(label);
            ImVec2 lp(p0.x + 4.0f, p0.y + 4.0f);
            dl->AddRectFilled(ImVec2(lp.x - 2.0f, lp.y - 1.0f),
                              ImVec2(lp.x + ts.x + 2.0f, lp.y + ts.y + 1.0f),
                              IM_COL32(0, 0, 0, 150), 2.0f);
            dl->AddText(lp, IM_COL32(255, 230, 170, 240), label);
        }
    }

    bool mouse_available = !ImGui::GetIO().WantCaptureMouse;
    g_runtime_guide_mouse_capture = s_drag_runtime_guide || (mouse_available && hovered >= 0);
    if ((hovered >= 0 && mouse_available) || s_drag_runtime_guide)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (!s_drag_runtime_guide && mouse_available && hovered >= 0 && ImGui::IsMouseClicked(0)) {
        s_drag_runtime_guide = true;
        s_drag_runtime_idx = hovered;
        g_tower_runtime_selected = hovered;
        s_drag_runtime_mouse = mouse;
        s_drag_runtime_x = g_tower_runtime_guides[hovered].x;
        s_drag_runtime_y = g_tower_runtime_guides[hovered].y;
        s_has_snap_x = s_has_snap_y = false;
        s_drag_runtime_saved_undo = false;
        int obj_i = runtime_guide_existing_object_for_index(hovered);
        if (obj_i >= 0) {
            undo_save_ex("Move Runtime Extra");
            s_drag_runtime_saved_undo = true;
            editor_project_clear_selection();
            g_sel_flags[obj_i] = 1;
            g_hl_obj = obj_i;
        }
    }
    if (!s_drag_runtime_guide && mouse_available && hovered >= 0 && ImGui::IsMouseClicked(1)) {
        s_runtime_ctx_idx = hovered;
        g_tower_runtime_selected = hovered;
        ImGui::OpenPopup("runtime_extra_ctx");
    }

    if (s_runtime_ctx_idx >= tower_runtime_guide_count())
        s_runtime_ctx_idx = -1;
    if (ImGui::BeginPopup("runtime_extra_ctx")) {
        bool has_guide = s_runtime_ctx_idx >= 0 && s_runtime_ctx_idx < tower_runtime_guide_count();
        RuntimeExtraGuide *e = has_guide ? &g_tower_runtime_guides[s_runtime_ctx_idx] : NULL;
        int placements = has_guide ? runtime_guide_existing_object_count(e) : 0;
        if (has_guide) {
            ImGui::Text("%s", e->label[0] ? e->label : e->asset);
            ImGui::TextDisabled("%s", e->source);
            ImGui::TextDisabled("%d baked placement%s", placements, placements == 1 ? "" : "s");
        } else {
            ImGui::TextDisabled("No runtime guide");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select BDB Placement", NULL, false, has_guide && placements > 0)) {
            int selected = select_runtime_guide_objects(s_runtime_ctx_idx);
            char msg[128];
            snprintf(msg, sizeof msg, "Selected %d runtime placement(s)", selected);
            stage_set_toast(msg);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Delete BDB Placement", "Del", false, has_guide && placements > 0)) {
            int removed = delete_runtime_guide_objects(s_runtime_ctx_idx);
            char msg[128];
            snprintf(msg, sizeof msg, "Deleted %d runtime placement(s)", removed);
            stage_set_toast(msg);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Delete All Runtime Placements", NULL, false, has_guide)) {
            int removed = delete_all_runtime_guide_objects(true);
            char msg[128];
            snprintf(msg, sizeof msg, "Deleted %d runtime placement(s)", removed);
            stage_set_toast(msg);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Hide Guide Overlay This Session", NULL, false, has_guide)) {
            hide_runtime_guide_for_session(s_runtime_ctx_idx);
            s_runtime_ctx_idx = -1;
            stage_set_toast("Runtime guide overlay hidden for this session");
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Hide All Runtime Guides This Session", NULL, false, has_guide)) {
            int hidden = hide_all_runtime_guides_for_session();
            s_runtime_ctx_idx = -1;
            char msg[128];
            snprintf(msg, sizeof msg, "Hidden %d runtime guide(s)", hidden);
            stage_set_toast(msg);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (s_drag_runtime_guide) {
        if (!ImGui::IsMouseDown(0) || s_drag_runtime_idx < 0 || s_drag_runtime_idx >= tower_runtime_guide_count()) {
            s_drag_runtime_guide = false;
            s_drag_runtime_idx = -1;
            s_has_snap_x = s_has_snap_y = false;
            s_drag_runtime_saved_undo = false;
        } else {
            RuntimeExtraGuide *e = &g_tower_runtime_guides[s_drag_runtime_idx];
            float scale = (g_zoom > 0) ? (float)g_zoom : 1.0f;
            int nx = s_drag_runtime_x + (int)((mouse.x - s_drag_runtime_mouse.x) / scale);
            int ny = s_drag_runtime_y + (int)((mouse.y - s_drag_runtime_mouse.y) / scale);
            runtime_snap_drag_position(s_drag_runtime_idx, &nx, &ny,
                                       &s_snap_x, &s_snap_y,
                                       &s_has_snap_x, &s_has_snap_y);
            if (nx != e->x || ny != e->y) {
                e->x = nx;
                e->y = ny;
                g_tower_runtime_guides_dirty = true;
                if (s_drag_runtime_saved_undo)
                    sync_runtime_guide_object_placement(s_drag_runtime_idx);
                g_view_changed = 1;
            }
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            if (s_has_snap_x) {
                float sx = runtime_extra_screen_x_for_world(e, s_snap_x);
                dl->AddLine(ImVec2(sx, 0.0f), ImVec2(sx, ds.y), IM_COL32(100, 210, 255, 190), 1.5f);
            }
            if (s_has_snap_y) {
                float sy = runtime_extra_screen_y_for_world(e, s_snap_y);
                dl->AddLine(ImVec2(0.0f, sy), ImVec2(ds.x, sy), IM_COL32(100, 210, 255, 190), 1.5f);
            }
        }
    }
}

/* ---- game view interactive overlay -------------------------------- */

