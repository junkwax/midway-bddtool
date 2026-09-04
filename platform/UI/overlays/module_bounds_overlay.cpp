#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/project_header.h"
#include "Core/world_module_utils.h"
#include "undo_manager.h"
#include "imgui.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <stdio.h>

enum ModuleBoundsResizeHandle {
    MOD_HANDLE_NONE   = 0,
    MOD_HANDLE_LEFT   = 1 << 0,
    MOD_HANDLE_RIGHT  = 1 << 1,
    MOD_HANDLE_TOP    = 1 << 2,
    MOD_HANDLE_BOTTOM = 1 << 3
};

struct ModuleBoundsScreenRect {
    int module_idx;
    char name[64];
    int x1, x2, y1, y2;             /* authored BDB rectangle, x2/y2 inclusive */
    int vx1, vy1, vx2, vy2;         /* rect actually drawn, canvas px, x2/y2 exclusive */
    int runtime_ox, runtime_oy;     /* BGND.ASM *BMOD offset when runtime */
    float sx1, sy1, sx2, sy2;
    bool runtime;
    bool locked;
};

/* Screen rect for a module, projected exactly the way the canvas draws it:
   authored bounds in BDB Source view, the BGND.ASM runtime placement in Runtime
   Layout. Both come from bdd_module_view_bounds so the picker below can never
   disagree with what the user sees. */
static bool module_bounds_project_rect(int module_idx, int window_w, int window_h,
                                       ModuleBoundsScreenRect *out)
{
    if (!out || module_idx < 0 || module_idx >= g_bdb_num_modules)
        return false;

    memset(out, 0, sizeof *out);
    out->module_idx = module_idx;
    if (!parse_module_bounds(module_idx, out->name, &out->x1, &out->x2,
                             &out->y1, &out->y2))
        return false;
    if (out->x2 < out->x1 || out->y2 < out->y1)
        return false;
    out->locked = module_is_locked(out->name);
    out->runtime = bdd_module_view_is_runtime(module_idx) != 0;
    if (out->runtime)
        bdd_module_runtime_placement(module_idx, &out->runtime_ox, &out->runtime_oy,
                                     NULL, NULL);

    if (!bdd_module_view_bounds(module_idx, &out->vx1, &out->vy1,
                                &out->vx2, &out->vy2))
        return false;

    BddScreenRect rect;
    if (!bdd_world_rect_screen_rect(out->vx1, out->vy1, out->vx2, out->vy2,
                                    g_view_x, g_view_y, g_zoom,
                                    window_w, window_h, &rect))
        return false;
    out->sx1 = (float)rect.x;
    out->sy1 = (float)rect.y;
    out->sx2 = (float)(rect.x + rect.w);
    out->sy2 = (float)(rect.y + rect.h);
    return true;
}

static int module_bounds_handle_at(const ModuleBoundsScreenRect *r, ImVec2 mouse,
                                   float tol)
{
    if (!r) return MOD_HANDLE_NONE;
    if (mouse.x < r->sx1 - tol || mouse.x > r->sx2 + tol ||
        mouse.y < r->sy1 - tol || mouse.y > r->sy2 + tol)
        return MOD_HANDLE_NONE;

    float dl = fabsf(mouse.x - r->sx1);
    float dr = fabsf(mouse.x - r->sx2);
    float dt = fabsf(mouse.y - r->sy1);
    float db = fabsf(mouse.y - r->sy2);
    bool near_l = dl <= tol;
    bool near_r = dr <= tol;
    bool near_t = dt <= tol;
    bool near_b = db <= tol;

    int h = MOD_HANDLE_NONE;
    if (near_l && near_r)
        h |= dl <= dr ? MOD_HANDLE_LEFT : MOD_HANDLE_RIGHT;
    else if (near_l)
        h |= MOD_HANDLE_LEFT;
    else if (near_r)
        h |= MOD_HANDLE_RIGHT;

    if (near_t && near_b)
        h |= dt <= db ? MOD_HANDLE_TOP : MOD_HANDLE_BOTTOM;
    else if (near_t)
        h |= MOD_HANDLE_TOP;
    else if (near_b)
        h |= MOD_HANDLE_BOTTOM;
    return h;
}

