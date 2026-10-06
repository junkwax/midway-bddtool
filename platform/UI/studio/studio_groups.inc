// Reusable artwork groups: save a selection, then drag it back onto the stage.
void open_save_group(Tab &t) {
    error.clear();
    std::snprintf(t.group_name, sizeof t.group_name, "Group %zu", t.document.state().groups.size() + 1);
    t.group_requested = true;
}
void save_group_dialog(Tab &t) {
    if (t.group_requested) { ImGui::OpenPopup("Save artwork group"); t.group_requested = false; }
    asset_modal_size(440, 210);
    if (!ImGui::BeginPopupModal("Save artwork group", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::Text("%zu selected placement%s", t.selected.size(), t.selected.size() == 1 ? "" : "s");
    ImGui::TextWrapped("Layers, palettes, flips and draw order are kept. Hidden placements are left out.");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##group-name", t.group_name, sizeof t.group_name,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Separator();
    if (ImGui::Button("Save group", {120, 0}) || enter) {
        if (t.document.add_group(t.selected, t.group_name, error)) {
            toast("Saved group. Drag it from Assets to place copies."); ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    ImGui::EndPopup();
}
// Places a group centered under the drop point, using the current group options.
bool drop_group(Tab &t, size_t index, Point world, Point cam) {
    const auto &s = t.document.state();
    if (index >= s.groups.size()) return false;
    const auto &g = s.groups[index];
    auto options = t.group_options;
    if (t.group_on_layer && t.plane < 0) { toast("Choose or add a layer first."); return false; }
    options.target_plane = t.group_on_layer ? t.plane : -1;
    int plane = options.target_plane >= 0 ? options.target_plane : g.items.front().plane;
    Point pos = world;
    if (t.camera_preview && plane >= 0 && plane < (int)s.planes.size()) {
        pos.x += cam.x * s.planes[plane].scroll;
        pos.y += cam.y;
    }
    options.origin = {std::round(pos.x - g.width / 2.0), std::round(pos.y - g.height / 2.0)};
    std::string why;
    auto ids = t.document.place_group(index, options, why);
    if (ids.empty()) { toast(why); return false; }
    t.selected = ids;
    return true;
}
void groups_strip(Tab &t, bool full) {
    const auto &groups = t.document.state().groups;
    if (groups.empty()) return;
    // The compact strip under the stage starts collapsed so artwork stays visible.
    std::string title = "Groups (" + std::to_string(groups.size()) + (full ? ")###artwork-groups" : ")###artwork-groups-compact");
    if (!ImGui::CollapsingHeader(title.c_str(), full ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    auto &o = t.group_options;
    ImGui::Checkbox("Mirror X", &o.mirror_x); ImGui::SameLine();
    ImGui::Checkbox("Mirror Y", &o.mirror_y); ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("Copies", &o.copies)) o.copies = std::clamp(o.copies, 1, kMaxGroupCopies);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("Gap", &o.gap)) o.gap = std::clamp(o.gap, -4096, 4096);
    ImGui::BeginDisabled(o.copies < 2);
    ImGui::SameLine(); ImGui::Checkbox("Vertical", &o.vertical);
    ImGui::SameLine(); ImGui::Checkbox("Alternate mirror", &o.alternate);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Mirror every other copy, such as the left and right sides of a pillar run.");
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Checkbox("Use selected layer", &t.group_on_layer);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place every item on the selected layer instead of each item's saved layer.");
    const float chip_w = 132, chip_h = 84;
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    int remove = -1;
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto &g = groups[i];
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Selectable("##group", false, 0, {chip_w, chip_h});
        if (ImGui::BeginDragDropSource()) {
            AssetPayload payload{t.id, (int)i};
            ImGui::SetDragDropPayload("STUDIO_GROUP", &payload, sizeof payload);
            ImGui::Text("%s (%zu placements)", g.name.c_str(), g.items.size() * (size_t)o.copies);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("group-tools")) {
            if (ImGui::MenuItem("Delete group")) remove = (int)i;
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%d x %d pixels | %zu placements\nDrag onto the stage to place.",
                              g.name.c_str(), g.width, g.height, g.items.size());
        auto *draw = ImGui::GetWindowDrawList();
        float scale = std::min({2.0f, (chip_w - 12) / std::max(1, g.width), (chip_h - 28) / std::max(1, g.height)});
        ImVec2 box(p.x + (chip_w - g.width * scale) / 2, p.y + 4);
        draw->PushClipRect(p, {p.x + chip_w, p.y + chip_h}, true);
        for (const auto &item : g.items) {
            size_t slot = 0;
            const auto *im = t.document.image(item.image, &slot);
            if (!im) continue;
            ImVec2 a(box.x + item.x * scale, box.y + item.y * scale);
            image(draw, t, (int)slot, item.palette, a, {a.x + im->w * scale, a.y + im->h * scale},
                  item.flip_x, item.flip_y);
        }
        draw->AddText({p.x + 6, p.y + chip_h - 20}, IM_COL32(197, 208, 218, 255), g.name.c_str());
        draw->PopClipRect();
        ImGui::PopID();
        if (i + 1 < groups.size() && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + chip_w < right)
            ImGui::SameLine();
    }
    if (remove >= 0 && t.document.remove_group((size_t)remove)) toast("Deleted group. Undo restores it.");
    ImGui::Spacing();
}
