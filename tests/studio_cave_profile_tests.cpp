#include "Core/studio_cave_export.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const fs::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
    require(bool(f), "Fixture write failed");
}
std::string read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Need scratch folder");
        fs::path work = fs::u8path(argv[1]) /
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto root = work / "game";
        for (auto d : {"data", "src", "tools"})
            fs::create_directories(root / d);
        write(root / "data/MK3CAVE.BDB",
              "mk3cave 1000 1000 255 4 7 4\n"
              "mk3cave4 0 999 0 100\nmk3cave3 0 999 200 300\n"
              "mk3cave2 0 999 295 500\nmk3cave1 0 999 700 800\n"
              "4000 40 20 0 0\n4000 650 220 3 1\n4000 50 450 6 2\n4000 180 740 9 3\n");
        std::vector<BddCoreImage> images;
        for (int i = 0; i < 4; ++i) {
            BddCoreImage im{};
            im.idx = i * 3;
            im.w = 16;
            im.h = 8;
            im.flags = 1;
            im.pix.assign(128, 1);
            images.push_back(im);
        }
        std::vector<BddCorePalette> pals(7);
        for (int i = 0; i < 7; ++i) {
            auto &p = pals[i];
            std::snprintf(p.name, sizeof p.name, "pal%d", i);
            p.count = 2;
            p.rgb555[1] = 0x7fff;
        }
        BddCoreSaveResult save{};
        require(bdd_core_save_bdd((root / "data/MK3CAVE.BDD").u8string().c_str(), images.data(), 4,
                                  pals.data(), 7, &save) != 0,
                save.error);
        std::ostringstream eq;
        eq << "mk3c_start .set 350\nmk3c_worldy .set -7\nmk3c_ground .set 225\n";
        for (int i = 1; i <= 8; ++i)
            eq << "mk3c_rate" << i << " .set 0018000H\n";
        for (int i = 1; i <= 4; ++i)
            eq << "mk3c_m" << i << "_x .set 0\nmk3c_m" << i << "_y .set 20\n";
        write(root / "src/MK3CAVBG.ASM", eq.str());
        write(root / "src/BGND.ASM", "; fixture\n");
        write(root / "tools/make_mk3cave.py",
              "def water_spans():\n    pass\n"
              "MOVES = {'mk3cave3': [(lambda x: x >= 600, -93, 0)]}\n"
              "if __name__ == \"__main__\":\n    pass\n");
        std::ostringstream slots;
        slots << "CUSTOM_VIDEO_SLOTS = {\n";
        for (int i = 0; i < 4; ++i) {
            auto name = "MK3CV" + std::to_string(i + 1);
            uint64_t start = 0x800000 + i * 32;
            slots << '\'' << name << ".IRW': (" << start << ',' << start + 16 << ", 'fixture'),\n";
            std::ostringstream lod;
            lod << "***> " << std::hex << 0x2000000 + i * 256 << ",1\nBBB> " << name << '\n';
            write(root / "data" / (name + ".LOD"), lod.str());
        }
        slots << "}\n";
        write(root / "makevrom.py", slots.str());
        for (auto name :
             {"build.py", "makerom.py", "mamerom.py", "src/MK3CAVE.ASM", "data/FL_CAVE.BIN",
              "data/IMGBUILD.BAT", "dosbox_load2.conf", "dosbox_asm.conf"})
            write(root / name, "fixture\n");
        Document d;
        std::string error;
        require(d.load((root / "data/MK3CAVE.BDB").u8string(), error), error);
        require(seed_cave_runtime(d, root.u8string(), error), error);
        require(d.state().objects[1].runtime_dx == -93 && d.state().planes[1].locked,
                "Water source move/lock lost");
        require(!d.move({d.state().objects[1].id}, 1, 0), "Water moved while protected");
        GameExport package;
        require(
            prepare_game_export(d, root.u8string(), (work / "package").u8string(), package, error),
            error);
        require(package.report.find("runtime offset 0, 20") != std::string::npos,
                "Half-pixel scroll origin drifted");
        Document copy;
        require(copy.load((work / "package/data/MK3CAVE.BDB").u8string(), error), error);
        require(copy.state().runtime_profile == "mk3cave" &&
                    copy.state().objects[1].runtime_dx == -93,
                "Profile roundtrip failed");
        for (size_t i = 0; i < d.state().objects.size(); ++i)
            require(copy.state().objects[i].object.depth == d.state().objects[i].object.depth &&
                        copy.state().objects[i].object.sy == d.state().objects[i].object.sy,
                    "Original coordinates normalized");
        auto a = d.scene({350, -7}), b = copy.scene({350, -7});
        for (size_t i = 0; i < a.size(); ++i)
            require(a[i].rect.x == b[i].rect.x && a[i].rect.y == b[i].rect.y,
                    "Runtime roundtrip moved artwork");
        auto bad = d;
        bad.set_plane_flags(1, false, false);
        bad.move({bad.state().objects[1].id}, 1, 0);
        GameExport rejected;
        require(!prepare_game_export(bad, root.u8string(), (work / "bad-water").u8string(),
                                     rejected, error),
                "Water edit accepted");
        write(root / "makevrom.py", slots.str() + "# changed after review\n");
        require(!apply_game_export(package, error), "Changed slots ignored");
        write(root / "makevrom.py", slots.str());
        require(apply_game_export(package, error), error);
        require(fs::exists(work / "package/APPLIED.txt") &&
                    fs::exists(root / "tools/bddtool_mk3cave_build.py") &&
                    read(root / "tools/make_mk3cave.py").find("BDDTOOL CAVE PROFILE BEGIN") !=
                        std::string::npos,
                "Apply did not complete");
        std::cout << "Custom cave profile: source moves, locks, fractional scroll, save/reopen, "
                     "water rejection, stale slots and apply passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
