#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "Core/image_processing.h"
#include "Core/world_module_utils.h"
#include "UI/view/game_view_controls.h"
#include "UI/view/navigation.h"
#include "UI/actions/object_position_undo.h"
#include "UI/view/world_view_helpers.h"
#include "undo_manager.h"
#include <imgui.h>
#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

struct GameViewParallaxPreset {
    const char *label;
    float factor;
};

static const GameViewParallaxPreset k_game_view_parallax_presets[] = {
    { "Locked screen 0.00x", 0.00f },
    { "Very far 0.08x",     0.08f },
    { "Far slow 0.15x",     0.15f },
    { "Back 0.25x",         0.25f },
    { "Mid 0.50x",          0.50f },
    { "Near 0.75x",         0.75f },
    { "Playfield 1.00x",    1.00f },
};

static bool s_game_view_parallax_highlight_module = true;
static int  s_game_view_parallax_module = -1;

static char s_game_view_frame_key[640] = "";
static int  s_game_view_frame_w = 400;
static int  s_game_view_frame_h = 254;
static int  s_game_view_frame_l = 0;
static int  s_game_view_frame_r = 0;
static int  s_game_view_default_frame_w = 400;
static int  s_game_view_default_frame_h = 254;
static int  s_game_view_default_frame_l = 0;
static int  s_game_view_default_frame_r = 0;
static bool s_game_view_default_cam_ok = false;
static int  s_game_view_default_cam_x = 0;
static int  s_game_view_default_cam_y = 0;
static bool s_game_view_default_ground_ok = false;
static int  s_game_view_default_ground_y = 200;

static float game_view_tools_avail_width(void)
{
    float w = ImGui::GetContentRegionAvail().x;
    return w < 80.0f ? 80.0f : w;
}

static float game_view_tools_button_width(const char *label)
{
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static float game_view_tools_checkbox_width(const char *label)
{
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
           ImGui::CalcTextSize(label).x;
}

static void game_view_tools_same_line_if_fits(float next_w, float spacing = 6.0f)
{
    if (ImGui::GetContentRegionAvail().x >= next_w + spacing)
        ImGui::SameLine(0.0f, spacing);
}

static void game_view_tools_same_line_button_if_fits(const char *label,
                                                     float spacing = 6.0f)
{
    game_view_tools_same_line_if_fits(game_view_tools_button_width(label), spacing);
}

static void game_view_tools_text_disabled_wrapped(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + game_view_tools_avail_width());
    ImGui::TextUnformatted(buf);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

static void game_view_tools_section_gap(void)
{
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

static bool game_view_tools_collapsing_header(const char *label,
                                              ImGuiTreeNodeFlags flags = 0)
{
    game_view_tools_section_gap();
    return ImGui::CollapsingHeader(label, flags);
}

struct GameViewSizeAuditItem {
    char action[48];
    char target[64];
    char detail[160];
    size_t save;
    int img_i;
    int pal_i;
};

static const char *game_view_runtime_parallax_label(float s)
{
    if (s <= 0.01f) return "screen-fixed";
    if (s > 0.98f && s < 1.02f) return "playfield";
    return s < 1.0f ? "far slow" : "near fast";
}

static bool game_view_runtime_name_ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

static int game_view_selected_object_module(void)
{
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        int mod = assign_module(g_obj[i].depth, g_obj[i].sy, ow, oh);
        if (mod >= 0)
            return mod;
    }
    return -1;
}

static int game_view_selected_object_in_module(int module_idx)
{
    if (module_idx < 0)
        return -1;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, ow, oh) == module_idx)
            return i;
    }
    return -1;
}

static bool game_view_module_name(int module_idx, char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (module_idx < 0 || module_idx >= g_bdb_num_modules) return false;
    return sscanf(g_bdb_modules[module_idx], "%63s", out) == 1 && out[0];
}

static bool game_view_module_plane_info(const char *module_name,
                                        int *ox, int *oy,
                                        float *scroll, int *rank)
{
    int plane_count = bdd_stage_plane_count();
    for (int p = 0; p < plane_count; p++) {
        char pn[32] = "";
        int pox = 0, poy = 0, prank = -1;
        float pscroll = 1.0f;
        if (!bdd_stage_plane_info(p, pn, sizeof pn, &pox, &poy, &pscroll, &prank))
            continue;
        if (!game_view_runtime_name_ieq(pn, module_name))
            continue;
        if (ox) *ox = pox;
        if (oy) *oy = poy;
        if (scroll) *scroll = pscroll;
        if (rank) *rank = prank;
        return true;
    }
    return false;
}

static bool game_view_builder_offset_for_selected_block(int module_idx,
                                                        const char *module_name,
                                                        int *ox, int *oy,
                                                        int *obj_out)
{
    int obj_i = game_view_selected_object_in_module(module_idx);
    if (obj_out) *obj_out = obj_i;
    if (obj_i < 0 || !module_name || !module_name[0] || !ox || !oy)
        return false;

    Img *im = img_find(g_obj[obj_i].ii);
    if (!im)
        return false;
    int hdr = (int)(im - g_img);
    if (hdr < 0 || hdr >= g_ni)
        return false;

    static BddBgndBlock blocks[512];
    int nb = bdd_stage_module_blocks(module_name, blocks,
                                     (int)(sizeof blocks / sizeof blocks[0]));
    if (nb <= 0)
        return false;

    int best = -1;
    int best_score = INT_MAX;
    int mx1 = 0, my1 = 0;
    parse_module_bounds(module_idx, NULL, &mx1, NULL, &my1, NULL);
    int local_x = g_obj[obj_i].depth - mx1;
    int local_y = g_obj[obj_i].sy - my1;
    for (int b = 0; b < nb; b++) {
        if (blocks[b].hdr != hdr)
            continue;
        int score = 0;
        if (blocks[b].pal != g_obj[obj_i].fl)
            score += 100000;
        int dx = blocks[b].x - local_x;
        int dy = blocks[b].y - local_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        score += dx + dy;
        if (score < best_score) {
            best_score = score;
            best = b;
        }
    }
    if (best < 0)
        return false;

    *ox = g_obj[obj_i].depth - blocks[best].x;
    *oy = g_obj[obj_i].sy - blocks[best].y;
    return true;
}

static bool game_view_module_block_bounds(const char *module_name,
                                          int *x1, int *x2,
                                          int *y1, int *y2)
{
    static BddBgndBlock blocks[768];
    int n = bdd_stage_module_blocks(module_name, blocks,
                                    (int)(sizeof blocks / sizeof blocks[0]));
    if (n <= 0)
        return false;
    int bx1 = INT_MAX, bx2 = INT_MIN;
    int by1 = INT_MAX, by2 = INT_MIN;
    for (int i = 0; i < n; i++) {
        int hdr = blocks[i].hdr;
        if (hdr < 0 || hdr >= g_ni)
            continue;
        Img *im = &g_img[hdr];
        if (im->w <= 0 || im->h <= 0)
            continue;
        if (blocks[i].x < bx1) bx1 = blocks[i].x;
        if (blocks[i].y < by1) by1 = blocks[i].y;
        if (blocks[i].x + im->w > bx2) bx2 = blocks[i].x + im->w;
        if (blocks[i].y + im->h > by2) by2 = blocks[i].y + im->h;
    }
    if (bx1 == INT_MAX || bx2 == INT_MIN || by1 == INT_MAX || by2 == INT_MIN)
        return false;
    if (x1) *x1 = bx1;
    if (x2) *x2 = bx2;
    if (y1) *y1 = by1;
    if (y2) *y2 = by2;
    return true;
}

