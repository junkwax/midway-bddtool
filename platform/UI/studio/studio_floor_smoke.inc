struct FloorSmoke {
    std::shared_ptr<const AssetBank> original, applied;
    size_t objects = 0, planes = 0;
    std::string png;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 0; t.asset = 2;
            original = t.document.state().assets; objects = t.document.state().objects.size(); planes = t.document.state().planes.size();
            png = (fs::u8path(app.smoke_dir) / "floor-source.png").u8string();
            std::vector<uint8_t> rgba(16 * 8 * 4, 255);
            stbi_write_png(png.c_str(), 16, 8, 4, rgba.data(), 16 * 4);
        }
        if (frame == 1 || frame == 2 || frame == 19 || frame == 20) move(app.floor_open_point);
        if (frame == 2 || frame == 3 || frame == 20 || frame == 21) io.AddMouseButtonEvent(0, frame == 2 || frame == 20);
        if (frame == 5 || frame == 6) move(app.floor_asset_point);
        if (frame == 6 || frame == 7) io.AddMouseButtonEvent(0, frame == 6);
        if (frame == 8) { t.floor_width = 369; t.floor_x = -8; t.floor_y = 201; }
        if (frame == 10 || frame == 11) move(app.floor_add_point);
        if (frame == 11 || frame == 12) io.AddMouseButtonEvent(0, frame == 11);
        if (frame == 15) t.document.undo();
        if (frame == 17) t.document.redo();
        if (frame == 23) app.floor_from_file(t, png);
        if (frame == 25 || frame == 26) move(app.floor_close_point);
        if (frame == 26 || frame == 27) io.AddMouseButtonEvent(0, frame == 26);
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty() && t.floor_error.empty();
        if (frame == 4) ok &= ImGui::IsPopupOpen("Floor");
        if (frame == 8) ok &= t.floor_draft.assets && t.document.state().assets == original;
        if (frame == 13) {
            ok &= !ImGui::IsPopupOpen("Floor") && t.document.state().objects.size() == objects + 3 && t.document.state().planes.size() == planes + 1;
            applied = t.document.state().assets;
        }
        if (frame == 16) ok &= t.document.state().assets == original && t.document.state().objects.size() == objects;
        if (frame == 18) ok &= t.document.state().assets == applied && t.document.state().objects.size() == objects + 3;
        if (frame == 24) ok &= t.floor_draft.assets && t.floor_draft.assets->data.images.front().w == 16 && t.document.state().assets == applied;
        if (frame == 28) ok &= !ImGui::IsPopupOpen("Floor") && t.document.state().assets == applied;
        if (!ok) std::fprintf(stderr, "Floor UI failed at %d: %s / %s\n", frame, app.error.c_str(), t.floor_error.c_str());
        if (frame == 29 && ok) std::fprintf(stderr, "Floor UI: dialog, selected artwork, repeat placement, undo/redo, PNG draft and Cancel passed at 900x640.\n");
        return ok;
    }
};
