#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdint.h>

void draw_tile_preview(void)
{
    if (!g_tile_preview || !g_have_bdb || !g_show_mk2_workflow || g_preview_mode) return;
    if (g_tile_img < 0 || g_tile_img >= g_ni) return;
    SDL_Texture *tile_tex = editor_texture_at(g_tile_img);
    if (!tile_tex) return;

    Img *im  = &g_img[g_tile_img];
    int  cols = (g_tile_cols < 1) ? 1 : (g_tile_cols > 32 ? 32 : g_tile_cols);
    int  rows = (g_tile_rows < 1) ? 1 : (g_tile_rows > 32 ? 32 : g_tile_rows);
    int  sx   = (g_tile_sx < 1) ? 1 : g_tile_sx;
    int  sy   = (g_tile_sy < 1) ? 1 : g_tile_sy;
    float zoom = (float)g_zoom;
    float iw  = im->w * zoom;
    float ih  = im->h * zoom;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float x0 = (g_tile_ox + c * sx) * zoom;
            float y0 = (g_tile_oy + r * sy) * zoom;
            dl->AddImage((ImTextureID)(intptr_t)tile_tex,
                         ImVec2(x0, y0), ImVec2(x0 + iw, y0 + ih),
                         ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,110));
            dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + iw, y0 + ih),
                        IM_COL32(80,200,255,180), 0.0f, 0, 1.0f);
        }
    }
}