static int game_view_parallax_preset_for(float factor)
{
    int best = -1;
    float best_diff = 999.0f;
    for (int i = 0; i < (int)(sizeof k_game_view_parallax_presets /
                              sizeof k_game_view_parallax_presets[0]); i++) {
        float diff = factor - k_game_view_parallax_presets[i].factor;
        if (diff < 0.0f) diff = -diff;
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best_diff <= 0.005f ? best : -1;
}

static void game_view_select_parallax_module(int module_idx)
{
    s_game_view_parallax_module = module_idx;
    g_game_view_focus_module_idx = module_idx;
    if (s_game_view_parallax_highlight_module && module_idx >= 0) {
        module_selection_select_only(module_idx);
        g_show_module_bounds = true;
    }
}

static void draw_game_view_parallax_module_highlight(int module_idx, const char *module_name)
{
    if (!s_game_view_parallax_highlight_module || !g_game_view ||
        module_idx < 0 || module_idx >= g_bdb_num_modules ||
        !module_name || !module_name[0])
        return;

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    BddScreenRect viewport;
    bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);

    int object_count = 0;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im || im->w <= 0 || im->h <= 0) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) == module_idx)
            object_count++;
    }

    {
        int ox = 0, oy = 0, rank = -1;
        float scroll = 1.0f;
        int bx1 = 0, bx2 = 0, by1 = 0, by2 = 0;
        if (game_view_module_plane_info(module_name, &ox, &oy, &scroll, &rank) &&
            game_view_module_block_bounds(module_name, &bx1, &bx2, &by1, &by2)) {
            int start_x = 0, start_y = 0;
            bdd_get_stage_start_camera(&start_x, &start_y);
            int scroll_origin_x = start_x;
            int plane_count = bdd_stage_plane_count();
            for (int p = 0; p < plane_count; p++) {
                char pn[32] = "";
                if (bdd_stage_plane_info(p, pn, sizeof pn, NULL, NULL, NULL, NULL) &&
                    game_view_runtime_name_ieq(pn, module_name)) {
                    bdd_stage_plane_scroll_origin(p, &scroll_origin_x);
                    break;
                }
            }
            int parallax = scroll_origin_x + (int)((float)(g_scroll_pos - start_x) * scroll);
            int ux1 = viewport.x + (ox + bx1 - parallax) * g_zoom;
            int uy1 = viewport.y + (oy + by1 - g_game_view_y) * g_zoom;
            int ux2 = viewport.x + (ox + bx2 - parallax) * g_zoom;
            int uy2 = viewport.y + (oy + by2 - g_game_view_y) * g_zoom;

            if (ux1 < viewport.clip_x) ux1 = viewport.clip_x;
            if (uy1 < viewport.clip_y) uy1 = viewport.clip_y;
            if (ux2 > viewport.clip_x + viewport.clip_w) ux2 = viewport.clip_x + viewport.clip_w;
            if (uy2 > viewport.clip_y + viewport.clip_h) uy2 = viewport.clip_y + viewport.clip_h;
            if (ux2 > ux1 && uy2 > uy1) {
                ImDrawList *dl = ImGui::GetBackgroundDrawList();
                ImVec2 p0((float)ux1, (float)uy1);
                ImVec2 p1((float)ux2, (float)uy2);
                dl->AddRectFilled(p0, p1, IM_COL32(255, 220, 60, 24), 0.0f);
                dl->AddRect(p0, p1, IM_COL32(255, 230, 80, 245), 0.0f, 0, 3.0f);

                char label[96];
                snprintf(label, sizeof label, "%s  %d object%s",
                         module_name, object_count, object_count == 1 ? "" : "s");
                ImVec2 ts = ImGui::CalcTextSize(label);
                ImVec2 lp((float)ux1 + 5.0f, (float)uy1 + 5.0f);
                dl->AddRectFilled(ImVec2(lp.x - 3.0f, lp.y - 2.0f),
                                  ImVec2(lp.x + ts.x + 5.0f, lp.y + ts.y + 3.0f),
                                  IM_COL32(4, 7, 12, 205), 3.0f);
                dl->AddText(lp, IM_COL32(255, 245, 180, 255), label);
            }
            return;
        }
    }

    int ux1 = INT_MAX, uy1 = INT_MAX, ux2 = INT_MIN, uy2 = INT_MIN;
    int visible = 0;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im || im->w <= 0 || im->h <= 0) continue;
        if (assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) != module_idx)
            continue;

        BddScreenRect r;
        if (!bdd_object_screen_rect(i, im->w, im->h,
                                    g_view_x, g_view_y, g_zoom,
                                    (int)ds.x, (int)ds.y,
                                    g_scroll_pos, &r))
            continue;
        if (r.x < ux1) ux1 = r.x;
        if (r.y < uy1) uy1 = r.y;
        if (r.x + r.w > ux2) ux2 = r.x + r.w;
        if (r.y + r.h > uy2) uy2 = r.y + r.h;
        visible++;
    }
    if (visible <= 0)
        return;

    if (ux1 < viewport.clip_x) ux1 = viewport.clip_x;
    if (uy1 < viewport.clip_y) uy1 = viewport.clip_y;
    if (ux2 > viewport.clip_x + viewport.clip_w) ux2 = viewport.clip_x + viewport.clip_w;
    if (uy2 > viewport.clip_y + viewport.clip_h) uy2 = viewport.clip_y + viewport.clip_h;
    if (ux2 <= ux1 || uy2 <= uy1)
        return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 p0((float)ux1, (float)uy1);
    ImVec2 p1((float)ux2, (float)uy2);
    dl->AddRectFilled(p0, p1, IM_COL32(255, 220, 60, 24), 0.0f);
    dl->AddRect(p0, p1, IM_COL32(255, 230, 80, 245), 0.0f, 0, 3.0f);

    char label[96];
    snprintf(label, sizeof label, "%s  %d object%s",
             module_name, visible, visible == 1 ? "" : "s");
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 lp((float)ux1 + 5.0f, (float)uy1 + 5.0f);
    dl->AddRectFilled(ImVec2(lp.x - 3.0f, lp.y - 2.0f),
                      ImVec2(lp.x + ts.x + 5.0f, lp.y + ts.y + 3.0f),
                      IM_COL32(4, 7, 12, 205), 3.0f);
    dl->AddText(lp, IM_COL32(255, 245, 180, 255), label);
}

static bool game_view_parse_stage_header(char *name, size_t name_sz,
                                         int *world_w, int *world_h, int *max_depth)
{
    char nm[64] = "";
    int ww = 0, wh = 0, md = 255, nmods = 0, npals = 0, nobj = 0;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &ww, &wh, &md, &nmods, &npals, &nobj) < 7)
        return false;
    if (name && name_sz > 0)
        snprintf(name, name_sz, "%s", nm);
    if (world_w) *world_w = ww;
    if (world_h) *world_h = wh;
    if (max_depth) *max_depth = md;
    return true;
}

static bool game_view_read_stage_frame(int *world_w, int *world_h,
                                       int *scroll_l, int *scroll_r)
{
    int ww = 400, wh = 254, md = 255;
    if (!game_view_parse_stage_header(NULL, 0, &ww, &wh, &md)) {
        int wx0 = 0, wx1 = 400, wy0 = 0, wy1 = 254;
        bdd_get_world_bounds(&wx0, &wx1, &wy0, &wy1);
        if (wx0 != INT_MAX && wx1 != INT_MIN) ww = wx1 > wx0 ? wx1 - wx0 : 400;
        if (wy0 != INT_MAX && wy1 != INT_MIN) wh = wy1 > wy0 ? wy1 - wy0 : 254;
    }
    if (ww < 400) ww = 400;
    if (wh < 254) wh = 254;

    int l = 0;
    int r = ww - 400;
    if (r < l) r = l;
    bdd_get_stage_scroll_limits(&l, &r);
    if (r < l) r = l;

    if (world_w) *world_w = ww;
    if (world_h) *world_h = wh;
    if (scroll_l) *scroll_l = l;
    if (scroll_r) *scroll_r = r;
    return true;
}

static void game_view_refresh_stage_frame_state(bool force)
{
    char key[640];
    snprintf(key, sizeof key, "%s|%s", g_bdb_path, g_name);
    if (!force && strncmp(key, s_game_view_frame_key, sizeof s_game_view_frame_key) == 0)
        return;

    snprintf(s_game_view_frame_key, sizeof s_game_view_frame_key, "%s", key);
    game_view_read_stage_frame(&s_game_view_frame_w, &s_game_view_frame_h,
                               &s_game_view_frame_l, &s_game_view_frame_r);
    s_game_view_default_frame_w = s_game_view_frame_w;
    s_game_view_default_frame_h = s_game_view_frame_h;
    s_game_view_default_frame_l = s_game_view_frame_l;
    s_game_view_default_frame_r = s_game_view_frame_r;
    s_game_view_default_cam_ok =
        bdd_get_stage_start_camera(&s_game_view_default_cam_x,
                                   &s_game_view_default_cam_y) != 0;
    s_game_view_default_ground_ok =
        bdd_get_stage_ground_y(&s_game_view_default_ground_y) != 0;
}

