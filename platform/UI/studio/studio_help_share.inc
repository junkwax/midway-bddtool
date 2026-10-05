    void open_link(const std::string &url) {
        if (SDL_OpenURL(url.c_str()) != 0) error = std::string("Could not open the link: ") + SDL_GetError();
    }
    void open_share_folder(const std::string &folder) {
        auto path = fs::absolute(fs::u8path(folder)).generic_u8string();
        std::string encoded;
        const char *hex = "0123456789ABCDEF";
        for (unsigned char c : path) {
            if (c == '/' || c == ':' || c == '-' || c == '_' || c == '.' ||
                (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) encoded += char(c);
            else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
        }
        open_link(std::string(path.front() == '/' ? "file://" : "file:///") + encoded);
    }
    ShareCredits share_credits(const Tab &t) const {
        return {t.share_author, t.share_description, t.share_sources, t.share_license, t.share_tested, t.share_name};
    }
    void start_share_bundle(Tab &t, const std::string &folder) {
        if (t.share_job.valid()) return;
        cancel_gesture();
        auto document = t.document; auto credits = share_credits(t);
        t.share_job = std::async(std::launch::async, [document, credits, folder] {
            return build_stage_share(document, credits, folder);
        });
    }
    void help_share_dialogs() {
        if (about_requested) { ImGui::OpenPopup("About bddtool"); about_requested = false; }
        asset_modal_size(560, 360);
        if (ImGui::BeginPopupModal("About bddtool", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextColored(accent, "bddtool");
            ImGui::Text("Version %s", BDDVIEW_APP_VERSION);
            ImGui::Text("Built %s at %s", __DATE__, __TIME__);
            ImGui::Text("%zu-bit build", sizeof(void*) * 8);
#ifdef _MSC_VER
            ImGui::Text("Compiler: MSVC %d", _MSC_VER);
#elif defined(__clang__)
            ImGui::Text("Compiler: Clang %s", __clang_version__);
#elif defined(__GNUC__)
            ImGui::Text("Compiler: GCC %s", __VERSION__);
#endif
            SDL_version runtime; SDL_GetVersion(&runtime);
            ImGui::Text("SDL %d.%d.%d | Dear ImGui %s", runtime.major, runtime.minor, runtime.patch, IMGUI_VERSION);
            ImGui::TextWrapped("Background artwork and layout editor for Midway BDB/BDD files. This describes the editor build; game build results are in Build & Check.");
            ImGui::Separator();
            if (ImGui::Button("Copy build information")) {
                std::string info = std::string("bddtool ") + BDDVIEW_APP_VERSION + "\nBuilt " + __DATE__ + " " + __TIME__ +
                    "\n" + std::to_string(sizeof(void*) * 8) + "-bit\nSDL " + std::to_string(runtime.major) + "." +
                    std::to_string(runtime.minor) + "." + std::to_string(runtime.patch) + "\nDear ImGui " + IMGUI_VERSION;
                ImGui::SetClipboardText(info.c_str());
            }
            if (ImGui::Button("GitHub wiki")) open_link(wiki_url);
            ImGui::SameLine(); if (ImGui::Button("Project / releases")) open_link(std::string(project_url) + "/releases");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            about_close_point = item_center();
            ImGui::EndPopup();
        }
        for (auto &tab_ptr : tabs) {
            auto &t = *tab_ptr;
            if (t.share_job.valid() && t.share_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try { t.share_bundle = std::make_unique<ShareBundle>(t.share_job.get()); t.share_show_result = t.share_bundle->ready; }
                catch (const std::exception &e) { t.share_bundle = std::make_unique<ShareBundle>(); t.share_bundle->error = e.what(); }
            }
        }
        if (!tab()) return;
        auto &t = *tab();
        if (t.share_requested) {
            if (!t.share_name[0]) std::snprintf(t.share_name, sizeof t.share_name, "%s", t.document.state().name.c_str());
            ImGui::OpenPopup("Share stage"); t.share_requested = false;
        }
        asset_modal_size(720, 590);
        if (!ImGui::BeginPopupModal("Share stage", nullptr, ImGuiWindowFlags_NoResize)) return;
        ImGui::BeginChild("share-body", ImVec2(0, -38));
        ImGui::Text("Share %s", t.document.state().name.c_str());
        ImGui::TextWrapped("Create a local ZIP, review it, then attach it to a GitHub stage submission. A maintainer can publish accepted submissions to the wiki. Nothing is uploaded by this dialog.");
        bool busy = t.share_job.valid();
        share_submission_enabled = false;
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(-170); ImGui::InputText("Bundle / wiki name", t.share_name, sizeof t.share_name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Letters, digits and underscores. Names the page and files; keeps internal stage/module identifiers.");
        ImGui::SetNextItemWidth(-170); ImGui::InputText("Author", t.share_author, sizeof t.share_author);
        ImGui::InputTextMultiline("Description", t.share_description, sizeof t.share_description, ImVec2(-170, 42));
        ImGui::InputTextMultiline("Credits / sources", t.share_sources, sizeof t.share_sources, ImVec2(-170, 42));
        ImGui::SetNextItemWidth(-170); ImGui::InputText("Use / license", t.share_license, sizeof t.share_license);
        ImGui::SetNextItemWidth(-170); ImGui::InputText("Game testing notes", t.share_tested, sizeof t.share_tested);
        ImGui::TextWrapped("Includes the current applied edits, BDB/BDD, layout/metadata, static previews, every image and a wiki page. Hidden and unplaced artwork is included. Review redistribution rights and testing notes before posting. Runtime IMG animations and game assembly are not bundled.");
        if (ImGui::Button("Build local bundle...")) {
            char parent[1024] = {};
            if (folder_dialog_open("Choose parent folder for the stage bundle", parent, sizeof parent)) {
                auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
                auto folder = fs::u8path(parent) / ("stage-share-" + std::to_string(stamp));
                start_share_bundle(t, folder.u8string());
            }
        }
        share_build_point = item_center();
        ImGui::EndDisabled();
        if (busy) ImGui::TextDisabled("Building local snapshot and previews...");
        if (t.share_bundle) {
            const auto &bundle = *t.share_bundle;
            if (!bundle.error.empty()) ImGui::TextWrapped("%s", bundle.error.c_str());
            if (bundle.ready) {
                bool current = bundle.revision == t.document.state().revision && bundle.assets == t.document.state().assets && bundle.credits == share_credits(t);
                ImGui::TextWrapped("ZIP: %s", bundle.zip.c_str());
                if (!current) ImGui::TextColored(accent, "Document or credits changed. Build a new bundle before submitting.");
                if (ImGui::Button("Open bundle folder")) open_share_folder(bundle.folder);
                ImGui::SameLine(); if (ImGui::Button("Copy wiki page")) ImGui::SetClipboardText(bundle.markdown.c_str());
                share_copy_point = item_center();
                ImGui::SameLine(); if (ImGui::Button("Copy ZIP path")) ImGui::SetClipboardText(bundle.zip.c_str());
                share_submission_enabled = current && !busy;
                ImGui::BeginDisabled(!share_submission_enabled);
                if (ImGui::Button("Open GitHub submission draft")) open_link(bundle.submission_url);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Attach the ZIP in the browser, review the draft, then submit it yourself. The editor does not sign in or post on your behalf.");
                if (t.share_show_result) { ImGui::SetScrollHereY(1); t.share_show_result = false; }
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        share_close_point = item_center();
        ImGui::EndPopup();
    }
