// Runtime actor sidecars (<STAGE>.runtime.json): an editor-side preview and authoring aid.
// Nothing here enters the Document, its undo history, Save, or the game build.
bool input_string(const char *label, std::string &value, size_t limit = 63) {
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, std::min(sizeof buffer, limit + 1))) return false;
    value = buffer;
    return true;
}
std::set<std::string> actor_labels(const RuntimeActorSet &set) {
    std::set<std::string> out;
    for (const auto &a : set.actors)
        for (const auto &f : a.frames) {
            std::string label = f.label;
            for (char &c : label) c = (char)std::toupper((unsigned char)c);
            if (!label.empty()) out.insert(label);
        }
    return out;
}
void refresh_actor_art(Tab &t, bool rescan = false) {
    if (t.actors_path.empty()) return;
    auto dir = fs::u8path(t.actors_path).parent_path().u8string();
    if (rescan || !t.actor_index || t.actor_index_dir != dir) {
        t.actor_index = std::make_shared<RuntimeArtIndex>(index_runtime_art(dir));
        t.actor_index_dir = dir;
    }
    t.actor_art = std::make_shared<RuntimeArt>(load_runtime_art(*t.actor_index, t.actors));
    t.actor_art_labels = actor_labels(t.actors);
}
// Reloads frame art only when the set of referenced labels changes.
void ensure_actor_art(Tab &t) {
    if (!t.actors_path.empty() && (!t.actor_art || actor_labels(t.actors) != t.actor_art_labels))
        refresh_actor_art(t);
}
void load_runtime_actors(Tab &t) {
    t.actors = {}; t.actor_art.reset(); t.actor_art_labels.clear();
    t.actors_undo.clear(); t.actors_redo.clear();
    t.actors_loaded = t.actors_dirty = t.actors_pending = false;
    t.actor_selected = -1; t.actor_ticks = 0;
    if (t.document.path().empty()) {
        t.actors_path.clear();
        t.actors_status = "Save the stage first; its runtime actor sidecar lives beside the BDB.";
        return;
    }
    t.actors_path = runtime_actor_sidecar_path(t.document.path());
    t.actors.stage = t.document.state().name;
    auto path = fs::u8path(t.actors_path);
    std::string name = path.filename().u8string();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        t.actors_status = "No sidecar yet. Saving after adding an actor creates " + name + ".";
        return;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer; buffer << in.rdbuf();
    RuntimeActorSet set; std::string why;
    if (!in.good() && !in.eof()) why = "the file could not be read";
    if (!why.empty() || !parse_runtime_actors(buffer.str(), set, why)) {
        t.actors_status = "Could not load " + name + ": " + why;
        return;
    }
    if (set.stage.empty()) set.stage = t.document.state().name;
    t.actors = std::move(set);
    t.actors_loaded = true;
    t.actor_selected = t.actors.actors.empty() ? -1 : 0;
    refresh_actor_art(t);
    t.actors_status = "Loaded " + std::to_string(t.actors.actors.size()) + " actors from " + name + ".";
}
bool save_runtime_actors(Tab &t) {
    if (t.actors_path.empty()) { t.actors_status = "Save the stage before saving runtime actors."; return false; }
    auto path = fs::u8path(t.actors_path), temp = path;
    temp += ".studio-tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << write_runtime_actors(t.actors);
        out.close();
        if (!out) { t.actors_status = "Could not write " + temp.u8string(); return false; }
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored; fs::remove(temp, ignored);
        t.actors_status = "Could not replace " + path.filename().u8string() + ": " + ec.message();
        return false;
    }
    t.actors_dirty = false; t.actors_loaded = true;
    t.actors_status = "Saved " + std::to_string(t.actors.actors.size()) + " actors to " + path.filename().u8string() + ".";
    toast("Saved runtime actor sidecar");
    return true;
}
// Close and quit prompts save whichever of the stage and its sidecar have changes.
bool save_stage_and_actors(Tab &t) {
    return (!t.document.dirty() || save(t)) && (!t.actors_dirty || save_runtime_actors(t));
}
// Edits made while a widget is held become one undo step when it is released.
void actors_changed(Tab &t, const RuntimeActorSet &before) {
    if (!t.actors_pending) { t.actors_pending_base = before; t.actors_pending = true; }
    t.actors_dirty = true;
}
void actors_settle(Tab &t, bool force = false) {
    if (!t.actors_pending || (!force && ImGui::IsAnyItemActive())) return;
    t.actors_undo.push_back(std::move(t.actors_pending_base));
    if (t.actors_undo.size() > 100) t.actors_undo.erase(t.actors_undo.begin());
    t.actors_redo.clear();
    t.actors_pending = false;
}
void actors_step(Tab &t, bool redo) {
    actors_settle(t, true);
    auto &from = redo ? t.actors_redo : t.actors_undo, &to = redo ? t.actors_undo : t.actors_redo;
    if (from.empty()) return;
    to.push_back(std::move(t.actors));
    t.actors = std::move(from.back());
    from.pop_back();
    t.actors_dirty = true;
    if (t.actor_selected >= (int)t.actors.actors.size()) t.actor_selected = (int)t.actors.actors.size() - 1;
}
int64_t actor_preview_ticks(const Tab &t) { return (int64_t)std::floor(t.actor_ticks); }
// Stage rectangle relative to the scene camera, matching the legacy preview's projection.
Rect actor_rect(const Tab &t, const RuntimeActor &a, const ActorPose &pose, Point cam, int w, int h) {
    double x = pose.x - cam.x * a.scroll, y = a.screen_space_y ? pose.y : pose.y - cam.y;
    const auto &s = t.document.state();
    if (a.source_object >= 0 && a.source_object < (int)s.objects.size()) {
        // Attached actors keep their offset from the source placement wherever it moves.
        for (const auto &item : t.document.scene(cam))
            if (item.object_index == (size_t)a.source_object) {
                const auto &o = s.objects[a.source_object].object;
                x = item.rect.x + (pose.x - o.depth);
                y = item.rect.y + (pose.y - o.sy);
                break;
            }
    }
    return {x, y, (double)w, (double)h};
}
bool actors_visible(const Tab &t) {
    if (!t.actors_show || t.source || t.solo >= 0) return false;
    if (t.actors_isolate)
        return t.actor_selected >= 0 && t.actor_selected < (int)t.actors.actors.size() &&
               t.actors.actors[t.actor_selected].enabled;
    return std::any_of(t.actors.actors.begin(), t.actors.actors.end(), [](const RuntimeActor &a) { return a.enabled; });
}
void draw_runtime_actors(Tab &t, ImDrawList *draw, ImVec2 origin, Point cam) {
    if (!actors_visible(t)) return;
    ensure_actor_art(t);
    if (actor_textures.size() < (t.actor_art ? t.actor_art->sources.size() : 0))
        actor_textures.resize(t.actor_art->sources.size());
    std::vector<size_t> order(t.actors.actors.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return t.actors.actors[a].layer < t.actors.actors[b].layer; });
    for (size_t i : order) {
        if (t.actors_isolate && (int)i != t.actor_selected) continue;
        const auto &a = t.actors.actors[i];
        auto pose = actor_pose(t.actors, i, actor_preview_ticks(t), t.actors_simulate);
        if (!pose.visible) continue;
        size_t source = 0;
        const AnimationFrame *frame = t.actor_art ? t.actor_art->find(a.frames[pose.frame].label, &source) : nullptr;
        const BddCoreImage *im = frame ? &t.actor_art->sources[source].artwork.assets->data.images[frame->image] : nullptr;
        auto r = actor_rect(t, a, pose, cam, im ? im->w : 24, im ? im->h : 24);
        ImVec2 p0 = vec(t.view.to_screen({r.x, r.y}, point(origin)));
        ImVec2 p1 = vec(t.view.to_screen({r.x + r.w, r.y + r.h}, point(origin)));
        bool selected = (int)i == t.actor_selected && t.actors_open;
        if (selected && t.actors_onion && a.frames.size() > 1 && t.actor_art) {
            auto ghost = [&](size_t ghost_frame, ImU32 tint) {
                size_t ghost_source = 0;
                const auto *ghost_art = t.actor_art->find(a.frames[ghost_frame].label, &ghost_source);
                if (!ghost_art || ghost_source >= t.actor_art->sources.size()) return;
                const auto &ghost_image = t.actor_art->sources[ghost_source].artwork.assets->data.images[ghost_art->image];
                ActorPose ghost_pose = pose;
                ghost_pose.frame = ghost_frame;
                ghost_pose.x += a.frames[ghost_frame].dx - a.frames[pose.frame].dx;
                ghost_pose.y += a.frames[ghost_frame].dy - a.frames[pose.frame].dy;
                ghost_pose.hflip = a.frames[ghost_frame].hfl_mode == 0 ? a.hfl : a.frames[ghost_frame].hfl_mode == 2;
                ghost_pose.vflip = a.frames[ghost_frame].vfl_mode == 0 ? a.vfl : a.frames[ghost_frame].vfl_mode == 2;
                auto gr = actor_rect(t, a, ghost_pose, cam, ghost_image.w, ghost_image.h);
                ImVec2 g0 = vec(t.view.to_screen({gr.x, gr.y}, point(origin)));
                ImVec2 g1 = vec(t.view.to_screen({gr.x + gr.w, gr.y + gr.h}, point(origin)));
                auto &ghost_cache = actor_textures[ghost_source]; ghost_cache.renderer = renderer;
                if (auto *tex = ghost_cache.get(t.actor_art->sources[ghost_source].artwork, ghost_art->image, ghost_art->palette))
                    draw->AddImage((ImTextureID)(intptr_t)tex, g0, g1,
                                   {ghost_pose.hflip ? 1.f : 0.f, ghost_pose.vflip ? 1.f : 0.f},
                                   {ghost_pose.hflip ? 0.f : 1.f, ghost_pose.vflip ? 0.f : 1.f}, tint);
            };
            int alpha = std::clamp((int)std::lround(t.actor_onion_alpha * 255.f), 16, 192);
            if (pose.frame > 0) ghost(pose.frame - 1, IM_COL32(100, 190, 255, alpha));
            else if (a.loop) ghost(a.frames.size() - 1, IM_COL32(100, 190, 255, alpha));
            if (pose.frame + 1 < a.frames.size()) ghost(pose.frame + 1, IM_COL32(255, 150, 80, alpha));
            else if (a.loop) ghost(0, IM_COL32(255, 150, 80, alpha));
        }
        if (im) {
            auto &cache = actor_textures[source];
            cache.renderer = renderer;
            if (auto *tex = cache.get(t.actor_art->sources[source].artwork, frame->image, frame->palette))
                draw->AddImage((ImTextureID)(intptr_t)tex, p0, p1, {pose.hflip ? 1.f : 0.f, pose.vflip ? 1.f : 0.f},
                               {pose.hflip ? 0.f : 1.f, pose.vflip ? 0.f : 1.f});
        } else {
            // Missing art stays visible as a labeled placeholder instead of silently vanishing.
            draw->AddRectFilled(p0, p1, IM_COL32(120, 40, 60, 140));
            draw->AddLine(p0, p1, IM_COL32(255, 120, 140, 200));
            draw->AddLine({p0.x, p1.y}, {p1.x, p0.y}, IM_COL32(255, 120, 140, 200));
        }
        if (selected || !im) draw->AddRect(p0, p1, selected ? selection_color : IM_COL32(255, 120, 140, 220), 0, 0, 1.5f);
        if (t.actors_labels) {
            char text[160];
            std::snprintf(text, sizeof text, "%s  %zu/%zu%s", a.name.c_str(), pose.frame + 1, a.frames.size(),
                          im ? "" : "  missing art");
            auto size = ImGui::CalcTextSize(text);
            draw->AddRectFilled({p0.x, p0.y - size.y - 4}, {p0.x + size.x + 8, p0.y}, IM_COL32(0, 0, 0, 170), 2);
            draw->AddText({p0.x + 4, p0.y - size.y - 2}, im ? IM_COL32(150, 235, 255, 230) : IM_COL32(255, 160, 170, 240), text);
        }
    }
}
void actor_toolbar(Tab &t) {
    if (t.source) return;
    ImGui::SameLine();
    std::string label = t.actors.actors.empty() ? std::string("Actors...") :
        "Actors (" + std::to_string(t.actors.actors.size()) + ")" + (t.actors_dirty ? "*" : "") + "...";
    if (ImGui::Button(label.c_str())) t.actors_open = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Runtime actor sidecar: preview and edit animated actors such as hanging bodies or tree faces.\nEditor-side only; the game runs its own actor code.");
    if (!t.actors.actors.empty()) {
        ImGui::SameLine(); ImGui::Checkbox("Show actors", &t.actors_show);
        ImGui::SameLine();
        if (ImGui::Button(t.actors_playing ? "Pause actors" : "Play actors")) t.actors_playing = !t.actors_playing;
    }
}
void runtime_actors_window(Tab &t) {
    if (!t.actors_open) return;
    auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(820.f, display.x - 40), std::min(640.f, display.y - 80)}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Appearing, {.5f, .5f});
    std::string title = std::string("Runtime actors") + (t.actors_dirty ? " *" : "") + "###runtime-actors";
    if (!ImGui::Begin(title.c_str(), &t.actors_open)) { ImGui::End(); return; }
    ensure_actor_art(t);
    const RuntimeActorSet before = t.actors;
    bool changed = false;
    ImGui::TextWrapped("%s", t.actors_path.empty() ? "No sidecar path yet." : t.actors_path.c_str());
    ImGui::TextDisabled("%s", t.actors_status.c_str());
    ImGui::TextDisabled("Preview and authoring only: the game runs its own actor code, and nothing here changes the BDB, BDD or game build.");
    ImGui::BeginDisabled(t.actors_path.empty() || !t.actors_dirty);
    if (ImGui::Button("Save sidecar")) save_runtime_actors(t);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(t.actors_path.empty());
    if (ImGui::Button("Reload")) { load_runtime_actors(t); refresh_actor_art(t, true); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reread the sidecar and rescan IMG files, discarding unsaved actor edits.");
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(t.actors_undo.empty() && !t.actors_pending);
    if (ImGui::Button("Undo##actors")) actors_step(t, false);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(t.actors_redo.empty());
    if (ImGui::Button("Redo##actors")) actors_step(t, true);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(t.actor_selected < 0);
    if (ImGui::Button("Copy BGND skeleton")) {
        ImGui::SetClipboardText(actor_bgnd_skeleton(t.actors.actors[t.actor_selected]).c_str());
        toast("Copied BGND actor skeleton; verify its driver before integrating");
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("Show on stage", &t.actors_show); ImGui::SameLine();
    ImGui::Checkbox("Labels", &t.actors_labels); ImGui::SameLine();
    ImGui::BeginDisabled(t.actor_selected < 0);
    ImGui::Checkbox("Solo selected", &t.actors_isolate); ImGui::SameLine();
    ImGui::Checkbox("Onion skin", &t.actors_onion);
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::Checkbox("Simulate triggers", &t.actors_simulate);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off matches the legacy preview: every actor loops from tick 0.\nOn: delayed actors appear after delay_min_ticks and always_static holds its first frame.\nThe game's random lineups and drop logic are not reproduced.");
    if (ImGui::Button(t.actors_playing ? "Pause" : "Play")) t.actors_playing = !t.actors_playing;
    auto step_frame = [&](int direction) {
        if (t.actor_selected < 0 || t.actor_selected >= (int)t.actors.actors.size()) return;
        const auto &actor = t.actors.actors[t.actor_selected];
        if (actor.frames.empty()) return;
        auto pose = actor_pose(t.actors, (size_t)t.actor_selected, actor_preview_ticks(t), t.actors_simulate);
        int target = (int)pose.frame + direction;
        if (actor.loop) target = (target + (int)actor.frames.size()) % (int)actor.frames.size();
        else target = std::clamp(target, 0, (int)actor.frames.size() - 1);
        auto tick_for_frame = actor_tick_for_frame(t.actors, (size_t)t.actor_selected, (size_t)target, t.actors_simulate);
        if (tick_for_frame >= 0) { t.actor_ticks = (double)tick_for_frame; t.actors_playing = false; }
        else toast("That frame is not reachable with the current trigger and phase settings");
    };
    ImGui::SameLine(); ImGui::BeginDisabled(t.actor_selected < 0);
    if (ImGui::Button("Previous frame")) step_frame(-1);
    ImGui::SameLine(); if (ImGui::Button("Next frame")) step_frame(1);
    ImGui::EndDisabled();
    if (t.actors_onion) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(100);
        float percent = t.actor_onion_alpha * 100.f;
        if (ImGui::SliderFloat("Onion opacity", &percent, 8.f, 60.f, "%.0f%%", ImGuiSliderFlags_NoInput))
            t.actor_onion_alpha = percent / 100.f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blue is the previous frame; orange is the next frame. Only the selected actor is ghosted.");
    }
    int span = 1;
    for (const auto &a : t.actors.actors) span = std::max(span, a.delay_min_ticks + actor_cycle_ticks(a) * 2);
    int tick = (int)std::min<int64_t>(actor_preview_ticks(t), span);
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("##actor-tick", &tick, 0, span, "Tick %d")) { t.actor_ticks = tick; t.actors_playing = false; }
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat("Speed", &t.actor_speed, .25f, 4.f, "%.2fx");
    ImGui::SameLine(); if (ImGui::Button("Restart")) t.actor_ticks = 0;
    if (t.animation.ready() && actors_visible(t))
        ImGui::TextDisabled("The built-in Forest animation preview is hidden while sidecar actors are shown.");
    if (t.actor_art) {
        const auto &art = *t.actor_art;
        ImGui::Text("%zu frames resolved from %zu IMG files in this folder", art.frames.size(),
                    t.actor_index ? t.actor_index->scanned : 0);
        if (!art.missing.empty() || !art.errors.empty()) {
            std::string head = "Missing or unreadable frames (" + std::to_string(art.missing.size() + art.errors.size()) + ")###actor-missing";
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            bool open = ImGui::CollapsingHeader(head.c_str());
            ImGui::PopStyleColor();
            if (open) {
                ImGui::TextWrapped("These labels are not in any IMG file beside the stage, so they draw as placeholders. The sidecar may predate a re-cut of the art.");
                std::string list;
                for (const auto &m : art.missing) list += (list.empty() ? "" : ", ") + m;
                for (const auto &e : art.errors) list += (list.empty() ? "" : "; ") + e;
                ImGui::TextWrapped("%s", list.c_str());
            }
        }
    }
    ImGui::Separator();
    // Actor list.
    ImGui::BeginChild("actor-list", {220, 0}, true);
    for (size_t i = 0; i < t.actors.actors.size(); ++i) {
        auto &a = t.actors.actors[i];
        ImGui::PushID((int)i);
        changed |= ImGui::Checkbox("##on", &a.enabled);
        ImGui::SameLine();
        bool missing = false;
        if (t.actor_art) for (const auto &f : a.frames) missing |= !t.actor_art->find(f.label);
        std::string name = a.name + (missing ? "  (missing art)" : "");
        if (ImGui::Selectable(name.c_str(), t.actor_selected == (int)i)) t.actor_selected = (int)i;
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(t.actors.actors.size() >= (size_t)kMaxRuntimeActors || t.actors_path.empty());
    if (ImGui::Button("Add")) {
        RuntimeActor a;
        a.name = "actor_" + std::to_string(t.actors.actors.size() + 1);
        a.code_label = make_actor_code_label(a.name);
        a.x = (int)std::lround(t.camera.x + 200); a.y = 100;
        a.frames.push_back({});
        t.actors.actors.push_back(a);
        t.actor_selected = (int)t.actors.actors.size() - 1;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(t.actor_selected < 0 || t.actors.actors.size() >= (size_t)kMaxRuntimeActors);
    if (ImGui::Button("Duplicate")) {
        auto copy = t.actors.actors[t.actor_selected];
        copy.name += "_copy"; copy.code_label = make_actor_code_label(copy.name); copy.x += 16;
        t.actors.actors.insert(t.actors.actors.begin() + t.actor_selected + 1, copy);
        ++t.actor_selected; changed = true;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(t.actor_selected < 0);
    if (ImGui::Button("Delete")) {
        t.actors.actors.erase(t.actors.actors.begin() + t.actor_selected);
        t.actor_selected = std::min(t.actor_selected, (int)t.actors.actors.size() - 1);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Up") && t.actor_selected > 0) {
        std::swap(t.actors.actors[t.actor_selected], t.actors.actors[t.actor_selected - 1]);
        --t.actor_selected; changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Down") && t.actor_selected + 1 < (int)t.actors.actors.size()) {
        std::swap(t.actors.actors[t.actor_selected], t.actors.actors[t.actor_selected + 1]);
        ++t.actor_selected; changed = true;
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SameLine();
    // Selected actor.
    ImGui::BeginChild("actor-detail", {0, 0}, true);
    if (t.actor_selected < 0 || t.actor_selected >= (int)t.actors.actors.size()) {
        ImGui::TextWrapped(t.actors_path.empty() ? "Save the stage to start a runtime actor sidecar."
                                                 : "Select an actor, or Add one to start.");
    } else {
        auto &a = t.actors.actors[t.actor_selected];
        ImGui::SetNextItemWidth(220); changed |= input_string("Name", a.name);
        ImGui::SameLine(); changed |= ImGui::Checkbox("Enabled", &a.enabled);
        ImGui::SetNextItemWidth(220); changed |= input_string("Trigger", a.trigger, 31);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("always, always_static, stage, or a game-specific name such as drop_0x0240.\nRecorded for integration; only delays and always_static affect the simulated preview.");
        int xy[2] = {a.x, a.y};
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputInt2("X / Y", xy)) { a.x = std::clamp(xy[0], -100000, 100000); a.y = std::clamp(xy[1], -100000, 100000); changed = true; }
        ImGui::SameLine(); changed |= ImGui::Checkbox("Screen-space Y", &a.screen_space_y);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Y stays fixed on screen like BGND set_xy_coordinates, instead of following the camera.");
        ImGui::SetNextItemWidth(100); changed |= ImGui::InputInt("Layer", &a.layer);
        ImGui::SameLine(); ImGui::SetNextItemWidth(110);
        if (ImGui::InputDouble("Scroll", &a.scroll, .05, .25, "%.4f")) { a.scroll = std::clamp(a.scroll, -16.0, 16.0); changed = true; }
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Frame ticks", &a.frame_ticks)) { a.frame_ticks = std::clamp(a.frame_ticks, 1, 600); changed = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Phase", &a.phase_ticks)) { a.phase_ticks = std::clamp(a.phase_ticks, 0, 100000); changed = true; }
        int delay[2] = {a.delay_min_ticks, a.delay_max_ticks};
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputInt2("Delay min / max", delay)) {
            a.delay_min_ticks = std::clamp(delay[0], 0, 1000000); a.delay_max_ticks = std::clamp(delay[1], a.delay_min_ticks, 1000000); changed = true;
        }
        changed |= ImGui::Checkbox("Loop", &a.loop); ImGui::SameLine();
        changed |= ImGui::Checkbox("Flip X", &a.hfl); ImGui::SameLine();
        changed |= ImGui::Checkbox("Flip Y", &a.vfl);
        int motion[2] = {a.motion_x, a.motion_y};
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputInt2("Motion X / Y (16.16 px/tick)", motion)) { a.motion_x = motion[0]; a.motion_y = motion[1]; changed = true; }
        ImGui::SetNextItemWidth(160); changed |= input_string("Sync group", a.sync_group, 31);
        ImGui::SameLine(); ImGui::SetNextItemWidth(130);
        static const char *roles[] = {"solo", "leader", "follower", "multipart_piece"};
        if (ImGui::BeginCombo("Role", a.group_role.c_str())) {
            for (const char *role : roles) if (ImGui::Selectable(role, a.group_role == role)) { a.group_role = role; changed = true; }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(100); changed |= ImGui::InputInt("Group index", &a.group_index);
        ImGui::SameLine(); changed |= ImGui::Checkbox("Lock timing to leader", &a.sync_group_timing);
        if (ImGui::TreeNode("Integration fields")) {
            ImGui::TextDisabled("Assembly hints are preserved; Source object and Replace source also affect the preview.");
            ImGui::SetNextItemWidth(200); changed |= input_string("Code label", a.code_label);
            ImGui::SetNextItemWidth(200); changed |= input_string("Code pattern", a.code_pattern, 31);
            ImGui::SetNextItemWidth(200); changed |= input_string("Frame driver", a.frame_driver, 23);
            ImGui::SetNextItemWidth(200); changed |= input_string("Insert list", a.insert_list, 23);
            ImGui::SetNextItemWidth(200); changed |= input_string("Scroll symbol", a.scroll_symbol, 31);
            changed |= ImGui::Checkbox("Emit BGND code", &a.emit_bgnd_code);
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputInt("Source object", &a.source_object);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("-1 for a free actor; otherwise the BDB object index this actor follows.");
            ImGui::SameLine(); changed |= ImGui::Checkbox("Replace source", &a.replace_source);
            if (a.replace_source && (a.source_object < 0 || a.source_object >= (int)t.document.state().objects.size()))
                ImGui::TextColored({1.f, .65f, .35f, 1.f}, "Choose a valid source object before its static placement can be hidden.");
            changed |= ImGui::Checkbox("Multipart piece", &a.multipart_piece);
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputInt("Part index", &a.part_index);
            ImGui::SameLine(); ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("Part count", &a.part_count)) { a.part_count = std::max(1, a.part_count); changed = true; }
            ImGui::SetNextItemWidth(200); changed |= input_string("Parent actor", a.parent_actor);
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Frames");
        static const char *flip_modes[] = {"inherit", "off", "on"};
        int remove = -1, up = -1;
        if (ImGui::BeginTable("actor-frames", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("#"); ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("dx"); ImGui::TableSetupColumn("dy"); ImGui::TableSetupColumn("Ticks");
            ImGui::TableSetupColumn("Flip X"); ImGui::TableSetupColumn("Flip Y"); ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            auto pose = actor_pose(t.actors, (size_t)t.actor_selected, actor_preview_ticks(t), t.actors_simulate);
            for (size_t f = 0; f < a.frames.size(); ++f) {
                auto &fr = a.frames[f];
                ImGui::PushID((int)f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (pose.visible && pose.frame == f) ImGui::TextColored(accent, ">%zu", f + 1); else ImGui::Text("%zu", f + 1);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
                bool found = t.actor_art && t.actor_art->find(fr.label);
                if (!found) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .55f, .6f, 1));
                changed |= input_string("##label", fr.label);
                if (!found) { ImGui::PopStyleColor(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("No IMG file beside the stage has this label."); }
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(56); changed |= ImGui::InputInt("##dx", &fr.dx, 0);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(56); changed |= ImGui::InputInt("##dy", &fr.dy, 0);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(56);
                if (ImGui::InputInt("##ticks", &fr.ticks, 0)) { fr.ticks = std::clamp(fr.ticks, 0, 600); changed = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 uses the actor's frame ticks.");
                for (int *mode : {&fr.hfl_mode, &fr.vfl_mode}) {
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(72);
                    ImGui::PushID(mode == &fr.hfl_mode ? "h" : "v");
                    if (ImGui::BeginCombo("##flip", flip_modes[std::clamp(*mode, 0, 2)])) {
                        for (int m = 0; m < 3; ++m) if (ImGui::Selectable(flip_modes[m], *mode == m)) { *mode = m; changed = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("^") && f > 0) up = (int)f;
                ImGui::SameLine();
                if (ImGui::SmallButton("x") && a.frames.size() > 1) remove = (int)f;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (up > 0) { std::swap(a.frames[up], a.frames[up - 1]); changed = true; }
        if (remove >= 0) { a.frames.erase(a.frames.begin() + remove); changed = true; }
        // Add a frame by label, with matches from the IMG index.
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##new-frame", "IMG label", t.actor_frame_label, sizeof t.actor_frame_label);
        ImGui::SameLine();
        ImGui::BeginDisabled(!t.actor_frame_label[0] || a.frames.size() >= (size_t)kMaxRuntimeFrames);
        if (ImGui::Button("Add frame")) {
            if (a.frames.size() == 1 && a.frames[0].label.empty()) a.frames[0].label = t.actor_frame_label;
            else a.frames.push_back({t.actor_frame_label});
            t.actor_frame_label[0] = 0; changed = true;
        }
        ImGui::EndDisabled();
        if (t.actor_frame_label[0] && t.actor_index) {
            std::string prefix = t.actor_frame_label;
            for (char &c : prefix) c = (char)std::toupper((unsigned char)c);
            int shown = 0;
            for (auto it = t.actor_index->files.lower_bound(prefix);
                 it != t.actor_index->files.end() && it->first.compare(0, prefix.size(), prefix) == 0 && shown < 8; ++it, ++shown) {
                std::string item = it->first + "  (" + fs::u8path(it->second).filename().u8string() + ")";
                if (ImGui::Selectable(item.c_str())) std::snprintf(t.actor_frame_label, sizeof t.actor_frame_label, "%s", it->first.c_str());
            }
        }
    }
    ImGui::EndChild();
    if (changed) actors_changed(t, before);
    actors_settle(t);
    ImGui::End();
}
