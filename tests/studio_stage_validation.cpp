#include "Core/studio_document.h"
#include "Core/studio_optimizer.h"
#include "Core/studio_rom_receipt.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &error) {
    if (!ok)
        throw std::runtime_error(error);
}
void write(const fs::path &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
    require(bool(out), "Cannot write report");
}
std::vector<uint32_t> render(const Document &doc, Rect box) {
    int w = (int)box.w, h = (int)box.h;
    require(w > 0 && h > 0 && uint64_t(w) * h < 16000000, "Render exceeds validation limit");
    std::vector<uint32_t> pixels((size_t)w * h);
    for (const auto &item : doc.scene()) {
        const auto &im = doc.state().assets->data.images[item.image_slot];
        const auto &pal = doc.state().assets->data.palettes[item.palette];
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                auto p = im.pix[(size_t)(item.vflip ? im.h - 1 - y : y) * im.w +
                                (item.hflip ? im.w - 1 - x : x)];
                int dx = (int)(item.rect.x - box.x) + x, dy = (int)(item.rect.y - box.y) + y;
                if (p && dx >= 0 && dy >= 0 && dx < w && dy < h)
                    pixels[(size_t)dy * w + dx] = 0x10000 | pal.rgb555[p];
            }
    }
    return pixels;
}
// Game generators may predicate on original BDB coordinates. Preserve that coordinate
// system instead of the authoring save path's normalized module shelves.
void save_original_coordinates(const Document &doc, const fs::path &folder) {
    const auto &s = doc.state();
    fs::create_directories(folder);
    std::vector<const char *> lines;
    for (const auto &p : s.planes)
        lines.push_back(p.source.line);
    std::vector<BddCoreObject> objects;
    for (const auto &p : s.objects)
        objects.push_back(p.object);
    std::ostringstream header;
    header << s.name << ' ' << s.world_w << ' ' << s.world_h << ' ' << s.depth << ' '
           << s.planes.size() << ' ' << s.assets->data.palettes.size() << ' ' << objects.size();
    BddCoreSaveResult result{};
    require(bdd_core_save_bdb((folder / "MK3CAVE.BDB").u8string().c_str(), header.str().c_str(),
                              lines.data(), (int)lines.size(), objects.data(), (int)objects.size(),
                              &result) != 0,
            result.error);
    require(bdd_core_save_bdd((folder / "MK3CAVE.BDD").u8string().c_str(),
                              s.assets->data.images.data(), (int)s.assets->data.images.size(),
                              s.assets->data.palettes.data(), (int)s.assets->data.palettes.size(),
                              &result) != 0,
            result.error);
}
int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--compare-rom") {
            auto before = capture_rom_receipt(argv[2]);
            auto after = capture_rom_receipt(argv[3]);
            require(before.valid, before.error);
            require(after.valid, after.error);
            fs::path folder = fs::u8path(argv[4]);
            require(!fs::exists(folder), "Receipt output already exists");
            fs::create_directories(folder);
            std::string error;
            require(save_rom_receipt(before, (folder / "before.romreceipt").u8string(), error),
                    error);
            require(save_rom_receipt(after, (folder / "after.romreceipt").u8string(), error),
                    error);
            auto report = compare_rom_receipts(before, after);
            write(folder / "comparison.txt", report);
            std::cout << report;
            return 0;
        }
        require(argc == 3, "Usage: studio_stage_validation MK3CAVE.BDB new-output-folder\n"
                           "   or: studio_stage_validation --compare-rom before-root after-root "
                           "new-output-folder");
        fs::path folder = fs::u8path(argv[2]);
        require(!fs::exists(folder), "Output already exists");
        fs::create_directories(folder);
        Document doc;
        std::string error;
        require(doc.load(argv[1], error), error);
        require(doc.state().name == "mk3cave" && doc.state().assets->data.palettes.size() == 7,
                "Expected original MK3CAVE with seven palettes");
        bool protected_water = false;
        for (int i = 0; i < (int)doc.state().planes.size(); i++)
            if (doc.state().planes[i].name == "mk3cave3") {
                doc.set_plane_flags(i, false, true);
                protected_water = true;
            }
        require(protected_water, "Water module was not identified");
        auto pixels = render(doc, doc.bounds());
        OptimizeOptions options;
        options.deep = true;
        options.compact_palettes = false;
        options.max_added_objects = 24;
        auto lossless = find_lossless_savings(doc, options);
        require(lossless.verified, lossless.error);
        auto shared = find_shared_savings(doc, options);
        require(shared.verified, shared.error);
        auto best = lossless.proposed.video_bits <= shared.proposed.video_bits ? lossless : shared;
        write(folder / "lossless.txt", optimization_report(lossless));
        write(folder / "shared.txt", optimization_report(shared));
        write(folder / "selected.txt", optimization_report(best));
        auto original = doc;
        require(doc.apply_optimization(best, error), error);
        require(render(doc, original.bounds()) == pixels,
                "Applied candidate changed the complete source scene");
        save_original_coordinates(doc, folder);
        Document reopened;
        require(reopened.load((folder / "MK3CAVE.BDB").u8string(), error), error);
        require(render(reopened, original.bounds()) == pixels,
                "Reopened candidate changed the source scene");
        std::cout << optimization_report(best)
                  << "Original source coordinates and complete scene roundtrip verified.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