static int module_bounds_round_nearest(int value, int step)
{
    if (step <= 0) return value;
    if (value >= 0) return ((value + step / 2) / step) * step;
    return -(((-value + step / 2) / step) * step);
}

/* Screen pixels -> canvas pixels, honouring grid snap. */
static void module_bounds_drag_delta(float screen_dx, float screen_dy,
                                     int *out_dx, int *out_dy)
{
    int z = g_zoom > 0 ? g_zoom : 1;
    int dx = (int)(screen_dx / (float)z);
    int dy = (int)(screen_dy / (float)z);
    if (g_grid_snap) {
        if (g_grid_sx > 1) dx = module_bounds_round_nearest(dx, g_grid_sx);
        if (g_grid_sy > 1) dy = module_bounds_round_nearest(dy, g_grid_sy);
    }
    if (out_dx) *out_dx = dx;
    if (out_dy) *out_dy = dy;
}

static void module_bounds_set_cursor(int handle)
{
    bool x = (handle & (MOD_HANDLE_LEFT | MOD_HANDLE_RIGHT)) != 0;
    bool y = (handle & (MOD_HANDLE_TOP | MOD_HANDLE_BOTTOM)) != 0;
    if (x && y) {
        bool nwse = ((handle & MOD_HANDLE_LEFT) && (handle & MOD_HANDLE_TOP)) ||
                    ((handle & MOD_HANDLE_RIGHT) && (handle & MOD_HANDLE_BOTTOM));
        ImGui::SetMouseCursor(nwse ? ImGuiMouseCursor_ResizeNWSE
                                   : ImGuiMouseCursor_ResizeNESW);
    } else if (x) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (y) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
}

static void module_bounds_rewrite(int module_idx, const char *name,
                                  int x1, int x2, int y1, int y2)
{
    if (module_idx < 0 || module_idx >= g_bdb_num_modules)
        return;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d",
             (name && name[0]) ? name : "MOD", x1, x2, y1, y2);
    if (!editor_project_set_module_line(module_idx, line)) {
        stage_set_toast("Module bounds overlap another module; objects stay with their current module");
        return;
    }
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
}

static void module_bounds_draw_handles(ImDrawList *dl,
                                       const ModuleBoundsScreenRect *r,
                                       ImU32 col)
{
    if (!dl || !r) return;
    float cx = (r->sx1 + r->sx2) * 0.5f;
    float cy = (r->sy1 + r->sy2) * 0.5f;
    const float s = 4.0f;
    ImVec2 pts[8] = {
        ImVec2(r->sx1, r->sy1), ImVec2(cx, r->sy1), ImVec2(r->sx2, r->sy1),
        ImVec2(r->sx1, cy),                         ImVec2(r->sx2, cy),
        ImVec2(r->sx1, r->sy2), ImVec2(cx, r->sy2), ImVec2(r->sx2, r->sy2)
    };
    for (int i = 0; i < 8; i++) {
        dl->AddRectFilled(ImVec2(pts[i].x - s, pts[i].y - s),
                          ImVec2(pts[i].x + s, pts[i].y + s),
                          IM_COL32(6, 10, 16, 220), 1.0f);
        dl->AddRect(ImVec2(pts[i].x - s, pts[i].y - s),
                    ImVec2(pts[i].x + s, pts[i].y + s),
                    col, 1.0f, 0, 1.5f);
    }
}

/* Runtime Layout drags move the module's BGND.ASM placement instead of its
   authored rectangle: the box on screen IS the runtime placement there, so
   editing the BDB rect from it would move a box the user cannot see and would
   silently re-assign whatever objects the resized rect swept up. File scope so
   a view switch mid-drag cannot leave a half-finished move armed. */
static bool  s_move = false;
static int   s_move_mod = -1;
static float s_move_mouse_x = 0.0f;
static float s_move_mouse_y = 0.0f;
static int   s_move_ox = 0, s_move_oy = 0;
static char  s_move_name[64] = "";
static bool  s_move_started = false;

