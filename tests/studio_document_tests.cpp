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
        {
            const uint8_t pixels[] = {255,0,0,255, 0,0,0,0, 255,0,0,255, 0,0,0,0};
            AssetBank input;
            std::string why;
            require(make_raster_asset("A", 4, 1, pixels, input, why), why);
            input.data.images.push_back(input.data.images[0]);
            input.metadata.push_back(input.metadata[0]);
            input.default_palettes.push_back(0);
            input.data.images[1].idx = 9; input.metadata[1].idx = 9;
            auto batch = Document::empty(); auto old = batch.state().assets;
            auto preview = prepare_asset_import(batch.state(), input, true, why);
            require(preview && preview->data.images.size() == 2 && batch.state().assets == old,
                    "Import review mutated the document");
            std::vector<int> ids;
            require(batch.import_assets(input, true, why, ids) && ids == std::vector<int>({0,1}), why);
            require(batch.state().assets->data.palettes.size() == 1 && batch.state().objects.empty(),
                    "Batch import changed layout or did not reuse palettes");
            require(batch.undo() && batch.state().assets == old && batch.redo(), "Batch import is not one undo action");
            auto before = batch.state().assets;
            input.default_palettes[1] = 99;
            require(!batch.import_assets(input, true, why, ids) && ids.empty() && batch.state().assets == before,
                    "Invalid later batch item partially imported");
            input.default_palettes[1] = 0;
            require(batch.import_assets(input, true, why, ids) && batch.state().assets->data.palettes.size() == 1,
                    "Exact palette reuse against existing artwork failed");
            require(batch.import_assets(input, false, why, ids) && batch.state().assets->data.palettes.size() == 2,
                    "Separate palettes option was ignored");
            batch.begin("Move");
            require(!batch.import_assets(input, true, why, ids) && batch.transaction_active(), "Import canceled an active edit");
            batch.cancel();
            auto file = (root / "batch-import.BDB").u8string();
            require(batch.save(file, why), why);
            Document reopened; require(reopened.load(file, why), why);
            require(reopened.state().assets->data.images.size() == 6 && reopened.state().assets->data.palettes.size() == 2,
                    "Batch did not survive verified Save/reopen");
            auto read = [](const fs::path &p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
            auto before_bdd = read(root / "batch-import.BDD"), before_bdb = read(file);
            // Simulate malformed imported state that the serializer cannot round-trip.
            auto broken = std::make_shared<AssetBank>(*reopened.state().assets);
            std::snprintf(broken->data.palettes[0].name, sizeof broken->data.palettes[0].name, "BROKEN NAME");
            const_cast<State &>(reopened.state()).assets = broken;
            require(!reopened.save(file, why) && why.find("Save verification failed") != std::string::npos,
                    "Save did not detect unreadable serialized palette data");
            require(read(root / "batch-import.BDD") == before_bdd && read(file) == before_bdb &&
                    !fs::exists(root / "batch-import.bddstudio-saving"), "Failed readback replaced original files");
            broken->data.images[0].pix.clear();
            require(!reopened.save(file, why) && why.find("pixel storage") != std::string::npos,
                    "Save passed a short pixel buffer to the serializer");
        }
        uint8_t rgba[] = {255, 0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0, 0};
        int image = 0;
        require(isolated.import_rgba("test", 4, 1, rgba, error, image), "Import failed");
        {
            auto edit = isolated;
            auto other = edit;
            auto first = edit.place(image, 0, 0, {0, 0});
            int copy = -1;
            require(edit.copy_palette_for_image(image, 0, error, copy) && copy == 1, error);
            require(edit.object(first)->object.fl == copy && edit.object(first)->object.wx == 0x4000,
                    "Palette copy corrupted flags or did not update palette binding");
            require(other.state().assets->data.palettes.size() == 1, "Palette copy leaked to other document");
            auto pal = edit.state().assets->data.palettes[copy];
            pal.rgb555[1] = 0x801f;
            require(edit.set_palette(copy, pal, error), error);
            require(edit.state().assets->data.palettes[0].rgb555[1] != 0x801f,
                    "Editing copied palette changed original");
            auto before = edit.state().assets;
            pal.count = 1;
            require(!edit.set_palette(copy, pal, error) && edit.state().assets == before,
                    "Palette shrink left dangling color references");
            auto pixels = edit.image(image)->pix;
            pixels[0] = 255;
            require(!edit.set_image_pixels(image, pixels, error) && edit.state().assets == before,
                    "Invalid pixel index was accepted");
            pixels[0] = 0;
            require(edit.set_image_pixels(image, pixels, error), error);
            require(edit.undo() && edit.state().assets == before, "Pixel undo did not restore shared bank");
            require(edit.redo() && edit.image(image)->pix[0] == 0, "Pixel redo failed");
            auto file = (root / "asset-tools.BDB").u8string();
            require(edit.save(file, error), error);
            Document restored;
            require(restored.load(file, error), error);
            require(restored.image(image)->pix == pixels &&
                    restored.state().assets->data.palettes[copy].rgb555[1] == 0x801f &&
                    restored.state().objects[0].object.fl == copy,
                    "Asset edits did not survive save/reopen exactly");
            // Every palette variant of a shared image constrains valid pixel indices.
            auto smaller = Document::empty();
            int small_image;
            uint8_t red[] = {255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0};
            require(smaller.import_rgba("small", 4, 1, red, error, small_image), error);
            require(smaller.copy_palette_for_image(small_image, 0, error, copy), error);
            auto expanded = smaller.state().assets->data.palettes[copy]; expanded.count = 3;
            require(smaller.set_palette(copy, expanded, error), error);
            smaller.place(small_image, 0, 0, {0,0});
            auto invalid = smaller.image(small_image)->pix; invalid[0] = 2;
            require(!smaller.set_image_pixels(small_image, invalid, error), "Alternate placement palette was ignored");
        }
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
