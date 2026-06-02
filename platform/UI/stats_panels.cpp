#include "bg_editor_globals.h"
#include "imgui.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void draw_level_stats_panel(void)
{
    if (!g_show_level_stats) return;
    set_left_panel_default(92.0f, 400.0f, 0.0f);
    ImGui::Begin("Level Size Stats##lstats", &g_show_level_stats,
                 ImGuiWindowFlags_AlwaysAutoResize);

    size_t img_pixels = 0;
    int    bpp8 = 0, bpp6 = 0, bpp4 = 0, bpp_lo = 0;
    for (int i = 0; i < g_ni; i++) {
        size_t px = (size_t)g_img[i].w * g_img[i].h;
        img_pixels += px;
        int mp = image_max_pixel(&g_img[i]);
        if      (mp >= 64) bpp8++;
        else if (mp >= 16) bpp6++;
        else if (mp >= 4)  bpp4++;
        else               bpp_lo++;
    }

    size_t pal_rom = (size_t)g_n_pals * 256 * 2;
    size_t overhead = (size_t)g_ni * 32 + (size_t)g_no * 16 + 256;
    size_t total    = img_pixels + pal_rom + overhead;

    const size_t BUDGET = 2u * 1024 * 1024;

    ImGui::TextDisabled("Estimated ROM footprint (raw, uncompressed)");
    ImGui::Separator();

    ImGui::Text("Images   %3d", g_ni);
    ImGui::SameLine(100);
    ImGui::Text("%.1f KB  (%zu px)", (double)img_pixels / 1024.0, img_pixels);
    ImGui::ProgressBar((float)((double)img_pixels / BUDGET), ImVec2(-1, 8),
                       img_pixels >= BUDGET ? "OVER" : "");

    ImGui::Text("Palettes %3d", g_n_pals);
    ImGui::SameLine(100);
    ImGui::Text("%.1f KB", (double)pal_rom / 1024.0);
    ImGui::ProgressBar((float)((double)pal_rom / BUDGET), ImVec2(-1, 8), "");

    ImGui::Text("Objects  %3d", g_no);
    ImGui::SameLine(100);
    ImGui::Text("~%.0f B overhead", (double)overhead);

    ImGui::Separator();
    float pct = (float)(100.0 * (double)total / BUDGET);
    ImVec4 bar_col = (pct < 60) ? ImVec4(0.2f,0.8f,0.2f,1) :
                     (pct < 85) ? ImVec4(0.9f,0.7f,0.1f,1) :
                                  ImVec4(1.0f,0.2f,0.1f,1);
    ImGui::TextColored(bar_col, "Total  ~%.1f KB  (%.0f%% of 2 MB budget)", (double)total/1024.0, pct);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_col);
    ImGui::ProgressBar((float)((double)total / BUDGET), ImVec2(-1, 12), "");
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::TextDisabled("Image bit-depth breakdown:");
    ImGui::Text("  8bpp (>=64 idx) : %d images", bpp8);
    ImGui::Text("  6bpp (16-63 idx): %d images", bpp6);
    ImGui::Text("  4bpp (4-15 idx) : %d images", bpp4);
    ImGui::Text("  <=2bpp (<4 idx) : %d images", bpp_lo);
    if (bpp8 > 0) {
        ImGui::TextColored(ImVec4(1,0.6f,0.2f,1),
            "  Tip: %d image(s) use high palette indices - check Bit Depth Preview.", bpp8);
    }

    ImGui::End();
}

void draw_bpp_preview_panel(void)
{
    if (!g_show_bpp_preview) return;
    static int          s_bpp      = 8;
    static SDL_Texture *s_tex      = nullptr;
    static int          s_last_img = -2;
    static int          s_last_bpp = -1;

    set_left_panel_default(300.0f, 380.0f, 0.0f);
    ImGui::Begin("Bit Depth Preview##bppwin", &g_show_bpp_preview,
                 ImGuiWindowFlags_AlwaysAutoResize);

    {
        static const int bpp_opts[] = {2, 4, 6, 8};
        for (int bi = 0; bi < 4; bi++) {
            int b = bpp_opts[bi];
            if (bi != 0) ImGui::SameLine(0, 6);
            if (s_bpp == b) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            char lbl[8]; snprintf(lbl, sizeof lbl, "%dbpp", b);
            if (ImGui::SmallButton(lbl)) s_bpp = b;
            if (s_bpp == b) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d colors (indices 0-%d)", 1<<b, (1<<b)-1);
        }
    }
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("%d colors", 1 << s_bpp);

    int img_i = active_image_index();
    Img *im = (img_i >= 0 && img_i < g_ni) ? &g_img[img_i] : nullptr;

    if (!im || !im->pix || im->w <= 0 || im->h <= 0) {
        if (s_tex) { SDL_DestroyTexture(s_tex); s_tex = nullptr; s_last_img = -2; }
        ImGui::TextDisabled("No image selected.");
        ImGui::End();
        return;
    }

    if (img_i != s_last_img || s_bpp != s_last_bpp) {
        if (s_tex) { SDL_DestroyTexture(s_tex); s_tex = nullptr; }
        int max_idx = (s_bpp == 8) ? 256 : (1 << s_bpp);
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                            ? g_pals[im->pal_idx] : nullptr;
        if (pal) {
            int n = im->w * im->h;
            static Uint32 pxbuf[512 * 512];
            if (n <= (int)(sizeof(pxbuf) / sizeof(pxbuf[0]))) {
                for (int i = 0; i < n; i++) {
                    Uint8 v = im->pix[i];
                    pxbuf[i] = (v > 0 && v < max_idx) ? pal[v] : 0u;
                }
                s_tex = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC, im->w, im->h);
                if (s_tex) {
                    SDL_SetTextureBlendMode(s_tex, SDL_BLENDMODE_BLEND);
                    SDL_UpdateTexture(s_tex, NULL, pxbuf, im->w * (int)sizeof(Uint32));
                }
            }
        }
        s_last_img = img_i;
        s_last_bpp = s_bpp;
    }

    float scale = (im->w <= 64 && im->h <= 64) ? 3.0f :
                  (im->w <= 128 && im->h <= 128) ? 2.0f : 1.0f;
    ImVec2 img_sz(im->w * scale, im->h * scale);
    if (s_tex) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x+img_sz.x, p.y+img_sz.y),
                                                  IM_COL32(60,60,60,255));
        ImGui::Image((ImTextureID)(uintptr_t)s_tex, img_sz);
    } else {
        ImGui::TextDisabled("(no palette / image too large)");
    }

    int max_idx = (s_bpp == 8) ? 256 : (1 << s_bpp);
    int clipped = 0, total_px = im->w * im->h;
    for (int i = 0; i < total_px; i++)
        if (im->pix[i] >= max_idx) clipped++;
    if (clipped == 0) {
        ImGui::TextColored(ImVec4(0.3f,1.0f,0.3f,1.0f),
            "All pixels fit in %dbpp  (max index %d)", s_bpp, image_max_pixel(im));
    } else {
        float pct = 100.0f * clipped / total_px;
        ImGui::TextColored(ImVec4(1.0f,0.35f,0.2f,1.0f),
            "%.1f%% clipped (%d px invisible at %dbpp)", pct, clipped, s_bpp);
    }
    ImGui::TextDisabled("Image 0x%02X  %dx%d  pal %d", im->idx, im->w, im->h, im->pal_idx);

    ImGui::End();
}
