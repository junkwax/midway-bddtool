void request_composite(Tab &t) {
    cancel_gesture();
    error.clear();
    t.composite_scope = !t.selected.empty() ? 0 : (t.plane >= 0 ? 1 : 2);
    t.composite_requested = true;
}

void composite_dialog(Tab &t) {
    if (t.composite_requested) { ImGui::OpenPopup("Export composite PNG"); t.composite_requested = false; }
    asset_modal_size(560, 390);
    if (!ImGui::BeginPopupModal("Export composite PNG", nullptr, ImGuiWindowFlags_NoResize)) return;
    const auto &state = t.document.state();
    ImGui::TextUnformatted("Export the resolved canvas as one tightly cropped image.");
    ImGui::TextWrapped("The PNG uses the current palettes, flips, layer transforms and draw order. Hidden artwork is omitted.");
    ImGui::Spacing();
    int visible_selected = 0;
    for (const auto &item : t.document.scene()) if (has(t.selected, item.id)) ++visible_selected;
    bool layer_valid = t.plane >= 0 && t.plane < (int)state.planes.size();
    ImGui::BeginDisabled(visible_selected == 0);
    ImGui::RadioButton(("Selection (" + std::to_string(visible_selected) + " visible)").c_str(), &t.composite_scope, 0);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!layer_valid);
    std::string layer_label = layer_valid ? "Current layer: " + state.planes[t.plane].name : "Current layer unavailable";
    ImGui::RadioButton(layer_label.c_str(), &t.composite_scope, 1);
    ImGui::EndDisabled();
    ImGui::RadioButton("Full visible stage", &t.composite_scope, 2);
    ImGui::Spacing();
    ImGui::Checkbox("Write companion placement JSON", &t.composite_metadata);
    ImGui::BeginDisabled(t.composite_scope != 2 || state.backdrop < 0);
    ImGui::Checkbox("Include saved stage background color", &t.composite_backdrop);
    ImGui::EndDisabled();
    if (t.composite_scope != 2) t.composite_backdrop = false;
    ImGui::TextDisabled("Transparent pixels remain transparent unless the stage background is included.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    bool valid = (t.composite_scope != 0 || visible_selected > 0) && (t.composite_scope != 1 || layer_valid);
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Export...", {120, 0})) {
        CompositeRequest request;
        request.scope = t.composite_scope == 0 ? CompositeScope::Selection :
                        t.composite_scope == 1 ? CompositeScope::Layer : CompositeScope::Stage;
        request.selection = t.selected; request.plane = t.plane;
        request.include_backdrop = t.composite_backdrop;
        auto result = render_composite(state, request);
        if (!result.ready) error = result.error;
        else {
            std::string stem = state.name.empty() ? "stage" : state.name;
            for (char &c : stem) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
            stem += t.composite_scope == 0 ? "_selection.png" : t.composite_scope == 1 ? "_layer.png" : "_stage.png";
            char path[1024] = {}; std::snprintf(path, sizeof path, "%s", stem.c_str());
            if (file_dialog_save_ext("Export composite PNG", "PNG image\0*.png\0", "png", path, sizeof path)) {
                bool png_ok = stbi_write_png(path, result.width, result.height, 4, result.rgba.data(), result.width * 4) != 0;
                bool json_ok = true;
                if (png_ok && t.composite_metadata) {
                    fs::path metadata = fs::u8path(path); metadata.replace_extension(".json");
                    std::ofstream output(metadata, std::ios::binary | std::ios::trunc);
                    output << result.metadata; output.close(); json_ok = bool(output);
                }
                if (!png_ok) error = "Could not write the composite PNG.";
                else if (!json_ok) error = "PNG exported, but the placement JSON could not be written.";
                else { toast("Exported resolved composite"); ImGui::CloseCurrentPopup(); }
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void request_sprite_sheet(Tab &t) {
    t.sheet_image_ids.clear();
    for (const auto &im : t.document.state().assets->data.images)
        if (t.selected_assets.count(im.idx))
            t.sheet_image_ids.push_back(im.idx);
    t.sheet_scope = t.sheet_image_ids.empty() ? 2 : 0;
    t.sheet_requested = true;
}

void sprite_sheet_dialog(Tab &t) {
    if (t.sheet_requested) { cancel_gesture(); error.clear(); ImGui::OpenPopup("Export artwork sheet"); t.sheet_requested = false; }
    asset_modal_size(550, 380);
    if (!ImGui::BeginPopupModal("Export artwork sheet", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::BeginDisabled(t.sheet_image_ids.empty());
    const auto selection = "Selected assets (" + std::to_string(t.sheet_image_ids.size()) + ")";
    ImGui::RadioButton(selection.c_str(), &t.sheet_scope, 0);
    ImGui::EndDisabled();
    ImGui::RadioButton("Artwork used by visible placements", &t.sheet_scope, 1);
    ImGui::RadioButton("Entire asset library", &t.sheet_scope, 2);
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("Columns", &t.sheet_columns, 1, 32);
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("Cell padding", &t.sheet_padding, 0, 16, "%d px");
    bool &metadata_enabled = t.sheet_scope == 0 ? t.sheet_selection_metadata : t.sheet_metadata;
    ImGui::Checkbox("Write companion cell JSON", &metadata_enabled);
    ImGui::TextDisabled("PNG cells have a transparent background.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    bool export_clicked = ImGui::Button("Export...", {120, 0});
    if (!smoke_dir.empty()) smoke_sheet_export = item_center();
    if (export_clicked) {
        SpriteSheetRequest request; request.placed_only = t.sheet_scope == 1;
        if (t.sheet_scope == 0) request.image_ids = t.sheet_image_ids;
        request.columns = t.sheet_columns; request.padding = t.sheet_padding;
        auto result = render_sprite_sheet(t.document.state(), request);
        if (!result.ready) error = result.error;
        else {
            std::string stem = document_title(t.document) + "_artwork_sheet.png";
            for (char &c : stem) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') c = '_';
            char path[1024] = {}; std::snprintf(path, sizeof path, "%s", stem.c_str());
            bool destination_chosen = false;
            if (!smoke_dir.empty()) {
                const auto output = (fs::u8path(smoke_dir) / stem).u8string();
                std::snprintf(path, sizeof path, "%s", output.c_str());
                destination_chosen = true;
            } else
                destination_chosen = file_dialog_save_ext("Export artwork sheet", "PNG image\0*.png\0", "png", path, sizeof path);
            if (destination_chosen) {
                bool png_ok = stbi_write_png(path, result.width, result.height, 4, result.rgba.data(), result.width * 4) != 0;
                bool json_ok = true;
                if (png_ok && metadata_enabled) {
                    fs::path metadata = fs::u8path(path); metadata.replace_extension(".json");
                    std::ofstream output(metadata, std::ios::binary | std::ios::trunc);
                    output << result.metadata; output.close(); json_ok = bool(output);
                }
                if (!png_ok) error = "Could not write the artwork sheet PNG.";
                else if (!json_ok) error = "PNG exported, but the cell JSON could not be written.";
                else { toast("Exported artwork sheet"); ImGui::CloseCurrentPopup(); }
            }
        }
    }
    ImGui::SameLine(); if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    ImGui::EndPopup();
}
