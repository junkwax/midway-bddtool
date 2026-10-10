void open_library_audit_dialog() {
    char folder[1024] = {};
    if (!folder_dialog_open("Open BDD library audit report", folder, sizeof folder)) return;
    LibraryAuditReport report; std::string why;
    if (!load_library_audit(folder, report, why)) { toast(why); return; }
    library_audit = std::move(report); library_audit_selected = -1;
    library_audit_filter[0] = 0; library_audit_incomplete = library_audit_active = false;
    library_audit_open = true;
}
bool find_library_audit_tools(std::string &script, std::string &analyzer, std::string &why) {
    why.clear(); script.clear(); analyzer.clear();
    char *base_text = SDL_GetBasePath();
    fs::path base = base_text ? fs::u8path(base_text) : fs::current_path();
    if (base_text) SDL_free(base_text);
#ifdef _WIN32
    auto analyzer_name = "studio_savings_audit.exe";
#else
    auto analyzer_name = "studio_savings_audit";
#endif
    std::vector<fs::path> analyzer_candidates = {base / analyzer_name, fs::current_path() / analyzer_name};
    for (const auto &candidate : analyzer_candidates)
        if (fs::is_regular_file(candidate)) { analyzer = fs::absolute(candidate).u8string(); break; }
    std::vector<fs::path> roots = {fs::current_path(), base};
    for (auto root : roots)
        for (int depth = 0; depth < 7 && !root.empty(); ++depth, root = root.parent_path()) {
            auto candidate = root / "tools/audit_bdd_library.py";
            if (fs::is_regular_file(candidate)) { script = fs::absolute(candidate).u8string(); break; }
        }
    if (script.empty() || analyzer.empty()) {
        why = "Could not locate tools/audit_bdd_library.py and studio_savings_audit beside this build.";
        return false;
    }
    return true;
}
void start_library_audit() {
    std::string script, analyzer, why;
    if (!find_library_audit_tools(script, analyzer, why)) { toast(why); return; }
    if (!*library_audit_game || !fs::is_directory(fs::u8path(library_audit_game))) {
        toast("Choose the game checkout to scan"); return;
    }
    if (!*library_audit_parent || !fs::is_directory(fs::u8path(library_audit_parent))) {
        toast("Choose an existing parent folder for the new report"); return;
    }
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto output = fs::absolute(fs::u8path(library_audit_parent) / ("bdd-library-audit-" + std::to_string(stamp)));
    auto log = output; log += ".log";
    if (!library_audit_job.start_library_audit(script, library_audit_game, analyzer, output.u8string(), log.u8string(), why)) {
        toast(why); return;
    }
    library_audit_output = output.u8string(); library_audit_log = log.u8string();
    library_audit_completion_handled = false;
}
void library_audit_runner_window() {
    if (!library_audit_runner_open) return;
    bool was_running = library_audit_job.running();
    library_audit_job.poll();
    if (was_running && !library_audit_job.running() && !library_audit_completion_handled) {
        library_audit_completion_handled = true;
        if (library_audit_job.exit_code() == 0) {
            LibraryAuditReport report; std::string why;
            if (load_library_audit(library_audit_output, report, why)) {
                library_audit = std::move(report); library_audit_selected = -1;
                library_audit_open = true; library_audit_runner_open = false;
                toast("Library audit completed and opened");
            } else toast("Audit finished but its report is invalid: " + why);
        } else toast("Library audit stopped with exit code " + std::to_string(library_audit_job.exit_code()));
    }
    auto display = ImGui::GetIO().DisplaySize;
    float desired_height = library_audit_job.started() ? 520.f : 220.f;
    ImGui::SetNextWindowSize({std::min(700.f, display.x - 30), std::min(desired_height, display.y - 50)}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Appearing, {.5f, .5f});
    bool running = library_audit_job.running();
    if (!ImGui::Begin("Run BDD library savings audit", running ? nullptr : &library_audit_runner_open)) { ImGui::End(); return; }
    auto runner_size = ImGui::GetWindowSize(), runner_pos = ImGui::GetWindowPos();
    ImVec2 bounded_size{std::min(runner_size.x, std::max(400.f, display.x - 20)),
                        std::min(desired_height, std::max(200.f, display.y - 20))};
    if (bounded_size.x != runner_size.x || bounded_size.y != runner_size.y) ImGui::SetWindowSize(bounded_size);
    ImVec2 bounded_pos{std::clamp(runner_pos.x, 10.f, std::max(10.f, display.x - bounded_size.x - 10)),
                       std::clamp(runner_pos.y, 10.f, std::max(10.f, display.y - bounded_size.y - 10))};
    if (bounded_pos.x != runner_pos.x || bounded_pos.y != runner_pos.y) ImGui::SetWindowPos(bounded_pos);
    ImGui::TextWrapped("Runs the existing read-only audit on frozen copies. It hashes protected source/build files before and after, and never applies an optimization or saves a game stage.");
    ImGui::BeginDisabled(running);
    ImGui::InputTextWithHint("##audit-game", "Game checkout", library_audit_game, sizeof library_audit_game);
    ImGui::SameLine(); if (ImGui::Button("Choose checkout..."))
        folder_dialog_open("Choose game checkout to audit", library_audit_game, sizeof library_audit_game);
    ImGui::InputTextWithHint("##audit-parent", "New report parent folder", library_audit_parent, sizeof library_audit_parent);
    ImGui::SameLine(); if (ImGui::Button("Choose output parent..."))
        folder_dialog_open("Choose parent folder for audit report", library_audit_parent, sizeof library_audit_parent);
    if (ImGui::Button("Start read-only audit")) start_library_audit();
    ImGui::EndDisabled();
    if (library_audit_job.started()) {
        ImGui::Separator();
        if (running) {
            ImGui::TextColored(accent, "Audit running..."); ImGui::SameLine();
            if (ImGui::Button("Cancel audit")) library_audit_job.cancel();
        } else ImGui::Text("Finished with exit code %d", library_audit_job.exit_code());
        ImGui::TextWrapped("Output: %s", library_audit_output.c_str());
        std::ifstream input(fs::u8path(library_audit_log), std::ios::binary);
        std::string log((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (log.size() > 16000) log.erase(0, log.size() - 16000);
        ImGui::BeginChild("audit-progress-log", {0, 230}, true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(log.empty() ? "Waiting for the audit process..." : log.c_str());
        if (running && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8) ImGui::SetScrollHereY(1);
        ImGui::EndChild();
        if (!running && !library_audit_log.empty()) {
            if (ImGui::Button("Copy log path")) ImGui::SetClipboardText(library_audit_log.c_str());
            if (fs::is_directory(fs::u8path(library_audit_output))) {
                ImGui::SameLine();
                if (ImGui::Button("Open completed report")) {
                    LibraryAuditReport report; std::string why;
                    if (load_library_audit(library_audit_output, report, why)) {
                        library_audit = std::move(report); library_audit_open = true; library_audit_runner_open = false;
                    } else toast(why);
                }
            }
        }
    }
    ImGui::End();
}
void open_library_audit_stage(const LibraryAuditRow &row, bool optimize = false) {
    auto source = fs::u8path(row.file);
    std::string why;
    OptimizeOptions settings; int engine_mode = 0;
    if (optimize && !library_audit_optimize_settings(row, settings, engine_mode, why)) { toast(why); return; }
    char *pref = SDL_GetPrefPath("midway-bddtool", "studio");
    if (!pref) { toast("Cannot locate the application review folder"); return; }
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto destination = fs::u8path(pref) / "library-reviews" /
        (source.stem().u8string() + "-" + std::to_string(stamp));
    SDL_free(pref);
    std::string review_path;
    if (!prepare_library_audit_review(library_audit, row, destination.u8string(), review_path, why)) { toast(why); return; }
    Document review;
    if (!review.load(review_path, why)) { toast("Cannot open the scratch review: " + why); return; }
    add(std::move(review)); library_audit_open = false;
    if (optimize) {
        auto *opened = tab();
        opened->audit_review = true;
        opened->audit_review_file = row.file;
        opened->audit_review_mode = row.best_mode;
        opened->audit_review_saved = row.best_saved;
        opened->audit_review_preserve_saved = row.preserve_saved;
        opened->optimize_options = settings;
        opened->optimize_plan.reset();
        page = 3; opened->optimize_mode = 0;
        start_optimization(*opened, engine_mode);
        toast("Opened the frozen scratch copy and started the reported savings scan");
    } else toast("Opened a scratch copy; the audit evidence remains unchanged");
}
void library_audit_window() {
    if (!library_audit_open) return;
    auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(940.f, display.x - 30), std::min(680.f, display.y - 50)}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Appearing, {.5f, .5f});
    if (!ImGui::Begin("BDD library savings audit", &library_audit_open)) { ImGui::End(); return; }
    auto window_size = ImGui::GetWindowSize(), window_pos = ImGui::GetWindowPos();
    ImVec2 bounded_size{std::min(window_size.x, std::max(400.f, display.x - 20)),
                        std::min(window_size.y, std::max(300.f, display.y - 20))};
    if (bounded_size.x != window_size.x || bounded_size.y != window_size.y) ImGui::SetWindowSize(bounded_size);
    ImVec2 bounded_pos{std::clamp(window_pos.x, 10.f, std::max(10.f, display.x - bounded_size.x - 10)),
                       std::clamp(window_pos.y, 10.f, std::max(10.f, display.y - bounded_size.y - 10))};
    if (bounded_pos.x != window_pos.x || bounded_pos.y != window_pos.y) ImGui::SetWindowPos(bounded_pos);
    ImGui::TextWrapped("Read-only results from %s", library_audit.folder.c_str());
    ImGui::TextDisabled("Modeled static-art savings; estimates are alternatives per file and are not measured LOAD2 or runtime approval.");
    ImGui::Text("%zu files", library_audit.rows.size()); ImGui::SameLine();
    ImGui::TextColored(accent, "Active preserve: %.1f KiB", library_audit.preserve_total / 1024.0); ImGui::SameLine();
    ImGui::Text("Active best: %.1f KiB", library_audit.best_total / 1024.0); ImGui::SameLine();
    ImGui::Text("Incomplete: %zu", library_audit.incomplete);
    ImGui::SetNextItemWidth(280); ImGui::InputTextWithHint("##library-filter", "Filter file, scope or mode", library_audit_filter, sizeof library_audit_filter);
    ImGui::SameLine(); ImGui::Checkbox("Active inputs", &library_audit_active);
    ImGui::SameLine(); ImGui::Checkbox("Incomplete only", &library_audit_incomplete);
    std::string needle = library_audit_filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    int visible = 0;
    if (ImGui::BeginTable("library-audit-table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                          {0, std::max(180.f, ImGui::GetContentRegionAvail().y - 105)})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("BDD", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Preserve", ImGuiTableColumnFlags_WidthFixed, 82);
        ImGui::TableSetupColumn("Best", ImGuiTableColumnFlags_WidthFixed, 82);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("+ objects", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Palettes", ImGuiTableColumnFlags_WidthFixed, 62);
        ImGui::TableSetupColumn("Build relationship", ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < library_audit.rows.size(); ++i) {
            const auto &row = library_audit.rows[i];
            std::string searchable = row.file + " " + row.scope + " " + row.best_mode + " " + row.unverified_modes;
            std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if ((!needle.empty() && searchable.find(needle) == std::string::npos) ||
                (library_audit_active && !row.active) || (library_audit_incomplete && row.unverified_modes.empty())) continue;
            ++visible; ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            std::string label = row.file + "##audit-" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), library_audit_selected == (int)i, ImGuiSelectableFlags_SpanAllColumns))
                library_audit_selected = (int)i;
            auto amount = [](int64_t value) { return value < 0 ? std::string("—") : std::to_string(value); };
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(amount(row.preserve_saved).c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(amount(row.best_saved).c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(row.best_mode.empty() ? "—" : row.best_mode.c_str());
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", row.extra_objects);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d", row.palettes_after);
            ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(row.scope.c_str());
        }
        ImGui::EndTable();
    }
    if (!visible) ImGui::TextDisabled("No audit rows match these filters.");
    if (library_audit_selected >= 0 && library_audit_selected < (int)library_audit.rows.size()) {
        const auto &row = library_audit.rows[library_audit_selected];
        ImGui::Separator();
        ImGui::Text("%s", row.file.c_str()); ImGui::SameLine();
        if (!row.unverified_modes.empty()) ImGui::TextColored({1.f, .65f, .35f, 1.f}, "Incomplete: %s", row.unverified_modes.c_str());
        auto evidence = (fs::u8path(library_audit.folder) / fs::u8path(row.detail)).lexically_normal().u8string();
        auto frozen = fs::u8path(library_audit.folder) / "inputs" / fs::u8path(row.file);
        std::error_code exists_error; bool has_frozen = fs::is_regular_file(frozen, exists_error);
        ImGui::BeginDisabled(!has_frozen);
        if (ImGui::Button("Open in Optimize")) open_library_audit_stage(row, true);
        ImGui::SameLine();
        if (ImGui::Button("Open stage only")) open_library_audit_stage(row);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(has_frozen ? "Copy the frozen stage companions to app storage and open that disposable copy."
                                        : "This report has no frozen input copy for the selected row.");
        ImGui::SameLine();
        if (ImGui::Button("Copy evidence folder")) { ImGui::SetClipboardText(evidence.c_str()); toast("Copied audit evidence folder"); }
        ImGui::SameLine(); ImGui::TextDisabled("Table + palette growth: %+d / %+d bytes", row.extra_table_bytes, row.extra_palette_bytes);
    }
    ImGui::End();
}
