struct Mk3LayoutSmoke {
    std::string source;
    State before;
    uint64_t applied = 0;
    bool synthetic = false;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 0; before = t.document.state();
            if (source.empty()) {
                synthetic = true;
                source = (fs::u8path(app.smoke_dir) / "MKBT.ASM").u8string();
                std::ofstream(source) << "test_mod\n\t.word 0,7,240,400,0,1200-scrrgt\n"
                    "\t.long calla\n\t.long rates\n\t.long lists\n\t.long bak1mods\n"
                    "\t.long PLANE1BMOD\n\t.word 0,0\n\t.long PLANE2BMOD\n\t.word 0,20\n\t.long PLANE3BMOD\n\t.word 0,30\n"
                    "\t.long center_x\n\t.long PLANE1BMOD,worldtlx1\n\t.long PLANE2BMOD,worldtlx2\n\t.long PLANE3BMOD,worldtlx3\n\t.long 0\n"
                    "\t.long FL_TEST\n\t.long TEST_P\n\t.word 2\n\t.long scrollx2\n\t.long skew_7\n"
                    "rates\n\t.long 0,0,0,0,0,>8000,>10000,>20000,>20000\n"
                    "lists\n\t.long baklst3,worldtlx3+16\n\t.long baklst2,worldtlx2+16\n\t.long -1,floor_code\n\t.long baklst1,worldtlx1+16\n\t.long 0\n"
                    "TEST_P\n\t.word 1,>7c00\n";
                std::ofstream(fs::u8path(app.smoke_dir) / "FL_TEST.BIN", std::ios::binary) << std::string(1800, 0);
            }
        }
        if (frame == 1 || frame == 2 || frame == 19 || frame == 20) move(app.layout_open_point);
        if (frame == 2 || frame == 3 || frame == 20 || frame == 21) io.AddMouseButtonEvent(0, frame == 2 || frame == 20);
        if (frame == 5) std::snprintf(t.layout_source, sizeof t.layout_source, "%s", source.c_str());
        if (frame == 6 || frame == 7) move(app.layout_review_point);
        if (frame == 7 || frame == 8) io.AddMouseButtonEvent(0, frame == 7);
        if (frame == 10 || frame == 11) move(app.layout_apply_point);
        if (frame == 11 || frame == 12) io.AddMouseButtonEvent(0, frame == 11);
        if (frame == 15) t.document.undo();
        if (frame == 17) t.document.redo();
        if (frame == 23 || frame == 24) move(app.layout_close_point);
        if (frame == 24 || frame == 25) io.AddMouseButtonEvent(0, frame == 24);
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty() && t.document.state().assets == before.assets;
        if (frame == 4) ok &= ImGui::IsPopupOpen("Game layout");
        if (frame == 9) ok &= t.mk3_layout.valid() && t.document.state().revision == before.revision;
        if (frame == 13) {
            applied = t.document.state().revision;
            ok &= applied != before.revision && !ImGui::IsPopupOpen("Game layout") && t.camera_preview &&
                t.camera.x == t.mk3_layout.start_x && t.camera.y == t.mk3_layout.start_y;
            ok &= t.floor.ready() && t.floor.mk3 && app.floor_visible(t);
            auto r = t.floor.rect(t.document.state(), t.camera);
            ok &= r.x == -400 && r.y + r.h == 254;
        }
        if (frame == 16) {
            ok &= t.document.state().revision == before.revision && t.document.state().start_y == before.start_y;
            if (synthetic) ok &= !app.floor_visible(t);
        }
        if (frame == 18) ok &= t.document.state().revision == applied && app.floor_visible(t);
        if (frame == 22) ok &= ImGui::IsPopupOpen("Game layout") && t.mk3_layout.revision != t.document.state().revision;
        if (frame == 26) ok &= !ImGui::IsPopupOpen("Game layout") && t.document.state().revision == applied;
        if (frame == 26) {
            t.show_floor = false; ok &= !app.floor_visible(t); t.show_floor = true;
            t.source = true; ok &= !app.floor_visible(t); t.source = false;
            t.solo = 0; ok &= !app.floor_visible(t); t.solo = -1;
            std::string saved = t.layout_source;
            std::snprintf(t.layout_source, sizeof t.layout_source, "%s", "changed/MKBT.ASM");
            ok &= !app.floor_visible(t);
            std::snprintf(t.layout_source, sizeof t.layout_source, "%s", saved.c_str());
        }
        if (!ok) std::fprintf(stderr, "MK3 layout UI failed at %d: %s / %s / %s\n", frame, app.error.c_str(), t.mk3_layout.error.c_str(), t.floor.notice.c_str());
        if (frame == 29 && ok) std::fprintf(stderr, "MK3 layout UI: review, apply, frame positioning, floor alignment/visibility, undo/redo, stale review and document isolation passed at 900x640.\n");
        return ok;
    }
};
