struct HelpShareSmoke {
    uint64_t revision = 0;
    std::shared_ptr<const AssetBank> assets;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640);
            revision = t.document.state().revision; assets = t.document.state().assets;
        }
        if (frame == 1 || frame == 2 || frame == 12 || frame == 13) move(app.help_menu_point);
        if (frame == 2 || frame == 3 || frame == 13 || frame == 14) io.AddMouseButtonEvent(0, frame == 2 || frame == 13);
        if (frame == 4 || frame == 5) move(app.about_open_point);
        if (frame == 5 || frame == 6) io.AddMouseButtonEvent(0, frame == 5);
        if (frame == 8 || frame == 9) move(app.about_close_point);
        if (frame == 9 || frame == 10) io.AddMouseButtonEvent(0, frame == 9);
        if (frame == 15 || frame == 16) move(app.share_open_point);
        if (frame == 16 || frame == 17) io.AddMouseButtonEvent(0, frame == 16);
        if (frame == 18) {
            std::snprintf(t.share_author, sizeof t.share_author, "UI test author");
            std::snprintf(t.share_license, sizeof t.share_license, "Synthetic fixture only");
        }
        if (frame == 20) app.start_share_bundle(t, (fs::u8path(app.smoke_dir) / "bundle").u8string());
        if (frame == 25 || frame == 26) move(app.share_copy_point);
        if (frame == 26 || frame == 27) io.AddMouseButtonEvent(0, frame == 26);
        if (frame == 29) std::snprintf(t.share_author, sizeof t.share_author, "Changed credits");
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty() && t.document.state().revision == revision && t.document.state().assets == assets;
        if (frame == 7) ok &= ImGui::IsPopupOpen("About bddtool");
        if (frame == 11) ok &= !ImGui::IsPopupOpen("About bddtool");
        if (frame == 18) ok &= ImGui::IsPopupOpen("Share stage");
        if (frame == 24) ok &= t.share_bundle && t.share_bundle->ready && app.share_submission_enabled;
        if (frame == 28) {
            const char *copied = ImGui::GetClipboardText();
            std::string text = copied ? copied : "";
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
            ok &= t.share_bundle && copied && t.share_bundle->markdown == text;
        }
        if (frame == 30) ok &= !app.share_submission_enabled;
        if (!ok) std::fprintf(stderr, "Help/share UI failed at %d: %s | %s\n", frame, app.error.c_str(), t.share_bundle ? t.share_bundle->error.c_str() : "no bundle yet");
        if (frame == 31 && ok) std::fprintf(stderr, "Help/share UI: About and Share menus, local bundle, page copy, unchanged document and stale credits passed at 900x640. No browser/posting used.\n");
        return ok;
    }
};
