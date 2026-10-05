// Asset workflows owned by App. Dialog drafts stay local until Apply/Import.
void open_palette_editor(Tab &t, int palette = -1) {
    cancel_gesture();
    error.clear();
    if (t.asset < 0 || t.asset >= (int)t.document.state().assets->data.images.size()) return;
    t.edit_palette = palette >= 0 ? palette : asset_palette(t, t.asset);
    if (t.edit_palette < 0 || t.edit_palette >= (int)t.document.state().assets->data.palettes.size()) return;
    t.edit_image = t.document.state().assets->data.images[t.asset].idx;
    t.edit_original = t.document.state().assets;
    t.palette_draft = t.edit_original->data.palettes[t.edit_palette];
    t.edit_color = std::min(1, t.palette_draft.count - 1);
    t.palette_requested = true;
}
void open_block_editor(Tab &t) {
    cancel_gesture();
    error.clear();
    if (t.asset < 0 || t.asset >= (int)t.document.state().assets->data.images.size()) return;
    int pi = asset_palette(t, t.asset);
    if (pi < 0 || pi >= (int)t.document.state().assets->data.palettes.size()) return;
    auto bank = std::make_shared<AssetBank>();
    bank->data.images.push_back(t.document.state().assets->data.images[t.asset]);
    bank->data.palettes.push_back(t.document.state().assets->data.palettes[pi]);
    t.edit_image = bank->data.images[0].idx;
    t.edit_original = t.document.state().assets;
    t.block_draft = {}; t.block_draft.assets = bank;
    t.edit_color = std::min(1, bank->data.palettes[0].count - 1);
    t.block_zoom = std::clamp(480 / std::max(bank->data.images[0].w, bank->data.images[0].h), 1, 16);
    t.block_undo.clear(); t.block_redo.clear(); t.block_painting = false;
    t.block_requested = true;
}
void export_asset_png(Tab &t) {
    const auto bank = t.document.state().assets;
    if (t.asset < 0 || t.asset >= (int)bank->data.images.size()) return;
    int pi = asset_palette(t, t.asset);
    if (pi < 0 || pi >= (int)bank->data.palettes.size()) return;
    char path[1024] = {};
    std::snprintf(path, sizeof path, "artwork_%d.png", bank->data.images[t.asset].idx);
    if (!file_dialog_save_ext("Export artwork as PNG", "PNG image\0*.png\0", "png", path, sizeof path)) return;
    const auto &im = bank->data.images[t.asset]; const auto &pal = bank->data.palettes[pi];
    std::vector<uint8_t> rgba((size_t)im.w * im.h * 4);
    if (!bdd_core_indexed_to_rgba(im.pix.data(), im.w, im.h, pal.argb, pal.count, rgba.data(), rgba.size()) ||
        !stbi_write_png(path, im.w, im.h, 4, rgba.data(), im.w * 4)) error = "Could not export artwork.";
    else toast("Exported artwork as PNG");
}
void palette_swatches(const BddCorePalette &pal, int &selected) {
    int cols = std::max(1, std::min(16, (int)(ImGui::GetContentRegionAvail().x / 28)));
    for (int i = 0; i < pal.count; ++i) {
        ImGui::PushID(i);
        if (i % cols) ImGui::SameLine(0, 4);
        uint32_t color = pal.argb[i];
        ImVec4 c(((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                 (color & 255) / 255.f, i ? 1.f : 0.f);
        if (ImGui::ColorButton("##color", c, ImGuiColorEditFlags_NoTooltip, {24, 24})) selected = i;
        if (i == selected) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), selection_color, 0, 0, 2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Index %d%s\nRGB555: %04X", i, i ? "" : " (transparent)", pal.rgb555[i]);
        ImGui::PopID();
    }
}
void asset_modal_size(float w, float h) {
    const auto size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(w, size.x - 24), std::min(h, size.y - 24)}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({size.x * .5f, size.y * .5f}, ImGuiCond_Always, {.5f, .5f});
}
void asset_dialogs(Tab &t) {
    batch_import_dialog(t);
    if (t.import_requested) { ImGui::OpenPopup("Import IMG artwork"); t.import_requested = false; }
    if (t.palette_requested) { ImGui::OpenPopup("Palette editor"); t.palette_requested = false; }
    if (t.block_requested) { ImGui::OpenPopup("Block editor"); t.block_requested = false; }
    asset_modal_size(780, 560);
    if (ImGui::BeginPopupModal("Import IMG artwork", nullptr, ImGuiWindowFlags_NoResize)) {
        if (t.import_library) {
            ImGui::TextWrapped("%s", t.import_library->path.c_str());
            ImGui::TextWrapped("Import selected images and their palettes into Assets. Place them on the stage afterward. Animation sequences are not imported.");
            t.import_filter.Draw("Filter labels", 260);
            if (ImGui::Button("Select visible")) {
                size_t count = std::count(t.import_selected.begin(), t.import_selected.end(), uint8_t(1));
                for (size_t i = 0; i < t.import_selected.size() && count < 128; ++i)
                    if (!t.import_selected[i] && t.import_library->images[i].problem.empty() &&
                        t.import_filter.PassFilter(t.import_library->images[i].label.c_str())) {
                        t.import_selected[i] = 1; ++count;
                    }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear selection")) std::fill(t.import_selected.begin(), t.import_selected.end(), 0);
            size_t selected = std::count(t.import_selected.begin(), t.import_selected.end(), uint8_t(1));
            ImGui::SameLine(); ImGui::TextDisabled("%zu selected (up to 128 per import)", selected);
            if (ImGui::BeginTable("img-import", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                                   {0, std::max(100.f, ImGui::GetContentRegionAvail().y - 50)})) {
                ImGui::TableSetupColumn("Image"); ImGui::TableSetupColumn("Size"); ImGui::TableSetupColumn("Status");
                ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
                for (size_t i = 0; i < t.import_library->images.size(); ++i) {
                    const auto &im = t.import_library->images[i];
                    if (!t.import_filter.PassFilter(im.label.c_str())) continue;
                    ImGui::PushID((int)i); ImGui::TableNextRow(); ImGui::TableNextColumn();
                    bool checked = t.import_selected[i] != 0;
                    ImGui::BeginDisabled(!im.problem.empty() || (!checked && selected >= 128));
                    if (ImGui::Checkbox(im.label.c_str(), &checked)) {
                        t.import_selected[i] = checked; selected += checked ? 1 : -1;
                    }
                    ImGui::EndDisabled(); ImGui::TableNextColumn(); ImGui::Text("%d x %d", im.width, im.height);
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", im.problem.empty() ? "Ready" : im.problem.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::BeginDisabled(selected == 0);
            if (ImGui::Button("Import selected")) {
                std::vector<std::string> labels; std::vector<int> ids;
                for (size_t i = 0; i < t.import_selected.size(); ++i)
                    if (t.import_selected[i]) labels.push_back(t.import_library->images[i].label);
                if (t.document.import_img(t.import_library->path, labels, error, ids)) {
                    t.asset = (int)t.document.state().assets->data.images.size() - (int)ids.size();
                    search[0] = 0; page = 1; tray = true;
                    toast("Imported " + std::to_string(ids.size()) + " IMG images. Drag artwork into a layer.");
                    ImGui::CloseCurrentPopup();
                }
            }
            import_apply_point = item_center();
            ImGui::EndDisabled(); ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        ImGui::EndPopup();
    }
    asset_modal_size(590, 660);
    if (ImGui::BeginPopupModal("Palette editor", nullptr, ImGuiWindowFlags_NoResize)) {
        const auto bank = t.document.state().assets;
        ImGui::TextWrapped("Editing this palette changes every artwork placement that uses it.");
        ImGui::SetNextItemWidth(350);
        std::string title = std::to_string(t.edit_palette) + ": " + t.palette_draft.name;
        if (ImGui::BeginCombo("Palette", title.c_str())) {
            for (int i = 0; i < (int)bank->data.palettes.size(); ++i) {
                auto name = std::to_string(i) + ": " + bank->data.palettes[i].name;
                if (ImGui::Selectable(name.c_str(), i == t.edit_palette)) {
                    t.edit_palette = i; t.palette_draft = bank->data.palettes[i]; t.edit_original = bank;
                    t.edit_color = std::min(1, t.palette_draft.count - 1);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Switching palettes discards the unapplied color draft.");
        int uses = 0;
        for (const auto &p : t.document.state().objects) if (p.object.fl == t.edit_palette) ++uses;
        ImGui::Text("%d placements use this palette", uses);
        if (ImGui::Button("Copy palette for this artwork")) {
            int next = 0;
            if (t.document.copy_palette_for_image(t.edit_image, t.edit_palette, error, next)) {
                t.edit_palette = next; t.edit_original = t.document.state().assets;
                t.palette_draft = t.edit_original->data.palettes[next];
                toast("Palette copied and assigned. Undo restores the original assignments.");
            }
        }
        ImGui::TextDisabled("Copies the saved palette; keeps other palette variants on placements.");
        ImGui::InputText("Name", t.palette_draft.name, sizeof t.palette_draft.name);
        ImGui::SliderInt("Color count", &t.palette_draft.count, 1, 256);
        t.edit_color = std::clamp(t.edit_color, 0, t.palette_draft.count - 1);
        ImGui::BeginChild("swatches", {0, std::max(90.f, ImGui::GetContentRegionAvail().y - 155)}, true);
        palette_swatches(t.palette_draft, t.edit_color);
        ImGui::EndChild();
        ImGui::Text("Color %d%s", t.edit_color, t.edit_color ? "" : " (transparent)");
        auto word = t.palette_draft.rgb555[t.edit_color];
        uint32_t argb = bdd_core_rgb555_to_argb(word);
        float rgb[] = {((argb >> 16) & 255) / 255.f, ((argb >> 8) & 255) / 255.f, (argb & 255) / 255.f};
        ImGui::BeginDisabled(t.edit_color == 0);
        if (ImGui::ColorEdit3("RGB color", rgb)) {
            uint32_t color = 0xff000000u | ((uint32_t)std::lround(rgb[0] * 255) << 16) |
                ((uint32_t)std::lround(rgb[1] * 255) << 8) | (uint32_t)std::lround(rgb[2] * 255);
            auto value = uint16_t((word & 0x8000) | bdd_core_argb_to_rgb555(color));
            t.palette_draft.rgb555[t.edit_color] = value;
            t.palette_draft.argb[t.edit_color] = bdd_core_rgb555_to_argb(value);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Colors use the game's 5-bit RGB precision. Index 0 stays transparent.");
        if (ImGui::Button("Apply palette")) {
            if (t.document.state().assets != t.edit_original) error = "Artwork changed. Reopen the palette editor.";
            else if (t.document.set_palette(t.edit_palette, t.palette_draft, error)) ImGui::CloseCurrentPopup();
        }
        palette_apply_point = item_center();
        ImGui::SameLine(); if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        ImGui::EndPopup();
    }
    block_editor_dialog(t);
}
void block_editor_dialog(Tab &t) {
    asset_modal_size(960, 690);
    if (!ImGui::BeginPopupModal("Block editor", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (!t.block_draft.assets) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    if (t.block_dirty) { block_textures.clear(); t.block_dirty = false; }
    // The draft has no history aliases; only the original document is immutable.
    auto bank = std::const_pointer_cast<AssetBank>(t.block_draft.assets);
    auto &im = bank->data.images[0]; const auto &pal = bank->data.palettes[0];
    ImGui::Text("Image %d  |  %d x %d", t.edit_image, im.w, im.h);
    ImGui::TextWrapped("Paint to edit; right-click picks a color. Apply changes every placement of this image. Cancel discards the draft.");
    auto checkpoint = [&] {
        size_t limit = std::max(size_t(1), std::min(size_t(32), size_t(32 * 1024 * 1024) / std::max(size_t(1), im.pix.size())));
        if (t.block_undo.size() >= limit) t.block_undo.erase(t.block_undo.begin());
        t.block_undo.push_back(im.pix); t.block_redo.clear();
    };
    ImGui::BeginDisabled(t.block_undo.empty());
    if (ImGui::Button("Undo stroke")) {
        t.block_redo.push_back(im.pix); im.pix = std::move(t.block_undo.back()); t.block_undo.pop_back(); t.block_dirty = true;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(t.block_redo.empty());
    if (ImGui::Button("Redo stroke")) {
        t.block_undo.push_back(im.pix); im.pix = std::move(t.block_redo.back()); t.block_redo.pop_back(); t.block_dirty = true;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Flip X")) { checkpoint(); for (int y = 0; y < im.h; ++y) std::reverse(im.pix.begin() + y * im.w, im.pix.begin() + (y + 1) * im.w); t.block_dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Flip Y")) { checkpoint(); for (int y = 0; y < im.h / 2; ++y) for (int x = 0; x < im.w; ++x) std::swap(im.pix[y * im.w + x], im.pix[(im.h - 1 - y) * im.w + x]); t.block_dirty = true; }
    ImGui::SameLine(); if (ImGui::Button("Erase")) t.edit_color = 0;
    ImGui::SameLine(); ImGui::Checkbox("Pixel grid", &t.block_grid);
    ImGui::SetNextItemWidth(200); ImGui::SliderInt("Zoom", &t.block_zoom, 1, 24);
    ImGui::BeginChild("block-colors", {250, -50}, true);
    ImGui::Text("Brush index %d", t.edit_color); palette_swatches(pal, t.edit_color);
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("block-canvas", {0, -50}, true, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float z = (float)t.block_zoom;
    block_paint_point = {origin.x + z * .5, origin.y + z * .5};
    ImGui::InvisibleButton("pixels", {im.w * z, im.h * z}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + im.w * z, origin.y + im.h * z}, IM_COL32(45, 48, 55, 255));
    if (auto *tex = block_textures.get(t.block_draft, 0, 0))
        draw->AddImage((ImTextureID)(intptr_t)tex, origin, {origin.x + im.w * z, origin.y + im.h * z});
    if (t.block_grid && z >= 5) {
        for (int x = 0; x <= im.w; ++x) draw->AddLine({origin.x + x * z, origin.y}, {origin.x + x * z, origin.y + im.h * z}, IM_COL32(100, 110, 120, 75));
        for (int y = 0; y <= im.h; ++y) draw->AddLine({origin.x, origin.y + y * z}, {origin.x + im.w * z, origin.y + y * z}, IM_COL32(100, 110, 120, 75));
    }
    auto mouse = ImGui::GetIO().MousePos;
    int x = (int)std::floor((mouse.x - origin.x) / z), y = (int)std::floor((mouse.y - origin.y) / z);
    bool inside = x >= 0 && x < im.w && y >= 0 && y < im.h && ImGui::IsItemHovered();
    if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) t.edit_color = im.pix[y * im.w + x];
    if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        checkpoint(); t.block_painting = true; t.block_last_x = x; t.block_last_y = y;
    }
    if (inside && t.block_painting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int a = t.block_last_x, b = t.block_last_y;
        int dx = std::abs(x - a), dy = -std::abs(y - b), sx = a < x ? 1 : -1, sy = b < y ? 1 : -1, e = dx + dy;
        for (;;) {
            im.pix[b * im.w + a] = (uint8_t)t.edit_color;
            if (a == x && b == y) break;
            int twice = 2 * e;
            if (twice >= dy) { e += dy; a += sx; }
            if (twice <= dx) { e += dx; b += sy; }
        }
        t.block_last_x = x; t.block_last_y = y; t.block_dirty = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) t.block_painting = false;
    ImGui::EndChild();
    if (ImGui::Button("Apply block")) {
        if (t.document.state().assets != t.edit_original) error = "Artwork changed. Reopen the block editor.";
        else if (t.document.set_image_pixels(t.edit_image, im.pix, error)) ImGui::CloseCurrentPopup();
    }
    block_apply_point = item_center();
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    if (!error.empty()) { ImGui::SameLine(); ImGui::TextWrapped("%s", error.c_str()); }
    ImGui::EndPopup();
}