static bool game_view_apply_stage_frame(int world_w, int world_h,
                                        int scroll_l, int scroll_r)
{
    char nm[64] = "";
    int old_w = 0, old_h = 0, md = 255;
    if (!game_view_parse_stage_header(nm, sizeof nm, &old_w, &old_h, &md)) {
        stage_set_toast("Stage frame unavailable: bad BDB header");
        return false;
    }
    if (world_w < 400) world_w = 400;
    if (world_h < 254) world_h = 254;
    if (scroll_r < scroll_l) scroll_r = scroll_l;

    if (world_w != old_w || world_h != old_h) {
        undo_save_ex("Set Game Preview Frame");
        snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
                 nm, world_w, world_h, md, g_bdb_num_modules, g_n_pals, g_no);
        g_dirty = 1;
        g_view_changed = 1;
    }

    bool limits_ok = stage_start_apply_bgnd_limits(scroll_l, scroll_r);
    s_game_view_frame_w = world_w;
    s_game_view_frame_h = world_h;
    s_game_view_frame_l = scroll_l;
    s_game_view_frame_r = scroll_r;
    if (g_scroll_pos < scroll_l) g_scroll_pos = scroll_l;
    if (g_scroll_pos > scroll_r) g_scroll_pos = scroll_r;

    char msg[160];
    snprintf(msg, sizeof msg, "Stage frame W=%d H=%d, scroll %d..%d%s",
             world_w, world_h, scroll_l, scroll_r,
             limits_ok ? "" : " (BGND limits not written)");
    stage_set_toast(msg);
    return true;
}

static bool game_view_content_frame(int *world_w, int *world_h,
                                    int *scroll_l, int *scroll_r)
{
    int min_x = INT_MAX, max_x = INT_MIN;
    int min_y = INT_MAX, max_y = INT_MIN;
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        int w = im ? im->w : 1;
        int h = im ? im->h : 1;
        if (g_obj[i].depth < min_x) min_x = g_obj[i].depth;
        if (g_obj[i].depth + w > max_x) max_x = g_obj[i].depth + w;
        if (g_obj[i].sy < min_y) min_y = g_obj[i].sy;
        if (g_obj[i].sy + h > max_y) max_y = g_obj[i].sy + h;
    }
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        if (!parse_module_bounds(m, NULL, &mx1, &mx2, &my1, &my2)) continue;
        if (mx1 < min_x) min_x = mx1;
        if (mx2 + 1 > max_x) max_x = mx2 + 1;
        if (my1 < min_y) min_y = my1;
        if (my2 + 1 > max_y) max_y = my2 + 1;
    }
    if (min_x == INT_MAX || max_x == INT_MIN ||
        min_y == INT_MAX || max_y == INT_MIN)
        return false;
    if (min_x > 0) min_x = 0;
    if (min_y > 0) min_y = 0;
    int ww = max_x - min_x;
    int wh = max_y - min_y;
    if (ww < 400) ww = 400;
    if (wh < 254) wh = 254;
    int l = min_x;
    int r = max_x - 400;
    if (r < l) r = l;
    if (world_w) *world_w = ww;
    if (world_h) *world_h = wh;
    if (scroll_l) *scroll_l = l;
    if (scroll_r) *scroll_r = r;
    return true;
}

static void game_view_set_match_start_here(int camera_x, int camera_y)
{
    g_stage_start_camera_x = camera_x;
    g_stage_start_camera_y = camera_y;
    g_stage_preview_worldx = camera_x;
    g_stage_start_camera_enabled = true;
    g_scroll_pos = camera_x;
    g_game_view_y = camera_y;
    stage_set_toast("Match start camera set in preview");
}

static int game_view_reset_all_layers(int layer)
{
    if (g_no <= 0) return 0;
    std::vector<unsigned char> mask((size_t)g_no, 0);
    int target_count = 0;
    for (int i = 0; i < g_no; i++) {
        if (g_obj_lock && g_obj_lock[i]) continue;
        if (((g_obj[i].wx >> 8) & 0xFF) == layer) continue;
        mask[(size_t)i] = 1;
        target_count++;
    }
    if (target_count <= 0) {
        stage_set_toast("All object layers already use 0x40");
        return 0;
    }

    ObjectRecordUndoCapture undo;
    if (!object_record_undo_capture_mask(&undo, mask.data()))
        return 0;
    for (int i = 0; i < g_no; i++) {
        if (!mask[(size_t)i]) continue;
        g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (layer << 8);
    }
    if (object_record_undo_commit(&undo, "Reset All Layers") > 0)
        g_dirty = 1;
    g_view_changed = 1;
    char msg[96];
    snprintf(msg, sizeof msg, "Reset %d object layer%s to 0x%02X",
             target_count, target_count == 1 ? "" : "s", layer);
    stage_set_toast(msg);
    return target_count;
}

static int game_view_primary_object(void)
{
    if (g_hl_obj >= 0 && g_hl_obj < g_no)
        return g_hl_obj;
    for (int i = 0; i < g_no; i++)
        if (g_sel_flags[i])
            return i;
    return -1;
}

static void draw_game_view_object_thumbnail(void)
{
    int obj_i = game_view_primary_object();
    if (obj_i < 0 || obj_i >= g_no)
        return;
    Img *im = img_find(g_obj[obj_i].ii);
    if (!im || im->w <= 0 || im->h <= 0)
        return;
    int img_i = (int)(im - g_img);
    if (img_i < 0 || img_i >= g_ni)
        return;
    SDL_Texture *tex = editor_texture_at(img_i);
    if (!tex)
        return;

    float max_dim = (float)(im->w > im->h ? im->w : im->h);
    float scale = max_dim > 0.0f ? 44.0f / max_dim : 1.0f;
    if (scale > 2.0f) scale = 2.0f;
    if (scale < 0.05f) scale = 0.05f;
    draw_editor_texture_transparent(tex, im->w * scale, im->h * scale);
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + game_view_tools_avail_width());
    ImGui::Text("Obj %d  img 0x%02X", obj_i, im->idx);
    ImGui::TextDisabled("%dx%d  pal %d  layer 0x%02X",
                        im->w, im->h, g_obj[obj_i].fl,
                        (g_obj[obj_i].wx >> 8) & 0xFF);
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton("Center Object"))
        center_view_on_object(obj_i);
    ImGui::EndGroup();
}

