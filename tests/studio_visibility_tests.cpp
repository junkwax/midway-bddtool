#include "Core/studio_visibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
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
std::vector<uint32_t> render(const State &s, Point camera) {
    std::vector<uint32_t> out(400 * 254);
    for (const auto &item : scene_items(s, camera)) {
        const auto &im = s.assets->data.images[item.image_slot];
        const auto &pal = s.assets->data.palettes[item.palette];
        for (int y = 0; y < 254; y++)
            for (int x = 0; x < 400; x++) {
                int ix = (int)std::floor(x + .5 - item.rect.x),
                    iy = (int)std::floor(y + .5 - item.rect.y);
                if (ix < 0 || iy < 0 || ix >= im.w || iy >= im.h)
                    continue;
                if (item.hflip)
                    ix = im.w - 1 - ix;
                if (item.vflip)
                    iy = im.h - 1 - iy;
                auto p = im.pix[(size_t)iy * im.w + ix];
                if (p)
                    out[y * 400 + x] = 0x10000 | pal.rgb555[p];
            }
    }
    return out;
}
State fixture() {
    State s;
    auto bank = std::make_shared<AssetBank>();
    BddCorePalette pal{};
    pal.count = 32;
    std::snprintf(pal.name, sizeof pal.name, "TEST");
    for (int i = 0; i < 32; i++) {
        pal.rgb555[i] = i == 1 ? 0 : (uint16_t)(i * 123);
        pal.argb[i] = bdd_core_rgb555_to_argb(pal.rgb555[i]);
    }
    bank->data.palettes.push_back(pal);
    for (int i = 0; i < 3; i++) {
        BddCoreImage im{};
        im.idx = i;
        im.w = i == 1 ? 32 : 64;
        im.h = 32;
        im.pix.resize(im.w * im.h);
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++)
                im.pix[y * im.w + x] = i == 1 ? 1 : (uint8_t)(1 + (x * 7 + y * 3 + i) % 31);
        bank->data.images.push_back(im);
        bank->default_palettes.push_back(0);
        BddImageMetadata m{};
        m.idx = i;
        bank->metadata.push_back(m);
    }
    s.assets = bank;
    for (int i = 0; i < 2; i++) {
        Plane p;
        p.bound = true;
        p.rank = i;
        p.name = "LAYER" + std::to_string(i);
        std::snprintf(p.source.name, sizeof p.source.name, "%s", p.name.c_str());
        p.source.parsed = 1;
        p.source.x2 = 799;
        p.source.y2 = 253;
        s.planes.push_back(p);
    }
    for (int i = 0; i < 3; i++) {
        Placement p;
        p.id = i + 1;
        p.plane = i == 1 ? 1 : 0;
        p.object.wx = 0x4000;
        p.object.ii = i;
        p.object.fl = 0;
        p.object.order = i;
        p.object.depth = i == 1 ? 32 : i == 2 ? 600 : 0;
        s.objects.push_back(p);
    }
    return s;
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch path");
        auto root = fs::absolute(
            fs::u8path(argv[1]) /
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        auto s = fixture();
        VisibilityOptions o{0, 8, 0, 4};
        auto plan = analyze_visibility(s, o);
        require(plan.analyzed && plan.verified && plan.changed_images == 2, plan.error);
        require(plan.images[0].covered == 32 * 32 && plan.images[2].outside == 64 * 32,
                "Covered/outside pixels misclassified");
        require(plan.after.assets->data.images[0].w == 32 &&
                    plan.proposed.objects == plan.baseline.objects,
                "Trim did not crop without extra placements");
        std::string error;
        require(verify_visibility(plan, error), error);
        for (double y = 0; y <= 4; y += .5)
            for (double x = 0; x <= 8; x += .5)
                require(render(s, {x, y}) == render(plan.after, {x, y}),
                        "Trim changed an in-range view");
        require(render(s, {500, 0}) != render(plan.after, {500, 0}),
                "Fixture failed to expose why out-of-range cameras require a new contract");
        for (int flip = 0; flip < 4; flip++) {
            auto flipped = s;
            flipped.objects[0].object.wx |= flip << 4;
            flipped.objects[1].object.wx |= (3 - flip) << 4;
            flipped.planes[0].scroll = flipped.planes[1].scroll = -.5;
            auto result = analyze_visibility(flipped, {-8, 8, -3, 4});
            require(result.verified && result.changed_images, result.error);
            for (double x : {-8., -3.5, 0., 1.25, 8.})
                for (double y : {-3., .5, 4.})
                    require(render(flipped, {x, y}) == render(result.after, {x, y}),
                            "XY flips/negative fractional parallax changed the trimmed view");
        }
        auto different_scroll = s;
        different_scroll.planes[1].scroll = .5;
        auto parallax = analyze_visibility(different_scroll, o);
        require(parallax.images[0].covered == 0 && !parallax.images[0].trimmed,
                "Different parallax was treated as permanent occlusion");
        auto reused = s;
        auto extra = reused.objects[0];
        extra.id = 10;
        extra.object.depth = 100;
        extra.object.order = 5;
        reused.objects.push_back(extra);
        auto sharing = analyze_visibility(reused, o);
        require(!sharing.images[0].trimmed && sharing.images[0].covered == 0,
                "A visible use of shared artwork was removed");
        auto hole = s;
        auto hole_bank = std::make_shared<AssetBank>(*s.assets);
        hole_bank->data.images[1].pix[0] = 0;
        hole.assets = hole_bank;
        auto holes = analyze_visibility(hole, o);
        require(holes.images[0].pixels[32] == 1 && holes.images[0].covered == 1023,
                "Transparent hole hid an opaque target pixel");
        for (int protection = 0; protection < 4; protection++) {
            auto protected_state = s;
            if (protection == 0)
                protected_state.objects[0].locked = true;
            if (protection == 1)
                protected_state.objects[0].hidden = true;
            if (protection == 2)
                protected_state.planes[0].bound = false;
            if (protection == 3) {
                auto bank = std::make_shared<AssetBank>(*s.assets);
                bank->metadata[0].frm = 1;
                protected_state.assets = bank;
            }
            auto protected_plan = analyze_visibility(protected_state, o);
            require(!protected_plan.images[0].trimmed && protected_plan.images[0].pixels[0] == 4,
                    "Protected artwork was trimmed");
            if (protection == 2)
                require(!protected_plan.verified && !protected_plan.changed_images,
                        "Unbound layout produced applicable trims");
        }
        auto dynamic_cover = s;
        auto dynamic_bank = std::make_shared<AssetBank>(*s.assets);
        dynamic_bank->metadata[1].frm = 1;
        dynamic_cover.assets = dynamic_bank;
        require(analyze_visibility(dynamic_cover, o).images[0].covered == 0,
                "Animated cover was used as permanent occlusion");
        auto tampered = plan;
        auto tampered_bank = std::make_shared<AssetBank>(*plan.after.assets);
        tampered_bank->data.images[0].pix[0] = 0;
        tampered.after.assets = tampered_bank;
        require(!verify_visibility(tampered, error),
                "Changed visible pixel escaped re-verification");
        tampered = plan;
        tampered.options.max_x = 650;
        require(!verify_visibility(tampered, error),
                "Changed camera contract escaped re-verification");
        require(!analyze_visibility(s, {8, 0, 0, 4}).analyzed, "Inverted camera range accepted");
        OptimizeProgress progress;
        progress.cancel = true;
        require(analyze_visibility(s, o, &progress).cancelled, "Cancellation ignored");

        BddCoreSaveResult saved{};
        auto bdd = root / "fixture.BDD";
        require(bdd_core_save_bdd(bdd.u8string().c_str(), s.assets->data.images.data(), 3,
                                  s.assets->data.palettes.data(), 1, &saved) != 0,
                saved.error);
        std::ofstream bdb(root / "fixture.BDB");
        bdb << "VISIBILITY 800 254 255 2 1 3\nLAYER0 0 799 0 63\nLAYER1 0 799 64 127\n"
               "4000 0 0 0 0\n4000 32 64 1 0\n4000 600 0 2 0\n";
        bdb.close();
        Document doc;
        require(doc.load((root / "fixture.BDB").u8string(), error), error);
        auto planes = doc.state().planes;
        for (auto &p : planes)
            p.y = 0;
        doc.seed_runtime(planes, 0, 0, 230);
        require(doc.save((root / "bound.BDB").u8string(), error), error);
        auto doc_plan = analyze_visibility(doc.state(), o);
        require(doc_plan.verified && doc_plan.changed_images, doc_plan.error);
        auto original = render(doc.state(), {4, 2});
        require(!doc.apply_visibility(doc_plan, false, error),
                "Applied without gameplay contract confirmation");
        require(doc.apply_visibility(doc_plan, true, error), error);
        require(render(doc.state(), {4, 2}) == original, "Apply changed view");
        require(doc.undo() && render(doc.state(), {4, 2}) == original, "Undo failed");
        require(doc.redo() && render(doc.state(), {4, 2}) == original, "Redo failed");
        require(!doc.apply_visibility(doc_plan, true, error), "Stale proposal applied");
        require(doc.save((root / "trimmed.BDB").u8string(), error), error);
        Document reopened;
        require(reopened.load((root / "trimmed.BDB").u8string(), error), error);
        require(render(reopened.state(), {4, 2}) == original, "Save/reopen changed view");
        if (argc >= 3) {
            Document real;
            require(real.load(argv[2], error), error);
            auto result = analyze_visibility(real.state(), {0, 1600, 0, 0});
            require(result.analyzed, result.error);
            std::ofstream out(root / "real-visibility.txt");
            out << visibility_report(result);
            require(!result.verified, "Raw unbound stage produced a verified trim");
        }
        std::cout << "Visibility continuous-range, occlusion, shared uses, protections, tampering "
                     "and roundtrip checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
