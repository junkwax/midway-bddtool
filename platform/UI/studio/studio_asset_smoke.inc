struct AssetToolsSmoke {
    std::string img_path;
    std::shared_ptr<const AssetBank> before_block, before_palette, before_import, before_png;
    int brush = 0, image = -1, palette = 0;
    uint16_t color = 0;
    void input(App &app, int frame) {
        if (!app.tab()) return;
        auto &t = *app.tab(); auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false; io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent((float)p.x, (float)p.y); };
        if (frame == 0) SDL_SetWindowSize(app.window, 1000, 720);
        if (frame == 1) {
            app.page = 1; app.open_block_editor(t); before_block = t.document.state().assets;
            image = t.edit_image; brush = t.document.image(image)->pix[0] ? 0 : 1; t.edit_color = brush;
        }
        if (frame == 2 || frame == 3) move(app.block_paint_point);
        if (frame == 3) io.AddMouseButtonEvent(0, true);
        if (frame == 4) move({app.block_paint_point.x + 4 * t.block_zoom, app.block_paint_point.y});
        if (frame == 5) io.AddMouseButtonEvent(0, false);
        if (frame == 6) move(app.block_apply_point);
        if (frame == 7 || frame == 8) io.AddMouseButtonEvent(0, frame == 7);
        if (frame == 10) t.document.undo();
        if (frame == 11) {
            before_palette = t.document.state().assets; app.open_palette_editor(t); palette = t.edit_palette;
        }
        if (frame == 12) {
            color = t.palette_draft.rgb555[1] ^ 31;
            t.palette_draft.rgb555[1] = color;
            t.palette_draft.argb[1] = bdd_core_rgb555_to_argb(color);
        }
        if (frame == 13) move(app.palette_apply_point);
        if (frame == 14 || frame == 15) io.AddMouseButtonEvent(0, frame == 14);
        if (frame == 17) t.document.undo();
        if (frame == 18 && !img_path.empty()) {
            before_import = t.document.state().assets; app.import_image(img_path);
        }
        if (frame == 19 && t.import_library)
            for (size_t i = 0; i < t.import_selected.size(); ++i)
                if (t.import_library->images[i].problem.empty()) { t.import_selected[i] = 1; break; }
        if (frame == 20 && before_import) move(app.import_apply_point);
        if ((frame == 21 || frame == 22) && before_import) io.AddMouseButtonEvent(0, frame == 21);
        if (frame == 24 && before_import) t.document.undo();
        if (frame == 26 && before_import) t.document.redo();
        if (frame == 28) {
            before_png = t.document.state().assets;
            const uint8_t rgba[] = {255,0,0,255, 0,0,0,0, 0,255,0,255, 0,0,255,255};
            auto path = (fs::u8path(app.smoke_dir) / "import-source.png").u8string();
            if (!stbi_write_png(path.c_str(), 4, 1, 4, rgba, 16)) app.error = "Cannot write smoke PNG";
            else app.import_image(path);
        }
        if (frame == 30) t.document.undo();
    }
    bool check(App &app, int frame) {
        if (!app.tab()) return false;
        auto &t = *app.tab(); bool ok = true;
        if (frame == 5) ok = t.document.state().assets == before_block &&
            t.block_draft.assets->data.images[0].pix[0] == brush &&
            t.block_draft.assets->data.images[0].pix[4] == brush;
        if (frame == 9) ok = t.document.state().assets != before_block &&
            t.document.image(image)->pix[0] == brush && t.document.image(image)->pix[4] == brush;
        if (frame == 10) ok = t.document.state().assets == before_block;
        if (frame == 16) ok = t.document.state().assets->data.palettes[palette].rgb555[1] == color &&
            t.document.state().assets != before_palette;
        if (frame == 17) ok = t.document.state().assets == before_palette;
        if ((frame == 23 || frame == 27) && before_import) ok =
            t.document.state().assets->data.images.size() == before_import->data.images.size() + 1;
        if (frame == 25 && before_import) ok = t.document.state().assets == before_import;
        if (frame == 29) ok = t.document.state().assets->data.images.size() == before_png->data.images.size() + 1 &&
            t.document.state().assets->data.images.back().pix[1] == 0;
        if (frame == 31) ok = t.document.state().assets == before_png;
        if (!ok || !app.error.empty()) {
            if (t.block_draft.assets) std::fprintf(stderr, "brush %d pixels %d/%d painting %d history %zu mouse %.1f/%.1f target %.1f/%.1f\n",
                brush, t.block_draft.assets->data.images[0].pix[0], t.block_draft.assets->data.images[0].pix[4],
                t.block_painting, t.block_undo.size(), ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                app.block_paint_point.x, app.block_paint_point.y);
            std::fprintf(stderr, "Asset tools UI failed at frame %d: %s\n", frame, app.error.c_str()); return false;
        }
        if (frame == 31) std::fprintf(stderr, "Asset tools UI: continuous painting, draft isolation, Apply, palette edits, PNG/IMG import and undo passed.\n");
        return true;
    }
};
