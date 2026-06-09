#include "Core/path_utils.h"
#include "UI/preview_image_file.h"
#include "UI/sdl_context.h"
#include "imgui.h"
#include "stb_image.h"

#include <cstdio>
#include <cstring>
#include <cstdint>

static SDL_Texture *g_preview_tex = NULL;
static char g_preview_tex_path[512] = "";
static int g_preview_tex_w = 0;
static int g_preview_tex_h = 0;

void clear_preview_image_file_texture(void)
{
    if (g_preview_tex) {
        SDL_DestroyTexture(g_preview_tex);
        g_preview_tex = NULL;
    }
    g_preview_tex_path[0] = '\0';
    g_preview_tex_w = 0;
    g_preview_tex_h = 0;
}

static bool load_preview_texture(const char *path)
{
    if (!path || !path[0]) return false;
    if (g_preview_tex && strcmp(g_preview_tex_path, path) == 0)
        return true;
    if (!stage_file_exists(path)) return false;

    int w = 0;
    int h = 0;
    int n = 0;
    unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba) return false;

    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(rgba, w, h, 32, w * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    SDL_Texture *tex = surf ? SDL_CreateTextureFromSurface(g_rend, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);
    stbi_image_free(rgba);
    if (!tex) return false;

    clear_preview_image_file_texture();
    g_preview_tex = tex;
    snprintf(g_preview_tex_path, sizeof g_preview_tex_path, "%s", path);
    g_preview_tex_w = w;
    g_preview_tex_h = h;
    return true;
}

void draw_preview_image_file(const char *label, const char *path)
{
    if (!path || !path[0]) {
        ImGui::TextDisabled("%s: no path", label);
        return;
    }
    if (!load_preview_texture(path)) {
        ImGui::TextDisabled("%s: %s", label, path);
        return;
    }
    ImGui::Text("%s: %s", label, path);
    float max_w = ImGui::GetContentRegionAvail().x;
    if (max_w > 260.0f) max_w = 260.0f;
    float sc = max_w / (float)g_preview_tex_w;
    if (sc > 1.0f) sc = 1.0f;
    if (sc < 0.1f) sc = 0.1f;
    ImGui::Image(g_preview_tex, ImVec2(g_preview_tex_w * sc, g_preview_tex_h * sc));
}
