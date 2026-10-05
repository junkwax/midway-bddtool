void import_folder_dialog() {
    char path[2048] = {};
    if (folder_dialog_open("Import PNG / TGA / BMP folder", path, sizeof path)) open_import_folder(path);
}
void open_import_folder(const std::string &folder) {
    if (!tab()) add(Document::empty());
    cancel_gesture(); error.clear(); auto &t = *tab();
    t.batch_files.clear(); t.batch_error.clear(); t.batch_input = {};
    t.batch_before.reset(); t.batch_preview.reset(); t.batch_folder = folder;
    try {
        std::vector<fs::path> paths;
        for (const auto &entry : fs::directory_iterator(fs::u8path(folder))) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().u8string();
            for (auto &c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (ext == ".png" || ext == ".bmp" || ext == ".tga") paths.push_back(entry.path());
            if (paths.size() > 2048) { error = "More than 2048 raster files. Choose a smaller folder."; return; }
        }
        std::sort(paths.begin(), paths.end());
        size_t selected = 0;
        for (const auto &path : paths) {
            Tab::ImportFile row; row.path = path.u8string(); int channels;
            if (!stbi_info(row.path.c_str(), &row.width, &row.height, &channels)) row.problem = "Cannot read image";
            else if (row.width < 1 || row.height < 1 || row.width > 4096 || row.height > 4096)
                row.problem = "Maximum size is 4096 x 4096";
            row.selected = row.problem.empty() && selected < 128;
            if (row.selected) ++selected;
            t.batch_files.push_back(std::move(row));
        }
        if (paths.empty()) { error = "No PNG, TGA or BMP files in this folder (subfolders are not scanned)."; return; }
        t.batch_requested = true;
    } catch (const std::exception &e) { error = e.what(); }
}
void review_import_folder(Tab &t) {
    t.batch_preview.reset(); t.batch_input = {}; t.batch_error.clear();
    size_t pixels = 0;
    for (auto &row : t.batch_files) if (row.selected) {
        int w, h, channels;
        auto fail = [&](const std::string &why) {
            row.problem = why; t.batch_error = fs::u8path(row.path).filename().u8string() + ": " + why;
        };
        if (!stbi_info(row.path.c_str(), &w, &h, &channels) || w < 1 || h < 1 || w > 4096 || h > 4096) {
            fail("Cannot decode image or invalid dimensions"); return;
        }
        if (size_t(w) * h > 16 * 1024 * 1024 - pixels) {
            fail("Selection exceeds 16 million pixels. Import fewer files at once."); return;
        }
        auto rgba = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(stbi_load(row.path.c_str(), &w, &h, &channels, 4), stbi_image_free);
        if (!rgba || w < 1 || h < 1 || w > 4096 || h > 4096 || size_t(w) * h > 16 * 1024 * 1024 - pixels) {
            fail("Could not decode bounded image data"); return;
        }
        AssetBank input;
        if (!make_raster_asset(fs::u8path(row.path).stem().u8string(), w, h, rgba.get(), input, t.batch_error)) {
            fail(t.batch_error); return;
        }
        pixels += size_t(w) * h;
        int id = (int)t.batch_input.data.images.size(), palette = (int)t.batch_input.data.palettes.size();
        input.data.images[0].idx = id; input.metadata[0].idx = id;
        std::snprintf(input.metadata[0].source, sizeof input.metadata[0].source, "%s", fs::u8path(row.path).filename().u8string().c_str());
        t.batch_input.data.images.push_back(std::move(input.data.images[0]));
        t.batch_input.data.palettes.push_back(input.data.palettes[0]);
        t.batch_input.metadata.push_back(input.metadata[0]); t.batch_input.default_palettes.push_back(palette);
        row.problem.clear(); row.width = w; row.height = h;
    }
    t.batch_before = t.document.state().assets; t.batch_revision = t.document.state().revision;
    t.batch_preview = prepare_asset_import(t.document.state(), t.batch_input, t.batch_reuse, t.batch_error);
}
void batch_import_dialog(Tab &t) {
    if (t.batch_requested) { ImGui::OpenPopup("Import artwork folder"); t.batch_requested = false; }
    asset_modal_size(850, 610);
    if (!ImGui::BeginPopupModal("Import artwork folder", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("%s", t.batch_folder.c_str());
    ImGui::TextWrapped("Choose up to 128 PNG, TGA or BMP files. Review decodes the selection; Import adds that reviewed artwork as one undoable edit.");
    auto invalidate = [&] { t.batch_preview.reset(); t.batch_input = {}; t.batch_error.clear(); };
    if (ImGui::Checkbox("Reuse identical palettes", &t.batch_reuse)) invalidate();
    if (ImGui::Button("Select first 128")) {
        size_t n = 0;
        for (auto &row : t.batch_files) row.selected = n++ < 128;
        invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear selection")) { for (auto &row : t.batch_files) row.selected = false; invalidate(); }
    size_t selected = std::count_if(t.batch_files.begin(), t.batch_files.end(), [](const auto &row) { return row.selected; });
    ImGui::SameLine(); ImGui::TextDisabled("%zu of %zu files selected", selected, t.batch_files.size());
    if (ImGui::BeginTable("folder-images", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                          {0, std::max(100.f, ImGui::GetContentRegionAvail().y - 145)})) {
        ImGui::TableSetupColumn("File"); ImGui::TableSetupColumn("Size"); ImGui::TableSetupColumn("Review");
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin((int)t.batch_files.size());
        while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            auto &row = t.batch_files[i]; ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::BeginDisabled(!row.selected && selected >= 128);
            if (ImGui::Checkbox(fs::u8path(row.path).filename().u8string().c_str(), &row.selected)) {
                selected += row.selected ? 1 : -1; invalidate();
            }
            ImGui::EndDisabled(); ImGui::TableNextColumn(); ImGui::Text("%d x %d", row.width, row.height);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.problem.empty() ? (t.batch_preview && row.selected ? "Reviewed" : "Not reviewed") : row.problem.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.problem.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(selected == 0);
    if (ImGui::Button("Review selection")) review_import_folder(t);
    batch_review_point = item_center();
    ImGui::EndDisabled(); ImGui::SameLine();
    bool current = t.batch_preview && t.batch_before == t.document.state().assets && t.batch_revision == t.document.state().revision;
    ImGui::BeginDisabled(!current);
    if (ImGui::Button("Import reviewed artwork")) {
        std::vector<int> ids;
        if (t.document.import_assets(t.batch_input, t.batch_reuse, t.batch_error, ids)) {
            t.asset = (int)t.document.state().assets->data.images.size() - (int)ids.size();
            page = 1; tray = true; search[0] = 0;
            toast("Imported " + std::to_string(ids.size()) + " images. Undo restores the whole batch.");
            t.batch_input = {}; t.batch_preview.reset(); t.batch_before.reset();
            ImGui::CloseCurrentPopup();
        }
    }
    batch_import_point = item_center();
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Cancel")) { invalidate(); t.batch_before.reset(); ImGui::CloseCurrentPopup(); }
    batch_cancel_point = item_center();
    if (t.batch_preview) {
        ImGui::Text("Images %zu -> %zu   |   Palettes %zu -> %zu", t.batch_before->data.images.size(), t.batch_preview->data.images.size(),
            t.batch_before->data.palettes.size(), t.batch_preview->data.palettes.size());
        if (!current) ImGui::TextWrapped("The document changed. Review again before importing.");
        else ImGui::TextDisabled("No files change until Save. Existing artwork and placements stay as authored.");
    }
    if (!t.batch_error.empty()) ImGui::TextWrapped("%s", t.batch_error.c_str());
    ImGui::EndPopup();
}
