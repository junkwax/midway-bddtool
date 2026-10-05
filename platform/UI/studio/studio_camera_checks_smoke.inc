struct CameraChecksSmoke {
    std::shared_ptr<const AssetBank> bank;
    uint64_t revision = 0;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 2;
            t.check_group = 5; t.camera_check_options = {0, 1600, 0, 0, 16, 358};
            bank = t.document.state().assets; revision = t.document.state().revision;
        }
        if (frame == 2 || frame == 3) move(app.camera_scan_point);
        if (frame == 3 || frame == 4) io.AddMouseButtonEvent(0, frame == 3);
        if (frame == 8 || frame == 9) move(app.camera_jump_point);
        if (frame == 9 || frame == 10) io.AddMouseButtonEvent(0, frame == 9);
        if (frame == 12) { app.page = 2; t.document.move({t.document.state().objects.front().id}, 1, 0); }
        if (frame == 16) t.document.undo();
        if (frame == 20) ++t.camera_check_options.step;
        if (frame == 23) --t.camera_check_options.step;
        if (frame == 25 || frame == 26) move(app.check_report_point);
        if (frame == 26 || frame == 27) io.AddMouseButtonEvent(0, frame == 26);
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty() && t.document.state().assets == bank;
        if (frame == 6) ok &= t.camera_check && t.camera_check->complete && t.camera_check->samples == 101 && app.check_visible_count >= 2;
        if (frame == 11) ok &= t.camera_check && app.page == 0 && t.camera_preview &&
            t.camera.x == t.camera_check->object_camera.x && t.camera.y == t.camera_check->object_camera.y;
        if (frame == 13 || frame == 21) ok &= app.check_visible_count == 0;
        if (frame == 17 || frame == 24) ok &= t.camera_check && camera_check_current(*t.camera_check, t.document.state(), t.camera_check_options) && app.check_visible_count >= 2;
        if (frame == 28) {
            const char *text = ImGui::GetClipboardText();
            ok &= text && std::string(text).find("101 samples") != std::string::npos &&
                std::string(text).find("object pressure") != std::string::npos && t.document.state().revision == revision;
        }
        if (!ok) std::fprintf(stderr, "Camera checks UI failed at frame %d (page %d, visible %d): %s\n", frame, app.page, app.check_visible_count, app.error.c_str());
        if (frame == 29 && ok) std::fprintf(stderr, "Camera checks UI: scan, camera navigation, edit/undo freshness, range freshness and report passed at 900x640.\n");
        return ok;
    }
};
