int draft_backdrop(const Tab &t) const {
    auto channel = [](float v) { return (int)std::round(std::clamp(v, 0.f, 1.f) * 31); };
    return t.backdrop_enabled ? (channel(t.backdrop_rgb[0]) << 10) | (channel(t.backdrop_rgb[1]) << 5) | channel(t.backdrop_rgb[2]) : -1;
}
int displayed_backdrop(const Tab &t) const {
    return t.backdrop_preview ? draft_backdrop(t) : t.document.state().backdrop;
}
void background_controls(Tab &t) {
    if (ImGui::Button("Background...")) {
        cancel_gesture();
        int color = std::max(0, t.document.state().backdrop);
        t.backdrop_enabled = t.document.state().backdrop >= 0;
        t.backdrop_rgb[0] = float((color >> 10) & 31) / 31;
        t.backdrop_rgb[1] = float((color >> 5) & 31) / 31;
        t.backdrop_rgb[2] = float(color & 31) / 31;
        ImGui::OpenPopup("Stage background");
    }
    background_open_point = item_center();
    t.backdrop_preview = false;
    ImGui::SetNextWindowSize({std::min(420.f, ImGui::GetIO().DisplaySize.x - 24), 0});
    if (!ImGui::BeginPopup("Stage background")) return;
    t.backdrop_preview = true;
    ImGui::SeparatorText("Editor canvas");
    ImGui::ColorEdit3("Canvas color", canvas_rgb, ImGuiColorEditFlags_DisplayHex);
    if (ImGui::SmallButton("Dark")) { canvas_rgb[0] = 20.f / 255; canvas_rgb[1] = 25.f / 255; canvas_rgb[2] = 31.f / 255; }
    ImGui::SameLine(); if (ImGui::SmallButton("Black")) std::fill(std::begin(canvas_rgb), std::end(canvas_rgb), 0.f);
    ImGui::SameLine(); if (ImGui::SmallButton("White")) std::fill(std::begin(canvas_rgb), std::end(canvas_rgb), 1.f);
    ImGui::TextWrapped("Canvas color is an editor setting for this session.");
    ImGui::SeparatorText("Stage backdrop");
    ImGui::Checkbox("Set stage color", &t.backdrop_enabled);
    ImGui::BeginDisabled(!t.backdrop_enabled);
    ImGui::ColorEdit3("Stage color", t.backdrop_rgb, ImGuiColorEditFlags_DisplayHex);
    ImGui::EndDisabled();
    int color = draft_backdrop(t);
    if (color >= 0) ImGui::TextDisabled("Game RGB555: %04X", color);
    ImGui::TextWrapped("Previewed inside the game frame. Apply saves the color with this map and can be undone. Uncheck to leave the game's existing color unchanged on export.");
    ImGui::TextWrapped("Supported MK2 game exports include this color. MK3 export and custom stage generators do not support backdrop edits yet.");
    if (color == 444) ImGui::TextWrapped("This color value is reserved by the game; choose another shade.");
    ImGui::BeginDisabled(color == 444);
    if (ImGui::Button("Apply stage color")) {
        if (t.document.set_backdrop(color)) {
            t.backdrop_preview = false; ImGui::CloseCurrentPopup(); toast("Stage background updated. Save keeps it with the map.");
        }
    }
    background_apply_point = item_center(); ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Cancel stage change")) { t.backdrop_preview = false; ImGui::CloseCurrentPopup(); }
    background_cancel_point = item_center();
    ImGui::EndPopup();
}
