#include "Core/studio_document.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 0.00001; }
void same_scene(const Document &a, const Document &b) {
    auto x = a.scene({73, 11}), y = b.scene({73, 11});
    require(x.size() == y.size(), "Scene count changed after reopen");
    for (size_t i = 0; i < x.size(); i++)
        require(near(x[i].rect.x, y[i].rect.x) && near(x[i].rect.y, y[i].rect.y) &&
                    x[i].palette == y[i].palette && x[i].hflip == y[i].hflip,
                "Scene changed after reopen");
}
int main(int argc, char **argv) {
    try {
        namespace fs = std::filesystem;
        require(argc >= 2, "Expected scratch folder");
        fs::path root = argv[1];
        fs::create_directories(root);
        Viewport v;
        v.pan = {-40, 123};
        Point origin{12, 55}, mouse{314, 225};
        auto anchor = v.to_world(mouse, origin);
        v.zoom_at(.25, mouse, origin);
        auto after = v.to_world(mouse, origin);
        require(near(anchor.x, after.x) && near(anchor.y, after.y), "Zoom anchor drift");
        v.fit({-500, 0, 4000, 400}, {0, 0, 800, 600});
        require(v.zoom < 1, "Cannot fit wide stage");
        auto d = Document::demo();
        auto untouched = Document::demo();
        auto id = d.state().objects.front().id;
        auto initial = *d.object(id);
        auto assets = d.state().assets;
        d.begin("Drag");
        d.preview_move({id}, 5, 8);
        d.preview_move({id}, 20, -9);
        require(d.object(id)->object.depth == initial.object.depth + 20,
                "Drag accumulated instead of using initial state");
        d.cancel();
        require(d.object(id)->object.depth == initial.object.depth, "Cancel failed");
        require(d.move({id}, 1600, -500), "Move failed");
        require(d.object(id)->plane == initial.plane, "Moving changed layer ownership");
        require(d.state().assets == assets, "Placement copied asset pixels");
        require(untouched.object(id)->object.depth == initial.object.depth,
                "Documents share editable state");
        auto file = (root / "moved.BDB").u8string();
        std::string error;
        require(d.save(file, error), "Save: " + error);
        require(!d.dirty(), "Save stayed dirty");
        Document reopened;
        require(reopened.load(file, error), "Reopen: " + error);
        same_scene(d, reopened);
        require(reopened.object(id)->plane == initial.plane, "Repack lost ownership");
        require(d.undo(), "Undo failed");
        require(d.dirty(), "Undo from saved revision should be dirty");
        require(d.redo() && !d.dirty(), "Redo should restore saved revision");
        d.move({id}, 1, 2);
        require(d.save((root / "recovery.BDB").u8string(), error, true), "Recovery failed");
        require(d.dirty() && fs::equivalent(fs::u8path(d.path()), fs::u8path(file)),
                "Recovery changed save point/path");
        d.set_plane_flags(initial.plane, false, true);
        require(!d.move({id}, 100, 100), "Locked artwork moved");
        require(!d.erase({id}), "Locked artwork deleted");
        require(d.duplicate({id}).empty(), "Locked artwork duplicated");
        d.set_plane_flags(initial.plane, false, false);
        auto dup = d.duplicate({id});
        require(dup.size() == 1, "Duplicate failed");
        d.undo();
        require(!d.object(dup[0]), "Undo duplicate failed");
        auto pos = d.scene().front().rect;
        require(d.pick({pos.x + 2, pos.y + 2}) != 0, "Rendered artwork cannot be picked");
        auto isolated = Document::empty();
        uint8_t rgba[] = {255, 0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0, 0};
        int image = 0;
        require(isolated.import_rgba("test", 4, 1, rgba, error, image), "Import failed");
        auto placed = isolated.place(image, 0, 0, {10, 20});
        require(placed != 0, "Place failed");
        require(isolated.pick({10, 20}) == placed && isolated.pick({11, 20}) == 0,
                "Transparent picking failed");
        isolated.set_object(placed, 10, 20, 0, true, false);
        require(isolated.pick({10, 20}) == 0 && isolated.pick({13, 20}) == placed,
                "Flipped picking failed");
        isolated.set_plane(0, "Parallax", 30, 40, .5);
        auto projected = isolated.scene({100, 10});
        require(near(projected[0].rect.x, -10) && near(projected[0].rect.y, 50),
                "Camera projection differs from placement");
        auto imported = (root / "imported.BDB").u8string();
        require(isolated.save(imported, error), error);
        fs::path meta = root / "imported.BDD.meta";
        {
            std::ofstream out(meta, std::ios::app);
            out << "FUTURE\topaque metadata\n";
        }
        Document metadata;
        require(metadata.load(imported, error), error);
        require(metadata.save((root / "copied.BDB").u8string(), error), error);
        std::ifstream in(root / "copied.BDD.meta");
        std::string raw((std::istreambuf_iterator<char>(in)), {});
        require(raw.find("FUTURE\topaque metadata") != std::string::npos, "Unknown metadata lost");
        auto revision = d.state().revision;
        require(!d.save((root / "missing-folder" / "fail.BDB").u8string(), error),
                "Invalid folder save succeeded");
        require(d.state().revision == revision && d.dirty(), "Failed save changed document");
        if (argc >= 3) {
            Document fixture;
            require(fixture.load(argv[2], error), "Fixture load: " + error);
            auto out = (root / "fixture.BDB").u8string();
            require(fixture.save(out, error), "Fixture save: " + error);
            Document copy;
            require(copy.load(out, error), "Fixture reopen: " + error);
            same_scene(fixture, copy);
            const auto &a = fixture.state().assets->data;
            const auto &b = copy.state().assets->data;
            require(a.images.size() == b.images.size() && a.palettes.size() == b.palettes.size(),
                    "Fixture assets changed count");
            for (size_t i = 0; i < a.images.size(); i++)
                require(a.images[i].pix == b.images[i].pix && a.images[i].idx == b.images[i].idx,
                        "Fixture pixels/IDs changed");
            for (size_t i = 0; i < a.palettes.size(); i++)
                for (int c = 0; c < a.palettes[i].count; c++)
                    require(a.palettes[i].rgb555[c] == b.palettes[i].rgb555[c],
                            "Fixture RGB555 changed");
            for (auto p : fixture.state().objects)
                if (p.plane < 0)
                    fixture.assign_plane({p.id}, 0);
            auto fid = fixture.state().objects.front().id;
            fixture.move({fid}, 4000, -500);
            require(fixture.save((root / "fixture-moved.BDB").u8string(), error),
                    "Fixture repack: " + error);
            require(copy.load((root / "fixture-moved.BDB").u8string(), error), error);
            same_scene(fixture, copy);
            std::cout << "Fixture save/reopen, pixels, RGB555, IDs and repacked scene passed.\n";
        }
        std::cout << "Studio document: navigation, gestures, independent history, locks, picking, "
                     "repack/reopen, metadata, recovery and failure checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
