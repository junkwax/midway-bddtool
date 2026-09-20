#include "UI/studio/studio_app.h"
#include "UI/assets/app_icon.h"
#include "UI/dialogs/native_file_dialogs.h"
#include "Core/editor_project_storage.h"
#include "Core/studio_game_export.h"
#include "Core/studio_game_build.h"
#include <fstream>
#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace studio {
namespace fs = std::filesystem;
namespace {
// Dominant blue sampled from appicon.png (#2580DE).
const ImVec4 accent(37.0f / 255, 128.0f / 255, 222.0f / 255, 1.0f);
const ImU32 selection_color = IM_COL32(37, 128, 222, 255);
ImVec4 accent_surface(float strength) {
    return ImVec4(.084f + (accent.x - .084f) * strength, .102f + (accent.y - .102f) * strength,
                  .123f + (accent.z - .123f) * strength, 1);
}
const char *stage_filter = "Midway backgrounds\0*.BDB;*.BDD;*.bdb;*.bdd\0All files\0*.*\0";
ImVec2 vec(Point p) { return ImVec2((float)p.x, (float)p.y); }
Point point(ImVec2 p) { return {p.x, p.y}; }
bool has(const std::vector<ObjectId> &ids, ObjectId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
struct Tab {
    uint64_t id = 0;
    Document document;
    Viewport view;
    std::vector<ObjectId> selected;
    int plane = 0, solo = -1, asset = 0;
    bool fit = true, source = false, camera_preview = false, move_layer = false;
    Point camera;
    double next_recovery = 0;
    uint64_t recovered_revision = 0;
    char game_root[1024] = {}, game_label[96] = {};
    std::unique_ptr<GameExport> game_export;
    GameBuild game_build;
};
struct AssetPayload {
    uint64_t tab;
    int slot;
};
struct TextureCache {
    SDL_Renderer *renderer = nullptr;
    std::shared_ptr<const AssetBank> bank;
    std::map<std::pair<int, int>, SDL_Texture *> values;
    void clear() {
        for (auto kv : values)
            SDL_DestroyTexture(kv.second);
        values.clear();
        bank.reset();
    }
    SDL_Texture *get(const State &s, int slot, int palette) {
        if (bank != s.assets) {
            clear();
            bank = s.assets;
        }
        if (!bank || slot < 0 || slot >= (int)bank->data.images.size())
            return nullptr;
        if (palette < 0 || palette >= (int)bank->data.palettes.size())
            return nullptr;
        auto key = std::make_pair(slot, palette);
        auto found = values.find(key);
        if (found != values.end())
            return found->second;
        const auto &im = bank->data.images[slot];
        const auto &pal = bank->data.palettes[palette];
        std::vector<uint8_t> rgba((size_t)im.w * im.h * 4);
        if (!bdd_core_indexed_to_rgba(im.pix.data(), im.w, im.h, pal.argb, pal.count, rgba.data(),
                                      rgba.size()))
            return nullptr;
        SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STATIC, im.w, im.h);
        if (!tex)
            return nullptr;
        SDL_UpdateTexture(tex, nullptr, rgba.data(), im.w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        values[key] = tex;
        return tex;
    }
};
class App {
  public:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    TextureCache textures;
    std::vector<std::unique_ptr<Tab>> tabs;
    int active = -1, page = 0, select_tab = -1;
    uint64_t next_tab = 1;
    bool running = true, grid = false, snap = true, tray = true, quit_pending = false;
    int close_pending = -1;
    bool show_recovery = false;
    std::string error, message, smoke_dir;
    char search[96] = {};
    double message_until = 0;
    bool dragging = false, marquee = false, moved = false;
    int drag_plane = -1;
    Point drag_start, drag_last;
    std::vector<ObjectId> drag_selection;
    Rect canvas_rect;
    Tab *tab() { return active >= 0 && active < (int)tabs.size() ? tabs[active].get() : nullptr; }
    void toast(const std::string &text) {
        message = text;
        message_until = ImGui::GetTime() + 5;
    }
    void add(Document d) {
        cancel_gesture();
        auto t = std::make_unique<Tab>();
        t->id = next_tab++;
        t->document = std::move(d);
        t->camera = {(double)t->document.state().start_x, (double)t->document.state().start_y};
        t->camera_preview =
            std::any_of(t->document.state().planes.begin(), t->document.state().planes.end(),
                        [](const Plane &p) { return p.bound; });
        t->next_recovery = ImGui::GetTime() + 60;
        if (!t->document.path().empty()) {
            auto root = fs::u8path(t->document.path()).parent_path().parent_path();
            if (fs::is_regular_file(root / "src" / "BGND.ASM"))
                std::snprintf(t->game_root, sizeof t->game_root, "%s", root.u8string().c_str());
        }
        if (t->document.state().planes.empty())
            t->plane = -1;
        bool assets = !t->document.state().has_bdb;
        tabs.push_back(std::move(t));
        active = (int)tabs.size() - 1;
        select_tab = active;
        page = assets ? 1 : 0;
    }
    void open(const std::string &path) {
        try {
            Document d;
            if (!d.load(path, error))
                return;
            for (size_t i = 0; i < tabs.size(); i++) {
                std::error_code ec;
                if (!tabs[i]->document.path().empty() &&
                    fs::equivalent(fs::u8path(d.path()), fs::u8path(tabs[i]->document.path()),
                                   ec)) {
                    cancel_gesture();
                    active = (int)i;
                    select_tab = active;
                    return;
                }
            }
            read_runtime_defaults(d);
            add(std::move(d));
        } catch (const std::exception &e) {
            error = e.what();
        }
    }
    void open_dialog() {
        char path[1024] = {};
        if (file_dialog_open("Open background", stage_filter, path, sizeof path))
            open(path);
    }
    bool save(Tab &t, bool save_as = false) {
        if (t.document.transaction_active()) {
            error = "Finish or cancel the current drag before saving.";
            return false;
        }
        std::string path = t.document.path();
        if (path.empty() || save_as) {
            char selected_path[1024] = {};
            std::snprintf(selected_path, sizeof selected_path, "%s",
                          path.empty() ? (t.document.state().name +
                                          (t.document.state().has_bdb ? ".BDB" : ".BDD"))
                                             .c_str()
                                       : path.c_str());
            if (!file_dialog_save_ext("Save background", stage_filter,
                                      t.document.state().has_bdb ? "BDB" : "BDD", selected_path,
                                      sizeof selected_path))
                return false;
            path = selected_path;
        }
        for (const auto &other : tabs)
            if (other.get() != &t && !other->document.path().empty()) {
                std::error_code ec;
                auto target = fs::weakly_canonical(fs::u8path(path));
                target.replace_extension();
                auto other_path = fs::weakly_canonical(fs::u8path(other->document.path()));
                other_path.replace_extension();
                if (target == other_path ||
                    fs::equivalent(fs::u8path(path), fs::u8path(other->document.path()), ec)) {
                    error = "That file is open in another tab. Choose another save location.";
                    return false;
                }
            }
        if (!t.document.save(path, error))
            return false;
        toast("Saved background and local layer layout");
        return true;
    }
    void import_image(const std::string &path) {
        if (!tab())
            add(Document::empty());
        int w = 0, h = 0, n = 0;
        if (!stbi_info(path.c_str(), &w, &h, &n) || w > 4096 || h > 4096) {
            error = "Cannot import this image; maximum dimensions are 4096 x 4096.";
            return;
        }
        auto *rgba = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!rgba) {
            error = "Could not decode the image.";
            return;
        }
        int id = 0;
        bool ok =
            tab()->document.import_rgba(fs::u8path(path).stem().u8string(), w, h, rgba, error, id);
        stbi_image_free(rgba);
        if (ok) {
            tab()->asset = (int)tab()->document.state().assets->data.images.size() - 1;
            tray = true;
            toast("Image imported. Drag it from Assets into a layer.");
        }
    }
    void import_dialog() {
        char path[1024] = {};
        if (file_dialog_open("Import artwork", "Images\0*.png;*.tga;*.bmp\0All files\0*.*\0", path,
                             sizeof path))
            import_image(path);
    }
    void cancel_gesture() {
        if (tab())
            tab()->document.cancel();
        dragging = marquee = moved = false;
        drag_plane = -1;
        drag_last = {};
    }
    void clean_selection() {
        if (!tab())
            return;
        auto &t = *tab();
        t.selected.erase(std::remove_if(t.selected.begin(), t.selected.end(),
                                        [&](ObjectId id) { return !t.document.object(id); }),
                         t.selected.end());
        if (t.plane >= (int)t.document.state().planes.size())
            t.plane = (int)t.document.state().planes.size() - 1;
        if (t.solo >= (int)t.document.state().planes.size())
            t.solo = -1;
    }
    void shortcuts() {
        auto &io = ImGui::GetIO();
        if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (dragging || marquee)
                cancel_gesture();
            else if (tab())
                tab()->selected.clear();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
            open_dialog();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
            add(Document::empty());
        auto *t = tab();
        if (!t)
            return;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            save(*t, io.KeyShift);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            cancel_gesture();
            if (io.KeyShift)
                t->document.redo();
            else
                t->document.undo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            cancel_gesture();
            t->document.redo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            cancel_gesture();
            t->selected = t->document.duplicate(t->selected);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            t->selected.clear();
            for (const auto &p : t->document.scene({}, t->source, t->solo))
                if (!p.locked)
                    t->selected.push_back(p.id);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0))
            t->fit = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W))
            close_pending = active;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            cancel_gesture();
            t->document.erase(t->selected);
        }
        if (!dragging && !t->source) {
            int dx = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0) -
                     (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
            int dy = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0) -
                     (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0);
            if (dx || dy) {
                int step = io.KeyShift ? 10 : 1;
                if (!t->selected.empty())
                    t->document.move(t->selected, dx * step, dy * step);
                else if (t->plane >= 0) {
                    t->document.begin("Move layer");
                    t->document.preview_plane(t->plane, dx * step, dy * step);
                    t->document.commit();
                }
            }
        }
        clean_selection();
    }
    void image(ImDrawList *draw, Tab &t, int slot, int palette, ImVec2 a, ImVec2 b,
               bool flipx = false, bool flipy = false, ImU32 tint = IM_COL32_WHITE) {
        auto *tex = textures.get(t.document.state(), slot, palette);
        if (!tex)
            return;
        draw->AddImage((ImTextureID)tex, a, b, ImVec2(flipx ? 1.0f : 0.0f, flipy ? 1.0f : 0.0f),
                       ImVec2(flipx ? 0.0f : 1.0f, flipy ? 0.0f : 1.0f), tint);
    }
    std::string image_name(const Tab &t, int slot) const {
        const auto &assets = *t.document.state().assets;
        if (slot < 0 || slot >= (int)assets.data.images.size())
            return "Missing image";
        if (slot < (int)assets.metadata.size() && assets.metadata[slot].label[0])
            return assets.metadata[slot].label;
        return "Image " + std::to_string(assets.data.images[slot].idx);
    }
    int asset_palette(const Tab &t, int slot) {
        int id = t.document.state().assets->data.images[slot].idx;
        for (const auto &p : t.document.state().objects)
            if (p.object.ii == id)
                return p.object.fl;
        const auto &defaults = t.document.state().assets->default_palettes;
        return slot < (int)defaults.size() ? defaults[slot] : 0;
    }
    void heading(const char *title, const char *subtitle = nullptr) {
        ImGui::TextUnformatted(title);
        if (subtitle)
            ImGui::TextDisabled("%s", subtitle);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    void menu() {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextColored(accent, "BDD STUDIO");
            ImGui::Separator();
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New stage", "Ctrl+N"))
                    add(Document::empty());
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    open_dialog();
                if (ImGui::MenuItem("Import artwork...", nullptr, false, tab() != nullptr))
                    import_dialog();
                if (ImGui::MenuItem("Save", "Ctrl+S", false, tab() != nullptr))
                    save(*tab());
                if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, tab() != nullptr))
                    save(*tab(), true);
                if (ImGui::MenuItem("Recovery copies..."))
                    show_recovery = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Explore sample stage"))
                    add(Document::demo());
                if (ImGui::MenuItem("Close tab", "Ctrl+W", false, tab() != nullptr))
                    close_pending = active;
                if (ImGui::MenuItem("Quit"))
                    quit_pending = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                auto *t = tab();
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, t && t->document.undo_label())) {
                    cancel_gesture();
                    t->document.undo();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, t && t->document.redo_label())) {
                    cancel_gesture();
                    t->document.redo();
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, t && !t->selected.empty()))
                    t->selected = t->document.duplicate(t->selected);
                if (ImGui::MenuItem("Delete", "Delete", false, t && !t->selected.empty()))
                    t->document.erase(t->selected);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Asset tray", nullptr, &tray);
                ImGui::MenuItem("Grid", nullptr, &grid);
                ImGui::MenuItem("Smart snap", nullptr, &snap);
                if (ImGui::MenuItem("Fit stage", "Ctrl+0", false, tab() != nullptr))
                    tab()->fit = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::TextUnformatted(
                    "Drag to move. Shift-click to select more. Ctrl+D duplicates.");
                ImGui::TextUnformatted(
                    "Wheel to zoom. Middle-drag or Space-drag to pan. Escape cancels.");
                ImGui::TextUnformatted("Arrow keys nudge 1 pixel; Shift nudges 10 pixels.");
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Specialist pixel, palette, IMG/LOD, and game-build tools remain in the "
                    "original editor. Launch bddview --legacy-ui [file.BDB] to use them.");
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
    void toolbar() {
        if (ImGui::Button("Open", ImVec2(64, 30)))
            open_dialog();
        ImGui::SameLine();
        ImGui::BeginDisabled(!tab());
        if (ImGui::Button("Save", ImVec2(64, 30)) && tab())
            save(*tab());
        ImGui::EndDisabled();
        ImGui::SameLine();
        auto *t = tab();
        ImGui::BeginDisabled(!t || !t->document.undo_label());
        if (ImGui::Button("Undo", ImVec2(60, 30)) && t) {
            cancel_gesture();
            t->document.undo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!t || !t->document.redo_label());
        if (ImGui::Button("Redo", ImVec2(60, 30)) && t) {
            cancel_gesture();
            t->document.redo();
        }
        ImGui::EndDisabled();
        float right = ImGui::GetWindowWidth() - 335;
        if (right > ImGui::GetCursorPosX() + 320)
            ImGui::SameLine(right);
        else
            ImGui::SameLine(0, 22);
        const char *pages[] = {"Stage", "Assets", "Build & Check"};
        for (int i = 0; i < 3; i++) {
            if (i)
                ImGui::SameLine(0, 4);
            bool selected = page == i;
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, accent_surface(.45f));
            if (ImGui::Button(pages[i], ImVec2(i == 2 ? 126.0f : 80.0f, 30))) {
                cancel_gesture();
                page = i;
            }
            if (selected)
                ImGui::PopStyleColor();
        }
    }
    void document_tabs() {
        if (tabs.empty())
            return;
        if (ImGui::BeginTabBar("documents", ImGuiTabBarFlags_FittingPolicyScroll |
                                                ImGuiTabBarFlags_AutoSelectNewTabs)) {
            for (size_t i = 0; i < tabs.size(); i++) {
                auto &t = *tabs[i];
                bool open = true;
                std::string name = t.document.state().name + "###doc" + std::to_string(t.id);
                ImGuiTabItemFlags flags =
                    t.document.dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
                if (select_tab == (int)i)
                    flags |= ImGuiTabItemFlags_SetSelected;
                if (ImGui::BeginTabItem(name.c_str(), &open, flags)) {
                    if (active != (int)i) {
                        cancel_gesture();
                        active = (int)i;
                    }
                    ImGui::EndTabItem();
                }
                if (!open)
                    close_pending = (int)i;
            }
            select_tab = -1;
            ImGui::EndTabBar();
        }
    }
    void tree(Tab &t) {
        heading("STAGE", "Layers and artwork");
        if (ImGui::Button("+ Layer")) {
            t.plane = t.document.add_plane();
            t.selected.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Show all")) {
            for (size_t i = 0; i < t.document.state().planes.size(); i++) {
                auto p = t.document.state().planes[i];
                if (p.hidden)
                    t.document.set_plane_flags((int)i, false, p.locked);
            }
            t.solo = -1;
        }
        ImGui::Spacing();
        std::vector<int> order;
        for (size_t i = 0; i < t.document.state().planes.size(); i++)
            order.push_back((int)i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return t.document.state().planes[a].rank > t.document.state().planes[b].rank;
        });
        for (int i : order) {
            auto p = t.document.state().planes[i];
            ImGui::PushID(i);
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (t.plane == i && t.selected.empty())
                flags |= ImGuiTreeNodeFlags_Selected;
            bool expanded = ImGui::TreeNodeEx("layer", flags, "%s%s", p.hidden ? "(hidden) " : "",
                                              p.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                t.plane = i;
                t.selected.clear();
                t.move_layer = true;
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(p.hidden ? "Show" : "Hide"))
                    t.document.set_plane_flags(i, !p.hidden, p.locked);
                if (ImGui::MenuItem(p.locked ? "Unlock" : "Lock"))
                    t.document.set_plane_flags(i, p.hidden, !p.locked);
                if (ImGui::MenuItem(t.solo == i ? "Clear solo" : "Solo"))
                    t.solo = t.solo == i ? -1 : i;
                ImGui::EndPopup();
            }
            if (expanded) {
                bool visible = !p.hidden, locked = p.locked;
                if (ImGui::Checkbox("Show", &visible))
                    t.document.set_plane_flags(i, !visible, p.locked);
                ImGui::SameLine();
                if (ImGui::Checkbox("Lock", &locked))
                    t.document.set_plane_flags(i, p.hidden, locked);
                if (ImGui::SmallButton(t.solo == i ? "Unsolo" : "Solo"))
                    t.solo = t.solo == i ? -1 : i;
                int count = 0;
                for (const auto &object : t.document.state().objects)
                    if (object.plane == i) {
                        size_t slot = 0;
                        t.document.image(object.object.ii, &slot);
                        ImGui::PushID((int)object.id);
                        std::string label = image_name(t, (int)slot);
                        if (ImGui::Selectable(label.c_str(), has(t.selected, object.id))) {
                            if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
                                t.selected.clear();
                            if (!has(t.selected, object.id))
                                t.selected.push_back(object.id);
                            t.plane = i;
                        }
                        ImGui::PopID();
                        count++;
                    }
                if (!count)
                    ImGui::TextDisabled("Drop artwork here");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        bool orphan = false;
        for (const auto &p : t.document.state().objects)
            if (p.plane < 0)
                orphan = true;
        if (orphan && ImGui::TreeNodeEx("Unassigned", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto &p : t.document.state().objects)
                if (p.plane < 0) {
                    std::string name = "Object " + std::to_string(p.id);
                    if (ImGui::Selectable(name.c_str(), has(t.selected, p.id))) {
                        t.selected = {p.id};
                        t.plane = -1;
                    }
                }
            ImGui::TreePop();
        }
    }
    void inspector(Tab &t) {
        heading("INSPECTOR");
        auto &doc = t.document;
        const auto &s = doc.state();
        if (!t.selected.empty()) {
            auto *ptr = doc.object(t.selected.front());
            if (!ptr)
                return;
            auto p = *ptr;
            size_t slot = 0;
            auto *im = doc.image(p.object.ii, &slot);
            ImGui::TextWrapped("%s", image_name(t, (int)slot).c_str());
            ImGui::TextDisabled("%zu selected", t.selected.size());
            if (im) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float scale = std::min(160.0f / im->w, 110.0f / im->h);
                ImGui::Dummy(ImVec2(160, 116));
                image(ImGui::GetWindowDrawList(), t, (int)slot, p.object.fl, pos,
                      ImVec2(pos.x + im->w * scale, pos.y + im->h * scale), (p.object.wx & 16) != 0,
                      (p.object.wx & 32) != 0);
                ImGui::TextDisabled("%d x %d pixels", im->w, im->h);
            }
            const Plane *plane = p.plane >= 0 ? &s.planes[p.plane] : nullptr;
            bool locked = p.locked || (plane && plane->locked);
            if (locked)
                ImGui::TextColored(accent, "Layer is locked");
            ImGui::BeginDisabled(locked);
            int xy[] = {p.object.depth - (plane ? plane->source.x1 : 0),
                        p.object.sy - (plane ? plane->source.y1 : 0)};
            bool fx = (p.object.wx & 16) != 0, fy = (p.object.wx & 32) != 0;
            int palette = p.object.fl;
            ImGui::SetNextItemWidth(-1);
            bool changed = ImGui::InputInt2("##position", xy, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Position in layer: X / Y");
            changed |= ImGui::Checkbox("Flip X", &fx);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Flip Y", &fy);
            if (changed)
                doc.set_object(p.id, xy[0], xy[1], palette, fx, fy);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##palette",
                                  palette >= 0 && palette < (int)s.assets->data.palettes.size()
                                      ? s.assets->data.palettes[palette].name
                                      : "Missing palette")) {
                for (size_t pi = 0; pi < s.assets->data.palettes.size(); pi++) {
                    ImGui::PushID((int)pi);
                    if (ImGui::Selectable(s.assets->data.palettes[pi].name, palette == (int)pi)) {
                        doc.set_object(p.id, xy[0], xy[1], (int)pi, fx, fy);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Palette");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##layer", plane ? plane->name.c_str() : "Assign to layer")) {
                for (size_t i = 0; i < s.planes.size(); i++)
                    if (ImGui::Selectable(s.planes[i].name.c_str(), p.plane == (int)i)) {
                        doc.assign_plane(t.selected, (int)i);
                        t.plane = (int)i;
                    }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            if (ImGui::Button("Duplicate", ImVec2(-1, 0)))
                t.selected = doc.duplicate(t.selected);
            if (ImGui::Button("Delete artwork", ImVec2(-1, 0))) {
                doc.erase(t.selected);
                t.selected.clear();
            }
            ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::TextDisabled("Arrow keys: 1 px / Shift: 10 px");
        } else if (t.plane >= 0 && t.plane < (int)s.planes.size()) {
            auto p = s.planes[t.plane];
            char name[64];
            std::snprintf(name, sizeof name, "%s", p.name.c_str());
            ImGui::TextDisabled("LAYER");
            ImGui::BeginDisabled(p.locked);
            ImGui::SetNextItemWidth(-1);
            bool changed =
                ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue);
            int xy[] = {p.x, p.y};
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::InputInt2("##layerpos", xy, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Stage position: X / Y");
            float scroll = (float)p.scroll;
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::InputFloat("##parallax", &scroll, 0, 0, "%.3fx",
                                         ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Parallax (1.0 = follows camera)");
            if (changed)
                doc.set_plane(t.plane, name, xy[0], xy[1], scroll);
            if (ImGui::Button("Move forward"))
                doc.reorder_plane(t.plane, 1);
            if (ImGui::Button("Move backward"))
                doc.reorder_plane(t.plane, -1);
            ImGui::EndDisabled();
            bool visible = !p.hidden, locked = p.locked;
            if (ImGui::Checkbox("Visible", &visible))
                doc.set_plane_flags(t.plane, !visible, p.locked);
            if (ImGui::Checkbox("Locked", &locked))
                doc.set_plane_flags(t.plane, p.hidden, locked);
            ImGui::Separator();
            ImGui::TextWrapped("Select this layer, then drag its artwork to move the entire layer. "
                               "Select a child in the tree to move one piece.");
            ImGui::Spacing();
            ImGui::TextDisabled("%s",
                                p.bound ? "Runtime placement imported" : "Estimated placement");
            ImGui::TextWrapped("Save keeps your local layout. Use Build & Check to review and "
                               "apply layer changes to the game.");
        } else
            ImGui::TextWrapped(
                "Select artwork on the canvas, or choose a layer in the stage tree.");
    }
    void canvas(Tab &t) {
        ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.0f, size.x);
        size.y = std::max(1.0f, size.y);
        canvas_rect = {origin.x, origin.y, size.x, size.y};
        if (t.fit) {
            t.view.fit(t.camera_preview ? Rect{0, 0, 400, 254} : t.document.bounds(t.source),
                       {0, 0, size.x, size.y});
            t.fit = false;
        }
        auto *draw = ImGui::GetWindowDrawList();
        auto &io = ImGui::GetIO();
        ImGui::InvisibleButton("canvas", size,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonMiddle |
                                   ImGuiButtonFlags_MouseButtonRight);
        bool hovered = ImGui::IsItemHovered();
        Point world = t.view.to_world(point(io.MousePos), point(origin));
        Point cam = t.camera_preview ? t.camera : Point{};
        if (hovered && io.MouseWheel != 0 && !dragging) {
            t.view.zoom_at(t.view.zoom * std::pow(1.15, io.MouseWheel), point(io.MousePos),
                           point(origin));
            world = t.view.to_world(point(io.MousePos), point(origin));
        }
        bool pan = hovered && (ImGui::IsMouseDragging(2) ||
                               (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDragging(0)));
        if (pan) {
            t.view.pan.x -= io.MouseDelta.x / t.view.zoom;
            t.view.pan.y -= io.MouseDelta.y / t.view.zoom;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto *payload = ImGui::AcceptDragDropPayload("STUDIO_ASSET")) {
                const auto &asset = *static_cast<const AssetPayload *>(payload->Data);
                int slot = asset.slot;
                if (asset.tab != t.id || slot < 0 ||
                    slot >= (int)t.document.state().assets->data.images.size())
                    toast("Import this artwork into the destination stage before placing it.");
                else if (t.source)
                    toast("Switch to Stage composition to place artwork.");
                else if (t.plane < 0)
                    toast("Choose or add a layer first.");
                else {
                    const auto &p = t.document.state().planes[t.plane];
                    Point pos = world;
                    if (t.camera_preview) {
                        pos.x += cam.x * p.scroll;
                        pos.y += cam.y;
                    }
                    ObjectId id = t.document.place(t.document.state().assets->data.images[slot].idx,
                                                   asset_palette(t, slot), t.plane, pos);
                    if (id)
                        t.selected = {id};
                    else
                        toast("Unlock the selected layer to place artwork.");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (hovered && !pan && !ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseClicked(0) &&
            !ImGui::GetDragDropPayload()) {
            ObjectId hit = t.document.pick(world, cam, t.source, t.solo);
            if (hit) {
                const auto *object = t.document.object(hit);
                bool layer_drag = t.move_layer && t.selected.empty() && t.plane >= 0 &&
                                  object->plane == t.plane && !io.KeyShift && !io.KeyCtrl &&
                                  !io.KeyAlt;
                if (io.KeyCtrl || io.KeyShift) {
                    if (has(t.selected, hit))
                        t.selected.erase(std::remove(t.selected.begin(), t.selected.end(), hit),
                                         t.selected.end());
                    else
                        t.selected.push_back(hit);
                } else if (!has(t.selected, hit) && !layer_drag)
                    t.selected = {hit};
                if (object->plane >= 0)
                    t.plane = object->plane;
                if (!t.source && !io.KeyCtrl) {
                    t.document.begin(layer_drag ? "Move layer" : "Move artwork");
                    drag_plane = layer_drag ? t.plane : -1;
                    drag_selection = t.selected;
                    dragging = true;
                    drag_start = world;
                    drag_last = {};
                    moved = false;
                }
            } else {
                if (!io.KeyShift && !io.KeyCtrl)
                    t.selected.clear();
                marquee = true;
                drag_start = world;
                drag_selection = t.selected;
            }
        }
        if (dragging && ImGui::IsMouseDown(0)) {
            int dx = (int)std::round(world.x - drag_start.x),
                dy = (int)std::round(world.y - drag_start.y);
            if (io.KeyShift) {
                if (std::abs(dx) > std::abs(dy))
                    dy = 0;
                else
                    dx = 0;
            }
            if (snap && !io.KeyAlt && drag_plane < 0 && !drag_selection.empty()) {
                // Align visible instance edges in the same scene coordinates used by picking.
                auto items = t.document.scene(cam, false, t.solo);
                const SceneItem *lead = nullptr;
                for (const auto &p : items)
                    if (p.id == drag_selection.front()) {
                        lead = &p;
                        break;
                    }
                if (lead) {
                    double bestx = 6 / t.view.zoom, besty = bestx;
                    int ax = 0, ay = 0;
                    double previous_x = drag_last.x, previous_y = drag_last.y;
                    for (const auto &q : items)
                        if (!has(drag_selection, q.id)) {
                            for (int a = 0; a < 2; a++)
                                for (int b = 0; b < 2; b++) {
                                    double ex = q.rect.x + b * q.rect.w -
                                                (lead->rect.x - previous_x + dx + a * lead->rect.w);
                                    double ey = q.rect.y + b * q.rect.h -
                                                (lead->rect.y - previous_y + dy + a * lead->rect.h);
                                    if (std::abs(ex) < bestx) {
                                        bestx = std::abs(ex);
                                        ax = (int)std::round(ex);
                                    }
                                    if (std::abs(ey) < besty) {
                                        besty = std::abs(ey);
                                        ay = (int)std::round(ey);
                                    }
                                }
                        }
                    dx += ax;
                    dy += ay;
                }
            }
            // Snapping must not reintroduce movement on the constrained axis.
            if (io.KeyShift) {
                if (std::abs(world.x - drag_start.x) > std::abs(world.y - drag_start.y))
                    dy = 0;
                else
                    dx = 0;
            }
            if (drag_plane >= 0)
                t.document.preview_plane(drag_plane, dx, dy);
            else
                t.document.preview_move(drag_selection, dx, dy);
            drag_last = {(double)dx, (double)dy};
            moved = moved || dx || dy;
        }
        if (marquee && !ImGui::IsMouseDown(0)) {
            Rect box{std::min(drag_start.x, world.x), std::min(drag_start.y, world.y),
                     std::abs(world.x - drag_start.x), std::abs(world.y - drag_start.y)};
            t.selected = drag_selection;
            if (box.w > 2 / t.view.zoom || box.h > 2 / t.view.zoom)
                for (const auto &p : t.document.scene(cam, t.source, t.solo))
                    if (!p.locked && p.rect.x < box.x + box.w && p.rect.x + p.rect.w > box.x &&
                        p.rect.y < box.y + box.h && p.rect.y + p.rect.h > box.y &&
                        !has(t.selected, p.id))
                        t.selected.push_back(p.id);
            marquee = false;
        }
        if (dragging && !ImGui::IsMouseDown(0)) {
            t.document.commit();
            dragging = false;
            drag_plane = -1;
            drag_last = {};
        }
        draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                            IM_COL32(20, 25, 31, 255));
        if (grid) {
            double step = 32;
            while (step * t.view.zoom < 16)
                step *= 2;
            for (double x = std::floor(t.view.pan.x / step) * step;
                 x < t.view.pan.x + size.x / t.view.zoom; x += step) {
                float sx = vec(t.view.to_screen({x, 0}, point(origin))).x;
                draw->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + size.y),
                              IM_COL32(37, 45, 54, 255));
            }
            for (double y = std::floor(t.view.pan.y / step) * step;
                 y < t.view.pan.y + size.y / t.view.zoom; y += step) {
                float sy = vec(t.view.to_screen({0, y}, point(origin))).y;
                draw->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + size.x, sy),
                              IM_COL32(37, 45, 54, 255));
            }
        }
        auto items = t.document.scene(cam, t.source, t.solo);
        for (const auto &p : items) {
            ImVec2 a = vec(t.view.to_screen({p.rect.x, p.rect.y}, point(origin))),
                   b = vec(
                       t.view.to_screen({p.rect.x + p.rect.w, p.rect.y + p.rect.h}, point(origin)));
            if (b.x < origin.x || a.x > origin.x + size.x || b.y < origin.y ||
                a.y > origin.y + size.y)
                continue;
            image(draw, t, (int)p.image_slot, p.palette, a, b, p.hflip, p.vflip);
            if (has(t.selected, p.id))
                draw->AddRect(a, b, selection_color, 0, 0, 1.5f);
        }
        if (t.selected.empty() && t.plane >= 0) {
            bool first = true;
            Rect bounds;
            for (const auto &p : items)
                if (t.document.state().objects[p.object_index].plane == t.plane) {
                    if (first) {
                        bounds = p.rect;
                        first = false;
                    } else {
                        double right = std::max(bounds.x + bounds.w, p.rect.x + p.rect.w),
                               bottom = std::max(bounds.y + bounds.h, p.rect.y + p.rect.h);
                        bounds.x = std::min(bounds.x, p.rect.x);
                        bounds.y = std::min(bounds.y, p.rect.y);
                        bounds.w = right - bounds.x;
                        bounds.h = bottom - bounds.y;
                    }
                }
            if (!first)
                draw->AddRect(vec(t.view.to_screen({bounds.x, bounds.y}, point(origin))),
                              vec(t.view.to_screen({bounds.x + bounds.w, bounds.y + bounds.h},
                                                   point(origin))),
                              selection_color, 0, 0, 1.5f);
        }
        if (!t.source) {
            Point frame = t.camera_preview ? Point{}
                                           : Point{(double)t.document.state().start_x,
                                                   (double)t.document.state().start_y};
            ImVec2 a = vec(t.view.to_screen(frame, point(origin))),
                   b = vec(t.view.to_screen({frame.x + 400, frame.y + 254}, point(origin)));
            if (t.camera_preview) {
                const ImU32 shade = IM_COL32(10, 15, 20, 145);
                const ImVec2 end(origin.x + size.x, origin.y + size.y);
                float x1 = std::clamp(a.x, origin.x, end.x), x2 = std::clamp(b.x, origin.x, end.x);
                float y1 = std::clamp(a.y, origin.y, end.y), y2 = std::clamp(b.y, origin.y, end.y);
                draw->AddRectFilled(origin, ImVec2(end.x, y1), shade);
                draw->AddRectFilled(ImVec2(origin.x, y2), end, shade);
                draw->AddRectFilled(ImVec2(origin.x, y1), ImVec2(x1, y2), shade);
                draw->AddRectFilled(ImVec2(x2, y1), ImVec2(end.x, y2), shade);
            }
            draw->AddRect(a, b, IM_COL32(211, 185, 125, 180), 0, 0, 1.0f);
            draw->AddText(ImVec2(a.x + 7, a.y + 6), IM_COL32(229, 208, 159, 220),
                          "GAME FRAME  400 x 254");
            double ground = t.document.state().ground - (t.camera_preview ? t.camera.y : 0);
            float gy = vec(t.view.to_screen({0, ground}, point(origin))).y;
            draw->AddLine(ImVec2(a.x, gy), ImVec2(b.x, gy), IM_COL32(211, 185, 125, 100));
        }
        if (marquee) {
            ImVec2 a = vec(t.view.to_screen(drag_start, point(origin))), b = io.MousePos;
            ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y)),
                hi(std::max(a.x, b.x), std::max(a.y, b.y));
            draw->AddRectFilled(lo, hi, IM_COL32(37, 128, 222, 25));
            draw->AddRect(lo, hi, selection_color);
        }
        if (items.empty())
            draw->AddText(ImVec2(origin.x + 32, origin.y + 42), IM_COL32(143, 159, 175, 255),
                          "Drag artwork from Assets into the selected layer.");
        draw->PopClipRect();
    }
    void assets(Tab &t, bool full) {
        ImGui::TextUnformatted("ASSETS");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##search", "Search artwork", search, sizeof search);
        ImGui::SameLine();
        if (ImGui::Button("Import..."))
            import_dialog();
        ImGui::SameLine();
        ImGui::TextDisabled("Drag to place");
        const auto bank = t.document.state().assets;
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / 124));
        if (ImGui::BeginTable("assetgrid", cols, ImGuiTableFlags_SizingStretchSame)) {
            for (size_t i = 0; i < bank->data.images.size(); i++) {
                std::string name = image_name(t, (int)i), filter = search;
                auto fold = [](std::string v) {
                    for (char &c : v)
                        if (c >= 'A' && c <= 'Z')
                            c += 'a' - 'A';
                    return v;
                };
                if (!filter.empty() && fold(name).find(fold(filter)) == std::string::npos)
                    continue;
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                auto &im = bank->data.images[i];
                ImVec2 p = ImGui::GetCursorScreenPos();
                float width = ImGui::GetContentRegionAvail().x, height = full ? 114.0f : 84.0f;
                bool click =
                    ImGui::Selectable("##asset", t.asset == (int)i, 0, ImVec2(width, height));
                if (click)
                    t.asset = (int)i;
                if (ImGui::BeginDragDropSource()) {
                    AssetPayload asset{t.id, (int)i};
                    ImGui::SetDragDropPayload("STUDIO_ASSET", &asset, sizeof asset);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }
                float scale = std::min((width - 16) / im.w, (height - 30) / im.h);
                ImVec2 a(p.x + (width - im.w * scale) / 2, p.y + 5),
                    b(a.x + im.w * scale, a.y + im.h * scale);
                image(ImGui::GetWindowDrawList(), t, (int)i, asset_palette(t, (int)i), a, b);
                ImGui::GetWindowDrawList()->PushClipRect(p, ImVec2(p.x + width, p.y + height),
                                                         true);
                ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 6, p.y + height - 22),
                                                    IM_COL32(197, 208, 218, 255), name.c_str());
                ImGui::GetWindowDrawList()->PopClipRect();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%d x %d pixels | image %d", name.c_str(), im.w, im.h,
                                      im.idx);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    bool game_build_running() const {
        for (const auto &t : tabs)
            if (t->game_build.running())
                return true;
        return false;
    }
    void prepare_game(Tab &t) {
        fs::path folder;
        if (!smoke_dir.empty())
            folder = fs::u8path(smoke_dir) / "exports";
        else {
            char *pref = SDL_GetPrefPath("midway-bddtool", "studio");
            if (!pref) {
                error = "Cannot locate the export folder.";
                return;
            }
            folder = fs::u8path(pref) / "exports";
            SDL_free(pref);
        }
        auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        folder /= "export-" + std::to_string(stamp);
        auto package = std::make_unique<GameExport>();
        if (prepare_game_export(t.document, t.game_root, folder.u8string(), *package, error,
                                t.game_label)) {
            t.game_export = std::move(package);
            toast("Export prepared. Review the changes below before applying.");
        }
    }
    void game_integration(Tab &t) {
        ImGui::TextUnformatted("GAME INTEGRATION");
        ImGui::TextWrapped(
            "Prepare the live layout, review it, then apply it to an existing game stage. The full "
            "game build regenerates LOAD2 tables and assembles the result.");
        ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - 150));
        ImGui::InputTextWithHint("##game-root", "Game checkout folder", t.game_root,
                                 sizeof t.game_root);
        ImGui::SameLine();
        if (ImGui::Button("Choose folder..."))
            folder_dialog_open("Choose game checkout", t.game_root, sizeof t.game_root);
        ImGui::SetNextItemWidth(270);
        ImGui::InputTextWithHint("##game-label", "Stage label (automatic if empty)", t.game_label,
                                 sizeof t.game_label);
        bool busy = game_build_running();
        ImGui::BeginDisabled(busy || !t.game_root[0] || t.document.transaction_active());
        if (ImGui::Button("Prepare game export"))
            prepare_game(t);
        ImGui::EndDisabled();
        if (t.game_export) {
            auto &package = *t.game_export;
            std::error_code path_error;
            auto selected_root = fs::weakly_canonical(fs::u8path(t.game_root), path_error);
            std::string selected_label = t.game_label;
            for (char &c : selected_label)
                if (c >= 'a' && c <= 'z')
                    c -= 32;
            bool current = !path_error && package.revision == t.document.state().revision &&
                           package.root == selected_root.u8string() &&
                           package.requested_label == selected_label;
            if (!current)
                ImGui::TextColored(accent,
                                   "Layout or destination changed. Prepare a fresh export.");
            ImGui::BeginChild("game-review", ImVec2(0, 210), ImGuiChildFlags_Border);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(package.report.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            if (ImGui::Button("Copy export folder"))
                ImGui::SetClipboardText(package.folder.c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(busy || !current || package.applied);
            if (ImGui::Button("Apply reviewed export")) {
                if (apply_game_export(package, error))
                    toast("Game sources updated; backups saved. Run Build game next.");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(busy || !current || !package.applied);
            if (ImGui::Button("Build game")) {
                bool unchanged = true;
                for (const auto &file : package.files) {
                    std::ifstream in(fs::u8path(package.root) / fs::u8path(file.relative),
                                     std::ios::binary);
                    std::string bytes((std::istreambuf_iterator<char>(in)), {});
                    if (!in || bytes != file.after) {
                        error = "Game source changed after apply: " + file.relative +
                                ". Prepare again.";
                        unchanged = false;
                        break;
                    }
                }
                if (unchanged &&
                    t.game_build.start(
                        package.root, (fs::u8path(package.folder) / "build.log").u8string(), error))
                    toast("Game build started. The log is shown below.");
            }
            ImGui::EndDisabled();
            if (package.applied)
                ImGui::TextDisabled("Sources applied. ROM packaging and emulator verification "
                                    "follow the game build.");
        }
        if (t.game_build.started()) {
            if (t.game_build.running())
                ImGui::TextColored(accent, "Game build running...");
            else if (t.game_build.exit_code() == 0)
                ImGui::TextColored(accent, "Game build finished successfully. Package and verify "
                                           "it in the emulator next.");
            else
                ImGui::TextColored(ImVec4(.98f, .48f, .42f, 1),
                                   "Game build failed (exit %d). See the log below.",
                                   t.game_build.exit_code());
            if (ImGui::SmallButton("Copy build log path"))
                ImGui::SetClipboardText(t.game_build.log_path().c_str());
            std::ifstream log(fs::u8path(t.game_build.log_path()),
                              std::ios::binary | std::ios::ate);
            if (log) {
                auto end = log.tellg();
                log.seekg(end > std::streamoff(32768) ? end - std::streamoff(32768)
                                                      : std::streampos(0));
                std::string text((std::istreambuf_iterator<char>(log)), {});
                ImGui::BeginChild("build-log", ImVec2(0, 180), ImGuiChildFlags_Border,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(text.c_str());
                ImGui::EndChild();
            }
        }
        ImGui::Separator();
    }
    void checks(Tab &t) {
        heading("Build & Check", "Review your layout, apply it to the game, and follow the build.");
        if (!t.document.notice().empty())
            ImGui::TextWrapped("%s", t.document.notice().c_str());
        ImGui::TextWrapped("Authoring preview shows current BDB artwork with local layer "
                           "transforms. Runtime actors, game-specific floor effects and compiled "
                           "ROM output are not verified in this workspace.");
        ImGui::Spacing();
        game_integration(t);
        auto issues = t.document.validate();
        ImGui::Text("%zu authoring issues", issues.size());
        for (size_t i = 0; i < issues.size(); i++) {
            const auto &issue = issues[i];
            ImGui::PushID((int)i);
            ImGui::TextColored(issue.error ? ImVec4(0.98f, .48f, .42f, 1)
                                           : ImVec4(.85f, .73f, .47f, 1),
                               "%s", issue.error ? "FIX" : "REVIEW");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", issue.message.c_str());
            if (issue.object) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Locate")) {
                    t.selected = {issue.object};
                    page = 0;
                    t.fit = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Specialist tools");
        ImGui::TextWrapped(
            "The existing editor remains available for pixel editing, palette reduction, IMG/LOD "
            "import, LOAD2 diagnostics, and external game assembly workflows.");
        if (ImGui::Button("Copy specialist launch command")) {
            std::string command = "bddview --legacy-ui";
            if (!t.document.path().empty())
                command += " \"" + t.document.path() + "\"";
            ImGui::SetClipboardText(command.c_str());
            toast("Launch command copied");
        }
    }
    void stage(Tab &t) {
        if (ImGui::Button(t.source ? "Source layout" : "Stage composition")) {
            cancel_gesture();
            t.source = !t.source;
            t.camera_preview = false;
            t.fit = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use Stage composition to drag artwork; Source layout shows the "
                              "packed file coordinates.");
        ImGui::SameLine();
        if (ImGui::Button("Fit"))
            t.fit = true;
        ImGui::SameLine();
        if (ImGui::Button("1:1"))
            t.view.zoom = 1;
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%", t.view.zoom * 100);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snap);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &grid);
        ImGui::SameLine();
        ImGui::BeginDisabled(t.source);
        if (ImGui::Checkbox("Camera preview", &t.camera_preview)) {
            cancel_gesture();
            t.fit = true;
        }
        ImGui::EndDisabled();
        float avail = ImGui::GetContentRegionAvail().y;
        float tray_h = tray ? std::min(164.0f, avail * .28f) : 0;
        float body_h = std::max(120.0f, avail - tray_h - 43);
        float width = ImGui::GetContentRegionAvail().x;
        float left = width < 1000 ? 185.0f : 218.0f, right = width < 1000 ? 240.0f : 276.0f,
              center = std::max(180.0f, width - left - right - 16);
        ImGui::BeginChild("tree", ImVec2(left, body_h), ImGuiChildFlags_Border);
        tree(t);
        ImGui::EndChild();
        ImGui::SameLine(0, 8);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("canvas-container", ImVec2(center, body_h), ImGuiChildFlags_Border,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        canvas(t);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 8);
        ImGui::BeginChild("inspector", ImVec2(0, body_h), ImGuiChildFlags_Border);
        inspector(t);
        ImGui::EndChild();
        ImGui::BeginDisabled(!t.camera_preview);
        if (ImGui::Button("Start")) {
            t.camera = {(double)t.document.state().start_x, (double)t.document.state().start_y};
        }
        ImGui::SameLine();
        float cx = (float)t.camera.x;
        Rect b = t.document.bounds();
        ImGui::SetNextItemWidth(std::max(120.0f, width - 450));
        if (ImGui::SliderFloat("##camera", &cx, (float)b.x, (float)std::max(b.x, b.x + b.w - 400),
                               "Camera X %.0f"))
            t.camera.x = std::round(cx);
        ImGui::SameLine();
        int cy = (int)t.camera.y;
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Y", &cy))
            t.camera.y = cy;
        ImGui::SameLine();
        if (ImGui::Button("Set start"))
            t.document.set_start((int)t.camera.x, (int)t.camera.y, t.document.state().ground);
        ImGui::EndDisabled();
        if (tray) {
            ImGui::BeginChild("assettray", ImVec2(0, tray_h), ImGuiChildFlags_Border);
            assets(t, false);
            ImGui::EndChild();
        }
    }
    void welcome() {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 60);
        ImGui::Indent(48);
        ImGui::TextColored(accent, "YOUR STAGE, IN VIEW");
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.8f);
        ImGui::TextUnformatted("Arrange the scene.");
        ImGui::SetWindowFontScale(1);
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Open a background, place artwork, and compose your layers directly on the canvas.");
        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::Button("Open BDB / BDD", ImVec2(200, 42)))
            open_dialog();
        ImGui::SameLine();
        if (ImGui::Button("New stage", ImVec2(150, 42)))
            add(Document::empty());
        ImGui::Spacing();
        if (ImGui::Button("Explore sample stage", ImVec2(200, 36)))
            add(Document::demo());
        ImGui::Spacing();
        ImGui::TextDisabled("You can also drop a BDB or BDD file into this window.");
        ImGui::Unindent(48);
    }
    void prompts() {
        if (show_recovery) {
            ImGui::OpenPopup("Recovery copies");
            show_recovery = false;
        }
        if (ImGui::BeginPopupModal("Recovery copies", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Open a recovery copy, then use Save as to keep it.");
            char *pref = SDL_GetPrefPath("midway-bddtool", "studio");
            if (pref) {
                fs::path folder = fs::u8path(pref) / "recovery";
                SDL_free(pref);
                std::error_code ec;
                std::vector<fs::path> files;
                for (fs::directory_iterator it(folder, ec), end; it != end && !ec;
                     it.increment(ec)) {
                    auto p = it->path();
                    if (p.extension() == ".BDB")
                        files.push_back(p);
                    else if (p.extension() == ".BDD") {
                        auto pair = p;
                        pair.replace_extension(".BDB");
                        if (!fs::exists(pair, ec))
                            files.push_back(p);
                    }
                }
                std::sort(files.rbegin(), files.rend());
                ImGui::BeginChild("copies", ImVec2(600, 260));
                if (files.empty())
                    ImGui::TextDisabled(
                        "No recovery copies yet. Edited stages are copied every minute.");
                for (const auto &path : files)
                    if (ImGui::Selectable(path.filename().u8string().c_str())) {
                        open(path.u8string());
                        ImGui::CloseCurrentPopup();
                    }
                ImGui::EndChild();
                if (ImGui::Button("Copy folder path"))
                    ImGui::SetClipboardText(folder.u8string().c_str());
                ImGui::SameLine();
            }
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        bool displaying_error = !error.empty();
        if (displaying_error)
            ImGui::OpenPopup("Could not complete action");
        if (ImGui::BeginPopupModal("Could not complete action", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 500);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", ImVec2(100, 0))) {
                error.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (displaying_error)
            return;
        if (close_pending >= 0 || quit_pending) {
            if ((quit_pending && game_build_running()) ||
                (close_pending >= 0 && close_pending < (int)tabs.size() &&
                 tabs[close_pending]->game_build.running())) {
                error =
                    "Wait for the game build to finish before closing its tab or the application.";
                quit_pending = false;
                close_pending = -1;
                return;
            }
            if (dragging || marquee)
                cancel_gesture();
            bool dirty = false;
            if (quit_pending) {
                for (auto &t : tabs)
                    dirty |= t->document.dirty();
            } else if (close_pending < (int)tabs.size())
                dirty = tabs[close_pending]->document.dirty();
            if (!dirty) {
                if (quit_pending)
                    running = false;
                else
                    close_tab();
            } else
                ImGui::OpenPopup("Save your changes?");
        }
        if (ImGui::BeginPopupModal("Save your changes?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(quit_pending ? "There are unsaved changes in your open stages."
                                                : "This stage has unsaved changes.");
            if (ImGui::Button("Save", ImVec2(110, 0))) {
                bool ok = true;
                if (quit_pending) {
                    for (auto &t : tabs)
                        if (t->document.dirty() && !save(*t)) {
                            ok = false;
                            break;
                        }
                } else if (close_pending >= 0)
                    ok = save(*tabs[close_pending]);
                if (ok) {
                    ImGui::CloseCurrentPopup();
                    if (quit_pending)
                        running = false;
                    else
                        close_tab();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard changes", ImVec2(145, 0))) {
                ImGui::CloseCurrentPopup();
                if (quit_pending)
                    running = false;
                else
                    close_tab();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                quit_pending = false;
                close_pending = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    void close_tab() {
        if (close_pending >= 0 && close_pending < (int)tabs.size()) {
            tabs.erase(tabs.begin() + close_pending);
            if (active >= close_pending)
                active--;
            if (active < 0 && !tabs.empty())
                active = 0;
        }
        close_pending = -1;
    }
    void recover() {
        if (!smoke_dir.empty())
            return;
        double now = ImGui::GetTime();
        for (auto &ptr : tabs) {
            auto &t = *ptr;
            if (now < t.next_recovery)
                continue;
            t.next_recovery = now + 60;
            if (!t.document.dirty() || t.document.transaction_active() ||
                t.recovered_revision == t.document.state().revision)
                continue;
            char *pref = SDL_GetPrefPath("midway-bddtool", "studio");
            if (!pref)
                continue;
            fs::path folder = fs::u8path(pref) / "recovery";
            SDL_free(pref);
            std::error_code ec;
            fs::create_directories(folder, ec);
            if (ec)
                continue;
            std::string local_error;
            std::string filename =
                "stage-" + std::to_string(session_stamp) + "-" + std::to_string(t.id) + ".BDB";
            if (t.document.save((folder / filename).u8string(), local_error, true)) {
                t.recovered_revision = t.document.state().revision;
            } else
                toast("Recovery copy failed: " + local_error);
        }
    }
    uint64_t session_stamp = 0;
    void frame() {
        for (auto &t : tabs)
            t->game_build.poll();
        shortcuts();
        clean_selection();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Studio", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        menu();
        toolbar();
        document_tabs();
        float remaining = std::max(100.0f, ImGui::GetContentRegionAvail().y - 29);
        ImGui::BeginChild("workspace", ImVec2(0, remaining));
        if (auto *t = tab()) {
            if (page == 0)
                stage(*t);
            else if (page == 1)
                assets(*t, true);
            else
                checks(*t);
        } else
            welcome();
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::GetTime() < message_until)
            ImGui::TextColored(accent, "%s", message.c_str());
        else if (auto *t = tab()) {
            ImGui::TextDisabled(
                "%zu selected  /  %zu objects    |    %.0f%%    |    %s    |    Authoring preview",
                t->selected.size(), t->document.state().objects.size(), t->view.zoom * 100,
                t->document.dirty() ? "Unsaved changes" : "Saved");
        } else
            ImGui::TextDisabled("Local files  /  Indexed artwork  /  Stage composition");
        ImGui::End();
        prompts();
        recover();
    }
};

// Deterministic input through ImGui's normal event path, used only by --studio-smoke.
struct InteractionSmoke {
    ObjectId id = 0;
    int initial_x = 0;
    Point mouse;
    void input(App &app, int frame) {
        auto *t = app.tab();
        if (!t)
            return;
        auto &io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        if (frame == 14) {
            app.page = 0;
            app.snap = false;
            t->camera_preview = false;
            t->fit = true;
            t->move_layer = false;
            t->selected.clear();
            id = t->document.state().objects.front().id;
            initial_x = t->document.object(id)->object.depth;
        }
        if (frame == 15) {
            auto r = t->document.scene().front().rect;
            mouse = t->view.to_screen({r.x + 10, r.y + 10}, {app.canvas_rect.x, app.canvas_rect.y});
            io.AddMousePosEvent((float)mouse.x, (float)mouse.y);
        }
        if (frame == 16)
            io.AddMouseButtonEvent(0, true);
        if (frame == 17)
            io.AddMousePosEvent((float)(mouse.x + 32 * t->view.zoom), (float)mouse.y);
        if (frame == 18)
            io.AddMouseButtonEvent(0, false);
        if (frame == 20) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_Z, true);
        }
        if (frame == 21) {
            io.AddKeyEvent(ImGuiKey_Z, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
        }
        if (frame == 22) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_Y, true);
        }
        if (frame == 23) {
            io.AddKeyEvent(ImGuiKey_Y, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
        }
        if (frame == 24)
            io.AddMouseButtonEvent(0, true);
        if (frame == 25)
            io.AddMousePosEvent((float)(mouse.x + 64 * t->view.zoom), (float)mouse.y);
        if (frame == 26)
            io.AddKeyEvent(ImGuiKey_Escape, true);
        if (frame == 27) {
            io.AddKeyEvent(ImGuiKey_Escape, false);
            io.AddMouseButtonEvent(0, false);
        }
    }
    bool check(App &app, int frame) {
        if (frame != 18 && frame != 20 && frame != 22 && frame != 27)
            return true;
        auto *p = app.tab()->document.object(id);
        int expected = initial_x + (frame == 20 ? 0 : 32);
        if (!p || p->object.depth != expected) {
            std::fprintf(stderr, "Studio interaction failed at frame %d: expected X %d, got %d\n",
                         frame, expected, p ? p->object.depth : -999);
            return false;
        }
        if (frame == 27)
            std::fprintf(
                stderr, "Studio canvas drag, keyboard undo/redo and Escape cancellation passed.\n");
        return true;
    }
};

void theme() {
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(9, 5);
    s.ItemSpacing = ImVec2(8, 7);
    s.WindowRounding = 0;
    s.ChildRounding = 5;
    s.FrameRounding = 4;
    s.TabRounding = 4;
    s.PopupRounding = 5;
    s.ScrollbarSize = 12;
    s.ChildBorderSize = 1;
    s.WindowBorderSize = 0;
    auto *c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(.065f, .079f, .096f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(.084f, .102f, .123f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(.105f, .125f, .15f, 1);
    c[ImGuiCol_MenuBarBg] = ImVec4(.065f, .079f, .096f, 1);
    c[ImGuiCol_Border] = ImVec4(.16f, .19f, .23f, 1);
    c[ImGuiCol_Text] = ImVec4(.85f, .89f, .93f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(.48f, .56f, .64f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(.12f, .15f, .18f, 1);
    c[ImGuiCol_FrameBgHovered] = accent_surface(.32f);
    c[ImGuiCol_FrameBgActive] = accent_surface(.50f);
    c[ImGuiCol_Button] = ImVec4(.14f, .18f, .22f, 1);
    c[ImGuiCol_ButtonHovered] = accent_surface(.40f);
    c[ImGuiCol_ButtonActive] = accent_surface(.60f);
    c[ImGuiCol_Header] = accent_surface(.35f);
    c[ImGuiCol_HeaderHovered] = accent_surface(.45f);
    c[ImGuiCol_HeaderActive] = accent_surface(.60f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(.30f, .62f, .95f, 1);
    c[ImGuiCol_Tab] = ImVec4(.09f, .12f, .15f, 1);
    c[ImGuiCol_TabActive] = accent_surface(.30f);
    c[ImGuiCol_TabHovered] = accent_surface(.50f);
    c[ImGuiCol_TabUnfocusedActive] = accent_surface(.20f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, .35f);
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_DragDropTarget] = accent;
}
} // namespace

int run(int argc, char **argv) {
    bool smoke = argc >= 3 && std::string(argv[1]) == "--studio-smoke";
    bdd_prepare_app_identity();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "studio: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    App app;
    app.session_stamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    app.window =
        SDL_CreateWindow("BDD Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900,
                         SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
    if (!app.window) {
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(app.window, 900, 640);
    bdd_set_app_icon(app.window);
    app.renderer =
        SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app.renderer)
        app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
    if (!app.renderer) {
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    app.textures.renderer = app.renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    theme();
    const char *fonts[] = {"C:/Windows/Fonts/segoeui.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/System/Library/Fonts/SFNS.ttf"};
    bool font = false;
    for (const char *path : fonts)
        if (fs::exists(path)) {
            font = io.Fonts->AddFontFromFileTTF(path, 16) != nullptr;
            if (font)
                break;
        }
    if (!font)
        io.Fonts->AddFontDefault();
    ImGui_ImplSDL2_InitForSDLRenderer(app.window, app.renderer);
    ImGui_ImplSDLRenderer2_Init(app.renderer);
    editor_project_storage_init();
    if (smoke) {
        app.smoke_dir = argv[2];
        fs::create_directories(fs::u8path(app.smoke_dir));
        if (argc >= 4)
            app.open(argv[3]);
        else
            app.add(Document::demo());
    } else if (argc >= 2 && std::string(argv[1]) == "--studio-demo")
        app.add(Document::demo());
    else if (argc >= 2)
        app.open(argv[1]);
    int frames = 0, rc = 0;
    InteractionSmoke interactions;
    while (app.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                app.quit_pending = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                app.cancel_gesture();
            if (event.type == SDL_DROPFILE) {
                std::string path = event.drop.file;
                SDL_free(event.drop.file);
                std::string ext = fs::u8path(path).extension().u8string();
                for (char &c : ext)
                    if (c >= 'A' && c <= 'Z')
                        c += 'a' - 'A';
                if (ext == ".bdb" || ext == ".bdd")
                    app.open(path);
                else
                    app.import_image(path);
            }
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        if (smoke && argc < 4)
            interactions.input(app, frames);
        ImGui::NewFrame();
        app.frame();
        if (smoke && argc < 4 && !interactions.check(app, frames)) {
            rc = 1;
            app.running = false;
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(app.renderer, 17, 20, 25, 255);
        SDL_RenderClear(app.renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);
        if (smoke && (frames == 3 || frames == 7 || frames == 11 || frames == 31)) {
            int w, h;
            SDL_GetRendererOutputSize(app.renderer, &w, &h);
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            std::string name = frames == 3    ? "stage.png"
                               : frames == 7  ? "compact.png"
                               : frames == 11 ? "assets.png"
                                              : "game-export.png";
            auto path = (fs::u8path(app.smoke_dir) / name).u8string();
            if (SDL_RenderReadPixels(app.renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, rgba.data(),
                                     w * 4) != 0 ||
                !stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4))
                rc = 1;
        }
        SDL_RenderPresent(app.renderer);
        if (smoke) {
            frames++;
            if (frames == 4) {
                SDL_SetWindowSize(app.window, 1000, 720);
                if (app.tab())
                    app.tab()->fit = true;
            }
            if (frames == 8)
                app.page = 1;
            if (frames == 29) {
                app.page = 2;
                if (argc >= 5 && std::string(argv[4]) == "--prepare" && app.tab())
                    app.prepare_game(*app.tab());
            }
            if (frames >= 33)
                app.running = false;
        }
    }
    app.textures.clear();
    editor_project_storage_shutdown();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return rc;
}
} // namespace studio
