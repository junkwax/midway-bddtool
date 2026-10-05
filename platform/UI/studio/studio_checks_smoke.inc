struct ChecksSmoke {
    ObjectId target = 0;
    bool was_dirty = false;
    std::shared_ptr<const AssetBank> original;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640);
            auto &s = const_cast<State &>(t.document.state());
            auto bank = std::make_shared<AssetBank>(*s.assets);
            auto &im = bank->data.images.front(); im.w = 252; im.pix.resize(size_t(im.w) * im.h, 1);
            s.assets = bank; original = bank; s.planes.back().bound = false;
            target = s.objects.front().id; was_dirty = t.document.dirty(); app.page = 2;
        }
        if (frame == 2 || frame == 3) move(app.check_locate_point);
        if (frame == 3 || frame == 4) io.AddMouseButtonEvent(0, frame == 3);
        if (frame == 6) { app.page = 2; t.check_group = 3; }
        if (frame == 8 || frame == 9) move(app.check_artwork_point);
        if (frame == 9 || frame == 10) io.AddMouseButtonEvent(0, frame == 9);
        if (frame == 12) { app.page = 2; t.check_group = 0; t.check_severity = 2; }
        if (frame == 14) {
            std::snprintf(t.check_filter.InputBuf, sizeof t.check_filter.InputBuf, "no-matching-issue"); t.check_filter.Build();
        }
        if (frame == 17) { t.check_filter.Clear(); t.check_severity = 0; }
        if (frame == 19 || frame == 20) move(app.check_report_point);
        if (frame == 20 || frame == 21) io.AddMouseButtonEvent(0, frame == 20);
        if (frame == 23) t.document.erase({target});
        if (frame == 25) t.document.undo();
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty() && t.document.state().assets == original;
        if (frame == 1) ok &= t.info_errors == 1 && t.info_warnings >= 1;
        if (frame == 5) {
            ok &= app.page == 0 && t.selected == std::vector<ObjectId>{target} && !t.camera_preview && !t.source && !t.focus_object;
            auto items = t.document.scene();
            for (const auto &item : items) if (item.id == target) {
                Point center = t.view.to_screen({item.rect.x + item.rect.w/2, item.rect.y + item.rect.h/2},
                                               {app.canvas_rect.x, app.canvas_rect.y});
                ok &= app.canvas_rect.contains(center);
            }
        }
        if (frame == 7) ok &= app.check_visible_count == 1;
        if (frame == 11) ok &= app.page == 1 && t.asset == 0 && !t.reveal_asset;
        if (frame == 13) ok &= app.check_visible_count == t.info_warnings;
        if (frame == 15) ok &= app.check_visible_count == 0;
        if (frame == 22) {
            const char *clipboard = ImGui::GetClipboardText();
            ok &= clipboard && std::string(clipboard).find("250-pixel") != std::string::npos;
        }
        if (frame == 24 || frame == 26) ok &= t.check_revision == t.document.state().revision;
        if (frame == 26) ok &= t.document.dirty() == was_dirty;
        if (!ok) std::fprintf(stderr, "Checks UI failed at frame %d: %s (errors %d, warnings %d, visible %d, page %d)\n",
            frame, app.error.c_str(), t.info_errors, t.info_warnings, app.check_visible_count, app.page);
        if (frame == 27 && ok) std::fprintf(stderr, "Checks UI: filters, locate framing, artwork navigation, report and undo cache passed at 900x640.\n");
        return ok;
    }
};
