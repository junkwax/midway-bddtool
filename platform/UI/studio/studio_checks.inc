    static const char *issue_group_name(IssueGroup group) {
        switch (group) {
        case IssueGroup::References: return "References";
        case IssueGroup::Artwork: return "Artwork / palettes";
        case IssueGroup::Load2: return "LOAD2";
        case IssueGroup::Layers: return "Layers";
        case IssueGroup::Camera: return "Camera / resources";
        }
        return "Authoring";
    }
    void locate_issue(Tab &t, const Issue &issue) {
        cancel_gesture();
        t.source = false; t.camera_preview = false; t.solo = -1;
        if (issue.object) {
            t.selected = {issue.object}; t.focus_object = issue.object;
            if (auto *p = t.document.object(issue.object)) {
                if (p->plane >= 0 && p->plane < (int)t.document.state().planes.size()) t.plane = p->plane;
                if (p->hidden || (p->plane >= 0 && p->plane < (int)t.document.state().planes.size() &&
                                  t.document.state().planes[p->plane].hidden))
                    toast("Selected artwork is hidden. Use the layer controls to reveal it.");
            }
            t.fit = false;
        } else if (issue.plane >= 0) {
            t.selected.clear(); t.plane = issue.plane; t.fit = true;
        }
        page = 0;
    }
    void start_camera_check(Tab &t) {
        if (t.camera_check_job.valid()) return;
        t.camera_check_error.clear();
        t.camera_check_progress = std::make_shared<OptimizeProgress>();
        auto state = t.document.state(); auto options = t.camera_check_options;
        auto progress = t.camera_check_progress;
        t.camera_check_job = std::async(std::launch::async, [state, options, progress] {
            return check_camera_range(state, options, progress.get());
        });
    }
    void camera_check_controls(Tab &t) {
        bool busy = t.camera_check_job.valid();
        ImGui::BeginDisabled(busy || !t.document.state().has_bdb);
        if (ImGui::Button("Scan camera range")) start_camera_check(t);
        camera_scan_point = item_center();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (busy) {
            if (ImGui::SmallButton("Cancel scan")) t.camera_check_progress->cancel = true;
        } else if (!t.camera_check) ImGui::TextDisabled("Camera / resource estimates: not scanned");
        else if (t.camera_check->complete && !camera_check_current(*t.camera_check, t.document.state(), t.camera_check_options))
            ImGui::TextColored(accent, "Scan is stale. Run again for this document/range.");
        else if (t.camera_check->complete)
            ImGui::TextDisabled("%d samples | peak %d objects + %d reserve | %d palettes",
                t.camera_check->samples, t.camera_check->peak_objects, t.camera_check->options.reserve, t.camera_check->peak_palettes);
        else ImGui::TextDisabled("Scan incomplete");
        if (!t.camera_check_error.empty()) ImGui::TextWrapped("%s", t.camera_check_error.c_str());
        if (t.camera_check && !t.camera_check->error.empty()) ImGui::TextWrapped("%s", t.camera_check->error.c_str());
        if (t.camera_check && t.camera_check->cancelled) ImGui::TextDisabled("Scan cancelled; no partial findings used.");
        if (ImGui::TreeNode("Camera range and assumptions")) {
            auto &o = t.camera_check_options;
            ImGui::BeginDisabled(busy);
            int x[] = {o.min_x, o.max_x}, y[] = {o.min_y, o.max_y};
            ImGui::SetNextItemWidth(240);
            if (ImGui::InputInt2("Camera X min / max", x)) { o.min_x = x[0]; o.max_x = x[1]; }
            ImGui::SetNextItemWidth(240);
            if (ImGui::InputInt2("Camera Y min / max", y)) { o.min_y = y[0]; o.max_y = y[1]; }
            ImGui::SetNextItemWidth(160); ImGui::InputInt("Sample step (px)", &o.step);
            ImGui::SetNextItemWidth(160); ImGui::InputInt("Reserve for fighters / effects / actors", &o.reserve);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Initial range uses the authored start and world width, not verified gameplay limits. Samples include both endpoints (up to 2048 positions). Hidden placements count toward export costs. Bounds only: no transparency/occlusion analysis, animation or allocation lifetime. The 56-object default reserve is adjustable; scan results are estimates.");
            ImGui::TreePop();
        }
        // Option edits invalidate findings immediately, not one render later.
        refresh_checks(t);
    }
    void authoring_checks(Tab &t) {
        refresh_checks(t);
        ImGui::SeparatorText("Authoring checks");
        camera_check_controls(t);
        ImGui::Text("%d errors  /  %d warnings", t.info_errors, t.info_warnings);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy report")) {
            std::string report = "bddtool authoring checks: " + t.document.state().name + "\n";
            report += "Static source checks only; not runtime or packed-build verification.\n";
            if (t.camera_check && camera_check_current(*t.camera_check, t.document.state(), t.camera_check_options))
                report += camera_check_report(*t.camera_check);
            else report += "Camera/resource scan not current (not run, incomplete or stale).\n";
            for (const auto &issue : t.check_issues) {
                report += std::string(issue.error ? "ERROR" : "WARNING") + " [" + issue_group_name(issue.group) + "] " + issue.message;
                if (issue.object) report += " [placement " + std::to_string(issue.object) + "]";
                if (issue.has_camera) report += " [camera " + std::to_string((int)issue.camera.x) + "," + std::to_string((int)issue.camera.y) + "]";
                report += "\n  Next: " + issue.next_step + "\n";
            }
            if (t.check_issues.empty()) report += "No current authoring issues.\n";
            ImGui::SetClipboardText(report.c_str()); toast("All authoring findings copied");
        }
        check_report_point = item_center();
        ImGui::TextWrapped("Checks include hidden artwork and unplaced assets. Save maintains counts, companions and backups, then verifies file readback. Game export prepares X-order on its own copy.");
        ImGui::SetNextItemWidth(130);
        ImGui::Combo("##check-severity", &t.check_severity, "All severities\0Errors\0Warnings\0");
        ImGui::SameLine(); ImGui::SetNextItemWidth(170);
        ImGui::Combo("##check-group", &t.check_group, "All categories\0References\0Artwork / palettes\0LOAD2\0Layers\0Camera / resources\0");
        t.check_filter.Draw("Search findings", std::min(260.0f, ImGui::GetContentRegionAvail().x - 130));
        // Keep export controls reachable even when a file has hundreds of bad references.
        float height = std::clamp(ImGui::GetContentRegionAvail().y * .55f, 150.0f, 360.0f);
        ImGui::BeginChild("authoring-findings", ImVec2(0, height), ImGuiChildFlags_Border);
        int shown = 0;
        for (size_t i = 0; i < t.check_issues.size(); ++i) {
            const auto &issue = t.check_issues[i];
            if (t.check_severity == 1 && !issue.error) continue;
            if (t.check_severity == 2 && issue.error) continue;
            if (t.check_group && t.check_group != (int)issue.group + 1) continue;
            std::string searchable = issue.message + " " + issue.next_step + " " + issue_group_name(issue.group);
            if (!t.check_filter.PassFilter(searchable.c_str())) continue;
            ++shown; ImGui::PushID((int)i);
            ImGui::TextColored(issue.error ? ImVec4(.98f, .48f, .42f, 1) : ImVec4(.85f, .73f, .47f, 1),
                               "%s / %s", issue.error ? "ERROR" : "REVIEW", issue_group_name(issue.group));
            ImGui::TextWrapped("%s", issue.message.c_str());
            ImGui::TextWrapped("%s", issue.next_step.c_str());
            if (issue.has_camera) {
                ImGui::TextDisabled("Sample camera: %.0f, %.0f", issue.camera.x, issue.camera.y);
                if (ImGui::SmallButton("View camera")) {
                    cancel_gesture(); t.selected.clear(); t.focus_object = 0;
                    t.source = false; t.solo = -1; t.camera_preview = true;
                    t.camera = issue.camera; t.fit = true; page = 0;
                    toast("Sampled camera selected. Hidden artwork remains hidden in the editor.");
                }
                if (shown == 1) camera_jump_point = item_center();
            }
            if (issue.object || issue.plane >= 0) {
                if (ImGui::SmallButton(issue.object ? "Locate placement" : "Open layer")) locate_issue(t, issue);
                if (shown == 1) check_locate_point = item_center();
            }
            if (issue.image_slot >= 0) {
                if (issue.object || issue.plane >= 0) ImGui::SameLine();
                if (ImGui::SmallButton("Open artwork")) {
                    t.asset = issue.image_slot; t.reveal_asset = true; search[0] = 0; page = 1;
                }
                if (shown == 1) check_artwork_point = item_center();
            }
            if (issue.palette >= 0 && issue.image_slot >= 0 &&
                t.document.state().assets->data.palettes[issue.palette].count >= 1 &&
                t.document.state().assets->data.palettes[issue.palette].count <= 256) {
                if (issue.object || issue.plane >= 0 || issue.image_slot >= 0) ImGui::SameLine();
                if (ImGui::SmallButton("Inspect palette")) {
                    if (issue.image_slot >= 0) t.asset = issue.image_slot;
                    open_palette_editor(t, issue.palette);
                }
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::PopID();
        }
        check_visible_count = shown;
        if (!shown) ImGui::TextWrapped(t.check_issues.empty()
            ? "No current authoring issues. Runtime appearance, animation and packed output still need build/game validation."
            : "No findings match these filters.");
        ImGui::EndChild();
    }
