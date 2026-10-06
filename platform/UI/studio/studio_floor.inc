std::string floor_source_key(const Tab &t) const {
    return t.layout_source[0] ? std::string("MK3:") + t.layout_source : std::string("MK2:") + t.game_root;
}
const FloorPreview &active_floor(const Tab &t) const { return t.floor_use_library ? t.floor_alternative : t.floor; }
bool floor_context_current(const Tab &t) const {
    return (t.floor_use_library ? t.floor_library_root : t.floor_root) == floor_source_key(t);
}
bool floor_layers_bound(const Tab &t) const {
    const auto &floor = active_floor(t);
    if (!floor.mk3) return true;
    for (const auto &module : floor.layout.background_modules) {
        bool found = false;
        for (const auto &plane : t.document.state().planes) {
            std::string name = plane.source.name;
            for (char &c : name) if (c >= 'a' && c <= 'z') c -= 32;
            if (name == module && plane.bound) found = true;
        }
        if (!found) return false;
    }
    return true;
}
bool floor_visible(const Tab &t) const {
    return !t.source && t.solo < 0 && t.show_floor && active_floor(t).ready() && floor_context_current(t) && floor_layers_bound(t);
}
void reload_floor(Tab &t) {
    t.floor = t.layout_source[0] ? load_mk3_floor_preview(t.document, t.layout_source)
                               : load_floor_preview(t.document, t.game_root);
    t.floor_root = floor_source_key(t);
    t.floor_use_library = false;
}
void open_floor(Tab &t) {
    cancel_gesture(); t.floor_requested = true; t.floor_error.clear(); t.floor_draft = {};
    t.floor_x = 0; t.floor_y = t.document.state().ground;
    t.floor_width = std::clamp(t.document.state().world_w, 1, 16384);
}
void floor_from_asset(Tab &t) {
    const auto &bank = *t.document.state().assets;
    if (t.asset < 0 || t.asset >= (int)bank.data.images.size()) return;
    int pi = asset_palette(t, t.asset);
    if (pi < 0 || pi >= (int)bank.data.palettes.size()) return;
    auto draft = std::make_shared<AssetBank>();
    draft->data.images.push_back(bank.data.images[t.asset]); draft->data.images[0].idx = 0;
    draft->data.palettes.push_back(bank.data.palettes[pi]); draft->default_palettes.push_back(0);
    draft->metadata.push_back(t.asset < (int)bank.metadata.size() ? bank.metadata[t.asset] : BddImageMetadata{});
    t.floor_draft.assets = draft; t.floor_error.clear();
}
void floor_from_file(Tab &t, const std::string &path) {
    t.floor_draft = {}; t.floor_error.clear();
    int w = 0, h = 0, n = 0;
    if (!stbi_info(path.c_str(), &w, &h, &n) || w < 1 || h < 1 || w > 4096 || h > 4096) {
        t.floor_error = "Choose a raster image up to 4096 x 4096."; return;
    }
    auto pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!pixels) { t.floor_error = "Could not decode floor artwork."; return; }
    auto draft = std::make_shared<AssetBank>();
    bool ok = make_raster_asset(fs::u8path(path).stem().u8string(), w, h, pixels, *draft, t.floor_error);
    stbi_image_free(pixels);
    if (ok) t.floor_draft.assets = draft;
}
void refresh_floor_libraries(Tab &t) {
    std::vector<std::string> folders;
    if (!t.document.path().empty()) folders.push_back(fs::u8path(t.document.path()).parent_path().u8string());
    if (*t.game_root) folders.push_back((fs::u8path(t.game_root) / "data").u8string());
    if (!t.floor_library.path.empty()) folders.push_back(fs::u8path(t.floor_library.path).parent_path().u8string());
    t.floor_libraries = discover_floor_libraries(folders, t.floor_library.path);
}
void load_floor_library(Tab &t, const std::string &path) {
    auto library = inspect_floor_library(path);
    if (!library.error.empty()) { t.floor_library_error = library.error; return; }
    t.floor_library = std::move(library); t.floor_filter.Clear();
    t.floor_choice = -1; t.floor_use_library = false; t.floor_library_error.clear();
    refresh_floor_libraries(t);
}
void find_floor_library(Tab &t) {
    refresh_floor_libraries(t);
    if (!t.floor_libraries.paths.empty()) {
        auto path = t.floor_libraries.paths.front(); load_floor_library(t, path);
    }
}
FloorComparison floor_pixel_comparison(Tab &t) {
    if (t.floor_root != floor_source_key(t) || t.floor_library_root != floor_source_key(t)) return {};
    auto art = t.floor_alternative.layout.artwork.assets, runtime = t.floor.layout.artwork.assets;
    if (art != t.floor_compared_art || runtime != t.floor_compared_runtime) {
        t.floor_comparison = compare_floor_pixels(t.floor_alternative, t.floor);
        t.floor_compared_art = art; t.floor_compared_runtime = runtime;
    }
    return t.floor_comparison;
}
void floor_comparison_status(Tab &t) {
    if (!t.floor_use_library) return;
    const auto comparison = floor_pixel_comparison(t);
    switch (comparison.match) {
        case FloorMatch::Exact: ImGui::TextColored(accent, "Exact pixel match to the loaded game floor"); break;
        case FloorMatch::DifferentSize: ImGui::TextWrapped("Different dimensions from the loaded game floor"); break;
        case FloorMatch::DifferentPixels: ImGui::TextWrapped("%zu of %zu pixels differ from the loaded game floor", comparison.different_pixels, comparison.total_pixels); break;
        default: ImGui::TextDisabled("No current game floor available for comparison"); break;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compares decoded texture colors and transparency. A difference does not prove the artwork is unused; runtime effects are excluded.");
}
bool select_floor_variant(Tab &t, int index) {
    if (index < 0) { t.floor_use_library = false; t.show_floor = true; t.floor_library_error.clear(); return true; }
    if (index >= (int)t.floor_library.entries.size()) return false;
    if (t.floor_root != floor_source_key(t)) reload_floor(t);
    auto preview = load_floor_library_entry(t.floor_library, size_t(index), t.floor);
    if (!preview.ready()) { t.floor_library_error = preview.notice; return false; }
    t.floor_alternative = std::move(preview); t.floor_choice = index;
    t.floor_use_library = true; t.floor_library_root = floor_source_key(t);
    t.floor_library_error.clear(); t.show_floor = true; t.source = false; t.solo = -1;
    if (!t.camera_preview) {
        t.camera_preview = true; t.camera = {double(t.document.state().start_x), double(t.document.state().start_y)}; t.fit = true;
    }
    return true;
}
void step_floor_variant(Tab &t, int direction) {
    int count = (int)t.floor_library.entries.size();
    int current = t.floor_use_library ? t.floor_choice : (direction > 0 ? -1 : 0);
    for (int n = 1; n <= count; ++n) {
        int index = (current + direction * n + count * 2) % count;
        const auto &entry = t.floor_library.entries[index];
        if (entry.problem.empty() && t.floor_filter.PassFilter(entry.label.c_str())) { select_floor_variant(t, index); return; }
    }
}
void floor_variant_controls(Tab &t) {
    ImGui::SetNextItemWidth(230);
    const char *label = t.floor_use_library ? t.floor_alternative.label.c_str() : "Game floor";
    if (ImGui::BeginCombo("##floor-variant", label)) {
        if (ImGui::Selectable("Game floor", !t.floor_use_library)) select_floor_variant(t, -1);
        for (size_t i = 0; i < t.floor_library.entries.size(); ++i) {
            const auto &entry = t.floor_library.entries[i];
            if (!t.floor_filter.PassFilter(entry.label.c_str())) continue;
            ImGui::PushID((int)i); ImGui::BeginDisabled(!entry.problem.empty());
            if (ImGui::Selectable(entry.label.c_str(), t.floor_use_library && t.floor_choice == (int)i)) select_floor_variant(t, (int)i);
            ImGui::EndDisabled(); ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); if (ImGui::SmallButton("<##floor")) step_floor_variant(t, -1);
    ImGui::SameLine(); if (ImGui::SmallButton(">##floor")) step_floor_variant(t, 1);
    floor_library_next_point = item_center();
    ImGui::SameLine(); if (ImGui::SmallButton("Browse floors")) t.floor_library_open = true;
    if (!t.floor_library_error.empty()) ImGui::TextWrapped("%s", t.floor_library_error.c_str());
}
void floor_library_window(Tab &t) {
    if (!t.floor_library_open) return;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(470.f, display.x - 24), std::min(540.f, display.y - 32)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({std::max(12.f, display.x - 482), 60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({360, 300}, {std::max(360.f, display.x - 24), std::max(300.f, display.y - 24)});
    if (!ImGui::Begin("Floor library", &t.floor_library_open)) { ImGui::End(); return; }
    auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    ImGui::SetWindowPos({std::clamp(pos.x, 0.f, std::max(0.f, display.x - size.x)),
                        std::clamp(pos.y, 0.f, std::max(0.f, display.y - size.y))});
    std::string chosen_library;
    auto library_name = fs::u8path(t.floor_library.path).filename().u8string();
    ImGui::SetNextItemWidth(std::max(140.f, ImGui::GetContentRegionAvail().x - 100));
    bool selecting = ImGui::BeginCombo("##floor-library-source", library_name.empty() ? "Nearby floor libraries" : library_name.c_str());
    floor_library_selector_point = item_center();
    if (selecting) {
        for (size_t i = 0; i < t.floor_libraries.paths.size(); ++i) {
            const auto &path = t.floor_libraries.paths[i];
            std::error_code ec;
            bool selected = !t.floor_library.path.empty() && fs::equivalent(fs::u8path(path), fs::u8path(t.floor_library.path), ec);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(fs::u8path(path).filename().u8string().c_str(), selected)) chosen_library = path;
            if (i == 1) floor_library_second_point = item_center();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            ImGui::PopID();
        }
        if (t.floor_libraries.paths.empty()) ImGui::TextDisabled("No nearby libraries found. Use Open IMG.");
        ImGui::EndCombo();
    }
    if (!chosen_library.empty()) load_floor_library(t, chosen_library);
    ImGui::SameLine(); if (ImGui::Button("Refresh list")) refresh_floor_libraries(t);
    if (ImGui::Button("Open IMG...")) {
        char path[1024] = {};
        std::snprintf(path, sizeof path, "%s", t.floor_library.path.c_str());
        if (file_dialog_open("Choose floor IMG library", "Midway image libraries\0*.IMG;*.img\0All files\0*.*\0", path, sizeof path)) load_floor_library(t, path);
    }
    ImGui::SameLine(); ImGui::TextDisabled("%zu choices", t.floor_library.entries.size());
    if (!t.floor_libraries.notice.empty()) ImGui::TextWrapped("%s", t.floor_libraries.notice.c_str());
    ImGui::TextWrapped("%s", t.floor_library.path.empty() ? "Open an IMG library to compare floors." : t.floor_library.path.c_str());
    ImGui::TextWrapped("Includes unused artwork. IMG does not record game usage. Numbered strips are joined in order, aligned at the bottom.");
    t.floor_filter.Draw("Search", 270);
    if (ImGui::BeginTable("floor-choices", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                          {0, std::max(70.f, ImGui::GetContentRegionAvail().y - 198)})) {
        ImGui::TableSetupColumn("Floor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 85);
        ImGui::TableSetupColumn("Strips", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        for (size_t i = 0; i < t.floor_library.entries.size(); ++i) {
            const auto &entry = t.floor_library.entries[i];
            if (!t.floor_filter.PassFilter(entry.label.c_str())) continue;
            ImGui::PushID((int)i); ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::BeginDisabled(!entry.problem.empty());
            if (ImGui::Selectable(entry.label.c_str(), t.floor_use_library && t.floor_choice == (int)i, ImGuiSelectableFlags_SpanAllColumns)) select_floor_variant(t, (int)i);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                for (const auto &strip : entry.strips) ImGui::TextUnformatted(strip.c_str());
                if (!entry.problem.empty()) ImGui::TextWrapped("%s", entry.problem.c_str());
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn(); ImGui::Text("%d x %d", entry.width, entry.height);
            ImGui::TableNextColumn(); ImGui::Text("%zu", entry.strips.size()); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    floor_variant_controls(t);
    floor_comparison_status(t);
    if (t.floor_use_library && t.floor_alternative.ready()) {
        const auto &art = t.floor_alternative.layout.artwork;
        const auto &im = art.assets->data.images.front(); floor_draft_textures.renderer = renderer;
        auto tex = floor_draft_textures.get(art, 0, 0);
        float scale = std::min({1.f, ImGui::GetContentRegionAvail().x / im.w, 45.f / im.h});
        if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, {im.w * scale, im.h * scale});
        if (!floor_layers_bound(t)) ImGui::TextWrapped("Apply the game layout to show this floor on Stage.");
    }
    if (ImGui::Button("Restore game floor")) select_floor_variant(t, -1);
    floor_library_restore_point = item_center(); ImGui::SameLine();
    ImGui::BeginDisabled(!t.floor_use_library || !t.floor_alternative.ready());
    if (ImGui::Button("Use as editable artwork")) {
        open_floor(t); t.floor_draft = t.floor_alternative.layout.artwork;
        t.floor_x = t.document.state().start_x + (int)t.floor_alternative.screen_x;
        t.floor_y = t.document.state().start_y + t.floor_alternative.screen_y;
        t.floor_width = t.floor_draft.assets->data.images.front().w; t.floor_library_open = false;
    }
    floor_library_art_point = item_center(); ImGui::EndDisabled();
    ImGui::TextDisabled("Preview only. Close this window to compare on Stage.");
    ImGui::End();
}
void floor_dialog(Tab &t) {
    if (t.floor_requested) { ImGui::OpenPopup("Floor"); t.floor_requested = false; }
    asset_modal_size(760, 570);
    if (!ImGui::BeginPopupModal("Floor", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::SeparatorText("Existing game floor");
    if (ImGui::Button("Load / reload game floor")) {
        reload_floor(t);
    }
    const auto &floor = active_floor(t);
    if (floor_context_current(t) && floor.ready()) {
        ImGui::SameLine(); ImGui::Checkbox("Show reference", &t.show_floor);
        ImGui::TextWrapped("%s", floor.source.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Palette source: %s", floor.palette_source.c_str());
        ImGui::TextWrapped("%s", floor.notice.c_str());
        if (!floor_layers_bound(t)) ImGui::TextWrapped("Apply the game layout to align and show this floor.");
        if (ImGui::SmallButton("Use this texture for new floor artwork")) {
            t.floor_draft = floor.layout.artwork;
            auto r = floor.rect(t.document.state(), {});
            // A new authored floor is a static 1x layer at the reference's start position.
            t.floor_x = t.document.state().start_x + (int)floor.screen_x; t.floor_y = (int)r.y; t.floor_width = (int)r.w;
            t.floor_error.clear();
        }
    } else ImGui::TextWrapped("%s", floor_context_current(t) ? floor.notice.c_str() : "Floor source changed. Reload its floor.");
    if (ImGui::Button("Browse IMG floors...")) {
        t.floor_library_open = true; ImGui::CloseCurrentPopup();
        if (t.floor_library.path.empty()) find_floor_library(t);
    }
    ImGui::SeparatorText("Add editable floor artwork");
    if (ImGui::Button("Choose PNG / TGA / BMP")) {
        char path[1024] = {};
        if (file_dialog_open("Choose floor artwork", "Raster artwork\0*.png;*.PNG;*.tga;*.bmp\0All files\0*.*\0", path, sizeof path)) floor_from_file(t, path);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(t.asset < 0 || t.asset >= (int)t.document.state().assets->data.images.size());
    if (ImGui::Button("Use selected asset")) floor_from_asset(t);
    floor_asset_point = item_center(); ImGui::EndDisabled();
    ImGui::SetNextItemWidth(125); ImGui::InputInt("Left X", &t.floor_x);
    ImGui::SameLine(); ImGui::SetNextItemWidth(125); ImGui::InputInt("Top Y", &t.floor_y);
    ImGui::SameLine(); ImGui::SetNextItemWidth(125); ImGui::InputInt("Width", &t.floor_width);
    if (t.floor_draft.assets) {
        const auto &im = t.floor_draft.assets->data.images.front();
        ImGui::Text("Source: %d x %d | %s", im.w, im.h, t.floor_width > im.w ? "Repeats to fill width" : "Crops to width; no scaling");
        floor_draft_textures.renderer = renderer;
        auto tex = floor_draft_textures.get(t.floor_draft, 0, 0);
        float scale = std::min({1.f, ImGui::GetContentRegionAvail().x / im.w, 64.f / im.h});
        if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, {im.w * scale, im.h * scale});
    }
    ImGui::TextWrapped("Adds a new Floor layer as one undoable edit. Blocks repeat without scaling; widths are split/padded for LOAD2. Adjust layer order and placement on Stage. Game export needs an explicit runtime layer binding.");
    if (!t.floor_error.empty()) ImGui::TextWrapped("%s", t.floor_error.c_str());
    ImGui::BeginDisabled(!t.floor_draft.assets || !t.document.state().runtime_profile.empty());
    if (ImGui::Button("Add floor to map")) {
        int plane;
        if (t.document.add_floor(*t.floor_draft.assets, t.floor_x, t.floor_y, t.floor_width, t.floor_error, plane)) {
            t.plane = plane; t.selected.clear(); t.source = false; t.solo = -1; t.fit = true;
            t.show_floor = false; page = 0;
            ImGui::CloseCurrentPopup(); toast("Added floor artwork. Undo restores the previous map.");
        }
    }
    floor_add_point = item_center(); ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    floor_close_point = item_center();
    if (!t.document.state().runtime_profile.empty()) ImGui::TextWrapped("This custom stage profile manages its floor separately; adding a floor layer is unsupported.");
    ImGui::EndPopup();
}
