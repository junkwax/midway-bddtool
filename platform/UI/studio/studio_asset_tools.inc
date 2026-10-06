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
    t.palette_adjust_base = t.palette_draft;
    t.palette_adjustment = {};
    t.palette_blend_target = -1; t.palette_blend_amount = 0;
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
void export_asset_tga(Tab &t) {
    const auto bank = t.document.state().assets;
    if (t.asset < 0 || t.asset >= (int)bank->data.images.size()) return;
    int pi = asset_palette(t, t.asset);
    if (pi < 0 || pi >= (int)bank->data.palettes.size()) return;
    const auto &im = bank->data.images[t.asset];
    auto result = encode_indexed_tga(im, bank->data.palettes[pi]);
    if (!result.ready) { error = result.error; return; }
    char path[1024] = {};
    std::snprintf(path, sizeof path, "artwork_%d.tga", im.idx);
    if (!file_dialog_save_ext("Export indexed artwork as TGA", "Indexed TGA image\0*.tga\0", "tga", path, sizeof path)) return;
    std::ofstream output(fs::u8path(path), std::ios::binary | std::ios::trunc);
    output.write((const char *)result.bytes.data(), (std::streamsize)result.bytes.size()); output.close();
    if (!output) error = "Could not export indexed TGA artwork.";
    else toast("Exported lossless indexed TGA");
}
void open_replace_artwork(Tab &t) {
    cancel_gesture(); error.clear();
    const auto bank = t.document.state().assets;
    if (t.asset < 0 || t.asset >= (int)bank->data.images.size()) return;
    int palette = asset_palette(t, t.asset);
    if (palette < 0 || palette >= (int)bank->data.palettes.size()) return;
    char path[1024] = {};
    if (!file_dialog_open("Replace artwork in place", "Indexed or raster artwork\0*.tga;*.TGA;*.png;*.PNG;*.bmp;*.BMP\0All files\0*.*\0", path, sizeof path)) return;
    ReplacementPixels replacement;
    std::string extension = fs::u8path(path).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (extension == ".tga") {
        std::ifstream input(fs::u8path(path), std::ios::binary);
        std::vector<uint8_t> bytes;
        if (input) { input.seekg(0, std::ios::end); auto size = input.tellg(); input.seekg(0);
            if (size >= 0 && size <= 32 * 1024 * 1024) bytes.assign(std::istreambuf_iterator<char>(input), {}); }
        if (bytes.empty()) replacement.error = "Could not read the indexed TGA, or it exceeds 32 MiB.";
        else replacement = prepare_indexed_tga_replacement(bytes, bank->data.palettes[palette]);
    } else {
        int w = 0, h = 0, channels = 0;
        auto *rgba = stbi_load(path, &w, &h, &channels, 4);
        if (!rgba) replacement.error = "Could not decode the replacement image.";
        else { replacement = prepare_rgba_replacement(w, h, rgba, bank->data.palettes[palette]); stbi_image_free(rgba); }
    }
    if (!replacement.ready) { error = replacement.error; return; }
    begin_replace_review(t, palette, std::move(replacement), path);
}
void begin_replace_review(Tab &t, int palette, ReplacementPixels replacement, const std::string &source) {
    const auto bank = t.document.state().assets;
    t.replace_draft = t.document.state();
    auto draft = std::make_shared<AssetBank>(*bank);
    auto &image = draft->data.images[t.asset];
    t.replace_image_id = image.idx; image.w = replacement.width; image.h = replacement.height;
    image.pix = std::move(replacement.pixels); t.replace_draft.assets = draft;
    t.replace_original = bank; t.replace_revision = t.document.state().revision;
    t.replace_palette = palette; t.replace_source = source; t.replace_requested = true;
    replace_textures.clear(); replace_diff.clear();
}

