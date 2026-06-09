#include "Core/editor_app_globals.h"
#include "UI/view/onboarding_hints.h"

#include "imgui.h"

#include <cmath>
/* ---- onboarding hint badge ---------------------------------------- */

/* Draw a pulsing colored "!" badge if *flag is true; returns true when clicked (dismiss) */
bool hint_badge(bool *flag, const char *id)
{
    if (!g_simple_mode || !flag || !*flag) return false;
    float t = (float)ImGui::GetTime();
    float alpha = 0.55f + 0.45f * sinf(t * 4.0f);
    ImGui::SameLine(0, 4);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.9f,0.6f,0.1f,alpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f,0.8f,0.2f,0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f,0.9f,0.4f,1.0f));
    bool clicked = ImGui::Button("!", ImVec2(18, 0));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to dismiss this tip");
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    if (clicked) *flag = false;
    return clicked;
}

