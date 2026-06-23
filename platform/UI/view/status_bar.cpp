#include "bg_editor.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/world_module_utils.h"
#include "UI/view/status_bar.h"
#include "imgui.h"

#include <limits.h>
#include <stdio.h>

static int stage_health(char issues[][64], int max_issues, int *issue_n)
{
    *issue_n = 0;
    if (!g_have_bdb || g_no == 0) return 0;
    int sev = 0;
    auto add = [&](int s, const char *msg) {
        if (s > sev) sev = s;
        if (*issue_n < max_issues) { snprintf(issues[(*issue_n)++], 64, "%s", msg); }
    };
    int object_cap = editor_project_object_capacity();
    int image_cap = editor_project_image_capacity();
    if (object_cap > 0 && g_no  > (int)(object_cap * 0.9f)) add(2, "Sprites near limit");
    else if (object_cap > 0 && g_no  > (int)(object_cap * 0.75f)) add(1, "Sprites 75%+ full");
    if (image_cap > 0 && g_ni  > (int)(image_cap  * 0.9f)) add(2, "Images near limit");
    else if (image_cap > 0 && g_ni  > (int)(image_cap  * 0.75f)) add(1, "Images 75%+ full");
    if (g_bdb_num_modules == 0) add(2, "No regions defined");
    if (g_bdb_num_modules > 0) {
        int outside = 0;
        for (int i = 0; i < g_no && outside < 3; i++) {
            Img *im = img_find(g_obj[i].ii);
            int ow = im ? im->w : 1, oh = im ? im->h : 1;
            if (assign_module(g_obj[i].depth, g_obj[i].sy, ow, oh) < 0) outside++;
        }
        if (outside >= 3) add(1, "Some sprites outside regions");
    }
    return sev;
}

void draw_status(void)
{
    if (!g_have_bdb) {
        char hint[128];
        if (g_simple_mode)
            snprintf(hint, sizeof hint, "Open a stage or create a New Project to get started  |  F1 for help");
        else
            snprintf(hint, sizeof hint, "Open a BDB/BDD file or create a New Project to get started  |  F1 for help");
        ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 20));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 20));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.95f));
        ImGui::Begin("##status_none", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    ImVec2 mp = ImGui::GetIO().MousePos;
    int wx = 0, wy = 0;
    char runtime_xy[32] = "";
    if (ImGui::IsMousePosValid(&mp)) {
        bdd_screen_to_world((int)mp.x, (int)mp.y, g_view_x, g_view_y, g_zoom, &wx, &wy);
        if (g_runtime_layout_view) {
            int rwx = 0, rwy = 0;
            bdd_world_point_runtime_origin(wx, wy, &rwx, &rwy);
            snprintf(runtime_xy, sizeof runtime_xy, "  In-game: (%d, %d)", rwx, rwy);
        }
    }

    int sel_count = 0;
    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_count++;

    int sel_x0 = INT_MAX, sel_y0 = INT_MAX, sel_x1 = INT_MIN, sel_y1 = INT_MIN;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        Img *sim = img_find(g_obj[i].ii);
        int iw = sim ? sim->w : 1, ih = sim ? sim->h : 1;
        if (g_obj[i].depth < sel_x0) sel_x0 = g_obj[i].depth;
        if (g_obj[i].sy    < sel_y0) sel_y0 = g_obj[i].sy;
        if (g_obj[i].depth + iw > sel_x1) sel_x1 = g_obj[i].depth + iw;
        if (g_obj[i].sy    + ih > sel_y1) sel_y1 = g_obj[i].sy    + ih;
    }
    char sel_bbox[48] = "";
    if (sel_count > 0)
        snprintf(sel_bbox, sizeof sel_bbox, "  |  Sel: (%d,%d) %dx%d",
                 sel_x0, sel_y0, sel_x1 - sel_x0, sel_y1 - sel_y0);

    char buf[640];
    int bar_h;
    if (g_simple_mode) {
        snprintf(buf, sizeof buf,
                 "%s%s  -  %d sprites  %d images  %d palettes  |  Zoom: %dx  |  (%d, %d)%s",
                 g_dirty ? "* " : "", g_name[0] ? g_name : "untitled",
                 g_no, g_ni, g_n_pals, g_zoom, wx, wy, sel_bbox);
        bar_h = 20;
    } else {
        const char *preview_mode = g_game_view ? "Game Preview" : "World View";
        const char *layout_mode = g_runtime_layout_view ? "Runtime Layout" : "BDB Source";
        snprintf(buf, sizeof buf,
                 "Objects: %d (%d sel)  Images: %d  Palettes: %d  Zoom: %dx  World: (%d, %d)  Mouse: (%d, %d)%s%s\n"
                 "Preview: %s / %s  Camera: (%d, %d)  Parallax: %s\n"
                 "BDB: %s\n"
                 "BDD: %s",
                 g_no, sel_count, g_ni, g_n_pals, g_zoom, g_view_x, g_view_y, wx, wy, sel_bbox, runtime_xy,
                 preview_mode, layout_mode, g_scroll_pos, g_game_view_y,
                 g_game_view ? "layer scroll active" : "shown in game preview",
                 g_bdb_path[0] ? g_bdb_path : "(none)",
                 g_bdd_path[0] ? g_bdd_path : "(none)");
        bar_h = 68;
    }
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - (float)bar_h));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, (float)bar_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.95f));
    ImGui::Begin("##status", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(buf);
    if (g_simple_mode && g_have_bdb) {
        char issues[6][64]; int issue_n = 0;
        int health = stage_health(issues, 6, &issue_n);
        ImVec4 dot_col = health == 0 ? ImVec4(0.2f,0.8f,0.3f,1) :
                         health == 1 ? ImVec4(1.0f,0.75f,0.1f,1) :
                                       ImVec4(1.0f,0.3f,0.3f,1);
        ImGui::SameLine();
        float right_x = ImGui::GetIO().DisplaySize.x - 24;
        ImGui::SetCursorPosX(right_x - ImGui::GetWindowPos().x);
        /* draw a filled health dot (the default font has no solid-circle glyph) */
        ImVec2 dp = ImGui::GetCursorScreenPos();
        float fh = ImGui::GetFontSize();
        float rad = fh * 0.32f;
        ImGui::Dummy(ImVec2(rad * 2.0f + 2.0f, fh));
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(dp.x + rad + 1.0f, dp.y + fh * 0.5f), rad,
            ImGui::ColorConvertFloat4ToU32(dot_col));
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            if (issue_n == 0) ImGui::TextUnformatted("Stage looks good");
            for (int ii = 0; ii < issue_n; ii++) ImGui::TextUnformatted(issues[ii]);
            ImGui::EndTooltip();
        }
    }
    ImGui::PopStyleColor();
    ImGui::End();
}
