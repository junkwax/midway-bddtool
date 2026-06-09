#include "bg_editor_globals.h"
#include "imgui.h"

void draw_empty_canvas_hint(void)
{
    if (!g_have_bdb || g_no > 0 || g_preview_mode || g_welcome_show) return;

    const bool has_images = (g_ni > 0);
    const char *title = has_images
        ? "Use Place once or Brush for repeats"
        : "Import a PNG to get started";
    const char *sub = has_images
        ? "Select Place, click the canvas once, then drag the selected object"
        : "Click Import in the sidebar, or drag a PNG file onto the canvas";

    ImVec2 ds  = ImGui::GetIO().DisplaySize;
    float  W   = 400.0f, H = 90.0f;
    float  cx  = ds.x * 0.5f, cy = ds.y * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(cx - W * 0.5f, cy - H * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##empty_hint", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize  |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar|
                 ImGuiWindowFlags_NoInputs   | ImGuiWindowFlags_NoNav     |
                 ImGuiWindowFlags_NoDecoration);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetWindowPos();
    ImVec2 p1 = ImVec2(p0.x + W, p0.y + H);
    dl->AddRect(p0, p1, IM_COL32(100,170,255, 80), 10.0f, 0, 1.5f);
    dl->AddRect(ImVec2(p0.x+3,p0.y+3), ImVec2(p1.x-3,p1.y-3),
                IM_COL32(100,170,255, 40), 8.0f, 0, 1.0f);

    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPos(ImVec2((W - tw) * 0.5f, 18.0f));
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 0.90f), "%s", title);

    float sw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPos(ImVec2((W - sw) * 0.5f, 44.0f));
    ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.80f, 0.70f), "%s", sub);

    ImGui::End();
}
