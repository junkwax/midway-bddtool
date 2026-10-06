struct BackgroundSmoke {
    State original;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 0;
            original = t.document.state();
        }
        if (frame == 1 || frame == 2 || frame == 12 || frame == 13) move(app.background_open_point);
        if (frame == 2 || frame == 3 || frame == 13 || frame == 14) io.AddMouseButtonEvent(0, frame == 2 || frame == 13);
        if (frame == 5 || frame == 16) {
            t.backdrop_enabled = true; t.backdrop_rgb[0] = 1; t.backdrop_rgb[1] = 0; t.backdrop_rgb[2] = 1;
            app.canvas_rgb[0] = .1f; app.canvas_rgb[1] = .1f; app.canvas_rgb[2] = .1f;
        }
        if (frame == 8 || frame == 9) move(app.background_cancel_point);
        if (frame == 9 || frame == 10) io.AddMouseButtonEvent(0, frame == 9);
        if (frame == 18 || frame == 19) move(app.background_apply_point);
        if (frame == 19 || frame == 20) io.AddMouseButtonEvent(0, frame == 19);
        if (frame == 23) t.document.undo();
        if (frame == 25) t.document.redo();
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); const auto &s = t.document.state();
        bool ok = app.error.empty() && s.assets == original.assets;
        if (frame == 4 || frame == 15) ok &= t.backdrop_preview;
        if (frame == 6 || frame == 17)
            ok &= app.displayed_backdrop(t) == 0x7c1f && s.revision == original.revision && s.backdrop == original.backdrop;
        if (frame == 11) ok &= !t.backdrop_preview && s.revision == original.revision && s.backdrop == original.backdrop && app.canvas_rgb[0] == .1f;
        if (frame == 21 || frame == 26) ok &= !t.backdrop_preview && s.backdrop == 0x7c1f && s.revision != original.revision;
        if (frame == 24) ok &= s.backdrop == original.backdrop && s.revision == original.revision;
        if (!ok) std::fprintf(stderr, "Background UI failed at frame %d: %s\n", frame, app.error.c_str());
        if (frame == 27 && ok) std::fprintf(stderr, "Background UI: open, live draft, canvas isolation, Cancel, Apply, undo/redo passed at 900x640.\n");
        return ok;
    }
};
