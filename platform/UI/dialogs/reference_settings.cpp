#include "Core/editor_project_globals.h"
#include "Core/editor_app_globals.h"
#include "UI/dialogs/native_file_dialogs.h"
#include "UI/dialogs/reference_settings.h"
#include "UI/view/right_panel_layout.h"
#include "UI/sdl/sdl_context.h"
#include "imgui.h"
#include "libs/stb_image.h"

#include <stddef.h>

void draw_ref_settings(void)
{
    if (!g_show_ref_settings) return;
    set_left_panel_default(92.0f, 320.0f, 180.0f);
    if (ImGui::Begin("Reference Image", &g_show_ref_settings)) {
        ImGui::Text("Load a transparent PNG tracing layer.");
        ImGui::Separator();
        if (ImGui::Button("Load PNG...")) {
            char path[512] = "";
            if (file_dialog_open("Open Reference PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path)) {
                int w, h, n;
                unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
                if (rgba) {
                    if (g_ref_tex) { SDL_DestroyTexture(g_ref_tex); g_ref_tex = NULL; }
                    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(rgba, w, h, 32, w * 4,
                        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
                    if (surf) {
                        g_ref_tex = SDL_CreateTextureFromSurface(g_rend, surf);
                        SDL_FreeSurface(surf);
                    }
                    stbi_image_free(rgba);
                    g_view_changed = 1;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear") && g_ref_tex) {
            SDL_DestroyTexture(g_ref_tex);
            g_ref_tex = NULL;
            g_view_changed = 1;
        }
        ImGui::Separator();
        ImGui::InputInt("Offset X", &g_ref_ox);
        ImGui::InputInt("Offset Y", &g_ref_oy);
    }
    ImGui::End();
}
