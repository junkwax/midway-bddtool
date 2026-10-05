struct ExportChecksSmoke {
    fs::path root, package;
    bool blocked = false;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 2; t.check_group = 6;
            root = fs::absolute(fs::u8path(app.smoke_dir) / "checkout"); package = fs::absolute(fs::u8path(app.smoke_dir) / "review");
            fs::create_directories(root / "data"); fs::create_directories(package / "data");
            std::ofstream(root / "data/STAGE.BDB", std::ios::binary) << "before";
            std::ofstream(package / "data/STAGE.BDB", std::ios::binary) << "after";
            std::ofstream(root / "data/STAGE.LOD", std::ios::binary) << "BBB> STAGE\n";
            auto p = std::make_unique<GameExport>();
            p->root = fs::canonical(root).u8string(); p->folder = fs::canonical(package).u8string();
            p->revision = t.document.state().revision; p->report = "Synthetic export used only for read-only freshness UI checks.";
            p->files.push_back({"data/STAGE.BDB", "before", "after", true});
            p->dependencies.push_back({"data/STAGE.LOD", "BBB> STAGE\n", "", true});
            std::snprintf(t.game_root, sizeof t.game_root, "%s", p->root.c_str()); t.game_label[0] = 0;
            t.game_export = std::move(p);
        }
        if (frame == 2 || frame == 3 || frame == 8) move(app.export_check_point);
        if (frame == 3 || frame == 4 || frame == 8 || frame == 9) io.AddMouseButtonEvent(0, frame == 3 || frame == 8);
        if (frame == 7) std::ofstream(root / "data/STAGE.LOD", std::ios::binary) << "XXX> STAGE\n";
        if (frame == 13 || frame == 14) move(app.export_review_point);
        if (frame == 14 || frame == 15) io.AddMouseButtonEvent(0, frame == 14);
        if (frame == 17) t.document.move({t.document.state().objects.front().id}, 1, 0);
        if (frame == 20) t.document.undo();
        if (frame == 21) {
            t.game_export->applied = true;
            std::ofstream(root / "data/STAGE.BDB", std::ios::binary) << "after";
            blocked = !app.start_checked_game_build(t) && !t.game_build.started() && app.error.find("STAGE.LOD") != std::string::npos;
            app.error.clear();
        }
        if (frame == 23) {
            std::ofstream(root / "data/STAGE.LOD", std::ios::binary) << "BBB> STAGE\n";
            app.start_export_check(t);
        }
        if (frame == 27) std::snprintf(t.game_label, sizeof t.game_label, "OTHER");
        if (frame == 30) t.game_label[0] = 0;
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty();
        if (frame == 6 || frame == 26) ok &= t.export_check && t.export_check->matches() && app.export_check_current(t);
        if (frame == 11) ok &= t.export_check && !t.export_check->matches() && app.check_visible_count == 1 && t.info_errors == 1;
        if (frame == 16) ok &= !t.jump_to_export && app.page == 2 && app.export_section_y > 100 && app.export_section_y < 180;
        if (frame == 18 || frame == 28) ok &= !app.export_check_current(t) && app.check_visible_count == 1 && t.info_errors == 0;
        if (frame == 20) ok &= app.export_check_current(t) && t.info_errors == 1;
        if (frame == 22) ok &= blocked;
        if (frame == 31) ok &= app.export_check_current(t) && t.export_check->matches();
        if (!ok) std::fprintf(stderr, "Export checks UI failed at %d: %s (visible %d, errors %d)\n", frame, app.error.c_str(), app.check_visible_count, t.info_errors);
        if (frame == 32 && ok) std::fprintf(stderr, "Export checks UI: byte recheck, dependency findings, review navigation, edit/undo/context freshness and blocked build passed. No game build launched.\n");
        return ok;
    }
};
