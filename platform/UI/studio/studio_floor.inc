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
void floor_dialog(Tab &t) {
    if (t.floor_requested) { ImGui::OpenPopup("Floor"); t.floor_requested = false; }
    asset_modal_size(760, 570);
    if (!ImGui::BeginPopupModal("Floor", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::SeparatorText("Existing game floor");
    if (ImGui::Button("Load / reload game floor")) {
        t.floor = load_floor_preview(t.document, t.game_root); t.floor_root = t.game_root;
    }
    if (t.floor_root == t.game_root && t.floor.ready()) {
        ImGui::SameLine(); ImGui::Checkbox("Show reference", &t.show_floor);
        ImGui::TextWrapped("%s", t.floor.source.c_str());
        ImGui::TextWrapped("%s", t.floor.notice.c_str());
        if (ImGui::SmallButton("Use this texture for new floor artwork")) {
            t.floor_draft = t.floor.layout.artwork;
            auto r = t.floor.rect(t.document.state(), {});
            t.floor_x = (int)r.x; t.floor_y = (int)r.y; t.floor_width = (int)r.w;
            t.floor_error.clear();
        }
    } else ImGui::TextWrapped("%s", t.floor_root == t.game_root ? t.floor.notice.c_str() : "Checkout changed. Reload its floor.");
    ImGui::SeparatorText("Add editable floor artwork");
    if (ImGui::Button("Choose PNG / TGA / BMP")) {
        char path[1024] = {};
        if (file_dialog_open("Choose floor artwork", "Raster artwork\0*.png;*.PNG;*.tga;*.bmp\0All files\0*.*\0", path, sizeof path)) floor_from_file(t, path);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(t.asset < 0 || t.asset >= (int)t.document.state().assets->data.images.size());
    if (ImGui::Button("Use selected asset")) floor_from_asset(t);
    floor_asset_point = item_center(); ImGui::EndDisabled();
    ImGui::TextDisabled("For IMG artwork, import and select an image in Assets first.");
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
