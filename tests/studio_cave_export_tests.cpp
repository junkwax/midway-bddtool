#include "Core/studio_cave_export.h"
#include "Core/studio_optimizer.h"
#include "Core/studio_runtime_order.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
    try {
        require(argc == 3 || argc == 4,
                "Usage: studio_cave_export_tests isolated-game-root new-package [--apply]");
        auto root = fs::absolute(fs::u8path(argv[1])), folder = fs::absolute(fs::u8path(argv[2]));
        Document d;
        std::string error;
        require(d.load((root / "data/MK3CAVE.BDB").u8string(), error), error);
        require(seed_cave_runtime(d, root.u8string(), error), error);
        require(d.state().runtime_profile == "mk3cave", "Missing custom binding");
        auto before = d;
        OptimizeOptions options;
        options.deep = true;
        options.compact_palettes = false;
        options.max_added_objects = 24;
        auto plan = find_lossless_savings(d, options);
        require(plan.verified, plan.error);
        require(d.apply_optimization(plan, error), error);
        GameExport package;
        require(prepare_game_export(d, root.u8string(), folder.u8string(), package, error), error);
        require(package.build_script == "tools/bddtool_mk3cave_build.py",
                "Custom build helper not selected");
        for (const auto &file : package.files)
            require(!file.existed || read(root / file.relative) == file.before,
                    "Prepare changed the checkout");
        Document saved;
        require(saved.load((folder / "data/MK3CAVE.BDB").u8string(), error), error);
        require(saved.state().runtime_profile == "mk3cave", "Custom profile did not survive save");
        require(d.state().objects.size() == saved.state().objects.size(),
                "Roundtrip lost placements");
        auto runtime = compare_runtime_order(d.state(), saved.state());
        require(runtime.checked && runtime.equivalent, runtime.error);
        auto bad = d;
        for (size_t i = 0; i < bad.state().planes.size(); ++i)
            if (bad.state().planes[i].name == "mk3cave3") {
                auto p = bad.state().planes[i];
                bad.set_plane_flags((int)i, false, false);
                bad.set_plane((int)i, p.name, p.x + 1, p.y, p.scroll);
            }
        GameExport rejected;
        require(!prepare_game_export(bad, root.u8string(), (folder.u8string() + "-bad").c_str(),
                                     rejected, error),
                "Changed water projection accepted");
        require(!fs::exists(folder.u8string() + "-bad"), "Rejected water edit created output");
        if (argc == 4) {
            require(std::string(argv[3]) == "--apply", "Unknown option");
            require(apply_game_export(package, error), error);
        }
        std::cout << package.report
                  << "\nCustom binding, protected water, prepare, source isolation and save/reopen "
                     "passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