static void draw_game_view_stage_frame_controls(void)
{
    ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f), "Stage Frame");

    float input_w = (game_view_tools_avail_width() - 92.0f) * 0.5f;
    if (input_w < 64.0f) input_w = 64.0f;
    if (input_w > 96.0f) input_w = 96.0f;

    ImGui::TextDisabled("Size");
    ImGui::SameLine(74.0f);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("W##gv_frame_w", &s_game_view_frame_w, 16, 128);
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("H##gv_frame_h", &s_game_view_frame_h, 8, 64);
    if (s_game_view_frame_w < 400) s_game_view_frame_w = 400;
    if (s_game_view_frame_h < 254) s_game_view_frame_h = 254;

    ImGui::TextDisabled("Scroll");
    ImGui::SameLine(74.0f);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("Left##gv_frame_l", &s_game_view_frame_l, 16, 128);
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("Right##gv_frame_r", &s_game_view_frame_r, 16, 128);
    if (s_game_view_frame_r < s_game_view_frame_l)
        s_game_view_frame_r = s_game_view_frame_l;

    if (ImGui::SmallButton("+128 W")) {
        s_game_view_frame_w += 128;
        s_game_view_frame_r += 128;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stretch the editable world width and scroll-right limit by 128 px.");
    game_view_tools_same_line_button_if_fits("+64 H");
    if (ImGui::SmallButton("+64 H"))
        s_game_view_frame_h += 64;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add vertical layout headroom to the BDB frame.");
    game_view_tools_same_line_button_if_fits("Default Limits");
    if (ImGui::SmallButton("Default Limits")) {
        s_game_view_frame_l = 0;
        s_game_view_frame_r = s_game_view_frame_w - 400;
        if (s_game_view_frame_r < 0) s_game_view_frame_r = 0;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use the simple 0..W-400 camera range.");

    if (ImGui::SmallButton("Fit Content")) {
        if (game_view_content_frame(&s_game_view_frame_w,
                                    &s_game_view_frame_h,
                                    &s_game_view_frame_l,
                                    &s_game_view_frame_r))
            game_view_apply_stage_frame(s_game_view_frame_w,
                                        s_game_view_frame_h,
                                        s_game_view_frame_l,
                                        s_game_view_frame_r);
        else
            stage_set_toast("No content to fit stage frame");
    }
    game_view_tools_same_line_button_if_fits("Apply Frame");
    if (ImGui::SmallButton("Apply Frame"))
        game_view_apply_stage_frame(s_game_view_frame_w,
                                    s_game_view_frame_h,
                                    s_game_view_frame_l,
                                    s_game_view_frame_r);
    game_view_tools_same_line_button_if_fits("Reset Defaults");
    if (ImGui::SmallButton("Reset Defaults")) {
        s_game_view_frame_w = s_game_view_default_frame_w;
        s_game_view_frame_h = s_game_view_default_frame_h;
        s_game_view_frame_l = s_game_view_default_frame_l;
        s_game_view_frame_r = s_game_view_default_frame_r;
        game_view_apply_stage_frame(s_game_view_frame_w,
                                    s_game_view_frame_h,
                                    s_game_view_frame_l,
                                    s_game_view_frame_r);
        if (s_game_view_default_cam_ok) {
            game_view_set_match_start_here(s_game_view_default_cam_x,
                                           s_game_view_default_cam_y);
        }
        if (s_game_view_default_ground_ok) {
            g_stage_start_ground_y = s_game_view_default_ground_y;
            g_stage_start_ground_enabled = true;
            g_match_start_fighter_box_y =
                g_stage_start_ground_y - g_match_start_fighter_box_h;
        }
        if (s_game_view_default_cam_ok || s_game_view_default_ground_ok)
            stage_start_apply_bgnd_start_placement();
    }
}

static void game_view_add_size_audit_item(std::vector<GameViewSizeAuditItem> &items,
                                          const char *action,
                                          const char *target,
                                          const char *detail,
                                          size_t save,
                                          int img_i,
                                          int pal_i)
{
    if (save == 0 || !action || !target || !detail)
        return;
    GameViewSizeAuditItem item;
    memset(&item, 0, sizeof item);
    snprintf(item.action, sizeof item.action, "%s", action);
    snprintf(item.target, sizeof item.target, "%s", target);
    snprintf(item.detail, sizeof item.detail, "%s", detail);
    item.save = save;
    item.img_i = img_i;
    item.pal_i = pal_i;
    items.push_back(item);
}

static void game_view_collect_size_audit_items(std::vector<GameViewSizeAuditItem> &items)
{
    items.clear();

    int unused_count = 0;
    int unused_focus = -1;
    size_t unused_save = 0;
    size_t unused_best = 0;
    for (int i = 0; i < g_ni; i++) {
        if (image_use_count(g_img[i].idx) != 0) continue;
        size_t save = mk2_estimate_image_bytes(&g_img[i]) + 12u;
        unused_count++;
        unused_save += save;
        if (save > unused_best) {
            unused_best = save;
            unused_focus = i;
        }
    }
    if (unused_count > 0) {
        char target[64];
        snprintf(target, sizeof target, "%d image%s", unused_count,
                 unused_count == 1 ? "" : "s");
        game_view_add_size_audit_item(items, "Delete unused art", target,
                                      "Dormant art not placed by any object.",
                                      unused_save, unused_focus, -1);
    }

    int unused_palettes = 0;
    int unused_palette_focus = -1;
    size_t unused_palette_save = 0;
    size_t unused_palette_best = 0;
    if (g_n_pals > 0) {
        std::vector<unsigned char> used((size_t)g_n_pals, 0);
        for (int i = 0; i < g_ni; i++)
            if (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals)
                used[(size_t)g_img[i].pal_idx] = 1;
        for (int i = 0; i < g_no; i++)
            if (g_obj[i].fl >= 0 && g_obj[i].fl < g_n_pals)
                used[(size_t)g_obj[i].fl] = 1;
        for (int p = 0; p < g_n_pals; p++) {
            if (used[(size_t)p]) continue;
            size_t save = (size_t)g_pal_count[p] * 2u;
            unused_palettes++;
            unused_palette_save += save;
            if (save > unused_palette_best) {
                unused_palette_best = save;
                unused_palette_focus = p;
            }
        }
    }
    if (unused_palettes > 0) {
        char target[64];
        snprintf(target, sizeof target, "%d palette%s", unused_palettes,
                 unused_palettes == 1 ? "" : "s");
        game_view_add_size_audit_item(items, "Remove unused palettes", target,
                                      "Small but safe cleanup.",
                                      unused_palette_save, -1, unused_palette_focus);
    }

    size_t trim_save = 0, bpp_save = 0, requant_save = 0;
    int trim_count = 0, bpp_count = 0, requant_count = 0;
    int trim_focus = -1, bpp_focus = -1, requant_focus = -1;
    size_t trim_best = 0, bpp_best = 0, requant_best = 0;
    int largest_placed = -1;
    size_t largest_bytes = 0;

    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!im->pix || im->w <= 0 || im->h <= 0) continue;
        int uses = image_use_count(im->idx);
        size_t raw = mk2_estimate_image_bytes(im);
        if (uses > 0 && raw > largest_bytes) {
            largest_bytes = raw;
            largest_placed = i;
        }

        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) {
            int nw = x2 - x1 + 1;
            int nh = y2 - y1 + 1;
            Img tmp = *im;
            tmp.w = nw;
            tmp.h = nh;
            size_t trimmed = mk2_estimate_image_bytes_for_bpp(&tmp, mk2_bpp_for_image(im));
            if (raw > trimmed) {
                size_t save = raw - trimmed;
                trim_save += save;
                trim_count++;
                if (save > trim_best) { trim_best = save; trim_focus = i; }
            }
        }

        int used_colors = 0, max_idx = 0;
        image_palette_usage_stats(im, &used_colors, &max_idx);
        int target = 0;
        if (used_colors > 0 && used_colors <= 15 && max_idx > 15)
            target = 15;
        else if (used_colors > 0 && used_colors <= 31 && max_idx > 31)
            target = 31;
        else if (used_colors > 0 && used_colors <= 63 && max_idx > 63)
            target = 63;
        if (target > 0) {
            size_t compact = mk2_estimate_image_bytes_for_bpp(im, mk2_bpp_for_max_index(target));
            if (raw > compact) {
                size_t save = raw - compact;
                bpp_save += save;
                bpp_count++;
                if (save > bpp_best) { bpp_best = save; bpp_focus = i; }
            }
        } else if (max_idx >= 64) {
            size_t compact = mk2_estimate_image_bytes_for_bpp(im, 6);
            if (raw > compact) {
                size_t save = raw - compact;
                requant_save += save;
                requant_count++;
                if (save > requant_best) { requant_best = save; requant_focus = i; }
            }
        }
    }

    if (trim_count > 0) {
        char target[64];
        snprintf(target, sizeof target, "%d image%s", trim_count,
                 trim_count == 1 ? "" : "s");
        game_view_add_size_audit_item(items, "Trim sprite borders", target,
                                      "Transparent border trim; lossless.",
                                      trim_save, trim_focus, -1);
    }
    if (bpp_count > 0) {
        char target[64];
        snprintf(target, sizeof target, "%d image%s", bpp_count,
                 bpp_count == 1 ? "" : "s");
        game_view_add_size_audit_item(items, "Compact BPP", target,
                                      "Compact palette indices to lower bit depth.",
                                      bpp_save, bpp_focus, -1);
    }
    if (requant_count > 0) {
        char target[64];
        snprintf(target, sizeof target, "%d image%s", requant_count,
                 requant_count == 1 ? "" : "s");
        game_view_add_size_audit_item(items, "Requantize color", target,
                                      "Reimport/requantize high-color art; visual review needed.",
                                      requant_save, requant_focus, -1);
    }

    std::vector<unsigned char> duplicate_gone((size_t)(g_ni > 0 ? g_ni : 0), 0);
    int dup_count = 0, dup_focus = -1;
    size_t dup_save = 0, dup_best = 0;
    for (int a = 0; a < g_ni; a++) {
        if (duplicate_gone[(size_t)a] || !g_img[a].pix) continue;
        for (int b = a + 1; b < g_ni; b++) {
            if (duplicate_gone[(size_t)b] || !g_img[b].pix) continue;
            bool match = image_pixels_match(&g_img[a], &g_img[b], false);
            if (!match && g_dup_include_mirrors)
                match = image_pixels_match(&g_img[a], &g_img[b], true);
            if (!match) continue;
            size_t save = mk2_estimate_image_bytes(&g_img[b]) + 12u;
            duplicate_gone[(size_t)b] = 1;
            dup_count++;
            dup_save += save;
            if (save > dup_best) { dup_best = save; dup_focus = b; }
        }
    }
    if (dup_count > 0) {
        char target[64], detail[160];
        snprintf(target, sizeof target, "%d duplicate%s", dup_count,
                 dup_count == 1 ? "" : "s");
        snprintf(detail, sizeof detail, "Merge exact%s duplicate images.",
                 g_dup_include_mirrors ? "/mirrored" : "");
        game_view_add_size_audit_item(items, "Merge duplicates", target, detail,
                                      dup_save, dup_focus, -1);
    }

    if (largest_placed >= 0 && largest_bytes > 0) {
        char target[64];
        Img *im = &g_img[largest_placed];
        snprintf(target, sizeof target, "0x%02X  %dx%d", im->idx, im->w, im->h);
        game_view_add_size_audit_item(items, "Rework largest sprite", target,
                                      "Biggest placed sprite; reimport/chop if you need more.",
                                      largest_bytes, largest_placed, -1);
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const GameViewSizeAuditItem &a, const GameViewSizeAuditItem &b) {
        if (a.save != b.save) return a.save > b.save;
        return strcmp(a.action, b.action) < 0;
    });
}

