#include "Core/studio_optimizer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <tuple>
using namespace studio;
namespace fs = std::filesystem;
void need(bool ok, const std::string &error) {
    if (!ok)
        throw std::runtime_error(error);
}
std::vector<uint32_t> layer(const Document &doc, int index, const BddCoreModule &box) {
    int w = box.x2 - box.x1 + 1, h = box.y2 - box.y1 + 1;
    need(w > 0 && h > 0 && uint64_t(w) * h < 16000000, "Layer too large");
    std::vector<uint32_t> out(size_t(w) * h);
    std::vector<Placement> objects;
    for (const auto &p : doc.state().objects)
        if (p.plane == index)
            objects.push_back(p);
    std::stable_sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) {
        return std::tie(a.object.depth, a.object.sy) < std::tie(b.object.depth, b.object.sy);
    });
    for (const auto &p : objects) {
        const auto &im = *doc.image(p.object.ii);
        const auto &pal = doc.state().assets->data.palettes.at(p.object.fl);
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                int v = im.pix[size_t(p.object.wx & 32 ? im.h - 1 - y : y) * im.w +
                               (p.object.wx & 16 ? im.w - 1 - x : x)];
                if (!v)
                    continue;
                need(v < pal.count, "Invalid palette");
                int dx = p.object.depth - box.x1 + x, dy = p.object.sy - box.y1 + y;
                need(dx >= 0 && dx < w && dy >= 0 && dy < h, "Layer bounds changed");
                out[size_t(dy) * w + dx] = 0x10000 | pal.rgb555[v];
            }
    }
    return out;
}
// Private integration experiment: keep original source coordinates for LOAD2 and
// compare its generated bounds before adjusting the isolated runtime placement.
int main(int argc, char **argv) {
    try {
        need(
            argc == 3 || (argc == 4 && std::string(argv[3]) == "--runtime-order"),
            "Usage: studio_validation_candidate source.BDB new-output-directory [--runtime-order]");
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
        if (argc == 4) {
            auto original = doc;
            std::vector<std::pair<uint64_t, int>> sources;
            for (const auto &im : doc.state().assets->data.images)
                sources.push_back({optimization_image_bits(im), im.idx});
            std::stable_sort(sources.begin(), sources.end(),
                             [](auto a, auto b) { return a.first > b.first; });
            for (auto entry : sources) {
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
                    exact &= layer(original, (int)i, original.state().planes[i].source) ==
                             layer(next, (int)i, original.state().planes[i].source);
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
