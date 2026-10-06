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
struct FloorLibrarySmoke {
    std::string path, runtime_source;
    std::string switch_target;
    bool synthetic = false, context_isolated = false;
    State original;
    std::shared_ptr<const AssetBank> runtime, first, copied;
    void input(App &app, int frame) {
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            SDL_SetWindowSize(app.window, 900, 640); app.page = 0;
            if (!runtime_source.empty()) {
                std::snprintf(t.layout_source, sizeof t.layout_source, "%s", runtime_source.c_str());
                t.mk3_layout = read_mk3_layout(t.document, runtime_source); app.apply_layout_review(t);
            }
            original = t.document.state();
            if (path.empty()) {
                synthetic = true;
                path = (fs::u8path(app.smoke_dir) / "MKFLOORS.IMG").u8string();
                ImgLibHeaderDisk h{}; h.imgcnt = 2; h.palcnt = 4; h.version = 0x500; h.temp = 0xabcd; h.oset = sizeof h;
                ImgImageDisk a{}, b{}; ImgPaletteDisk p{}; p.numc = 2;
                std::snprintf(a.name, sizeof a.name, "FLOOR_A"); std::snprintf(b.name, sizeof b.name, "FLOOR_B");
                a.w = b.w = 8; a.h = b.h = 2; a.palnum = b.palnum = 3;
                p.oset = sizeof h + sizeof a + sizeof b + sizeof p; a.oset = p.oset + 4; b.oset = a.oset + 16;
                std::ofstream out(path, std::ios::binary);
                out.write((const char *)&h, sizeof h); out.write((const char *)&a, sizeof a);
                out.write((const char *)&b, sizeof b); out.write((const char *)&p, sizeof p);
                out.write("\0\0\0\x7c", 4); out << std::string(16, char(1)) << std::string(16, char(0));
                out.close();
                std::ifstream input(path, std::ios::binary);
                std::string bytes{std::istreambuf_iterator<char>(input), {}};
                std::ofstream(fs::u8path(app.smoke_dir) / "MKFLOOR2.IMG", std::ios::binary) << bytes;
            }
            app.load_floor_library(t, path);
            if (!t.floor.ready()) { t.floor = load_floor_library_entry(t.floor_library, 0, {}); t.floor_root = app.floor_source_key(t); }
            runtime = t.floor.layout.artwork.assets; t.floor_library_open = true;
        }
        if (frame == 1) { app.select_floor_variant(t, 0); first = t.floor_alternative.layout.artwork.assets; }
        if (frame == 2 || frame == 3) move(app.floor_library_next_point);
        if (frame == 3 || frame == 4) io.AddMouseButtonEvent(0, frame == 3);
        if (frame == 6 || frame == 7) move(app.floor_library_restore_point);
        if (frame == 7 || frame == 8) io.AddMouseButtonEvent(0, frame == 7);
        if (frame == 9) app.select_floor_variant(t, 0);
        if (frame == 10 || frame == 11) move(app.floor_library_art_point);
        if (frame == 11 || frame == 12) io.AddMouseButtonEvent(0, frame == 11);
        if (frame == 14 || frame == 15) move(app.floor_add_point);
        if (frame == 15 || frame == 16) io.AddMouseButtonEvent(0, frame == 15);
        if (frame == 18) t.document.undo();
        if (frame == 20) t.document.redo();
        if (frame == 22) {
            std::string source = t.layout_source;
            std::snprintf(t.layout_source, sizeof t.layout_source, "%s", "changed/MKBT.ASM");
            context_isolated = app.floor_pixel_comparison(t).match == FloorMatch::Unavailable;
            std::snprintf(t.layout_source, sizeof t.layout_source, "%s", source.c_str());
        }
        if (frame == 23) app.load_floor_library(t, path + ".missing");
        if (frame == 25) { t.floor_library_error.clear(); app.select_floor_variant(t, 1); t.floor_library_open = true; }
        if (frame == 26 && t.floor_libraries.paths.size() > 1) switch_target = t.floor_libraries.paths[1];
        if (!switch_target.empty()) {
            if (frame == 26 || frame == 27) move(app.floor_library_selector_point);
            if (frame == 27 || frame == 28) io.AddMouseButtonEvent(0, frame == 27);
            if (frame == 29 || frame == 30) move(app.floor_library_second_point);
            if (frame == 30 || frame == 31) io.AddMouseButtonEvent(0, frame == 30);
            if (frame == 32) app.select_floor_variant(t, 0);
        }
    }
    bool check(App &app, int frame) {
        auto &t = *app.tab(); bool ok = app.error.empty();
        if (frame < 16 || frame == 19) ok &= t.document.state().assets == original.assets && t.document.state().revision == original.revision;
        if (frame == 5) ok &= t.floor_use_library && t.floor_choice == 1 && t.floor_alternative.layout.artwork.assets != first && app.floor_visible(t);
        if (synthetic && frame == 1) ok &= app.floor_pixel_comparison(t).match == FloorMatch::Exact && t.floor_libraries.paths.size() == 2;
        if (synthetic && frame == 5) ok &= app.floor_pixel_comparison(t).match == FloorMatch::DifferentPixels && app.floor_pixel_comparison(t).different_pixels == 16;
        if (frame == 8) ok &= !t.floor_use_library && app.active_floor(t).layout.artwork.assets == runtime && app.floor_visible(t);
        if (frame == 13) ok &= ImGui::IsPopupOpen("Floor") && t.floor_draft.assets == t.floor_alternative.layout.artwork.assets;
        if (frame == 17 || frame == 21) ok &= t.document.state().objects.size() > original.objects.size() && !ImGui::IsPopupOpen("Floor");
        if (frame == 21) copied = t.document.state().assets;
        if (frame == 22) ok &= context_isolated;
        if (frame == 24) ok &= !t.floor_library_error.empty() && t.floor_library.path == path && t.floor_alternative.ready();
        if (frame != 23 && frame != 24) ok &= t.floor_library_error.empty();
        if (frame == 31 && !switch_target.empty()) ok &= t.floor_library.path == switch_target && !t.floor_use_library && t.floor_choice == -1 && t.document.state().assets == copied;
        if (!ok) std::fprintf(stderr, "Floor library UI failed at %d: %s / %s / %s | library %s expected %s, choice %d\n", frame, app.error.c_str(), t.floor_error.c_str(), t.floor_library_error.c_str(), t.floor_library.path.c_str(), switch_target.c_str(), t.floor_choice);
        if (frame == 32 && ok) std::fprintf(stderr, "Floor library UI: cycle, restore runtime, nearby library switching, pixel comparison, preview isolation, editable copy, undo/redo and failed-load isolation passed at 900x640.\n");
        return ok;
    }
};
