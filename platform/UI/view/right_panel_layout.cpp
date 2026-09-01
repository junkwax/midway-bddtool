/* Default placement for the free-floating left-hand tool windows. The right
   side is no longer a rail of snap-docked windows -- see UI/view/right_sidebar.cpp. */

#include "bg_editor_globals.h"
#include "imgui.h"

void set_left_panel_default(float y, float w, float h)
{
    if (y < 60.0f) y = 60.0f;
    ImGui::SetNextWindowPos(ImVec2(8.0f, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
}
