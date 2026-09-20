#include "Core/studio_game_export.h"
#include "Core/studio_game_build.h"
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
void write(const fs::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
std::string assembly(int count = 3) {
    std::string text =
        "; retained comment\r\ntest_mod\r\n\t.word 123 ; background\r\n\t.word 224\r\n\t.word "
        "-8\r\n\t.word 200\r\n\t.word 0\r\n\t.word 1000\r\n\t.long calla_test\r\n\t.long "
        "test_scroll\r\n\t.long test_dlists\r\n\t.long bak1mods\r\n";
    for (int i = 1; i <= count; i++)
        text += "\t.long PLANE" + std::to_string(i) +
                "BMOD\r\n\t.word 0,0 ; retained offset comment\r\n";
    text += "\t.long center_x\r\n\t.long PLANE1BMOD,worldtlx\r\n\t.long 0\r\ntest_scroll\r\n";
    for (int i = 0; i < 9; i++)
        text += "\t.long 020000h\r\n";
    text += "test_dlists\r\n";
    for (int i = 1; i <= count; i++) {
        text += "\t.long baklst" + std::to_string(i) + ",worldtlx" +
                (i == 1 ? "" : std::to_string(i)) + "+16\r\n";
        if (i == 1)
            text += "\t.long -2 ; shadows\r\n\t.long objlst,worldtlx+16 ; fighters\r\n";
    }
    text += "\t.long 0\r\ncalla_test\r\n\tmovi "
            "existing_actor,a0\r\n\trets\r\nother_stage_data\r\n\t.word 42\r\n";
    return text;
}
void projection(const Document &d, const AssemblyExport &output) {
    for (const auto &p : output.planes) {
        int index = -1;
        for (size_t i = 0; i < d.state().planes.size(); i++)
            if (d.state().planes[i].source.name == p.module)
                index = (int)i;
        require(index >= 0, "Missing exported plane");
        int mx = 1000000, my = 1000000;
        for (auto &obj : d.state().objects)
            if (obj.plane == index) {
                mx = std::min(mx, obj.object.depth);
                my = std::min(my, obj.object.sy);
            }
        for (int camera : {0, 200, 333})
            for (auto &item : d.scene({(double)camera, 7})) {
                auto &obj = d.state().objects[item.object_index];
                if (obj.plane != index)
                    continue;
                double game_x = obj.object.depth - mx + p.x - p.center_origin -
                                (camera - d.state().start_x) * p.scroll;
                double game_y = obj.object.sy - my + p.y - 7;
                require(std::abs(game_x - item.rect.x) <= .51 &&
                            std::abs(game_y - item.rect.y) <= .51,
                        "Exported runtime projection differs from live scene");
            }
    }
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch folder");
        auto root =
            fs::path(argv[1]) /
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        fs::create_directories(root / "game" / "data");
        fs::create_directories(root / "game" / "src");
        std::string error;
        auto d = Document::demo();
        auto original = assembly();
        AssemblyExport out;
        require(export_game_assembly(d, original, out, error), error);
        projection(d, out);
        require(
            out.text.find("\t.long -2 ; shadows\r\n\t.long objlst,worldtlx+16 ; fighters\r\n") !=
                std::string::npos,
            "Fighters or shadows changed");
        require(out.text.substr(out.text.find("calla_test\r\n\tmovi")) ==
                    original.substr(original.find("calla_test\r\n\tmovi")),
                "Runtime actors/unrelated data changed");
        d.move({d.state().objects.front().id}, -83, -61);
        d.set_plane(1, "Pillars", 53, 77, 1.25);
        d.reorder_plane(0, 1);
        require(export_game_assembly(d, original, out, error), error);
        projection(d, out);
        auto list = out.text.substr(out.text.find("test_dlists\r\n\t.long baklst"));
        require(list.find("baklst2") < list.find("baklst1"), "Layer order not exported");
        require(
            !export_game_assembly(d, original + "shared\r\n\t.long test_scroll\r\n", out, error),
            "Shared scroll table was patched");
        require(!export_game_assembly(d, original + assembly(), out, error),
                "Duplicate labels accepted");
        auto edited = d;
        edited.add_plane();
        edited.set_plane(3, "new", 50, 60, 1);
        require(export_game_assembly(edited, original, out, error) &&
                    out.report.find("not shown in game") != std::string::npos,
                "Unused source layer was not identified in the review");
        d.set_plane(0, "bad", 0, 0, -1);
        require(!export_game_assembly(d, original, out, error),
                "Unsupported negative parallax accepted");
        auto single = Document::empty();
        uint8_t rgba[4 * 4 * 4];
        for (auto &x : rgba)
            x = 255;
        int image = 0;
        require(single.import_rgba("white", 4, 4, rgba, error, image), error);
        single.place(image, 0, 0, {10, 20});
        auto game = root / "game";
        require(single.save((game / "data" / "UNTITLED.BDB").u8string(), error), error);
        write(game / "data" / "STAGE.LOD", "BBB> UNTITLED\n");
        write(game / "src" / "BGND.ASM", assembly(1));
        auto extra = single;
        extra.add_plane();
        GameExport rejected;
        require(!prepare_game_export(extra, game.u8string(), (root / "new-module").u8string(),
                                     rejected, error),
                "Unintegrated new module exported");
        single.set_plane(0, "main", 41, 27, .75);
        GameExport package;
        require(prepare_game_export(single, game.u8string(), (root / "package").u8string(), package,
                                    error),
                error);
        require(read(game / "src" / "BGND.ASM") == assembly(1), "Preparing modified game sources");
        write(game / "src" / "BGND.ASM", assembly(1) + "; concurrent edit\r\n");
        require(!apply_game_export(package, error), "Concurrent changes overwritten");
        require(prepare_game_export(single, game.u8string(), (root / "package2").u8string(),
                                    package, error),
                error);
        auto staged = root / "package2" / "src" / "BGND.ASM";
        auto staged_bytes = read(staged);
        write(staged, staged_bytes + "tamper");
        require(!apply_game_export(package, error), "Modified staged package accepted");
        write(staged, staged_bytes);
        fs::create_directory(game / ".bddstudio-applying");
        require(!apply_game_export(package, error), "Unfinished checkout transaction ignored");
        require(!prepare_game_export(single, game.u8string(), (root / "locked").u8string(),
                                     rejected, error),
                "Prepared export against an unfinished checkout transaction");
        require(read(game / "src" / "BGND.ASM") == assembly(1) + "; concurrent edit\r\n",
                "Locked checkout was modified");
        fs::remove(game / ".bddstudio-applying");
        require(apply_game_export(package, error), error);
        require(fs::exists(root / "package2" / "APPLIED.txt"), "No apply receipt");
        require(!fs::exists(game / ".bddstudio-applying"), "Successful apply left checkout locked");
        require(read(root / "package2" / "backups" / "src" / "BGND.ASM") ==
                    assembly(1) + "; concurrent edit\r\n",
                "Backup lost original assembly");
        Document reopened;
        require(reopened.load((game / "data" / "UNTITLED.BDB").u8string(), error), error);
        require(reopened.scene()[0].rect.x == single.scene()[0].rect.x,
                "Applied project changed composition");
        write(
            game / "build.py",
            "import "
            "os,sys\nprint('BUILD_TEST:'+os.path.basename(os.getcwd()),flush=True)\nsys.exit(7)\n");
        GameBuild job;
        require(job.start(game.u8string(), (root / "build.log").u8string(), error), error);
        require(!job.start(game.u8string(), (root / "build.log").u8string(), error),
                "Concurrent build started");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (job.running() && std::chrono::steady_clock::now() < deadline) {
            job.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(!job.running() && job.exit_code() == 7, "Build exit code not reported");
        require(read(root / "build.log").find("BUILD_TEST:game") != std::string::npos,
                "Build log or working directory incorrect");
        write(game / "build.py", "print('BUILD_OK',flush=True)\n");
        require(job.start(game.u8string(), (root / "build.log").u8string(), error), error);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (job.running() && std::chrono::steady_clock::now() < deadline) {
            job.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(!job.running() && job.exit_code() == 0, "Successful build reported failure");
        if (argc >= 4) {
            Document real;
            require(real.load(argv[2], error), error);
            AssemblyExport actual;
            require(export_game_assembly(real, read(argv[3]), actual, error), error);
            projection(real, actual);
            write(root / "real-export.ASM", actual.text);
            std::cout << "Real assembly matched " << actual.label << " with "
                      << actual.planes.size() << " planes.\n";
        }
        std::cout
            << "Game export: projection, tight bounds, centering, parallax, draw order, actor "
               "preservation, conflicts, package integrity, apply and backups passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
