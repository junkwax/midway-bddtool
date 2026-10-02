#include "Core/studio_optimizer.h"
#include "studio_validation_checks.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <tuple>
#include <set>
using namespace studio;
namespace fs = std::filesystem;
void need(bool ok, const std::string &error) {
    if (!ok)
        throw std::runtime_error(error);
}
// Private integration experiment: keep original source coordinates for LOAD2 and
// compare its generated bounds before adjusting the isolated runtime placement.
int main(int argc, char **argv) {
    try {
        need(argc >= 3, "Usage: studio_validation_candidate source.BDB new-output-directory "
                        "[--runtime-order] [--exclude-image=DECIMAL_ID ...]");
        bool runtime_order = false;
        std::set<int> excluded;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--runtime-order")
                runtime_order = true;
            else if (arg.rfind("--exclude-image=", 0) == 0) {
                auto value = arg.substr(16);
                size_t used = 0;
                int id = std::stoi(value, &used);
                need(used == value.size() && id >= 0, "Invalid excluded image ID");
                excluded.insert(id);
            } else
                need(false, "Unknown argument: " + arg);
        }
        need(excluded.empty() || runtime_order, "Exclusions require --runtime-order");
        fs::path folder = fs::absolute(fs::u8path(argv[2]));
        need(!fs::exists(folder), "Output already exists");
        Document doc;
        std::string error;
        need(doc.load(argv[1], error), error);
        OptimizeOptions options;
        options.deep = true;
        options.compact_palettes = false;
        options.max_added_objects = 24;
        std::ostringstream audit;
        if (runtime_order) {
            auto original = doc;
            std::vector<std::pair<uint64_t, int>> sources;
            for (const auto &im : doc.state().assets->data.images)
                sources.push_back({optimization_image_bits(im), im.idx});
            std::stable_sort(sources.begin(), sources.end(),
                             [](auto a, auto b) { return a.first > b.first; });
            for (auto entry : sources) {
                if (excluded.count(entry.second)) {
                    audit << "Image " << entry.second << ": excluded by request\n";
                    continue;
                }
                if (validation::protects_unaligned(original.state().assets->data.images,
                                                   entry.second)) {
                    audit << "Image " << entry.second
                          << ": skipped - preserves LOAD2 unaligned-image buffer context\n";
                    continue;
                }
                options.source_image = entry.second;
                options.max_added_objects =
                    24 - int(doc.state().objects.size() - original.state().objects.size());
                auto plan = find_lossless_savings(doc, options);
                if (!plan.verified || plan.proposed.video_bits >= plan.baseline.video_bits)
                    continue;
                auto next = doc;
                need(next.apply_optimization(plan, error), error);
                bool exact = true;
                for (size_t i = 0; i < doc.state().planes.size(); ++i)
                    for (bool reverse : {false, true})
                        exact &= validation::layer(original.state(), (int)i,
                                                   original.state().planes[i].source, reverse) ==
                                 validation::layer(next.state(), (int)i,
                                                   original.state().planes[i].source, reverse);
                audit << "Image " << entry.second
                      << (exact ? ": accepted\n"
                                : ": REJECTED - runtime layer order changes pixels\n");
                if (exact)
                    doc = std::move(next);
            }
            auto a = optimization_budget(original.state()), b = optimization_budget(doc.state());
            audit << "Runtime-order checked estimate: " << a.video_bits / 8 << " -> "
                  << b.video_bits / 8 << " bytes; placements " << a.objects << " -> " << b.objects
                  << '\n';
        } else {
            auto plan = find_lossless_savings(doc, options);
            need(plan.verified, plan.error);
            need(doc.apply_optimization(plan, error), error);
            audit << optimization_report(plan);
        }
        fs::create_directories(folder);
        std::ofstream(folder / "optimization.txt") << audit.str();
        const auto &s = doc.state();
        std::vector<const char *> lines;
        for (const auto &p : s.planes)
            lines.push_back(p.source.line);
        std::vector<BddCoreObject> objects;
        for (const auto &p : s.objects)
            objects.push_back(p.object);
        // BAKGND.ASM searches the block table by X. LOAD2 retains input order.
        std::stable_sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) {
            return std::tie(a.depth, a.sy, a.order) < std::tie(b.depth, b.sy, b.order);
        });
        for (size_t i = 0; i < objects.size(); ++i)
            objects[i].order = (int)i;
        std::ostringstream header;
        header << s.name << ' ' << s.world_w << ' ' << s.world_h << ' ' << s.depth << ' '
               << s.planes.size() << ' ' << s.assets->data.palettes.size() << ' ' << objects.size();
        auto stem = fs::u8path(argv[1]).stem().u8string();
        BddCoreSaveResult result{};
        need(bdd_core_save_bdb((folder / (stem + ".BDB")).u8string().c_str(), header.str().c_str(),
                               lines.data(), (int)lines.size(), objects.data(), (int)objects.size(),
                               &result) != 0,
             result.error);
        need(bdd_core_save_bdd((folder / (stem + ".BDD")).u8string().c_str(),
                               s.assets->data.images.data(), (int)s.assets->data.images.size(),
                               s.assets->data.palettes.data(), (int)s.assets->data.palettes.size(),
                               &result) != 0,
             result.error);
        std::cout << audit.str();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