void replace_artwork_dialog(Tab &t) {
    if (t.replace_requested) { ImGui::OpenPopup("Replace artwork in place"); t.replace_requested = false; }
    asset_modal_size(720, 640);
    if (!ImGui::BeginPopupModal("Replace artwork in place", nullptr, ImGuiWindowFlags_NoResize)) return;
    size_t old_slot = 0; const auto *old_image = t.document.image(t.replace_image_id, &old_slot);
    bool current = old_image && t.document.state().revision == t.replace_revision && t.document.state().assets == t.replace_original;
    if (!current || old_slot >= t.replace_draft.assets->data.images.size()) {
        ImGui::TextWrapped("The document changed. Close this review and choose the replacement again.");
    } else {
        const auto &next = t.replace_draft.assets->data.images[old_slot];
        int uses = 0; for (const auto &p : t.document.state().objects) if (p.object.ii == t.replace_image_id) ++uses;
        ImGui::Text("Image %d  |  %d placement%s", t.replace_image_id, uses, uses == 1 ? "" : "s");
        ImGui::TextWrapped("%s", t.replace_source.c_str());
        if (old_image->w != next.w || old_image->h != next.h)
            ImGui::TextColored(ImVec4(1, .72f, .25f, 1), "Dimensions change from %d x %d to %d x %d. Placement origins stay fixed.", old_image->w, old_image->h, next.w, next.h);
        else ImGui::Text("Dimensions remain %d x %d.", old_image->w, old_image->h);
        auto &view = t.replace_compare;
        bool show_after = compare_controls(view, true, "Current", "Replacement");
        // Keep the explanation and Apply/Cancel row visible below the preview.
        float footer = 2 * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() +
                       ImGui::GetStyle().ItemSpacing.y * 2 + (error.empty() ? 0 : ImGui::GetTextLineHeightWithSpacing());
        auto room = ImGui::GetContentRegionAvail(); room.y = std::max(80.f, room.y - footer);
        auto *old_texture = textures.get(t.document.state(), (int)old_slot, t.replace_palette);
        auto *new_texture = replace_textures.get(t.replace_draft, (int)old_slot, t.replace_palette);
        if (view.mode == CompareMode::SideBySide) {
            ImGui::Columns(2, "replacement-preview", false);
            auto preview = [&](const char *label, SDL_Texture *texture, const BddCoreImage &im) {
                ImGui::TextUnformatted(label);
                float scale = std::min({1.0f, 280.0f / std::max(1, im.w), (room.y - 24) / std::max(1, im.h)});
                if (texture) ImGui::Image((ImTextureID)texture, {im.w * scale, im.h * scale});
            };
            preview("Current", old_texture, *old_image); ImGui::NextColumn();
            preview("Replacement", new_texture, next); ImGui::Columns(1);
        } else {
            // Both images share the top-left origin, matching how placements keep their origin.
            int w = std::max(old_image->w, next.w), h = std::max(old_image->h, next.h);
            float scale = std::min({8.0f, room.x / std::max(1, w), (room.y - 24) / std::max(1, h)});
            if (scale >= 1) scale = std::floor(scale); // Whole-pixel zoom keeps edges crisp.
            auto a = ImGui::GetCursorScreenPos();
            ImVec2 b(a.x + w * scale, a.y + h * scale);
            ImGui::Dummy({w * scale, h * scale});
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(a, b, IM_COL32(10, 12, 16, 255));
            auto image = [&](SDL_Texture *texture, const BddCoreImage &im, ImU32 tint = IM_COL32_WHITE) {
                if (texture) draw->AddImage((ImTextureID)texture, a, {a.x + im.w * scale, a.y + im.h * scale}, {0, 0}, {1, 1}, tint);
            };
            if (view.mode == CompareMode::Wipe) {
                float split = a.x + w * scale * view.split;
                draw->PushClipRect(a, {split, b.y}, true); image(old_texture, *old_image); draw->PopClipRect();
                draw->PushClipRect({split, a.y}, b, true); image(new_texture, next); draw->PopClipRect();
                draw->AddLine({split, a.y}, {split, b.y}, selection_color, 2);
                ImGui::TextDisabled("Current on left | Replacement on right");
            } else if (view.mode == CompareMode::Overlay) {
                image(old_texture, *old_image);
                image(new_texture, next, IM_COL32(255, 255, 255, (int)std::lround(255 * view.opacity)));
                ImGui::TextDisabled("Replacement drawn over current at %.0f%% opacity", 100 * view.opacity);
            } else if (view.mode == CompareMode::Difference) {
                const auto &before_pal = t.document.state().assets->data.palettes[t.replace_palette];
                const auto &after_pal = t.replace_draft.assets->data.palettes[t.replace_palette];
                auto *texture = replace_diff.get(
                    {(uint64_t)(uintptr_t)t.replace_original.get(), (uint64_t)(uintptr_t)t.replace_draft.assets.get(),
                     (uint64_t)old_slot, (uint64_t)t.replace_palette},
                    [&] { return diff_views(render_indexed_image(*old_image, before_pal), render_indexed_image(next, after_pal)); });
                if (texture) draw->AddImage((ImTextureID)texture, a, b);
                outline_changes(replace_diff.diff, a, scale);
                difference_summary(replace_diff.diff);
            } else {
                image(show_after ? new_texture : old_texture, show_after ? next : *old_image);
                ImGui::TextDisabled("%s", show_after ? "Replacement" : "Current");
            }
        }
        ImGui::Separator();
        ImGui::TextWrapped("Image ID, placement coordinates, palette assignments and metadata remain unchanged. Apply is one undoable document edit.");
        if (ImGui::Button("Apply replacement", {150, 0})) {
            if (t.document.replace_image(t.replace_image_id, next.w, next.h, next.pix, error)) {
                replace_textures.clear(); replace_diff.clear(); toast("Replaced artwork in place"); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", {100, 0})) { replace_textures.clear(); replace_diff.clear(); ImGui::CloseCurrentPopup(); }
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    ImGui::EndPopup();
}

void open_canvas_resize(Tab &t) {
    cancel_gesture(); error.clear();
    const auto bank = t.document.state().assets;
    if (t.asset < 0 || t.asset >= (int)bank->data.images.size()) return;
    t.canvas_image_id = bank->data.images[t.asset].idx;
    std::fill(std::begin(t.canvas_edges), std::end(t.canvas_edges), 0);
    t.canvas_requested = true;
}

void canvas_resize_dialog(Tab &t) {
    if (t.canvas_requested) { ImGui::OpenPopup("Trim or pad artwork canvas"); t.canvas_requested = false; }
    asset_modal_size(590, 470);
    if (!ImGui::BeginPopupModal("Trim or pad artwork canvas", nullptr, ImGuiWindowFlags_NoResize)) return;
    size_t slot = 0; const auto *im = t.document.image(t.canvas_image_id, &slot);
    if (!im) { ImGui::TextUnformatted("Artwork is no longer available."); }
    else {
        ImGui::Text("Image %d  |  Current canvas %d x %d", im->idx, im->w, im->h);
        ImGui::TextWrapped("Positive values add transparent pixels. Negative values trim transparent pixels. Every placement is compensated so visible artwork stays fixed, including flipped uses.");
        if (ImGui::Button("Trim all transparent borders")) {
            int minx = im->w, miny = im->h, maxx = -1, maxy = -1;
            for (int y = 0; y < im->h; ++y) for (int x = 0; x < im->w; ++x)
                if (im->pix[size_t(y) * im->w + x]) {
                    minx = std::min(minx, x); miny = std::min(miny, y);
                    maxx = std::max(maxx, x); maxy = std::max(maxy, y);
                }
            if (maxx < 0) error = "Completely transparent artwork cannot be auto-trimmed.";
            else {
                t.canvas_edges[0] = -minx; t.canvas_edges[1] = -miny;
                t.canvas_edges[2] = -(im->w - 1 - maxx); t.canvas_edges[3] = -(im->h - 1 - maxy);
            }
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(110); ImGui::InputInt("Left", &t.canvas_edges[0]);
        ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputInt("Top", &t.canvas_edges[1]);
        ImGui::SetNextItemWidth(110); ImGui::InputInt("Right", &t.canvas_edges[2]);
        ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputInt("Bottom", &t.canvas_edges[3]);
        int width = im->w + t.canvas_edges[0] + t.canvas_edges[2];
        int height = im->h + t.canvas_edges[1] + t.canvas_edges[3];
        bool valid_size = width > 0 && height > 0 && width <= 4096 && height <= 4096 && uint64_t(width) * height <= 16000000;
        ImGui::Text("Result: %d x %d", width, height);
        if (valid_size && width % BDD_CORE_MK2_BG_WIDTH_ALIGN)
            ImGui::TextColored(ImVec4(1, .72f, .25f, 1), "Width is not divisible by 4 and may shear in LOAD2.");
        if (!valid_size) ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "Resulting dimensions are invalid or exceed safe limits.");
        ImGui::TextDisabled("Visible pixels, IDs, palettes, metadata, layer ownership and draw order remain unchanged.");
        ImGui::Separator();
        ImGui::BeginDisabled(!valid_size || !(t.canvas_edges[0] || t.canvas_edges[1] || t.canvas_edges[2] || t.canvas_edges[3]));
        if (ImGui::Button("Apply canvas change", {155, 0})) {
            if (t.document.resize_image_canvas(t.canvas_image_id, t.canvas_edges[0], t.canvas_edges[1],
                                               t.canvas_edges[2], t.canvas_edges[3], error)) {
                toast("Resized artwork canvas without moving visible pixels"); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled(); ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    ImGui::EndPopup();
}
void palette_swatches(const BddCorePalette &pal, int &selected,
                      const std::array<uint64_t, 256> *usage = nullptr) {
    int cols = std::max(1, std::min(16, (int)(ImGui::GetContentRegionAvail().x / 28)));
    for (int i = 0; i < pal.count; ++i) {
        ImGui::PushID(i);
        if (i % cols) ImGui::SameLine(0, 4);
        uint32_t color = pal.argb[i];
        ImVec4 c(((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                 (color & 255) / 255.f, i ? 1.f : 0.f);
        if (ImGui::ColorButton("##color", c, ImGuiColorEditFlags_NoTooltip, {24, 24})) selected = i;
        if (i == selected) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), selection_color, 0, 0, 2);
        if (usage && !(*usage)[i]) {
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(a, b, IM_COL32(190, 200, 212, 150), 1);
            ImGui::GetWindowDrawList()->AddLine({a.x, b.y}, {b.x, a.y}, IM_COL32(190, 200, 212, 150), 1);
        }
        if (ImGui::IsItemHovered()) {
            if (usage) ImGui::SetTooltip("Index %d%s\nRGB555: %04X\n%llu pixels", i, i ? "" : " (transparent)", pal.rgb555[i], (unsigned long long)(*usage)[i]);
            else ImGui::SetTooltip("Index %d%s\nRGB555: %04X", i, i ? "" : " (transparent)", pal.rgb555[i]);
        }
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
    replace_artwork_dialog(t);
    canvas_resize_dialog(t);
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
    asset_modal_size(590, 820);
    if (ImGui::BeginPopupModal("Palette editor", nullptr, ImGuiWindowFlags_NoResize)) {
        const auto bank = t.document.state().assets;
        // The color picker and Apply/Close stay pinned below; everything above scrolls.
        float footer = 2 * ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetFrameHeightWithSpacing() +
                       (error.empty() ? 0 : 2 * ImGui::GetTextLineHeightWithSpacing());
        ImGui::BeginChild("palette-body", {0, -footer});
        ImGui::TextWrapped("Editing this palette changes every artwork placement that uses it.");
        ImGui::SetNextItemWidth(350);
        std::string title = std::to_string(t.edit_palette) + ": " + t.palette_draft.name;
        if (ImGui::BeginCombo("Palette", title.c_str())) {
            for (int i = 0; i < (int)bank->data.palettes.size(); ++i) {
                auto name = std::to_string(i) + ": " + bank->data.palettes[i].name;
                if (ImGui::Selectable(name.c_str(), i == t.edit_palette)) {
                    t.edit_palette = i; t.palette_draft = bank->data.palettes[i]; t.edit_original = bank;
                    t.palette_adjust_base = t.palette_draft; t.palette_adjustment = {};
                    t.palette_blend_target = -1; t.palette_blend_amount = 0;
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
                t.palette_adjust_base = t.palette_draft; t.palette_adjustment = {};
                t.palette_blend_target = -1; t.palette_blend_amount = 0;
                toast("Palette copied and assigned. Undo restores the original assignments.");
            }
        }
        ImGui::TextDisabled("Copies the saved palette; keeps other palette variants on placements.");
        if (ImGui::Button("Import RGB555 palette...")) {
            char path[1024] = {};
            if (file_dialog_open("Import RGB555 palette", "bddtool RGB555 palette\0*.rgb555\0All files\0*.*\0", path, sizeof path)) {
                std::ifstream input(fs::u8path(path), std::ios::binary);
                std::string text;
                if (input) {
                    input.seekg(0, std::ios::end); auto size = input.tellg(); input.seekg(0);
                    if (size >= 0 && size <= 16384) text.assign(std::istreambuf_iterator<char>(input), {});
                }
                BddCorePalette imported{};
                if (text.empty()) error = "Could not read the palette file, or it exceeds 16 KiB.";
                else if (decode_palette_file(text, imported, error)) {
                    t.palette_draft = imported; t.palette_adjust_base = imported; t.palette_adjustment = {};
                    t.palette_blend_target = -1; t.palette_blend_amount = 0;
                    t.edit_color = std::min(1, imported.count - 1);
                    toast("Palette loaded into the review draft");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Export RGB555 palette...")) {
            char path[1024] = {}; std::snprintf(path, sizeof path, "%s.rgb555", t.palette_draft.name);
            if (file_dialog_save_ext("Export RGB555 palette", "bddtool RGB555 palette\0*.rgb555\0", "rgb555", path, sizeof path)) {
                std::ofstream output(fs::u8path(path), std::ios::binary | std::ios::trunc);
                auto text = encode_palette_file(t.palette_draft);
                output.write(text.data(), (std::streamsize)text.size()); output.close();
                if (!output) error = "Could not export the palette file."; else toast("Exported RGB555 palette");
            }
        }
        ImGui::TextDisabled("Import changes only this review draft; Apply palette commits it with undo.");
        ImGui::InputText("Name", t.palette_draft.name, sizeof t.palette_draft.name);
        ImGui::SliderInt("Color count", &t.palette_draft.count, 1, 256);
        if (ImGui::CollapsingHeader("Color adjustments", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Preview the whole palette at the game's RGB555 precision. Index 0 remains transparent.");
            bool changed = false;
            const char *blend_name = t.palette_blend_target >= 0 && t.palette_blend_target < (int)bank->data.palettes.size()
                ? bank->data.palettes[t.palette_blend_target].name : "None";
            if (ImGui::BeginCombo("Blend toward", blend_name)) {
                if (ImGui::Selectable("None", t.palette_blend_target < 0)) {
                    t.palette_blend_target = -1; t.palette_blend_amount = 0; changed = true;
                }
                for (int i = 0; i < (int)bank->data.palettes.size(); ++i) if (i != t.edit_palette) {
                    std::string label = std::to_string(i) + ": " + bank->data.palettes[i].name;
                    if (ImGui::Selectable(label.c_str(), t.palette_blend_target == i)) {
                        t.palette_blend_target = i; changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(t.palette_blend_target < 0);
            changed |= ImGui::SliderFloat("Blend amount", &t.palette_blend_amount, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_None);
            ImGui::EndDisabled();
            changed |= ImGui::SliderFloat("Brightness", &t.palette_adjustment.brightness, -1.0f, 1.0f, "%+.2f");
            changed |= ImGui::SliderFloat("Contrast", &t.palette_adjustment.contrast, 0.0f, 2.0f, "%.2fx");
            changed |= ImGui::SliderFloat("Saturation", &t.palette_adjustment.saturation, 0.0f, 2.0f, "%.2fx");
            if (changed) {
                char name[sizeof t.palette_draft.name]; std::memcpy(name, t.palette_draft.name, sizeof name);
                int count = t.palette_draft.count;
                BddCorePalette colors = t.palette_adjust_base;
                if (t.palette_blend_target >= 0 && t.palette_blend_target < (int)bank->data.palettes.size())
                    colors = blend_palette(colors, bank->data.palettes[t.palette_blend_target], t.palette_blend_amount);
                t.palette_draft = adjust_palette(colors, t.palette_adjustment);
                std::memcpy(t.palette_draft.name, name, sizeof name); t.palette_draft.count = count;
            }
            if (ImGui::Button("Reset adjustments")) {
                char name[sizeof t.palette_draft.name]; std::memcpy(name, t.palette_draft.name, sizeof name);
                int count = t.palette_draft.count;
                t.palette_adjustment = {}; t.palette_blend_target = -1; t.palette_blend_amount = 0;
                t.palette_draft = t.palette_adjust_base;
                std::memcpy(t.palette_draft.name, name, sizeof name); t.palette_draft.count = count;
            }
            ImGui::SameLine();
            if (ImGui::Button("Use current colors as baseline")) {
                t.palette_adjust_base = t.palette_draft; t.palette_adjustment = {};
                t.palette_blend_target = -1; t.palette_blend_amount = 0;
            }
        }
        t.edit_color = std::clamp(t.edit_color, 0, t.palette_draft.count - 1);
        PaletteUsage usage;
        const auto *usage_image = t.document.image(t.edit_image);
        if (usage_image) usage = analyze_palette_usage(*usage_image);
        // Collapsed by default so Apply stays visible at compact window sizes.
        std::string usage_title = usage_image ? "Selected artwork usage: " + std::to_string(usage.used_colors) +
            " colors, " + std::to_string(usage.current_bpp) + " BPP###palette-usage" : "";
        if (usage_image && ImGui::CollapsingHeader(usage_title.c_str())) {
            ImGui::Text("%d opaque colors | highest index %d | %d BPP", usage.used_colors,
                        usage.max_index, usage.current_bpp);
            if (usage.compact_bpp < usage.current_bpp)
                ImGui::TextColored(accent, "Dense indices could reach %d BPP (about %llu raw bytes less)",
                                   usage.compact_bpp, (unsigned long long)usage.raw_saving_bytes);
            else ImGui::TextDisabled("Indices are already within the smallest BPP tier for this color count.");
            const auto &blockers = usage.current_bpp > 4 ? usage.blocks_4bpp : usage.blocks_2bpp;
            if (!blockers.empty()) {
                std::string text = usage.current_bpp > 4 ? "Indices keeping this above 4 BPP: " :
                                                          "Indices keeping this above 2 BPP: ";
                for (size_t i = 0; i < blockers.size() && i < 16; ++i) {
                    if (i) text += ", "; text += std::to_string(blockers[i]);
                }
                if (blockers.size() > 16) text += ", ...";
                ImGui::TextWrapped("%s", text.c_str());
            }
            ImGui::TextDisabled("Crossed swatches are unused by this artwork. Counts are shown on hover.");
        }
        ImGui::BeginChild("swatches", {0, std::max(120.f, ImGui::GetContentRegionAvail().y)}, true);
        palette_swatches(t.palette_draft, t.edit_color, usage_image ? &usage.pixels : nullptr);
        ImGui::EndChild();
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