static void game_view_focus_audit_image(int img_i)
{
    if (img_i < 0 || img_i >= g_ni)
        return;
    g_tile_img = img_i;
    g_place_tool_img = img_i;
    int selected = mk2_select_objects_by_image(g_img[img_i].idx);
    g_budget_relief_highlight_img_ii = g_img[img_i].idx;
    g_show_images = true;
    char msg[96];
    if (selected > 0) {
        if (g_hl_obj >= 0 && g_hl_obj < g_no)
            center_view_on_object(g_hl_obj);
        snprintf(msg, sizeof msg, "Selected %d placement(s) for image 0x%02X",
                 selected, g_img[img_i].idx);
    } else {
        snprintf(msg, sizeof msg, "Focused image 0x%02X; no placements found",
                 g_img[img_i].idx);
    }
    stage_set_toast(msg);
}

static void game_view_focus_audit_palette(int pal_i)
{
    if (pal_i < 0 || pal_i >= g_n_pals)
        return;
    g_sel_pal = pal_i;
    snprintf(g_obj_filter, sizeof g_obj_filter, "p%d", pal_i);
    char msg[96];
    snprintf(msg, sizeof msg, "Focused palette %d: %s",
             pal_i, g_pal_name[pal_i]);
    stage_set_toast(msg);
}

static void game_view_focus_audit_item(const GameViewSizeAuditItem &item)
{
    if (item.img_i >= 0) {
        game_view_focus_audit_image(item.img_i);
    } else if (item.pal_i >= 0) {
        game_view_focus_audit_palette(item.pal_i);
    }
}

static bool s_quick_size_audit_open = false;

