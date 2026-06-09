#include "UI/assets/image_draw_helpers.h"
#include "UI/assets/texture_cache.h"

#include "imgui.h"

SDL_Texture *editor_texture_at(int img_i)
{
    if (!g_textures || img_i < 0 || img_i >= g_ni_tex)
        return nullptr;
    return g_textures[img_i];
}

static void draw_checkerboard_background(ImVec2 min, ImVec2 max, float cell = 8.0f)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 c0 = IM_COL32(48, 48, 52, 255);
    const ImU32 c1 = IM_COL32(92, 92, 96, 255);
    int row = 0;
    for (float y = min.y; y < max.y; y += cell, row++) {
        int col = 0;
        float y2 = y + cell;
        if (y2 > max.y) y2 = max.y;
        for (float x = min.x; x < max.x; x += cell, col++) {
            float x2 = x + cell;
            if (x2 > max.x) x2 = max.x;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x2, y2), ((row + col) & 1) ? c1 : c0);
        }
    }
}

static void draw_transparent_image(SDL_Texture *tex, ImVec2 size,
                                   ImVec2 uv0 = ImVec2(0, 0),
                                   ImVec2 uv1 = ImVec2(1, 1))
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    draw_checkerboard_background(p, ImVec2(p.x + size.x, p.y + size.y));
    ImGui::Image(tex, size, uv0, uv1);
}

void draw_editor_texture_transparent(SDL_Texture *tex, float width, float height)
{
    draw_transparent_image(tex, ImVec2(width, height));
}

void draw_editor_texture_transparent_uv(SDL_Texture *tex, float width, float height,
                                        float u0, float v0, float u1, float v1)
{
    draw_transparent_image(tex, ImVec2(width, height), ImVec2(u0, v0), ImVec2(u1, v1));
}

static void draw_solid_backdrop_image(SDL_Texture *tex, ImVec2 size, bool white,
                                      ImVec2 uv0 = ImVec2(0, 0),
                                      ImVec2 uv1 = ImVec2(1, 1))
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 bg = white ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
    ImU32 edge = white ? IM_COL32(40, 40, 40, 160) : IM_COL32(210, 210, 210, 120);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg);
    ImGui::Image(tex, size, uv0, uv1);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), edge);
}

