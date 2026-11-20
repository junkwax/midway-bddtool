struct AssetTraySmoke {
    Point grip;

    void input(App &app, int frame) {
        auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        auto move = [&](Point at) { io.AddMousePosEvent(float(at.x), float(at.y)); };
        if (frame == 1) { grip = app.smoke_tray_grip; move(grip); }
        if (frame == 3) move({grip.x, grip.y - 132});
        if (frame == 6) move(app.smoke_asset_tiles[1]);
        if (frame == 9) {
            io.AddKeyEvent(ImGuiMod_Shift, true);
            move(app.smoke_asset_tiles[3]);
        }
        if (frame == 12) {
            io.AddKeyEvent(ImGuiMod_Shift, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            move(app.smoke_asset_tiles[2]);
        }
        if (frame == 15) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        if (frame == 17) move(app.smoke_sheet_button);
        if (frame == 22) move(app.smoke_sheet_export);
        if (frame == 2 || frame == 7 || frame == 10 || frame == 13 || frame == 18 || frame == 23)
            io.AddMouseButtonEvent(0, true);
        if (frame == 4 || frame == 8 || frame == 11 || frame == 14 || frame == 19 || frame == 24)
            io.AddMouseButtonEvent(0, false);
    }

    bool check(App &app, int frame) {
        const auto &t = *app.tab();
        bool ok = true;
        if (frame == 5) ok = grip.y - app.smoke_tray_grip.y >= 120;
        if (frame == 11) ok = t.selected_assets == std::set<int>{1, 2, 3};
        if (frame == 14) ok = t.selected_assets == std::set<int>{1, 3};
        if (frame == 20) ok = t.sheet_scope == 0 && t.sheet_image_ids == std::vector<int>{1, 3};
        if (frame == 25) {
            auto png = fs::u8path(app.smoke_dir) / "COURTYARD_artwork_sheet.png";
            int w = 0, h = 0, channels = 0;
            ok = stbi_info(png.u8string().c_str(), &w, &h, &channels) &&
                 w == 264 && h == 164 && channels == 4 && !fs::exists(png.replace_extension(".json"));
        }
        if (!ok) std::fprintf(stderr, "Asset tray UI failed at frame %d: %s\n", frame, app.error.c_str());
        if (frame == 29) std::fprintf(stderr, "Asset tray UI: resize, range and toggle selection, and PNG sheet export passed.\n");
        return ok;
    }
};
