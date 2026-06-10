#include "bg_editor.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "UI/sdl/sdl_context.h"
#include "UI/view/stage_overview.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <climits>

static SDL_Texture *g_ov_tex = NULL;
static bool g_ov_open = false;
static int g_ov_w = 0;
static int g_ov_h = 0;
static char g_ov_err[256] = "";

bool stage_overview_is_open(void)
{
    return g_ov_open;
}

/* Native full-stage composite: draw every object in BDB order using its
   per-object palette (fl), transparency index 0, and horizontal/vertical
   flip flags. Mirrors the texture-cache expansion; needs no external tools. */
static void ov_render(void)
{
    g_ov_err[0] = '\0';
    if (g_ov_tex) { SDL_DestroyTexture(g_ov_tex); g_ov_tex = NULL; }
    g_ov_w = g_ov_h = 0;

    if (!g_have_bdb || g_no <= 0) {
        snprintf(g_ov_err, sizeof g_ov_err, "No stage loaded - open a .BDB and .BDD first.");
        return;
    }

    /* Canvas size: source render uses the declared world size. Runtime render
       uses the same projected bounds as the editor canvas. */
    int world_w = 0, world_h = 0;
    if (g_bdb_header[0]) {
        char nm[64]; int d = 0, m = 0, p = 0, o = 0;
        sscanf(g_bdb_header, "%63s %d %d %d %d %d %d", nm, &world_w, &world_h, &d, &m, &p, &o);
    }
    int wx_min = 0;
    int wy_min = 0;
    int wx_max = world_w;
    int wy_max = world_h;
    if (g_runtime_layout_view) {
        bdd_get_editor_layout_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    } else {
        int bx_min = 0, bx_max = 0, by_min = 0, by_max = 0;
        bdd_get_world_bounds(&bx_min, &bx_max, &by_min, &by_max);
        if (bx_min != INT_MAX && bx_max != INT_MIN && by_min != INT_MAX && by_max != INT_MIN) {
            if (bx_min < wx_min) wx_min = bx_min;
            if (by_min < wy_min) wy_min = by_min;
            if (bx_max > wx_max) wx_max = bx_max;
            if (by_max > wy_max) wy_max = by_max;
        }
    }
    if (wx_min == INT_MAX || wx_max == INT_MIN || wy_min == INT_MAX || wy_max == INT_MIN ||
        wx_max <= wx_min || wy_max <= wy_min) {
        wx_min = 0;
        wy_min = 0;
        wx_max = world_w > 0 ? world_w : 1;
        wy_max = world_h > 0 ? world_h : 1;
    }
    int W = wx_max - wx_min;
    int H = wy_max - wy_min;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > 16384) W = 16384;
    if (H > 16384) H = 16384;

    Uint32 *canvas = (Uint32 *)calloc((size_t)W * (size_t)H, sizeof(Uint32));
    if (!canvas) {
        snprintf(g_ov_err, sizeof g_ov_err, "Out of memory for %dx%d composite.", W, H);
        return;
    }

    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        int pal_idx = g_obj[i].fl;
        const Uint32 *pal = (pal_idx >= 0 && pal_idx < g_n_pals) ? g_pals[pal_idx] : NULL;
        int ox = g_obj[i].depth, oy = g_obj[i].sy;
        if (g_runtime_layout_view)
            bdd_object_editor_origin(i, &ox, &oy);
        ox -= wx_min;
        oy -= wy_min;
        int hfl = g_obj[i].hfl, vfl = g_obj[i].vfl;
        for (int yy = 0; yy < im->h; yy++) {
            int dy = oy + yy;
            if (dy < 0 || dy >= H) continue;
            int src_y = vfl ? (im->h - 1 - yy) : yy;
            for (int xx = 0; xx < im->w; xx++) {
                int dx = ox + xx;
                if (dx < 0 || dx >= W) continue;
                int src_x = hfl ? (im->w - 1 - xx) : xx;
                Uint8 idx = im->pix[src_y * im->w + src_x];
                if (idx == 0) continue;  /* transparent */
                canvas[(size_t)dy * (size_t)W + (size_t)dx] =
                    pal ? pal[idx] : (0xFF000000u | (Uint32)(idx * 0x010101u));
            }
        }
    }

    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(canvas, W, H, 32, W * 4,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (surf) {
        SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
        g_ov_tex = SDL_CreateTextureFromSurface(g_rend, surf);
        SDL_FreeSurface(surf);
        if (g_ov_tex) {
            SDL_SetTextureBlendMode(g_ov_tex, SDL_BLENDMODE_BLEND);
            g_ov_w = W;
            g_ov_h = H;
        } else {
            snprintf(g_ov_err, sizeof g_ov_err, "Texture upload failed.");
        }
    } else {
        snprintf(g_ov_err, sizeof g_ov_err, "Composite surface creation failed.");
    }
    free(canvas);
}

void toggle_stage_overview(void)
{
    if (!g_ov_open) {
        g_ov_open = true;
        ov_render();
    } else {
        g_ov_open = false;
    }
}

/* Native render is synchronous; nothing to poll. Kept for the render loop. */
void poll_stage_overview_result(void)
{
}

void draw_stage_overview(void)
{
    if (!g_ov_open) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ds, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, 0.97f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin("##stageoverview", &g_ov_open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::End();
        return;
    }

    ImGui::SetCursorPos(ImVec2(ds.x - 90, 10));
    if (ImGui::Button("Close  [Esc]") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        g_ov_open = false;

    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Stage Overview");
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Re-render")) ov_render();

    if (g_ov_err[0]) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_ov_err);
    }

    if (g_ov_tex) {
        float avail_w = ds.x - 20.0f;
        float avail_h = ds.y - 50.0f;
        float scale = (avail_w / g_ov_w < avail_h / g_ov_h)
                      ? avail_w / g_ov_w : avail_h / g_ov_h;
        float iw = g_ov_w * scale;
        float ih = g_ov_h * scale;
        float ox = (ds.x - iw) * 0.5f;
        ImGui::SetCursorPos(ImVec2(ox, 46));
        ImGui::Image((ImTextureID)(intptr_t)g_ov_tex, ImVec2(iw, ih));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stage: %dx%d px  (displaying at %.0f%%)",
                              g_ov_w, g_ov_h, scale * 100.0f);
    } else if (!g_ov_err[0]) {
        ImGui::SetCursorPos(ImVec2(ds.x / 2 - 100, ds.y / 2));
        ImGui::TextDisabled("Click Re-render to generate the full stage composite.");
    }

    ImGui::End();
}
