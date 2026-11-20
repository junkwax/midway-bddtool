struct StageEditingSmoke {
    ObjectId outside = 0, clicked = 0;
    size_t placements = 0;
    Point original;
    bool saved = false;

    void input(App &app, int frame) {
        auto &t = *app.tab();
        auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        auto move = [&](Point p) { io.AddMousePosEvent(float(p.x), float(p.y)); };
        if (frame == 0) {
            app.page = 0;
            app.tray = false;
            const auto &objects = t.document.state().objects;
            placements = objects.size();
            outside = objects[0].id;
            clicked = objects[1].id;
            t.document.move({outside}, 1100, 0);
            const auto &p = *t.document.object(outside);
            original = {double(p.object.depth), double(p.object.sy)};
        }
        if (frame == 1 || frame == 4)
            move(app.smoke_outside_rows[outside]);
        if (frame == 2 || frame == 5 || frame == 10 || frame == 17)
            io.AddMouseButtonEvent(0, true);
        if (frame == 3 || frame == 8 || frame == 13 || frame == 18)
            io.AddMouseButtonEvent(0, false);
        if (frame == 6 || frame == 7)
            move(app.smoke_layer_rows[1]);
        if (frame == 9)
            move(app.smoke_layer_rows[1]);
        if (frame == 11 || frame == 12)
            move(app.smoke_layer_rows[2]);
        if (frame == 16) {
            t.selected = {outside, clicked};
            auto scene = t.document.scene();
            auto p = std::find_if(scene.begin(), scene.end(), [&](const SceneItem &item) { return item.id == clicked; });
            t.fit = false;
            t.view.zoom = 1;
            t.view.pan = {p->rect.x - 50, p->rect.y - 50};
            move({app.canvas_rect.x + 60, app.canvas_rect.y + 60});
        }
        if (frame == 19 || frame == 21)
            io.AddKeyEvent(ImGuiKey_Delete, frame == 19);
        if (frame == 23) {
            auto &doc = t.document;
            saved = doc.undo() && doc.set_object_z({clicked}, 79) &&
                    doc.save((fs::u8path(app.smoke_dir) / "New Stage.BDB").u8string(), app.error);
        }
    }

    bool check(App &app, int frame) {
        auto &t = *app.tab();
        const auto &doc = t.document;
        bool ok = true;
        if (frame == 3)
            ok = t.selected == std::vector<ObjectId>{outside} && t.view.pan.x > 600;
        if (frame == 8) {
            const auto &p = *doc.object(outside);
            const auto &layer = doc.state().planes[1];
            ok = p.plane == 1 && p.object.depth + layer.x - layer.source.x1 == original.x &&
                 p.object.sy + layer.y - layer.source.y1 == original.y;
        }
        if (frame == 13)
            ok = doc.plane_stack_position(1) == 2 && doc.plane_stack_position(2) == 1;
        if (frame == 18)
            ok = t.selected == std::vector<ObjectId>{clicked};
        if (frame == 19 || frame == 21)
            ok = !doc.object(clicked) && doc.object(outside) && t.selected.empty() &&
                 doc.state().objects.size() == placements - 1;
        if (frame == 23) {
            Document reopened;
            std::string error;
            ok = saved && document_title(doc) == "New Stage" && reopened.load(doc.path(), error) &&
                 document_title(reopened) == "New Stage" && reopened.plane_stack_position(1) == 2;
            const auto *restacked = reopened.object(clicked);
            ok = ok && restacked && ((restacked->object.wx >> 8) & 0xff) == 79;
        }
        if (!ok)
            std::fprintf(stderr, "Stage editing UI failed at frame %d: %s\n", frame, app.error.c_str());
        if (frame == 29 && ok)
            std::fprintf(stderr, "Stage editing UI: locate, drag artwork, restack layers, delete, undo and save title passed.\n");
        return ok;
    }
};
