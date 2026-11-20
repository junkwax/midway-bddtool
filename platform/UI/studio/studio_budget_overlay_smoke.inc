struct BudgetOverlaySmoke {
    double original_ms = 0;

    void input(App &app, int frame) {
        auto &t = *app.tab();
        auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        if (frame == 0) {
            t.camera_preview = false;
            t.camera = {};
        }
        if (frame == 1)
            io.AddMousePosEvent(float(app.smoke_budget_toggle.x), float(app.smoke_budget_toggle.y));
        if (frame == 2 || frame == 3)
            io.AddMouseButtonEvent(0, frame == 2);
        if (frame == 5)
            t.document.move({t.document.state().objects.front().id}, 2000, 0);
        if (frame == 7)
            t.camera = {-10000, -10000};
    }

    bool check(App &app, int frame) {
        const auto &t = *app.tab();
        bool ok = true;
        if (frame == 4) {
            original_ms = app.smoke_budget_ms;
            ok = t.show_budget && t.camera_preview && original_ms > 0 && app.smoke_budget_outlines == 5;
        }
        if (frame == 5) ok = app.smoke_budget_ms > 0 && app.smoke_budget_ms < original_ms;
        if (frame == 7) ok = app.smoke_budget_ms == 0 && app.smoke_budget_outlines == 0;
        if (!ok) std::fprintf(stderr, "Budget overlay UI failed at frame %d.\n", frame);
        if (frame == 29) std::fprintf(stderr, "Budget overlay UI: toggle, outlines, live artwork edit and camera update passed.\n");
        return ok;
    }
};
