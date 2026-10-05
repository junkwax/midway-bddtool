struct BatchImportSmoke {
    std::string folder;
    std::shared_ptr<const AssetBank> before, imported;
    void input(App &app, int frame) {
        auto &io = ImGui::GetIO(); io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        auto &t = *app.tab();
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640);
            folder = (fs::u8path(app.smoke_dir) / "input").u8string(); fs::create_directories(fs::u8path(folder));
            const uint8_t rgba[] = {255,0,0,255, 0,0,0,0, 0,255,0,255, 255,0,0,255};
            for (auto name : {"a.png", "b.png"}) {
                auto p = (fs::u8path(folder) / name).u8string();
                if (!stbi_write_png(p.c_str(), 4, 1, 4, rgba, 16)) app.error = "Cannot write fixture";
            }
            std::ofstream(fs::u8path(folder) / "z-bad.png") << "invalid png";
        }
        if (frame == 1) { app.page = 1; before = t.document.state().assets; app.open_import_folder(folder); }
        if (frame == 2 || frame == 3) move(app.batch_review_point);
        if (frame == 3 || frame == 4) io.AddMouseButtonEvent(0, frame == 3);
        if (frame == 6 || frame == 7) move(app.batch_import_point);
        if (frame == 7 || frame == 8) io.AddMouseButtonEvent(0, frame == 7);
        if (frame == 10) t.document.undo();
        if (frame == 11) t.document.redo();
        if (frame == 12) {
            imported = t.document.state().assets; app.open_import_folder(folder);
            for (auto &row : t.batch_files) row.selected = true;
        }
        if (frame == 14 || frame == 15) move(app.batch_review_point);
        if (frame == 15 || frame == 16) io.AddMouseButtonEvent(0, frame == 15);
        if (frame == 18 || frame == 19) move(app.batch_cancel_point);
        if (frame == 19 || frame == 20) io.AddMouseButtonEvent(0, frame == 19);
        if (frame == 22) t.document.save((fs::u8path(app.smoke_dir) / "imported.BDB").u8string(), app.error);
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty();
        if (frame == 5) ok &= t.batch_preview && t.document.state().assets == before &&
            t.batch_preview->data.images.size() == before->data.images.size() + 2 &&
            t.batch_preview->data.palettes.size() == before->data.palettes.size() + 1;
        if (frame == 9 || frame == 11) ok &= t.document.state().assets->data.images.size() == before->data.images.size() + 2;
        if (frame == 10) ok &= t.document.state().assets == before;
        if (frame == 17) ok &= !t.batch_preview && !t.batch_error.empty() && t.document.state().assets == imported;
        if (frame == 21) ok &= t.document.state().assets == imported && !ImGui::IsPopupOpen("Import artwork folder");
        if (frame == 23) ok &= !t.document.dirty();
        if (!ok) std::fprintf(stderr, "Batch import UI failed at %d: %s | %s\n", frame, app.error.c_str(), t.batch_error.c_str());
        if (frame == 24 && ok) std::fprintf(stderr, "Batch import: review, exact palette reuse, atomic import, undo/redo, failed review, cancel and verified Save passed at 900x640.\n");
        return ok;
    }
};
