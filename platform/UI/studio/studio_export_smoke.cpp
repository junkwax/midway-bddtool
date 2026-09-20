#include "UI/studio/studio_app.h"
#include "Core/studio_game_export.h"
#include "Core/editor_project_storage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <chrono>

namespace studio {
int game_export_smoke(const std::string &output, const std::string &input,
                      const std::string &game_root) {
    namespace fs = std::filesystem;
    editor_project_storage_init();
    auto check = [](bool ok, const std::string &message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    try {
        fs::path work = fs::absolute(fs::u8path(output));
        if (input.empty())
            work /= std::to_string(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
        check(!fs::exists(work), "Use a new smoke output folder.");
        auto game = work / "game";
        auto runtime = work / "runtime";
        fs::create_directories(game / "src");
        fs::create_directories(game / "data");
        std::string error;
        std::string fixture = input, source_root = game_root;
        if (input.empty()) {
            auto source = work / "fixture";
            fs::create_directories(source / "src");
            fs::create_directories(source / "data");
            auto demo = Document::empty();
            uint8_t pixels[4 * 4 * 4];
            std::fill(std::begin(pixels), std::end(pixels), 255);
            int image = 0;
            check(demo.import_rgba("test tile", 4, 4, pixels, error, image), error);
            demo.add_plane();
            demo.add_plane();
            for (int i = 0; i < 3; i++) {
                demo.place(image, 0, i, {20.0 + i * 10, 30.0});
                demo.place(image, 0, i, {60.0 + i * 10, 40.0});
            }
            fixture = (source / "data" / "UNTITLED.BDB").u8string();
            source_root = source.u8string();
            check(demo.save(fixture, error), error);
            fs::remove(source / "data" / "UNTITLED.bddstudio");
            std::ofstream assembly(source / "src" / "BGND.ASM");
            assembly << "demo_mod\n\t.word 0\n\t.word 224\n\t.word -8\n\t.word 200\n"
                        "\t.word 0\n\t.word 1000\n\t.long calla_demo\n\t.long demo_scroll\n"
                        "\t.long demo_dlists\n\t.long bak1mods\n";
            for (const auto &plane : demo.state().planes)
                assembly << "\t.long " << plane.source.name << "BMOD\n\t.word 0,0\n";
            assembly << "\t.long 0\ndemo_scroll\n";
            for (int i = 0; i < 9; i++)
                assembly << "\t.long 020000h\n";
            assembly << "demo_dlists\n\t.long baklst1,worldtlx+16\n"
                        "\t.long -2\n\t.long ghost_lst,worldtlx+16\n"
                        "\t.long objlst,worldtlx+16\n\t.long baklst2,worldtlx2+16\n"
                        "\t.long baklst3,worldtlx3+16\n\t.long 0\ncalla_demo\n\trets\n";
        }
        Document doc;
        check(doc.load(fixture, error), error);
        read_runtime_defaults(doc);
        if (input.empty()) {
            check(doc.state().planes.size() == 3 && doc.state().planes[0].rank == 10 &&
                      doc.state().planes[1].rank == 50 && doc.state().planes[2].rank == 60,
                  "Actor display-list entries interrupted background draw-order import.");
        }
        const auto name = doc.state().name;
        check(doc.save((game / "data" / (name + ".BDB")).u8string(), error, true), error);
        fs::copy_file(fs::u8path(source_root) / "src" / "BGND.ASM", game / "src" / "BGND.ASM");
        {
            std::ofstream lod(game / "data" / "TEST.LOD");
            lod << "BBB> " << name << '\n';
        }
        int changed_plane = -1;
        for (size_t i = 0; i < doc.state().planes.size(); i++)
            if (doc.state().planes[i].bound) {
                changed_plane = (int)i;
                break;
            }
        check(changed_plane >= 0, "Fixture has no runtime bindings.");
        auto p = doc.state().planes[changed_plane];
        doc.set_plane(changed_plane, p.name, p.x + 17, p.y - 9, .75);
        doc.reorder_plane(changed_plane, 1);
        for (const auto &obj : doc.state().objects)
            if (obj.plane == changed_plane) {
                auto id = obj.id;
                doc.move({id}, -33, 11);
                break;
            }
        GameExport package;
        check(prepare_game_export(doc, game.u8string(), (work / "package").u8string(), package,
                                  error),
              error);
        check(apply_game_export(package, error), error);
        // Independently re-import the applied assembly through the existing runtime parser.
        // Supply only regenerated BMOD dimensions here: LOAD2 compression/ROM output is not
        // simulated.
        fs::create_directories(runtime / "src");
        fs::create_directories(runtime / "data");
        for (const auto &ext : {".BDB", ".BDD"})
            fs::copy_file(game / "data" / (name + ext), runtime / "data" / (name + ext));
        fs::copy_file(game / "src" / "BGND.ASM", runtime / "src" / "BGND.ASM");
        Document applied;
        check(applied.load((game / "data" / (name + ".BDB")).u8string(), error), error);
        GameExport repeated;
        check(prepare_game_export(applied, game.u8string(), (work / "package-again").u8string(),
                                  repeated, error),
              error);
        for (const auto &file : repeated.files)
            if (file.relative == "src/BGND.ASM")
                check(file.before == file.after,
                      "Re-export after source repacking drifted the runtime layout.");
        {
            std::ofstream table(runtime / "src" / "BGNDTBL.ASM");
            for (size_t i = 0; i < applied.state().planes.size(); i++) {
                int minx = 1000000, miny = 1000000, maxx = -1000000, maxy = -1000000, count = 0;
                for (const auto &obj : applied.state().objects)
                    if (obj.plane == (int)i) {
                        auto *im = applied.image(obj.object.ii);
                        check(im != nullptr, "Missing fixture image");
                        minx = std::min(minx, obj.object.depth);
                        miny = std::min(miny, obj.object.sy);
                        maxx = std::max(maxx, obj.object.depth + im->w);
                        maxy = std::max(maxy, obj.object.sy + im->h);
                        count++;
                    }
                if (count)
                    table << applied.state().planes[i].source.name << "BMOD:\n\t.word "
                          << maxx - minx << ',' << maxy - miny << ',' << count << "\n";
            }
        }
        Document imported;
        check(imported.load((runtime / "data" / (name + ".BDB")).u8string(), error), error);
        read_runtime_defaults(imported);
        for (int camera : {0, 123, 333}) {
            auto expected = doc.scene({(double)camera, 7}),
                 actual = imported.scene({(double)camera, 7});
            std::vector<ObjectId> expected_order, actual_order;
            for (const auto &item : actual) {
                auto *original = doc.object(item.id);
                if (original && original->plane >= 0 && doc.state().planes[original->plane].bound)
                    actual_order.push_back(item.id);
            }
            for (const auto &item : expected) {
                auto *object = doc.object(item.id);
                if (object->plane < 0 || !doc.state().planes[object->plane].bound)
                    continue;
                expected_order.push_back(item.id);
                auto found = std::find_if(actual.begin(), actual.end(),
                                          [&](const SceneItem &x) { return x.id == item.id; });
                check(found != actual.end() && std::abs(found->rect.x - item.rect.x) <= 1 &&
                          std::abs(found->rect.y - item.rect.y) <= 1,
                      "Applied assembly re-import did not reproduce the edited scene.");
            }
            if (expected_order != actual_order) {
                for (size_t i = 0; i < doc.state().planes.size(); i++)
                    std::cerr << doc.state().planes[i].source.name << " rank "
                              << doc.state().planes[i].rank << " -> "
                              << imported.state().planes[i].rank << '\n';
                for (size_t i = 0; i < std::min(expected_order.size(), actual_order.size()); i++)
                    if (expected_order[i] != actual_order[i]) {
                        std::cerr << "First differing object " << expected_order[i] << " -> "
                                  << actual_order[i] << '\n';
                        break;
                    }
            }
            check(expected_order == actual_order,
                  "Applied assembly re-import changed plane draw order.");
        }
        std::cout << "Runtime adapter -> edited document -> prepare/apply -> runtime re-import "
                     "passed at three camera positions.\n";
        editor_project_storage_shutdown();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        editor_project_storage_shutdown();
        return 1;
    }
}
} // namespace studio