static void module_bounds_cancel_move(void)
{
    s_move = false;
    s_move_mod = -1;
    s_move_started = false;
}

void draw_module_bounds_overlay(void)
{
    g_module_bounds_mouse_capture = false;
    if (!g_show_module_bounds || !g_have_bdb || g_bdb_num_modules <= 0 ||
        g_preview_mode || g_game_view) {
        module_bounds_cancel_move();
        return;
    }

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouse_available = !ImGui::GetIO().WantCaptureMouse && g_cur_tool == 0;
    float tol = g_zoom > 2 ? 8.0f : 7.0f;

    static bool s_resize = false;
    static int  s_resize_mod = -1;
    static int  s_resize_handle = MOD_HANDLE_NONE;
    static float s_resize_mouse_x = 0.0f;
    static float s_resize_mouse_y = 0.0f;
    static int s_resize_x1 = 0, s_resize_x2 = 0;
    static int s_resize_y1 = 0, s_resize_y2 = 0;
    static char s_resize_name[64] = "";
    static bool s_resize_undo_saved = false;
    int selected_mod = -1;
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        Img *him = img_find(g_obj[g_hl_obj].ii);
        if (him) selected_mod = assign_module(g_obj[g_hl_obj].depth,
                                              g_obj[g_hl_obj].sy,
                                              him->w, him->h);
    }

    int hot_mod = -1;
    int hot_handle = MOD_HANDLE_NONE;
    long hot_area = 0;
    int hot_move_mod = -1;
    long hot_move_area = 0;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        ModuleBoundsScreenRect r;
        long area;
        if (!module_bounds_project_rect(m, (int)ds.x, (int)ds.y, &r))
            continue;
        if (s_resize || s_move || !mouse_available)
            continue;

        area = (long)(r.vx2 - r.vx1) * (long)(r.vy2 - r.vy1);
        if (r.runtime) {
            /* Grab the frame to slide the whole runtime placement. Only the edge
               band is hot so clicks inside a plane still reach its objects; a
               module that is already selected can also be grabbed by its body. */
            bool on_edge = module_bounds_handle_at(&r, mouse, tol) != MOD_HANDLE_NONE;
            bool in_body = module_selection_get(m) &&
                           mouse.x >= r.sx1 && mouse.x <= r.sx2 &&
                           mouse.y >= r.sy1 && mouse.y <= r.sy2;
            if ((on_edge || in_body) && (hot_move_mod < 0 || area < hot_move_area)) {
                hot_move_mod = m;
                hot_move_area = area;
            }
            continue;
        }

        int handle = module_bounds_handle_at(&r, mouse, tol);
        if (handle != MOD_HANDLE_NONE) {
            if (hot_mod < 0 || area < hot_area) {
                hot_mod = m;
                hot_handle = handle;
                hot_area = area;
            }
        }
    }

    if (!s_resize && !s_move && hot_move_mod >= 0 && mouse_available &&
        ImGui::IsMouseClicked(0)) {
        ModuleBoundsScreenRect r;
        if (module_bounds_project_rect(hot_move_mod, (int)ds.x, (int)ds.y, &r)) {
            if (r.locked) {
                stage_set_toast("Module is locked");
            } else {
                module_selection_select_only(hot_move_mod);
                s_move = true;
                s_move_mod = hot_move_mod;
                s_move_mouse_x = mouse.x;
                s_move_mouse_y = mouse.y;
                s_move_ox = r.runtime_ox;
                s_move_oy = r.runtime_oy;
                snprintf(s_move_name, sizeof s_move_name, "%s", r.name);
                s_move_started = false;
            }
        }
    }

    if (!s_resize && !s_move && hot_mod >= 0 && mouse_available && ImGui::IsMouseClicked(0)) {
        ModuleBoundsScreenRect r;
        if (module_bounds_project_rect(hot_mod, (int)ds.x, (int)ds.y, &r)) {
            if (r.locked) {
                stage_set_toast("Module is locked");
            } else {
                module_selection_select_only(hot_mod);
                s_resize = true;
                s_resize_mod = hot_mod;
                s_resize_handle = hot_handle;
                s_resize_mouse_x = mouse.x;
                s_resize_mouse_y = mouse.y;
                s_resize_x1 = r.x1;
                s_resize_x2 = r.x2;
                s_resize_y1 = r.y1;
                s_resize_y2 = r.y2;
                snprintf(s_resize_name, sizeof s_resize_name, "%s", r.name);
                s_resize_undo_saved = false;
            }
        }
    }

    int move_preview_dx = 0;
    int move_preview_dy = 0;
    if (s_move) {
        module_bounds_drag_delta(mouse.x - s_move_mouse_x, mouse.y - s_move_mouse_y,
                                 &move_preview_dx, &move_preview_dy);
        if (ImGui::IsMouseDown(0)) {
            if (move_preview_dx != 0 || move_preview_dy != 0)
                s_move_started = true;
        } else {
            if (s_move_started && (move_preview_dx != 0 || move_preview_dy != 0)) {
                /* Writes BGND.ASM only -- no BDB rectangle moves, so nothing is
                   re-assigned to another module and placements may overlap freely. */
                if (stage_bgnd_set_module_offset(s_move_name,
                                                 s_move_ox + move_preview_dx,
                                                 s_move_oy + move_preview_dy)) {
                    bdd_invalidate_stage_module_cache();
                    g_view_changed = 1;
                    char msg[128];
                    snprintf(msg, sizeof msg, "%s runtime placement %d,%d",
                             s_move_name, s_move_ox + move_preview_dx,
                             s_move_oy + move_preview_dy);
                    stage_set_toast(msg);
                }
            }
            module_bounds_cancel_move();
            move_preview_dx = 0;
            move_preview_dy = 0;
        }
    }

    if (s_resize && ImGui::IsMouseDown(0)) {
        int dx = (int)((mouse.x - s_resize_mouse_x) / (float)(g_zoom > 0 ? g_zoom : 1));
        int dy = (int)((mouse.y - s_resize_mouse_y) / (float)(g_zoom > 0 ? g_zoom : 1));
        if (g_grid_snap) {
            if (g_grid_sx > 1)
                dx = module_bounds_round_nearest(dx, g_grid_sx);
            if (g_grid_sy > 1)
                dy = module_bounds_round_nearest(dy, g_grid_sy);
        }
        int nx1 = s_resize_x1;
        int nx2 = s_resize_x2;
        int ny1 = s_resize_y1;
        int ny2 = s_resize_y2;
        if (s_resize_handle & MOD_HANDLE_LEFT)   nx1 = s_resize_x1 + dx;
        if (s_resize_handle & MOD_HANDLE_RIGHT)  nx2 = s_resize_x2 + dx;
        if (s_resize_handle & MOD_HANDLE_TOP)    ny1 = s_resize_y1 + dy;
        if (s_resize_handle & MOD_HANDLE_BOTTOM) ny2 = s_resize_y2 + dy;
        if (nx2 < nx1) {
            if (s_resize_handle & MOD_HANDLE_LEFT) nx1 = nx2;
            else nx2 = nx1;
        }
        if (ny2 < ny1) {
            if (s_resize_handle & MOD_HANDLE_TOP) ny1 = ny2;
            else ny2 = ny1;
        }
        if (nx1 != s_resize_x1 || nx2 != s_resize_x2 ||
            ny1 != s_resize_y1 || ny2 != s_resize_y2) {
            if (!s_resize_undo_saved) {
                undo_save_ex("Resize Module Frame");
                s_resize_undo_saved = true;
            }
            module_bounds_rewrite(s_resize_mod, s_resize_name,
                                  nx1, nx2, ny1, ny2);
        }
    }

    if (s_resize && !ImGui::IsMouseDown(0)) {
        if (s_resize_undo_saved) {
            char msg[128];
            int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            char name[64] = "";
            if (parse_module_bounds(s_resize_mod, name, &x1, &x2, &y1, &y2)) {
                snprintf(msg, sizeof msg, "Resized %s to %dx%d",
                         name, x2 - x1 + 1, y2 - y1 + 1);
                stage_set_toast(msg);
            }
        }
        s_resize = false;
        s_resize_mod = -1;
        s_resize_handle = MOD_HANDLE_NONE;
        s_resize_undo_saved = false;
    }

    g_module_bounds_mouse_capture = s_resize || s_move ||
                                    ((hot_mod >= 0 || hot_move_mod >= 0) && mouse_available);
    if (g_module_bounds_mouse_capture) {
        if (s_move || hot_move_mod >= 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (!s_move)
                ImGui::SetTooltip("Drag to move this module's runtime placement.\n"
                                  "Writes its BGND.ASM offset only -- the BDB rectangle and\n"
                                  "every object stay put, so runtime placements may overlap freely.");
        } else {
            module_bounds_set_cursor(s_resize ? s_resize_handle : hot_handle);
            if (!s_resize)
                ImGui::SetTooltip("Drag module edge to resize; drag a corner to resize width and height.");
        }
    }

    for (int m = 0; m < g_bdb_num_modules; m++) {
        ModuleBoundsScreenRect r;
        if (!module_bounds_project_rect(m, (int)ds.x, (int)ds.y, &r))
            continue;
        bool moving = s_move && m == s_move_mod;
        float move_px = moving ? (float)move_preview_dx * (float)(g_zoom > 0 ? g_zoom : 1) : 0.0f;
        float move_py = moving ? (float)move_preview_dy * (float)(g_zoom > 0 ? g_zoom : 1) : 0.0f;
        float sx1 = r.sx1 + move_px;
        float sy1 = r.sy1 + move_py;
        float sx2 = r.sx2 + move_px;
        float sy2 = r.sy2 + move_py;

        int pals = 0, layers = 0, first = -1;
        int objects = module_collect_stats(m, &pals, &layers, &first);
        bool selected = (m == selected_mod) || module_selection_get(m);
        bool locked = r.locked;
        bool hot = (m == hot_mod) || (m == hot_move_mod) || moving ||
                   (s_resize && m == s_resize_mod);
        ImU32 line_col = locked ? IM_COL32(255, 165, 40, 220) :
                         hot ? IM_COL32(110, 240, 255, 245) :
                         selected ? IM_COL32(255, 230, 90, 235) :
                         (pals > MK2_RUNTIME_PALETTE_SLOTS ? IM_COL32(255, 90, 90, 200) :
                          objects == 0 ? IM_COL32(115, 125, 145, 120) :
                                         IM_COL32(130, 170, 255, 170));
        ImU32 fill_col = locked ? IM_COL32(255, 150, 30, 22) :
                         selected ? IM_COL32(255, 220, 60, 26) :
                         (pals > MK2_RUNTIME_PALETTE_SLOTS ? IM_COL32(255, 70, 70, 18) :
                                         IM_COL32(80, 130, 255, 14));
        ImVec2 p0(sx1, sy1);
        ImVec2 p1(sx2, sy2);
        dl->AddRectFilled(p0, p1, fill_col);
        dl->AddRect(p0, p1, line_col, 0.0f, 0,
                    (selected || hot) ? 2.5f : (locked ? 2.0f : 1.5f));
        if (selected || hot) {
            ModuleBoundsScreenRect handle_r = r;
            handle_r.sx1 = sx1;
            handle_r.sy1 = sy1;
            handle_r.sx2 = sx2;
            handle_r.sy2 = sy2;
            module_bounds_draw_handles(dl, &handle_r, line_col);
        }

        char label[160];
        if (r.runtime)
            snprintf(label, sizeof label, "%s%d %s  runtime %d,%d  obj:%d pal:%d",
                     locked ? "[LOCKED] " : "", m, r.name,
                     r.runtime_ox + (moving ? move_preview_dx : 0),
                     r.runtime_oy + (moving ? move_preview_dy : 0),
                     objects, pals);
        else
            snprintf(label, sizeof label, "%s%d %s  obj:%d pal:%d",
                     locked ? "[LOCKED] " : "", m, r.name, objects, pals);
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
