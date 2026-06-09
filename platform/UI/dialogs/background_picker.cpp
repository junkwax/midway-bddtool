#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "UI/dialogs/background_picker.h"
#include "UI/view/right_panel_layout.h"
#include "imgui.h"

void draw_bg_picker(void)
{
    if (!g_show_bg_picker) return;
    set_left_panel_default(92.0f, 250.0f, 100.0f);
    if (ImGui::Begin("Background Color", &g_show_bg_picker)) {
        float col[3] = { g_bg_color[0]/255.0f, g_bg_color[1]/255.0f, g_bg_color[2]/255.0f };
        if (ImGui::ColorPicker3("##bgcol", col)) {
            g_bg_color[0] = (Uint8)(col[0] * 255);
            g_bg_color[1] = (Uint8)(col[1] * 255);
            g_bg_color[2] = (Uint8)(col[2] * 255);
        }
    }
    ImGui::End();
}