static void draw_quick_size_audit_contents(void)
{
    if (!g_have_bdb || g_ni <= 0) {
        ImGui::TextDisabled("No stage art loaded.");
        return;
    }

    Mk2Budget b = mk2_collect_budget();
    size_t pixel_bytes = b.raw_image_bytes;
    size_t pal_bytes = (size_t)b.palette_entries * 2u;
    size_t overhead = (size_t)g_no * 8u + (size_t)g_ni * 12u + (size_t)g_bdb_num_modules * 16u;
    size_t total = b.estimated_payload;

    ImGui::Text("Map estimate: %.1f KB", total / 1024.0);
    ImGui::SameLine();
    ImGui::TextDisabled("pixels %.1f | palettes %.1f | tables %.1f KB",
                        pixel_bytes / 1024.0, pal_bytes / 1024.0, overhead / 1024.0);
    if (g_gate_payload_limit > 0) {
        size_t delta = total > (size_t)g_gate_payload_limit
            ? total - (size_t)g_gate_payload_limit
            : (size_t)g_gate_payload_limit - total;
        ImGui::TextDisabled("Gate: %.1f KB  %s %.1f KB",
                            g_gate_payload_limit / 1024.0,
                            total > (size_t)g_gate_payload_limit ? "over by" : "free",
                            delta / 1024.0);
    }

    std::vector<GameViewSizeAuditItem> items;
    game_view_collect_size_audit_items(items);
    if (items.empty()) {
        ImGui::TextDisabled("No obvious quick wins. Full Optimize tools can still inspect art manually.");
    } else if (ImGui::BeginTable("quick_size_audit", 5,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("find", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("best move");
        ImGui::TableSetupColumn("target", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("KB", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("note");
        ImGui::TableHeadersRow();

        int shown = 0;
        for (size_t i = 0; i < items.size() && shown < 6; i++, shown++) {
            const GameViewSizeAuditItem &item = items[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (item.img_i >= 0 || item.pal_i >= 0) {
                if (ImGui::SmallButton("Find"))
                    game_view_focus_audit_item(item);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Highlight the largest representative candidate for this row.");
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.action);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.target);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", item.save / 1024.0);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", item.detail);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::SmallButton("Open Optimize Tools")) {
        mk2_workflow_show_optimize_section();
        stage_set_toast("Opened MK2 Optimize tools");
    }
}

void open_quick_size_audit(void)
{
    s_quick_size_audit_open = true;
}

void draw_quick_size_audit_window(void)
{
    if (!s_quick_size_audit_open)
        return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Quick Size Audit", &s_quick_size_audit_open,
                     ImGuiWindowFlags_NoCollapse)) {
        draw_quick_size_audit_contents();
    }
    ImGui::End();
}

static void draw_game_view_runtime_parallax_editor(void)
{
    if (g_bdb_num_modules <= 0)
        return;

    static int s_loaded_module = -2;
    static int s_preset = 4;
    static float s_custom_factor = 0.50f;
    static char s_stage_key[640] = "";

    char key[640];
    snprintf(key, sizeof key, "%s|%s", g_bdb_path, g_name);
    if (strncmp(key, s_stage_key, sizeof s_stage_key) != 0) {
        snprintf(s_stage_key, sizeof s_stage_key, "%s", key);
        s_game_view_parallax_module = game_view_selected_object_module();
        if (s_game_view_parallax_module < 0)
            s_game_view_parallax_module = module_selection_first();
        if (s_game_view_parallax_module < 0 && g_bdb_num_modules > 0)
            s_game_view_parallax_module = 0;
        if (s_game_view_parallax_highlight_module && s_game_view_parallax_module >= 0)
            module_selection_select_only(s_game_view_parallax_module);
        s_loaded_module = -2;
    }
    if (s_game_view_parallax_module < 0 || s_game_view_parallax_module >= g_bdb_num_modules)
        s_game_view_parallax_module = g_bdb_num_modules > 0 ? 0 : -1;
    g_game_view_focus_module_idx = s_game_view_parallax_module;

    char cur_name[64] = "";
    if (!game_view_module_name(s_game_view_parallax_module, cur_name, sizeof cur_name))
        return;

    int cur_ox = 0, cur_oy = 0, cur_rank = -1;
    float cur_scroll = 1.0f;
    bool placed = game_view_module_plane_info(cur_name, &cur_ox, &cur_oy,
                                              &cur_scroll, &cur_rank);
    if (s_loaded_module != s_game_view_parallax_module) {
        s_loaded_module = s_game_view_parallax_module;
        if (placed)
            s_custom_factor = cur_scroll;
        s_preset = game_view_parallax_preset_for(s_custom_factor);
    }
    draw_game_view_parallax_module_highlight(s_game_view_parallax_module, cur_name);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f), "Runtime Parallax");
    if (placed) {
        game_view_tools_text_disabled_wrapped("%s  %.2fx %s  offset %d,%d",
                                             cur_name, cur_scroll,
                                             game_view_runtime_parallax_label(cur_scroll),
                                             cur_ox, cur_oy);
    } else {
        game_view_tools_text_disabled_wrapped("%s  not placed in BGND.ASM", cur_name);
    }

    char combo_label[160];
    if (placed) {
        snprintf(combo_label, sizeof combo_label, "%s  %.2fx %s",
                 cur_name, cur_scroll, game_view_runtime_parallax_label(cur_scroll));
    } else {
        snprintf(combo_label, sizeof combo_label, "%s  not placed", cur_name);
    }
    ImGui::TextDisabled("Module");
    ImGui::SetNextItemWidth(game_view_tools_avail_width());
    if (ImGui::BeginCombo("##gv_parallax_module", combo_label)) {
        for (int m = 0; m < g_bdb_num_modules; m++) {
            char mn[64] = "";
            if (!game_view_module_name(m, mn, sizeof mn)) continue;
            int ox = 0, oy = 0, rank = -1;
            float scroll = 1.0f;
            bool m_placed = game_view_module_plane_info(mn, &ox, &oy, &scroll, &rank);
            char row[180];
            if (m_placed) {
                snprintf(row, sizeof row, "%s  %.2fx %s  %d,%d",
                         mn, scroll, game_view_runtime_parallax_label(scroll), ox, oy);
            } else {
                snprintf(row, sizeof row, "%s  not placed", mn);
            }
            bool selected = (m == s_game_view_parallax_module);
            if (ImGui::Selectable(row, selected)) {
                game_view_select_parallax_module(m);
                s_loaded_module = -2;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::SmallButton("Use Selected")) {
        int selected_mod = game_view_selected_object_module();
        if (selected_mod >= 0) {
            game_view_select_parallax_module(selected_mod);
            s_loaded_module = -2;
        } else {
            stage_set_toast("Selected object is not inside a module");
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick the module containing the selected object.");
    game_view_tools_same_line_if_fits(game_view_tools_checkbox_width("Highlight"), 8.0f);
    if (ImGui::Checkbox("Highlight##gv_parallax_module_highlight",
                        &s_game_view_parallax_highlight_module)) {
        if (s_game_view_parallax_highlight_module &&
            s_game_view_parallax_module >= 0)
            module_selection_select_only(s_game_view_parallax_module);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Outline this module in Game Preview and highlight it in module view.");
    game_view_tools_same_line_if_fits(game_view_tools_checkbox_width("Dim Others"), 8.0f);
    ImGui::Checkbox("Dim Others##gv_parallax_focus", &g_game_view_focus_module_enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fade every other module while tuning this one. Turn off to preview the full scene.");

    int builder_x1 = 0, builder_x2 = 0, builder_y1 = 0, builder_y2 = 0;
    bool have_builder_bounds = parse_module_bounds(s_game_view_parallax_module, NULL,
                                                   &builder_x1, &builder_x2,
                                                   &builder_y1, &builder_y2);
    if (have_builder_bounds) {
        if (placed) {
            int dx = cur_ox - builder_x1;
            int dy = cur_oy - builder_y1;
            game_view_tools_text_disabled_wrapped(
                "Module top-left: builder %d,%d  runtime %d,%d  delta %+d,%+d",
                builder_x1, builder_y1, cur_ox, cur_oy, dx, dy);
            if (dx != 0 || dy != 0) {
                if (ImGui::SmallButton("Use Module Top-Left")) {
                    if (stage_bgnd_set_module_offset(cur_name, builder_x1, builder_y1)) {
                        s_loaded_module = -2;
                        g_view_changed = 1;
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Set this module's BGND.ASM runtime offset to its BDB/source top-left.");
            }
        } else {
            game_view_tools_text_disabled_wrapped("Module top-left: builder %d,%d",
                                                 builder_x1, builder_y1);
        }
    }

    int selected_obj = -1;
    int selected_ox = 0, selected_oy = 0;
    if (placed &&
        game_view_builder_offset_for_selected_block(s_game_view_parallax_module,
                                                    cur_name,
                                                    &selected_ox, &selected_oy,
                                                    &selected_obj)) {
        int sdx = selected_ox - cur_ox;
        int sdy = selected_oy - cur_oy;
        game_view_tools_text_disabled_wrapped(
            "Selected object %d wants runtime offset %d,%d  delta %+d,%+d",
            selected_obj, selected_ox, selected_oy, sdx, sdy);
        if (sdx != 0 || sdy != 0) {
            if (ImGui::SmallButton("Use Selected Builder Spot")) {
                if (stage_bgnd_set_module_offset(cur_name, selected_ox, selected_oy)) {
                    s_loaded_module = -2;
                    g_view_changed = 1;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Align this selected object's runtime block to its BDB/source position.");
        }
    }

    if (!placed) {
        int mx1 = 0, my1 = 0;
        parse_module_bounds(s_game_view_parallax_module, NULL, &mx1, NULL, &my1, NULL);
        if (ImGui::SmallButton("Create Runtime Placement")) {
            if (stage_bgnd_create_module_placement(cur_name, mx1, my1))
                s_loaded_module = -2;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adds this module as a BGND.ASM runtime plane at its source position.");
        game_view_tools_same_line_button_if_fits("Reset All 1.00x");
        if (ImGui::Button("Reset All 1.00x")) {
            if (stage_bgnd_reset_all_module_parallax(1.0f))
                s_loaded_module = -2;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sets every baklst1-8 module plane in this stage's scroll table to playfield speed.");
        return;
    }

    const int preset_count = (int)(sizeof k_game_view_parallax_presets /
                                  sizeof k_game_view_parallax_presets[0]);
    const char *preset_label = s_preset >= 0 && s_preset < preset_count
        ? k_game_view_parallax_presets[s_preset].label
        : "Custom";
    ImGui::TextDisabled("Preset");
    ImGui::SetNextItemWidth(game_view_tools_avail_width());
    if (ImGui::BeginCombo("##gv_parallax_preset", preset_label)) {
        for (int i = 0; i < preset_count; i++) {
            bool selected = (i == s_preset);
            if (ImGui::Selectable(k_game_view_parallax_presets[i].label, selected)) {
                s_preset = i;
                s_custom_factor = k_game_view_parallax_presets[i].factor;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::Separator();
        if (ImGui::Selectable("Custom", s_preset < 0))
            s_preset = -1;
        ImGui::EndCombo();
    }
    float apply_factor = (s_preset >= 0 && s_preset < preset_count)
        ? k_game_view_parallax_presets[s_preset].factor
        : s_custom_factor;
    if (s_preset < 0) {
        ImGui::SetNextItemWidth(96.0f);
        if (ImGui::InputFloat("Factor##gv_parallax_factor", &s_custom_factor, 0.0f, 0.0f, "%.2f")) {
            if (s_custom_factor < 0.0f) s_custom_factor = 0.0f;
            if (s_custom_factor > 1.0f) s_custom_factor = 1.0f;
            apply_factor = s_custom_factor;
        }
    } else {
        game_view_tools_text_disabled_wrapped("Factor %.2f", apply_factor);
    }
    char apply_label[64];
    snprintf(apply_label, sizeof apply_label, "Apply %.2fx", apply_factor);
    if (ImGui::Button(apply_label)) {
        if (apply_factor < 0.0f) apply_factor = 0.0f;
        if (apply_factor > 1.0f) apply_factor = 1.0f;
        if (stage_bgnd_set_module_parallax(cur_name, apply_factor))
            s_loaded_module = -2;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Writes this module's BGND.ASM plane scroll rate.");
    game_view_tools_same_line_button_if_fits("Reset All 1.00x");
    if (ImGui::Button("Reset All 1.00x")) {
        if (stage_bgnd_reset_all_module_parallax(1.0f))
            s_loaded_module = -2;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sets every baklst1-8 module plane in this stage's scroll table to playfield speed.");
}

static void draw_game_view_match_start_controls(void)
{
    ImGui::Checkbox("Show Fighter Guide", &g_match_start_fighter_box_visible);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shows the draggable match-start fighter guide.");

    int old_h = g_match_start_fighter_box_h;
    float input_w = (game_view_tools_avail_width() - 92.0f) * 0.5f;
    if (input_w < 64.0f) input_w = 64.0f;
    if (input_w > 96.0f) input_w = 96.0f;
    ImGui::TextDisabled("Guide");
    ImGui::SameLine(74.0f);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("W##fighter_box_w", &g_match_start_fighter_box_w, 1, 8);
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputInt("H##fighter_box_h", &g_match_start_fighter_box_h, 1, 8);
    if (g_match_start_fighter_box_w < 8) g_match_start_fighter_box_w = 8;
    if (g_match_start_fighter_box_h < 16) g_match_start_fighter_box_h = 16;
    if (g_match_start_fighter_box_w > 160) g_match_start_fighter_box_w = 160;
    if (g_match_start_fighter_box_h > 240) g_match_start_fighter_box_h = 240;
    if (old_h != g_match_start_fighter_box_h)
        g_match_start_fighter_box_y = g_stage_start_ground_y - g_match_start_fighter_box_h;

    int ground_edit = g_stage_start_ground_y;
    ImGui::TextDisabled("Ground");
    ImGui::SameLine(74.0f);
    ImGui::SetNextItemWidth(94.0f);
    if (ImGui::InputInt("Y##fighter_box_ground", &ground_edit, 1, 8)) {
        g_stage_start_ground_y = ground_edit;
        g_stage_start_ground_enabled = true;
        g_match_start_fighter_box_visible = true;
        g_match_start_fighter_box_y =
            g_stage_start_ground_y - g_match_start_fighter_box_h;
    }

    if (ImGui::SmallButton("Use Guide Floor")) {
        g_stage_start_ground_y =
            g_match_start_fighter_box_y + g_match_start_fighter_box_h;
        g_stage_start_ground_enabled = true;
        stage_set_toast("Fighter ground Y set from guide box");
    }
    game_view_tools_same_line_button_if_fits("Apply Ground");
    if (ImGui::SmallButton("Apply Ground"))
        stage_start_apply_bgnd_ground(g_stage_start_ground_y);

    if (ImGui::SmallButton("Set Start Here"))
        game_view_set_match_start_here(g_scroll_pos, g_game_view_y);
    game_view_tools_same_line_button_if_fits("Center Start");
    if (ImGui::SmallButton("Center Start")) {
        int cx = s_game_view_frame_l +
                 (s_game_view_frame_r - s_game_view_frame_l) / 2;
        int cy = (s_game_view_frame_h > 254)
               ? (s_game_view_frame_h - 254) / 2
               : 0;
        game_view_set_match_start_here(cx, cy);
    }
    game_view_tools_same_line_button_if_fits("Apply Match Start");
    if (ImGui::SmallButton("Apply Match Start")) {
        g_stage_start_camera_enabled = true;
        g_stage_start_ground_enabled = true;
        stage_start_apply_bgnd_start_placement();
    }
}

static void draw_game_view_selection_layer_controls(void)
{
    draw_game_view_object_thumbnail();

    int preset_count = mk2_layer_preset_count();
    char visible_layers[256] = "";
    for (int li = 0; li < preset_count; li++) {
        int byte = mk2_layer_preset_wx(li);
        bool has = false;
        for (int i = 0; i < g_no && !has; i++)
            if (((g_obj[i].wx >> 8) & 0xFF) == byte) has = true;
        if (!has) continue;
        char part[48];
        snprintf(part, sizeof part, "%s%s",
                 visible_layers[0] ? ", " : "",
                 layer_friendly_name(byte));
        strncat(visible_layers, part,
                sizeof visible_layers - strlen(visible_layers) - 1);
    }
    game_view_tools_text_disabled_wrapped("Visible Layers: %s",
                                         visible_layers[0] ? visible_layers : "none");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Depth/paint-order byte only. Preview scroll is controlled by the object's module.");

    if (ImGui::SmallButton("Reset All Layers 0x40"))
        game_view_reset_all_layers(0x40);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Undoable. Sets every unlocked object to Floor/play depth 0x40.");

    int sel_count = 0;
    int cur_layer = -1;
    int primary_obj = -1;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        sel_count++;
        if (cur_layer == -1) {
            cur_layer = (g_obj[i].wx >> 8) & 0xFF;
            primary_obj = i;
        }
    }
    if (sel_count <= 0)
        return;

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f,0.85f,0.4f,1.0f),
                       "Assign Layer  (%d selected)", sel_count);
    for (int li = 0; li < preset_count; li++) {
        int byte = mk2_layer_preset_wx(li);
        bool is_cur = (cur_layer == byte);
        if (is_cur) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f,0.55f,0.90f,1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f,0.65f,1.00f,1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f,0.45f,0.80f,1.00f));
        }
        char lbl[32];
        snprintf(lbl, sizeof lbl, "%s##la%d", layer_friendly_name(byte), li);
        if (ImGui::SmallButton(lbl)) {
            ObjectRecordUndoCapture undo;
            object_record_undo_capture_selected(&undo);
            for (int i = 0; i < g_no; i++) {
                if (!g_sel_flags[i] || g_obj_lock[i]) continue;
                g_obj[i].wx = (g_obj[i].wx & 0x00FF) | (byte << 8);
            }
            if (object_record_undo_commit(&undo, "Assign Layer") > 0)
                g_dirty = 1;
            g_view_changed = 1;
        }
        if (is_cur) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set depth/paint-order to 0x%02X (%s).",
                              byte, layer_friendly_name(byte));
        if (li + 1 < preset_count)
            game_view_tools_same_line_if_fits(86.0f, 3.0f);
    }

    if (primary_obj >= 0) {
        Img *pim = img_find(g_obj[primary_obj].ii);
        int mod = assign_module(g_obj[primary_obj].depth,
                                g_obj[primary_obj].sy,
                                pim ? pim->w : 1,
                                pim ? pim->h : 1);
        float eff_scroll = bdd_object_game_scroll_factor(primary_obj);
        char mod_name[64] = "";
        if (mod >= 0 &&
            parse_module_bounds(mod, mod_name, NULL, NULL, NULL, NULL)) {
            game_view_tools_text_disabled_wrapped("Scroll %.2fx from module \"%s\".",
                                                 eff_scroll, mod_name);
        } else {
            game_view_tools_text_disabled_wrapped("Scroll %.2fx from object layer.",
                                                 eff_scroll);
        }
    }
}

static void draw_game_view_side_tools_panel(const BddScreenRect &vp, ImVec2 ds)
{
    float tool_w = 400.0f;
    float x = (float)(vp.x + vp.w + 12);
    if (x + tool_w > ds.x - 12.0f)
        x = ds.x - tool_w - 12.0f;
    if (x < 8.0f)
        x = 8.0f;
    float y = (float)vp.y;
    float min_y = (float)bg_editor_canvas_top_px() + 8.0f;
    if (y < min_y) y = min_y;
    float h = ds.y - y - 88.0f;
    if (h < 280.0f) h = 280.0f;
    if (h > 620.0f) h = 620.0f;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(tool_w, h), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Game View Tools")) {
        draw_game_view_stage_frame_controls();
        if (game_view_tools_collapsing_header("Module Parallax",
                                              ImGuiTreeNodeFlags_DefaultOpen))
            draw_game_view_runtime_parallax_editor();
        if (game_view_tools_collapsing_header("Match Start & Fighter Guide"))
            draw_game_view_match_start_controls();
        if (game_view_tools_collapsing_header("Selection & Layers"))
            draw_game_view_selection_layer_controls();
    }
    ImGui::End();
}

void draw_game_view_controls(void)
{
        /* game view auto-scroll (parallax play) */
        static bool  s_gv_play  = false;
        static float s_gv_speed      = 60.0f;  /* px/sec */
        static float s_gv_accum      = 0.0f;   /* sub-pixel accumulator */
        static int   s_gv_dir        = 1;      /* +1 right, -1 left */
    
        /* game view controls */
        if (g_game_view && g_have_bdb && !g_preview_mode) {
            int wx_min=0, wx_max=1024, wy_min=0, wy_max=0;
            bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
            int scroll_max = wx_max - 400;
            int scroll_y_max = wy_max - 254;
            {
                static char s_fighter_box_stage_key[640] = "";
                char key[640];
                snprintf(key, sizeof key, "%s|%s", g_bdb_path, g_name);
                if (strncmp(key, s_fighter_box_stage_key, sizeof s_fighter_box_stage_key) != 0) {
                    snprintf(s_fighter_box_stage_key, sizeof s_fighter_box_stage_key, "%s", key);
                    int parsed_ground = 0;
                    if (bdd_get_stage_ground_y(&parsed_ground)) {
                        g_stage_start_ground_y = parsed_ground;
                        g_match_start_fighter_box_y =
                            g_stage_start_ground_y - g_match_start_fighter_box_h;
                    }
                }
            }
    
            /* auto-zoom: fire when game view first opens AND whenever window is resized */
            {
                static float s_last_dsx = 0.0f, s_last_dsy = 0.0f;
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                bool size_changed = (ds.x != s_last_dsx || ds.y != s_last_dsy);
                if (g_gv_needs_autozoom || size_changed) {
                    g_gv_needs_autozoom = false;
                    s_last_dsx = ds.x; s_last_dsy = ds.y;
                    fit_game_preview_zoom_to_window();
                    focus_editor_on_game_preview_screen();
                }
            }
    
            /* advance auto-scroll this frame */
            if (s_gv_play && scroll_max > wx_min) {
                s_gv_accum += s_gv_speed * ImGui::GetIO().DeltaTime * (float)s_gv_dir;
                int step = (int)s_gv_accum;
                if (step != 0) {
                    s_gv_accum -= (float)step;
                    g_scroll_pos += step;
                    if (g_scroll_pos >= scroll_max) { g_scroll_pos = scroll_max; s_gv_dir = -1; }
                    if (g_scroll_pos <= wx_min)      { g_scroll_pos = wx_min;     s_gv_dir =  1; }
                }
            }
            if (g_scroll_pos < wx_min)      g_scroll_pos = wx_min;
            if (g_scroll_pos > scroll_max)  g_scroll_pos = scroll_max;
            if (g_game_view_y < wy_min)     g_game_view_y = wy_min;
            if (g_game_view_y > scroll_y_max) g_game_view_y = scroll_y_max;
            game_view_refresh_stage_frame_state(false);
    
            /* Controls panel sits bottom-center under the game viewport. */
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            BddScreenRect vp;
            bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &vp);
            float anchor_y = (float)(vp.y + vp.h + 6);        /* reserved space below viewport */
            float max_anchor_y = ds.y - 136.0f;
            float panel_w = ds.x - 48.0f;
            if (panel_w > 760.0f) panel_w = 760.0f;
            if (panel_w < 500.0f) panel_w = 500.0f;

            draw_game_view_side_tools_panel(vp, ds);

            if (anchor_y > max_anchor_y)
                anchor_y = max_anchor_y;
            if (anchor_y < (float)bg_editor_canvas_top_px())
                anchor_y = (float)bg_editor_canvas_top_px();
            ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, anchor_y),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(panel_w, 0), ImGuiCond_Always);  /* height auto */
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f,0.08f,0.12f,0.92f));
            if (ImGui::Begin("##gameview", NULL, ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_AlwaysAutoResize))
            {
                const char *layout_label =
                    g_runtime_layout_view ? "Runtime Layout" : "BDB Source";
                ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f), "Game Preview");
                ImGui::SameLine(0, 10);
                ImGui::TextDisabled("%s  |  Camera X %d  Y %d  |  Frame %dx%d",
                                    layout_label, g_scroll_pos, g_game_view_y,
                                    s_game_view_frame_w, s_game_view_frame_h);

                ImGui::Separator();
                ImGui::TextDisabled("Playback");
                ImGui::SameLine(92.0f);
                if (s_gv_play) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f,0.24f,0.18f,0.95f));
                    if (ImGui::Button("Pause##gv_play", ImVec2(64.0f, 0.0f)))
                        s_gv_play = false;
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f,0.50f,0.28f,0.95f));
                    if (ImGui::Button("Play##gv_play", ImVec2(64.0f, 0.0f))) {
                        s_gv_play = true;
                        s_gv_dir = 1;
                        s_gv_accum = 0;
                    }
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Auto-scrolls the camera left and right.");
                ImGui::SameLine(0, 6);
                if (ImGui::SmallButton("Match Start")) {
                    route_to_game_preview_screen(true, false);
                    s_gv_play = false;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Reset to the BGND match-start camera.");
                ImGui::SameLine(0, 6);
                if (ImGui::SmallButton("Left Edge"))  { g_scroll_pos = wx_min;     s_gv_play = false; }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("-50"))        { g_scroll_pos -= 50;        s_gv_play = false; }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("-10"))        { g_scroll_pos -= 10;        s_gv_play = false; }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("+10"))        { g_scroll_pos += 10;        s_gv_play = false; }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("+50"))        { g_scroll_pos += 50;        s_gv_play = false; }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("Right Edge")) { g_scroll_pos = scroll_max; s_gv_play = false; }

                ImGui::TextDisabled("Speed");
                ImGui::SameLine(92.0f);
                ImGui::SetNextItemWidth(150.0f);
                ImGui::SliderFloat("##gv_speed", &s_gv_speed, 10.0f, 400.0f, "%.0f px/s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Auto-scroll speed.");
                ImGui::SameLine(0, 10);
                ImGui::TextDisabled("Camera X");
                ImGui::SameLine(0, 6);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##gv_camera_x", &g_scroll_pos, wx_min, scroll_max);

                ImGui::TextDisabled("Camera Y");
                ImGui::SameLine(92.0f);
                if (ImGui::SmallButton("Top"))    { g_game_view_y = wy_min;       }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("-20"))    { g_game_view_y -= 20;          }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("+20"))    { g_game_view_y += 20;          }
                ImGui::SameLine(0, 4);
                if (ImGui::SmallButton("Bottom")) { g_game_view_y = scroll_y_max; }
                ImGui::SameLine(0, 10);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##gv_camera_y", &g_game_view_y, wy_min, scroll_y_max);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Vertical camera offset.");

                /* out-of-viewport warning — objects outside current camera window */
                {
                    int below = 0;
                    for (int i = 0; i < g_no; i++) {
                        int oy = g_obj[i].sy;
                        Img *im = img_find(g_obj[i].ii);
                        if (!im) continue;
                        if (g_runtime_layout_view) {
                            int ox = g_obj[i].depth;
                            gv_object_origin(i, &ox, &oy);
                        }
                        if (oy >= g_game_view_y + 254 || oy + im->h <= g_game_view_y) below++;
                    }
                    if (below > 0) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                            "  %d object%s off-screen  (Y: %d-%d)",
                            below, below == 1 ? "" : "s", g_game_view_y, g_game_view_y + 254);
                        ImGui::SameLine(0, 8);
                        if (ImGui::SmallButton("World View  (see all)")) {
                            g_game_view = 0;
                            zoom_to_fit();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Switch to world view and fit all objects -\n"
                                "a gold border shows where the game screen is.");
                    }
                }
            }
            ImGui::PopStyleColor();
            ImGui::End();
        } else {
            s_gv_play = false;
            g_gv_needs_autozoom = true;  /* re-fit next time game view opens */
        }
    
    
}
