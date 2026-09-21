#include "Core/studio_optimizer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <algorithm>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::vector<uint32_t> render(const Document &d, Rect bounds) {
    int w = (int)bounds.w, h = (int)bounds.h;
    require((uint64_t)w * h < 64000000, "Fixture render too large");
    std::vector<uint32_t> result((size_t)w * h);
    for (const auto &s : d.scene()) {
        auto &im = d.state().assets->data.images[s.image_slot];
        auto &pal = d.state().assets->data.palettes[s.palette];
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                int px = (int)std::llround(s.rect.x - bounds.x) + x,
                    py = (int)std::llround(s.rect.y - bounds.y) + y;
                auto p = im.pix[(size_t)(s.vflip ? im.h - 1 - y : y) * im.w +
                                (s.hflip ? im.w - 1 - x : x)];
                if (p && px >= 0 && py >= 0 && px < w && py < h)
                    result[(size_t)py * w + px] = 0x10000 | pal.rgb555[p];
            }
    }
    return result;
}
void check_roundtrip(Document doc, const OptimizationPlan &plan, const fs::path &root) {
    std::string error;
    auto before = doc;
    auto bounds = doc.bounds();
    auto original = render(doc, bounds);
    require(doc.apply_optimization(plan, error), error);
    require(render(doc, bounds) == original,
            "Applied scene differs (palette or mirrored placement)");
    require(doc.undo() && render(doc, bounds) == original &&
                doc.state().assets == before.state().assets,
            "Undo did not restore original assets");
    require(doc.redo() && render(doc, bounds) == original, "Redo changed scene");
    require(doc.save((root / "optimized.BDB").u8string(), error), error);
    Document reopened;
    require(reopened.load((root / "optimized.BDB").u8string(), error), error);
    require(render(reopened, bounds) == original, "Save/reopen changed optimized scene");
    require(reopened.state().assets->data.images.size() == plan.proposed.images,
            "Save retained replaced image payloads");
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch path");
        auto root =
            fs::u8path(argv[1]) /
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        fs::create_directories(root);
        BddCoreImage im{};
        im.idx = 7;
        im.w = 64;
        im.h = 32;
        im.pix.resize(64 * 32);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 64; x++) {
                int xx = x < 32 ? x : 63 - x, yy = y < 16 ? y : 31 - y;
                im.pix[y * 64 + x] = (xx < 4 || yy < 2) ? 0 : (uint8_t)(33 + (xx * 3 + yy * 5) % 7);
            }
        BddCorePalette palettes[2]{};
        for (int pi = 0; pi < 2; pi++) {
            std::snprintf(palettes[pi].name, sizeof palettes[pi].name, "TEST%d", pi);
            palettes[pi].count = 64;
            for (int i = 0; i < 64; i++) {
                palettes[pi].rgb555[i] = (uint16_t)((i * 31 + pi * 1024) & 32767);
                palettes[pi].argb[i] = bdd_core_rgb555_to_argb(palettes[pi].rgb555[i]);
            }
            palettes[pi].rgb555[33] = 0;
            palettes[pi].argb[33] = 0xff000000; // opaque black remains opaque
        }
        BddCoreSaveResult saved{};
        require(bdd_core_save_bdd((root / "fixture.BDD").u8string().c_str(), &im, 1, palettes, 2,
                                  &saved) != 0,
                saved.error);
        std::ofstream bdb(root / "fixture.BDB");
        bdb << "OPTCASE 800 254 255 1 2 4\nLAYER1 0 500 0 253\n4000 20 20 7 0\n4010 110 20 7 "
               "1\n4020 200 20 7 0\n4030 290 20 7 1\n";
        bdb.close();
        Document doc;
        std::string error;
        require(doc.load((root / "fixture.BDB").u8string(), error), error);
        OptimizeOptions options;
        options.policy = 0;
        auto plan = find_lossless_savings(doc, options);
        require(plan.verified, plan.error);
        require(!plan.changes.empty(), "No mirror/compact opportunity found");
        require(plan.changes[0].pieces.size() > 1 &&
                    plan.proposed.video_bits < plan.baseline.video_bits,
                "Mirrored subdivision did not save bytes");
        require(plan.proposed.palettes > plan.baseline.palettes,
                "Palette variants were not independently remapped");
        check_roundtrip(doc, plan, root);
        auto changed = doc;
        changed.move({doc.state().objects[0].id}, 1, 0);
        require(!changed.apply_optimization(plan, error), "Stale plan applied");
        auto broken = plan;
        broken.changes[0].pieces[0].flip_y = !broken.changes[0].pieces[0].flip_y;
        require(!verify_optimization(broken, error), "Bad mirror escaped pixel verification");
        broken = plan;
        broken.after.objects.front().object.depth++;
        require(!verify_optimization(broken, error), "Wrong split placement escaped verification");
        options.compact_palettes = false;
        auto preserve = find_lossless_savings(doc, options);
        require(preserve.verified, preserve.error);
        require(preserve.proposed.palettes == preserve.baseline.palettes,
                "Index-preserving mode changed palette count");
        require(!preserve.changes.empty(), "Index-preserving mirror reuse failed");
        auto sub = root / "preserved";
        fs::create_directories(sub);
        check_roundtrip(doc, preserve, sub);
        options.max_added_objects = 0;
        auto limited = find_lossless_savings(doc, options);
        require(limited.verified && limited.proposed.objects <= limited.baseline.objects,
                "Placement limit exceeded");
        OptimizeProgress progress;
        progress.cancel = true;
        require(find_lossless_savings(doc, options, &progress).cancelled, "Cancellation ignored");
        // Whole images shared through both axis flips, without changing palette indices.
        BddCoreImage pair[2];
        for (int i = 0; i < 2; i++) {
            pair[i].idx = 20 + i;
            pair[i].w = 32;
            pair[i].h = 8;
            pair[i].pix.resize(256);
        }
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 32; x++)
                pair[0].pix[y * 32 + x] = (uint8_t)(1 + (x * 13 + y * 7 + x * y) % 63);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 32; x++)
                pair[1].pix[y * 32 + x] = pair[0].pix[(7 - y) * 32 + 31 - x];
        require(bdd_core_save_bdd((root / "pair.BDD").u8string().c_str(), pair, 2, palettes, 2,
                                  &saved) != 0,
                saved.error);
        std::ofstream pair_bdb(root / "pair.BDB");
        pair_bdb
            << "PAIR 800 254 255 1 2 2\nLAYER1 0 500 0 253\n4000 20 20 14 0\n4030 120 20 15 1\n";
        pair_bdb.close();
        Document paired;
        require(paired.load((root / "pair.BDB").u8string(), error), error);
        OptimizeOptions reuse;
        reuse.compact_palettes = false;
        reuse.max_pieces = 1;
        auto shared = find_lossless_savings(paired, reuse);
        require(shared.verified && shared.proposed.images == 1 &&
                    shared.proposed.video_bits * 2 == shared.baseline.video_bits,
                "Whole-image XY mirror reuse did not remove the second payload");
        auto shared_root = root / "shared";
        fs::create_directories(shared_root);
        check_roundtrip(paired, shared, shared_root);

        auto fixture = [&](BddCoreImage artwork, const std::string &name) {
            auto dir = root / name;
            fs::create_directories(dir);
            artwork.idx = 7;
            require(bdd_core_save_bdd((dir / "fixture.BDD").u8string().c_str(), &artwork, 1,
                                      palettes, 2, &saved) != 0,
                    saved.error);
            fs::copy_file(root / "fixture.BDB", dir / "fixture.BDB");
            Document d;
            require(d.load((dir / "fixture.BDB").u8string(), error), error);
            return d;
        };
        // A partial edge cannot be found by equally dividing the entire image.
        BddCoreImage strip;
        strip.w = 104;
        strip.h = 25;
        strip.pix.resize((size_t)strip.w * strip.h);
        for (int y = 0; y < strip.h; y++)
            for (int x = 0; x < strip.w; x++) {
                int xx = x % 24;
                strip.pix[(size_t)y * strip.w + x] = (uint8_t)(1 + (xx * 13 + y * 7 + xx * y) % 63);
            }
        auto repeats = fixture(strip, "partial-repeat");
        OptimizeOptions exact;
        exact.compact_palettes = false;
        exact.policy = 0;
        exact.max_pieces = 5;
        auto repeated = find_lossless_savings(repeats, exact);
        require(repeated.verified &&
                    repeated.proposed.video_bits * 2 < repeated.baseline.video_bits,
                "Constant-size repeat with leftover edge was missed");
        check_roundtrip(repeats, repeated, root / "partial-repeat");

        BddCoreImage islands;
        islands.w = 92;
        islands.h = 48;
        islands.pix.resize((size_t)islands.w * islands.h);
        const int regions[][4] = {{4, 3, 12, 16}, {32, 20, 20, 20}, {76, 5, 8, 9}};
        for (const auto &r : regions)
            for (int y = r[1]; y < r[1] + r[3]; y++)
                for (int x = r[0]; x < r[0] + r[2]; x++)
                    islands.pix[(size_t)y * islands.w + x] =
                        (uint8_t)(1 + (x * 13 + y * 7 + x * y) % 63);
        auto sparse = fixture(islands, "islands");
        exact.max_pieces = 3;
        auto trimmed = find_lossless_savings(sparse, exact);
        require(trimmed.verified && !trimmed.changes.empty(), "Blank corridors were not split");
        int covered = 0;
        for (const auto &piece : trimmed.changes.front().pieces)
            covered += piece.w * piece.h;
        require(covered < islands.w * islands.h / 2, "Empty internal space remained in subframes");
        check_roundtrip(sparse, trimmed, root / "islands");

        // Deliberate pattern editing: compare against an independently constructed reference,
        // then prove packing, all placement flips, palettes, undo and disk roundtrip.
        for (int y = 0; y < strip.h; y++)
            for (int x = 0; x < strip.w; x++)
                strip.pix[(size_t)y * strip.w + x] =
                    (x + 2 * y) % 11 == 0 ? 0 : (uint8_t)(1 + (x * 13 + y * 7 + x * y) % 63);
        auto artistic = fixture(strip, "artistic");
        for (int mode = 0; mode < 4; mode++)
            for (int far = 0; far < 2; far++) {
                PatternOptions p;
                p.image = 7;
                p.mode = (PatternMode)mode;
                p.offset = mode == 1 ? 2 : 4;
                p.span = mode == 1 ? 7 : 24;
                p.alternate_flip = far != 0;
                p.use_far_side = far != 0;
                auto pattern = preview_pattern(artistic, p);
                require(pattern.valid, pattern.error);
                require(pattern.changed_pixels && pattern.silhouette_pixels,
                        "Pattern change/silhouette metrics are empty");
                BddCoreImage expected = strip;
                for (int y = 0; y < strip.h; y++)
                    for (int x = 0; x < strip.w; x++) {
                        int sx = x, sy = y;
                        if (mode < 2) {
                            int pos = mode == 0 ? x : y;
                            int start = (pos / p.span) * p.span;
                            int rel = pos - start;
                            if (far && (pos / p.span) % 2)
                                rel = p.span - 1 - rel;
                            if (mode == 0)
                                sx = p.offset + rel;
                            else
                                sy = p.offset + rel;
                        } else if (mode == 2) {
                            if ((!far && x >= strip.w / 2) || (far && x < strip.w / 2))
                                sx = strip.w - 1 - x;
                        } else {
                            if ((!far && y > strip.h / 2) || (far && y < strip.h / 2))
                                sy = strip.h - 1 - y;
                        }
                        expected.pix[(size_t)y * strip.w + x] =
                            strip.pix[(size_t)sy * strip.w + sx];
                    }
                auto folder = "pattern-" + std::to_string(mode) + "-" + std::to_string(far);
                auto reference = fixture(expected, folder);
                auto edited = artistic;
                auto bounds = artistic.bounds();
                auto original = render(artistic, bounds), wanted = render(reference, bounds);
                require(original != wanted, "Pattern test is not an actual artwork change");
                require(edited.apply_pattern(pattern, error), error);
                require(render(edited, bounds) == wanted,
                        "Pattern packing changed intended pixels");
                require(edited.undo() && render(edited, bounds) == original,
                        "Pattern undo failed to restore unique source artwork");
                require(edited.redo() && render(edited, bounds) == wanted, "Pattern redo failed");
                auto output = root / folder / "result.BDB";
                require(edited.save(output.u8string(), error), error);
                Document reopened;
                require(reopened.load(output.u8string(), error) &&
                            render(reopened, bounds) == wanted,
                        "Pattern save/reopen changed artwork");
                require(!edited.apply_pattern(pattern, error), "Stale pattern applied");
                auto bad = pattern;
                bad.options.use_far_side = !bad.options.use_far_side;
                if (mode < 2)
                    bad.options.offset = 0;
                require(!verify_pattern(bad, error),
                        "Tampered pattern source escaped verification");
                require(pattern.packing.proposed.palettes == 2, "Pattern changed palette indices");
            }
        PatternOptions invalid;
        invalid.image = 7;
        invalid.offset = 100;
        invalid.span = 24;
        require(!preview_pattern(artistic, invalid).valid, "Out-of-bounds motif accepted");
        auto locked = artistic;
        locked.set_plane_flags(0, false, true);
        invalid.offset = 0;
        require(!preview_pattern(locked, invalid).valid, "Locked pattern artwork changed");
        auto closest = suggest_pattern(artistic, invalid);
        require(closest.valid && closest.options.span == invalid.span &&
                    closest.options.offset % 4 == 0,
                "Closest-group search failed");
        uint64_t best_pattern_score = UINT64_MAX;
        for (int offset = 0; offset <= strip.w - invalid.span; offset += 4) {
            auto candidate = invalid;
            candidate.offset = offset;
            auto check = preview_pattern(artistic, candidate);
            require(check.valid, check.error);
            best_pattern_score =
                std::min(best_pattern_score, check.changed_pixels + check.silhouette_pixels * 2);
        }
        require(closest.changed_pixels + closest.silhouette_pixels * 2 == best_pattern_score,
                "Closest-group suggestion missed a better sampled window");
        // A stage strip spans distinct images, flipped placements and gaps; one source image
        // is also used on another layer and must survive the replacement.
        auto layer_root = root / "layer-pattern";
        fs::create_directories(layer_root);
        strip.idx = 7;
        auto other = strip;
        other.idx = 20;
        for (auto &p : other.pix)
            if (p)
                p = (uint8_t)(1 + p % 63);
        BddCoreImage layer_images[] = {strip, other};
        require(bdd_core_save_bdd((layer_root / "fixture.BDD").u8string().c_str(), layer_images, 2,
                                  palettes, 2, &saved) != 0,
                saved.error);
        std::ofstream layer_bdb(layer_root / "fixture.BDB");
        layer_bdb << "LAYERCASE 800 500 255 2 2 5\nSPIKES 0 500 0 100\nOTHER 0 500 200 300\n"
                     "4000 20 20 7 0\n4010 130 20 14 0\n4020 230 20 7 0\n4030 330 20 14 0\n"
                     "4000 20 220 7 1\n";
        layer_bdb.close();
        Document layer_doc;
        require(layer_doc.load((layer_root / "fixture.BDB").u8string(), error), error);
        PatternOptions layer_options;
        layer_options.plane = 0;
        layer_options.span = 28;
        layer_options.offset = 8;
        layer_options.alternate_flip = true;
        auto layer_plan = preview_pattern(layer_doc, layer_options);
        require(layer_plan.valid, layer_plan.error);
        require(layer_plan.uses == 4 && layer_plan.packing.changes.size() == 1,
                "Layer pattern did not replace the whole strip");
        const auto &composite = layer_plan.source.assets->data.images.back();
        std::vector<uint8_t> expected_composite((size_t)composite.w * composite.h);
        for (const auto &p : layer_doc.state().objects) {
            if (p.plane != 0)
                continue;
            const auto &im = *layer_doc.image(p.object.ii);
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++) {
                    auto value = im.pix[(size_t)((p.object.wx & 0x20) ? im.h - 1 - y : y) * im.w +
                                        ((p.object.wx & 0x10) ? im.w - 1 - x : x)];
                    if (value)
                        expected_composite[(size_t)y * composite.w + p.object.depth - 20 + x] =
                            value;
                }
        }
        require(composite.pix == expected_composite, "Layer composition lost gaps, flips or order");
        auto original_layer_pixels = render(layer_doc, layer_doc.bounds());
        auto layer_bounds = layer_doc.bounds();
        auto original_assets = layer_doc.state().assets;
        require(layer_doc.apply_pattern(layer_plan, error), error);
        require(layer_doc.image(7) && layer_doc.image(7)->pix == strip.pix && !layer_doc.image(20),
                "Layer replacement modified an image shared with another layer");
        auto patterned_layer_pixels = render(layer_doc, layer_bounds);
        require(patterned_layer_pixels != original_layer_pixels,
                "Whole-layer pattern made no change");
        require(layer_doc.undo() && layer_doc.state().assets == original_assets &&
                    render(layer_doc, layer_bounds) == original_layer_pixels,
                "Layer pattern undo failed");
        require(layer_doc.redo() && render(layer_doc, layer_bounds) == patterned_layer_pixels,
                "Layer pattern redo failed");
        auto layer_output = layer_root / "result.BDB";
        require(layer_doc.save(layer_output.u8string(), error), error);
        Document layer_reopened;
        require(layer_reopened.load(layer_output.u8string(), error) &&
                    render(layer_reopened, layer_bounds) == patterned_layer_pixels,
                "Layer pattern save/reopen changed artwork");
        layer_options.plane = 0;
        require(!preview_pattern(artistic, layer_options).valid,
                "Layer pattern accepted mixed palette assignments");
        // Shared opaque pixels across translated, XY-mirrored images with unique central shading.
        auto reuse_root = root / "residuals";
        fs::create_directories(reuse_root);
        BddCoreImage bases[2];
        bases[0].idx = 40;
        bases[0].w = 128;
        bases[0].h = 64;
        bases[0].pix.resize(128 * 64);
        bases[1].idx = 41;
        bases[1].w = 136;
        bases[1].h = 72;
        bases[1].pix.resize(136 * 72);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 128; x++) {
                auto common = (uint8_t)(8 + (x * 13 + y * 7 + x * y) % 24);
                bool detail = x >= 32 && x < 64;
                bases[0].pix[y * 128 + x] = detail ? (uint8_t)(1 + (x + y) % 7) : common;
                bases[1].pix[(71 - y - 4) * 136 + 135 - x - 4] =
                    detail ? (uint8_t)(33 + (x * 3 + y) % 7) : common;
            }
        require(bdd_core_save_bdd((reuse_root / "fixture.BDD").u8string().c_str(), bases, 2,
                                  palettes, 2, &saved) != 0,
                saved.error);
        std::ofstream reuse_bdb(reuse_root / "fixture.BDB");
        reuse_bdb << "DETAILS 1000 500 255 1 2 8\nDETAILS1 0 1000 0 499\n";
        for (int i = 0; i < 8; i++)
            reuse_bdb << std::hex << (0x4000 | ((i % 4) << 4)) << std::dec << ' '
                      << (20 + (i % 4) * 200) << ' ' << (20 + (i / 4) * 120) << ' ' << std::hex
                      << (40 + i / 4) << std::dec << ' ' << i % 2 << '\n';
        reuse_bdb.close();
        Document detail_doc;
        require(detail_doc.load((reuse_root / "fixture.BDB").u8string(), error), error);
        OptimizeOptions detail_options;
        detail_options.deep = true;
        auto details = find_shared_savings(detail_doc, detail_options);
        require(details.verified && details.changes.size() == 2 &&
                    details.proposed.video_bits < details.baseline.video_bits,
                "Translated shared base plus unique details was missed: " + details.error);
        require(details.proposed.palettes == details.baseline.palettes,
                "Shared bases remapped palettes");
        check_roundtrip(detail_doc, details, reuse_root);
        auto constrained = detail_options;
        constrained.max_pieces = 1;
        auto no_details = find_shared_savings(detail_doc, constrained);
        require(no_details.verified && no_details.changes.empty(), "Shared-piece limit ignored");
        OptimizeProgress cancel_shared;
        cancel_shared.cancel = true;
        require(find_shared_savings(detail_doc, detail_options, &cancel_shared).cancelled,
                "Shared scan cancellation ignored");
        auto heat = optimization_regions(details);
        require(
            std::any_of(heat.begin(), heat.end(), [](const auto &r) { return r.kinds & 4; }) &&
                std::any_of(heat.begin(), heat.end(), [](const auto &r) { return r.kinds & 16; }),
            "Shared/detail heatmap regions missing");
        for (const auto &r : heat) {
            const auto &im = *detail_doc.image(r.image);
            require(r.change >= 0 && r.change < (int)details.changes.size() && r.rect.x >= 0 &&
                        r.rect.y >= 0 && r.rect.x + r.rect.w <= im.w && r.rect.y + r.rect.h <= im.h,
                    "Heatmap region escaped its source image");
        }
        auto overlapping = details;
        auto corrupt = std::make_shared<AssetBank>(*details.after.assets);
        int common_id = details.changes[0].pieces[0].image;
        for (auto &im : corrupt->data.images)
            if (im.idx == common_id)
                for (auto &p : im.pix)
                    if (!p)
                        p = 1;
        overlapping.after.assets = corrupt;
        require(!verify_optimization(overlapping, error),
                "Opaque shared/detail overlap escaped verification");
        auto window = [](const State &s, Point camera) {
            std::vector<uint32_t> pixels(400 * 254);
            for (const auto &item : scene_items(s, camera)) {
                const auto &im = s.assets->data.images[item.image_slot];
                const auto &pal = s.assets->data.palettes[item.palette];
                for (int y = 0; y < im.h; y++)
                    for (int x = 0; x < im.w; x++) {
                        int sx = (int)std::floor(item.rect.x) + x,
                            sy = (int)std::floor(item.rect.y) + y;
                        auto p = im.pix[(size_t)(item.vflip ? im.h - 1 - y : y) * im.w +
                                        (item.hflip ? im.w - 1 - x : x)];
                        if (p && sx >= 0 && sy >= 0 && sx < 400 && sy < 254)
                            pixels[sy * 400 + sx] = 0x10000u | pal.rgb555[p];
                    }
            }
            return pixels;
        };
        details.before.planes[0].scroll = details.after.planes[0].scroll = .5;
        for (Point camera : {Point{-80, -20}, Point{0, 0}, Point{241, 19}, Point{610, 90}})
            require(window(details.before, camera) == window(details.after, camera),
                    "Camera comparison changed exact artwork");
        auto scene = scene_items(details.before, {100, 20});
        require(!scene.empty() && scene.front().rect.x == detail_doc.scene().front().rect.x - 50,
                "Camera preview did not apply layer parallax");
        auto trim_regions = optimization_regions(trimmed);
        require(std::any_of(trim_regions.begin(), trim_regions.end(),
                            [](const auto &r) { return r.kinds & 1; }),
                "Blank-trim heatmap missing");
        auto palette_regions = optimization_regions(plan);
        require(std::any_of(palette_regions.begin(), palette_regions.end(),
                            [](const auto &r) { return r.kinds & 2; }),
                "Palette heatmap missing");
        if (argc >= 3) {
            Document real;
            require(real.load(argv[2], error), error);
            options = {};
            if (argc >= 4 && std::string(argv[3]) == "--deep")
                options.deep = true;
            auto actual = find_lossless_savings(real, options);
            require(actual.verified, actual.error);
            std::ofstream report(root / "optimization.txt");
            report << optimization_report(actual);
            std::cout << optimization_report(actual);
            if (!actual.changes.empty()) {
                auto r = root / "real";
                fs::create_directories(r);
                check_roundtrip(real, actual, r);
            }
            auto shared_actual = find_shared_savings(real, options);
            require(shared_actual.verified, shared_actual.error);
            std::ofstream shared_report(root / "shared-pieces.txt");
            shared_report << optimization_report(shared_actual);
            std::cout << "Shared-base scan: " << shared_actual.changes.size() << " source images; "
                      << shared_actual.baseline.video_bits / 8 << " -> "
                      << shared_actual.proposed.video_bits / 8 << " modeled bytes.\n";
            if (!shared_actual.changes.empty()) {
                auto r = root / "real-shared";
                fs::create_directories(r);
                check_roundtrip(real, shared_actual, r);
            }
            // The final layer in the local MK3CAVE fixture is the jagged spike strip.
            if (real.state().name == "mk3cave") {
                PatternOptions cave;
                cave.plane = (int)real.state().planes.size() - 1;
                cave.span = 128;
                cave.offset = 128;
                auto study = preview_pattern(real, cave);
                require(study.valid, study.error);
                auto before = optimization_budget(real.state());
                std::cout << "Cave spike pattern: " << study.uses << " original placements, "
                          << study.changed_pixels << " changed pixels, " << study.silhouette_pixels
                          << " silhouette pixels; stage video estimate " << before.video_bits / 8
                          << " -> " << study.packing.proposed.video_bits / 8 << " bytes.\n";
                auto modified = real;
                require(modified.apply_pattern(study, error), error);
                auto dir = root / "cave-pattern";
                fs::create_directories(dir);
                require(modified.save((dir / "pattern.BDB").u8string(), error), error);
                Document reopened;
                require(reopened.load((dir / "pattern.BDB").u8string(), error), error);
                require(render(modified, real.bounds()) == render(reopened, real.bounds()),
                        "Cave pattern save/reopen failed");
            }
        }
        std::cout << "Lossless optimizer: palette variants, XY flips, exact pixels, undo/redo, "
                     "save/reopen, stale plans and limits passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
