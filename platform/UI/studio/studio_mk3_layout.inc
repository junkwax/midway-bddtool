bool apply_layout_review(Tab &t) {
    if (!t.document.apply_mk3_layout(t.mk3_layout, error)) return false;
    t.camera = {double(t.document.state().start_x), double(t.document.state().start_y)};
    t.camera_preview = true; t.source = false; t.solo = -1; t.fit = true;
    t.selected.clear(); t.focus_object = 0;
    t.camera_check_options.min_x = t.document.state().start_x;
    t.camera_check_options.max_x = std::max(t.document.state().start_x, t.document.state().world_w - 400);
    t.camera_check_options.min_y = t.camera_check_options.max_y = t.document.state().start_y;
    auto folder = fs::u8path(t.document.path()).parent_path().u8string();
    if (t.layout_remember && !folder.empty()) layout_sources[folder] = t.mk3_layout.source;
    reload_floor(t); t.show_floor = true;
    toast("Loaded MK3 layer layout. Undo restores the previous arrangement; Save keeps this layout.");
    return true;
}
void mk3_layout_dialog(Tab &t) {
    if (t.layout_requested) { ImGui::OpenPopup("Game layout"); t.layout_requested = false; }
    asset_modal_size(780, 550);
    if (!ImGui::BeginPopupModal("Game layout", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("BDB/BDD module rectangles describe the source sheet. MK3's MKBT.ASM supplies the game camera, layer offsets, centering, parallax and draw order.");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 95);
    if (ImGui::InputTextWithHint("##layout-source", "MK3 source file: MKBT.ASM", t.layout_source, sizeof t.layout_source)) t.mk3_layout = {};
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        char path[1024] = {};
        if (file_dialog_open("Choose MK3 stage definitions", "MK3 stage definitions\0MKBT.ASM;mkbt.asm\0Assembly\0*.ASM;*.asm\0", path, sizeof path)) {
            std::snprintf(t.layout_source, sizeof t.layout_source, "%s", path);
            t.mk3_layout = read_mk3_layout(t.document, path);
        }
    }
    ImGui::BeginDisabled(!t.layout_source[0]);
    if (ImGui::Button("Review layout")) t.mk3_layout = read_mk3_layout(t.document, t.layout_source);
    layout_review_point = item_center();
    ImGui::EndDisabled();
    ImGui::Checkbox("Use this source for other maps in this asset folder during this session", &t.layout_remember);
    ImGui::TextWrapped("Apply replaces layer transforms and the start camera as one undoable edit. Pixels, source rectangles and placements stay intact. Save stores the resulting layout beside the map; nothing writes to the game source.");
    if (!t.mk3_layout.error.empty()) ImGui::TextWrapped("%s", t.mk3_layout.error.c_str());
    bool current = t.mk3_layout.valid() && t.mk3_layout.revision == t.document.state().revision && t.mk3_layout.assets == t.document.state().assets;
    if (t.mk3_layout.valid() && !current) ImGui::TextColored(accent, "Document changed. Review again before applying.");
    ImGui::BeginChild("layout-review", {0, 210}, ImGuiChildFlags_Border);
    if (!t.mk3_layout.report.empty()) ImGui::TextWrapped("%s", t.mk3_layout.report.c_str());
    else ImGui::TextWrapped("Select the matching MK3 source revision. An artwork-only archive does not contain enough information to reconstruct the in-game layout automatically.");
    ImGui::EndChild();
    ImGui::BeginDisabled(!current);
    if (ImGui::Button("Apply game layout") && apply_layout_review(t)) ImGui::CloseCurrentPopup();
    layout_apply_point = item_center(); ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    layout_close_point = item_center();
    ImGui::EndPopup();
}
