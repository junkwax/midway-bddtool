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
        // Three independently translated/flipped images must share ONE base, retaining all details.
        auto family_root = root / "shared-family";
        fs::create_directories(family_root);
        BddCoreImage family_images[3] = {bases[0], bases[1], {}};
        auto &third = family_images[2];
        third.idx = 42;
        third.w = 136;
        third.h = 80;
        third.pix.resize(136 * 80);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 128; x++)
                third.pix[(y + 8) * 136 + 135 - x - 4] =
                    x >= 32 && x < 64 ? (uint8_t)(48 + (x + y * 3) % 7) : bases[0].pix[y * 128 + x];
        require(bdd_core_save_bdd((family_root / "fixture.BDD").u8string().c_str(), family_images,
                                  3, palettes, 2, &saved) != 0,
                saved.error);
        std::ofstream family_bdb(family_root / "fixture.BDB");
        family_bdb << "FAMILY 1000 500 255 1 2 12\nFAMILY1 0 1000 0 499\n";
        for (int i = 0; i < 12; i++)
            family_bdb << std::hex << (0x4000 | ((i % 4) << 4)) << std::dec << ' '
                       << (20 + (i % 4) * 200) << ' ' << (20 + (i / 4) * 120) << ' ' << std::hex
                       << (40 + i / 4) << std::dec << ' ' << i % 2 << '\n';
        family_bdb.close();
        Document family_doc;
        require(family_doc.load((family_root / "fixture.BDB").u8string(), error), error);
        auto family_plan = find_shared_savings(family_doc, detail_options);
        require(family_plan.verified && family_plan.changes.size() == 3,
                "Three-image family was limited to a pair: " + family_plan.error);
        int shared_id = family_plan.changes.front().pieces.front().image;
        for (const auto &change : family_plan.changes)
            require(change.pieces.front().role == 1 && change.pieces.front().image == shared_id,
                    "Family stored more than one common base");
        check_roundtrip(family_doc, family_plan, family_root);
        auto family_cap = detail_options;
        family_cap.max_added_objects = 8;
        auto capped_family = find_shared_savings(family_doc, family_cap);
        require(capped_family.verified && capped_family.changes.size() == 2 &&
                    capped_family.proposed.objects <= capped_family.baseline.objects + 8,
                "Family did not fall back to a pair within the placement cap");

        // Unknown 12-pixel period: discover it without supplying a group size.
        auto discovery_root = root / "pattern-discovery";
        fs::create_directories(discovery_root);
        BddCoreImage periodic;
        periodic.idx = 7;
        periodic.w = 192;
        periodic.h = 24;
        periodic.pix.resize(192 * 24);
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 192; x++)
                periodic.pix[y * 192 + x] =
                    (uint8_t)(1 + ((x % 12) * 13 + y * 7 + (x % 12) * y) % 55);
        require(bdd_core_save_bdd((discovery_root / "fixture.BDD").u8string().c_str(), &periodic, 1,
                                  palettes, 2, &saved) != 0,
                saved.error);
        std::ofstream discovery_bdb(discovery_root / "fixture.BDB");
        discovery_bdb << "PERIOD 800 254 255 1 2 4\nPERIOD1 0 800 0 253\n"
                         "4000 0 0 7 0\n4010 200 0 7 1\n4020 0 100 7 0\n4030 200 100 7 1\n";
        discovery_bdb.close();
        Document discovery_doc;
        require(discovery_doc.load((discovery_root / "fixture.BDB").u8string(), error), error);
        PatternOptions discovery_options;
        discovery_options.image = 7;
        discovery_options.span = 32;
        auto discoveries = discover_patterns(discovery_doc, discovery_options);
        require(discoveries.error.empty() && !discoveries.proposals.empty(), discoveries.error);
        require(std::any_of(discoveries.proposals.begin(), discoveries.proposals.end(),
                            [](const auto &p) {
                                return p.changed_pixels == 0 && p.options.span == 12 &&
                                       p.options.mode == PatternMode::RepeatX;
                            }),
                "Unknown exact repeat period was missed");
        for (const auto &p : discoveries.proposals) {
            require(p.valid && verify_pattern(p, error), error);
            require(p.packing.proposed.video_bits < discoveries.baseline.video_bits,
                    "Discovery proposed a ROM increase");
        }
        auto exact_discovery =
            std::find_if(discoveries.proposals.begin(), discoveries.proposals.end(),
                         [](const auto &p) { return p.changed_pixels == 0; });
        auto before_discovery = render(discovery_doc, discovery_doc.bounds());
        require(discovery_doc.apply_pattern(*exact_discovery, error), error);
        require(render(discovery_doc, discovery_doc.bounds()) == before_discovery,
                "Exact discovered pattern changed the stage");
        require(discovery_doc.undo() &&
                    render(discovery_doc, discovery_doc.bounds()) == before_discovery,
                "Discovered pattern undo failed");
        require(discovery_doc.redo(), "Discovered pattern redo failed");
        require(discovery_doc.save((discovery_root / "discovered.BDB").u8string(), error), error);
        Document discovery_reopened;
        require(discovery_reopened.load((discovery_root / "discovered.BDB").u8string(), error),
                error);
        require(render(discovery_reopened, discovery_reopened.bounds()) == before_discovery,
                "Discovered pattern save/reopen changed pixels");
        auto stale_discovery = discovery_reopened;
        require(!stale_discovery.apply_pattern(*exact_discovery, error),
                "Discovered proposal applied to a different asset snapshot");
        PatternOptions vertical_search;
        vertical_search.image = 7;
        vertical_search.mode = PatternMode::RepeatY;
        auto vertical_discoveries = discover_patterns(artistic, vertical_search);
        require(vertical_discoveries.error.empty(), vertical_discoveries.error);
        for (const auto &p : vertical_discoveries.proposals) {
            require(p.options.mode == PatternMode::RepeatY ||
                        p.options.mode == PatternMode::MirrorY,
                    "Vertical discovery changed the wrong axis");
            require(verify_pattern(p, error), error);
        }
        OptimizeProgress cancel_discovery;
        cancel_discovery.cancel = true;
        auto cancelled_discovery =
            discover_patterns(discovery_doc, discovery_options, &cancel_discovery);
        require(cancelled_discovery.cancelled && cancelled_discovery.proposals.empty(),
                "Cancelled pattern search returned proposals");
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
        {
            auto color_root = root / "equivalent-colors";
            fs::create_directories(color_root);
            BddCorePalette variants[2]{};
            for (int p = 0; p < 2; p++) {
                std::snprintf(variants[p].name, sizeof variants[p].name, "COLORS%d", p);
                variants[p].count = 64;
                for (int c = 0; c < 64; c++) variants[p].rgb555[c] = (uint16_t)(c + p * 100);
                for (int c : {1, 9, 17, 25}) variants[p].rgb555[c] = 0; // Opaque black, never transparent.
                for (int c : {2, 10, 18, 26}) variants[p].rgb555[c] = (uint16_t)(512 + p);
                for (int c : {3, 11, 19, 27}) variants[p].rgb555[c] = (uint16_t)(800 + p);
                for (int c = 0; c < 64; c++) variants[p].argb[c] = bdd_core_rgb555_to_argb(variants[p].rgb555[c]);
            }
            BddCoreImage colored[2];
            for (int i = 0; i < 2; i++) {
                colored[i].idx = 7 + i;
                colored[i].w = 96; colored[i].h = 32;
                colored[i].pix.resize(96 * 32);
                for (int y = 0; y < 32; y++) for (int x = 0; x < 96; x++) {
                    int klass = (x * 7 + y * 3 + x * y) % 4;
                    int idx = klass ? klass + (i ? 8 : 0) + ((x / 4 + (y / 4) * (i + 1)) % 2 ? 16 : 0) : 0;
                    colored[i].pix[y * 96 + x] = (uint8_t)idx;
                }
            }
            auto write_colors = [&]() {
                require(bdd_core_save_bdd((color_root / "fixture.BDD").u8string().c_str(),
                        colored, 2, variants, 2, &saved) != 0, saved.error);
            };
            write_colors();
            std::ofstream colors_bdb(color_root / "fixture.BDB");
            colors_bdb << "COLORS 800 254 255 1 2 8\nCOLORS1 0 799 0 253\n";
            for (int i = 0; i < 8; i++) colors_bdb << std::hex << (0x4000 | ((i % 4) << 4))
                << std::dec << ' ' << (i % 4) * 120 << ' ' << (i / 4) * 80 << ' '
                << std::hex << (7 + i / 4) << std::dec << ' ' << i % 2 << '\n';
            colors_bdb.close();
            Document colors_doc;
            require(colors_doc.load((color_root / "fixture.BDB").u8string(), error), error);
            OptimizeOptions color_options;
            color_options.palette_reuse_only = true;
            auto colors_plan = find_lossless_savings(colors_doc, color_options);
            require(colors_plan.verified && colors_plan.changes.size() == 2 && colors_plan.proposed.images == 1,
                    "Equivalent-color artwork did not share one payload: " + optimization_report(colors_plan));
            require(colors_plan.proposed.objects == colors_plan.baseline.objects,
                    "Whole-image palette reuse added placements");
            for (const auto &change : colors_plan.changes)
                require(change.reindexed && change.equivalent_indices > 0,
                        "Equivalent-color change lost static-palette review marker");
            check_roundtrip(colors_doc, colors_plan, color_root);
            auto capped = color_options;
            capped.max_palettes = 2;
            auto capped_colors = find_lossless_savings(colors_doc, capped);
            require(capped_colors.verified && capped_colors.changes.empty(), "Palette cap ignored");
            auto preserving = color_options;
            preserving.compact_palettes = false;
            auto preserved_colors = find_lossless_savings(colors_doc, preserving);
            require(preserved_colors.verified && std::none_of(preserved_colors.changes.begin(),
                    preserved_colors.changes.end(), [](const auto &c) { return c.reindexed || c.equivalent_indices; }),
                    "Index-preserving scan merged opaque indices");
            variants[1].rgb555[17] = 999; // Same in one palette, different in the other.
            variants[1].argb[17] = bdd_core_rgb555_to_argb(999);
            write_colors();
            Document different_variant;
            require(different_variant.load((color_root / "fixture.BDB").u8string(), error), error);
            auto variant_plan = find_lossless_savings(different_variant, color_options);
            require(variant_plan.verified && variant_plan.proposed.images == 2,
                    "Indices merged despite a conflicting palette variant");
            auto variant_root = color_root / "variants";
            fs::create_directories(variant_root);
            check_roundtrip(different_variant, variant_plan, variant_root);
            // A pure permutation with identical BPP still needs neutral canonicalization to seed sharing.
            for (auto &image : colored) for (auto &value : image.pix)
                if (value) value = (uint8_t)(1 + ((value - 1) % 8) % 3);
            for (auto &value : colored[1].pix) if (value) value = value == 3 ? 1 : value + 1;
            write_colors();
            Document permutation;
            require(permutation.load((color_root / "fixture.BDB").u8string(), error), error);
            auto permutation_plan = find_lossless_savings(permutation, color_options);
            require(permutation_plan.verified && permutation_plan.proposed.images == 1,
                    "Same-BPP index permutations were not shared");
            auto permutation_root = color_root / "permutation";
            fs::create_directories(permutation_root);
            check_roundtrip(permutation, permutation_plan, permutation_root);
        }
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
            auto palette_options = options;
            palette_options.palette_reuse_only = true;
            auto palette_actual = find_lossless_savings(real, palette_options);
            require(palette_actual.verified, palette_actual.error);
            std::ofstream palette_report(root / "palette-reuse.txt");
            palette_report << optimization_report(palette_actual);
            std::cout << "Palette reuse: " << palette_actual.baseline.video_bits / 8 << " -> "
                      << palette_actual.proposed.video_bits / 8 << " modeled bytes, palettes "
                      << palette_actual.baseline.palettes << " -> " << palette_actual.proposed.palettes
                      << ", placements " << palette_actual.proposed.objects << ".\n";
            if (!palette_actual.changes.empty()) {
                auto r = root / "real-palette-reuse";
                fs::create_directories(r);
                check_roundtrip(real, palette_actual, r);
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
                auto discovered = discover_patterns(real, cave);
                require(discovered.error.empty() && !discovered.proposals.empty(), discovered.error);
                std::ofstream discovery_report(root / "discovered-patterns.txt");
                discovery_report << discovered.sampled << " sampled windows; " << discovered.packed
                                 << " packed candidates; " << discovered.proposals.size()
                                 << " tradeoffs\n";
                for (size_t i = 0; i < discovered.proposals.size(); i++) {
                    const auto &p = discovered.proposals[i];
                    discovery_report << "mode=" << (int)p.options.mode << " span=" << p.options.span
                                     << " offset=" << p.options.offset
                                     << " alternating=" << p.options.alternate_flip
                                     << " far=" << p.options.use_far_side
                                     << " changed=" << p.changed_pixels
                                     << " silhouette=" << p.silhouette_pixels
                                     << " video_bytes=" << p.packing.proposed.video_bits / 8
                                     << " objects=" << p.packing.proposed.objects << '\n';
                    auto candidate_doc = real;
                    require(candidate_doc.apply_pattern(p, error), error);
                    auto candidate_path = dir / ("discovered-" + std::to_string(i) + ".BDB");
                    require(candidate_doc.save(candidate_path.u8string(), error), error);
                    Document checked;
                    require(checked.load(candidate_path.u8string(), error), error);
                    require(render(candidate_doc, real.bounds()) == render(checked, real.bounds()),
                            "Discovered cave proposal changed during save/reopen");
                }
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
