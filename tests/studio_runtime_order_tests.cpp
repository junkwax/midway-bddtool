#include "Core/studio_runtime_order.h"
#include "Core/studio_optimizer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void need(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
BddCoreImage image(int id, int w, int h, uint8_t value) {
    BddCoreImage im{};
    im.idx = id;
    im.w = w;
    im.h = h;
    im.pix.assign((size_t)w * h, value);
    return im;
}
Placement object(ObjectId id, int image, int y, int order) {
    Placement p;
    p.id = id;
    p.plane = 0;
    p.object.ii = image;
    p.object.sy = y;
    p.object.order = order;
    return p;
}
int main(int argc, char **argv) {
    try {
        std::string error;
        if (argc == 4 && std::string(argv[1]) == "--compare") {
            Document a, b;
            need(a.load(argv[2], error), error);
            need(b.load(argv[3], error), error);
            auto result = compare_runtime_order(a.state(), b.state());
            std::cout << result.report;
            return !result.checked ? 1 : result.equivalent ? 0 : 2;
        }
        need(argc == 2, "Need scratch directory");
        auto root = fs::u8path(argv[1]) /
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root);
        auto bank = std::make_shared<AssetBank>();
        bank->data.images = {image(0, 4, 4, 1), image(1, 4, 2, 2), image(2, 4, 3, 1),
                             image(3, 4, 1, 1)};
        BddCorePalette pal{};
        pal.count = 3;
        pal.rgb555[1] = 0;
        pal.rgb555[2] = 31;
        bank->data.palettes.push_back(pal);
        OptimizationPlan plan;
        plan.before.assets = bank;
        plan.before.planes.resize(1);
        plan.before.objects = {object(1, 0, 0, 0), object(2, 1, 2, 1)};
        plan.after = plan.before;
        plan.after.objects = {object(1, 2, 0, 0), object(3, 3, 3, 1), object(2, 1, 2, 2)};
        OptimizeChange change;
        change.source_image = 0;
        OptimizePiece top, bottom;
        top.w = bottom.w = 4;
        top.h = 3;
        bottom.y = 3;
        bottom.h = 1;
        top.image = 2;
        bottom.image = 3;
        top.palettes[0] = bottom.palettes[0] = 0;
        change.pieces = {top, bottom};
        plan.changes.push_back(change);
        auto bad = compare_runtime_order(plan.before, plan.after);
        need(bad.checked && !bad.equivalent && bad.changed_samples == 8,
             "Split Y priority regression was not detected");
        need(!verify_optimization(plan, error) &&
                 error.find("Runtime drawing order") != std::string::npos,
             "Lossless verifier accepted individually exact pieces with wrong overlap order");
        plan.before.objects[1].object.wx = plan.after.objects[2].object.wx = 0x100;
        need(verify_optimization(plan, error), error);
        auto a = plan.before, b = a;
        // All-hidden editor layers still become game artwork.
        a.planes[0].hidden = b.planes[0].hidden = true;
        a.objects[0].hidden = b.objects[0].hidden = true;
        b.objects[0].object.depth = 5;
        need(!compare_runtime_order(a, b).equivalent, "Editor visibility hid exported changes");
        b = a;
        b.objects[0].runtime_dx = -93;
        need(!compare_runtime_order(a, b).equivalent, "Custom runtime source shift ignored");
        b = a;
        b.objects[0].object.fl = 7;
        need(!compare_runtime_order(a, b).checked, "Missing palette accepted");
        b = a;
        b.objects[0].object.depth = 20000000;
        need(!compare_runtime_order(a, b).checked, "Oversized layer comparison allocated");
        b = a;
        b.planes[0].scroll = .5;
        need(!compare_runtime_order(a, b).checked, "Parallax change accepted");
        // Equal Z/Y overlap can differ in only one arrival direction.
        a.objects = {object(1, 0, 0, 0), object(2, 1, 0, 1)};
        b = a;
        b.objects[1].object.depth = 1;
        need(!compare_runtime_order(a, b).equivalent, "Equal-key arrival order was ignored");
        // Translating source rectangles and compensating the layer is a no-op.
        b = a;
        b.planes[0].source.x1 += 100;
        b.planes[0].source.y1 += 200;
        for (auto &p : b.objects) {
            p.object.depth += 100;
            p.object.sy += 200;
        }
        need(compare_runtime_order(a, b).equivalent, "Source repacking changed projection");
        auto pixels = std::make_shared<AssetBank>(*bank);
        pixels->data.images[0] = image(0, 2, 2, 0);
        pixels->data.images[0].pix = {0, 1, 2, 0};
        a.assets = pixels;
        a.objects = {object(1, 0, 0, 0)};
        b = a;
        auto flipped = std::make_shared<AssetBank>(*pixels);
        flipped->data.images[0].pix = {0, 2, 1, 0};
        b.assets = flipped;
        b.objects[0].object.wx = 0x30;
        need(compare_runtime_order(a, b).equivalent, "Flip reconstruction differs");
        flipped->data.images[0].pix[2] = 0;
        need(!compare_runtime_order(a, b).equivalent, "Opaque black became transparent unnoticed");

        // Export sorting must remap sidecar flags and leave the editing document intact.
        auto doc = Document::empty();
        uint8_t rgba[4 * 4 * 4];
        for (auto &v : rgba)
            v = 255;
        int id = -1;
        need(doc.import_rgba("white", 4, 4, rgba, error, id), error);
        doc.place(id, 0, 0, {50, 0});
        doc.place(id, 0, 0, {-10, -20});
        need(doc.save((root / "seed.BDB").u8string(), error), error);
        {
            std::ofstream sidecar(root / "seed.bddstudio", std::ios::app);
            sidecar << "object 0 1 0\nobject 1 0 1\n";
        }
        need(doc.load((root / "seed.BDB").u8string(), error), error);
        need(doc.move({doc.state().objects[0].id}, 800, 0), "Could not force source repacking");
        auto revision = doc.state().revision;
        auto first = doc.state().objects.front().id;
        std::string report;
        need(doc.save_game_sources((root / "sorted.BDB").u8string(), report, error), error);
        Document sorted;
        need(sorted.load((root / "sorted.BDB").u8string(), error), error);
        need(sorted.state().objects[0].object.depth < sorted.state().objects[1].object.depth &&
                 sorted.state().objects[0].locked && sorted.state().objects[1].hidden,
             "X sorting or object sidecar remapping failed");
        need(doc.state().revision == revision && doc.state().objects.front().id == first,
             "Export mutated the editing document");
        need(report.find("two X arrival orders") != std::string::npos,
             "Export omitted runtime comparison scope");
        // The custom cave source-shift sidecar is indexed by serialized row too.
        {
            std::ifstream input(root / "seed.bddstudio", std::ios::binary);
            std::string layout{std::istreambuf_iterator<char>(input), {}};
            input.close();
            need(layout.rfind("BDDSTUDIO 1", 0) == 0, "Unexpected fixture layout version");
            layout[10] = '2';
            std::ofstream output(root / "seed.bddstudio", std::ios::binary);
            output << layout << "profile mk3cave\nsource-shift 0 -93\nsource-shift 1 0\n";
        }
        Document custom, custom_sorted;
        need(custom.load((root / "seed.BDB").u8string(), error), error);
        need(custom.save_game_sources((root / "custom.BDB").u8string(), report, error), error);
        need(custom_sorted.load((root / "custom.BDB").u8string(), error), error);
        need(custom_sorted.state().runtime_profile == "mk3cave" &&
                 custom_sorted.state().objects[0].runtime_dx == 0 &&
                 custom_sorted.state().objects[1].runtime_dx == -93,
             "X sorting attached a custom runtime offset to the wrong object");
        std::cout << "Runtime order, optimizer gate and sorted export checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
