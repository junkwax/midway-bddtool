#include "Core/studio_animation_optimizer.h"
#include <iostream>
#include <stdexcept>
using namespace studio;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
AnimationPreview fixture() {
    AnimationPreview s;
    auto bank = std::make_shared<AssetBank>();
    BddCorePalette pal{};
    pal.count = 64;
    for (int i = 1; i < pal.count; i++) {
        pal.rgb555[i] = i == 31 ? 0 : (uint16_t)(i * 37);
        pal.argb[i] = 0xff000000 | (i * 737);
    }
    bank->data.palettes.push_back(pal);
    for (int i = 0; i < 3; i++) {
        BddCoreImage im{};
        im.idx = i;
        im.w = 32;
        im.h = 32;
        im.pix.resize(1024);
        for (int y = 1; y < 31; y++)
            for (int x = 0; x < 32; x++)
                im.pix[y * 32 + x] = ((x + y * 7) % 5) ? 1 : 31;
        // Every pose has a different moving detail and one transparency change.
        im.pix[(8 + i) * 32 + 10] = 17;
        im.pix[(17 + i) * 32 + 19] = 0;
        bank->data.images.push_back(im);
        s.frames.push_back({i, 0, 11, -5, "FRAME" + std::to_string(i)});
    }
    s.artwork.assets = bank;
    s.sequence = {0, 1, 2, 1, 0};
    s.anchors = {{10, 20}, {80, 20}};
    s.frame_ticks = 5;
    s.scroll = .5;
    return s;
}
int main(int argc, char **argv) {
    try {
        auto source = fixture();
        auto bank = source.artwork.assets;
        auto a = analyze_animation(source);
        require(a.error.empty() && a.alternatives.size() == 5, a.error);
        require(a.alternatives[2].max_pieces == 2 && a.alternatives[2].reused_pieces >= 2,
                "Did not discover shared stationary pixels.");
        require(a.alternatives[2].video_bits < a.baseline_bits,
                "Shared-base fixture did not save space.");
        require(a.alternatives[1].palette_bytes == 8,
                "Did not compact the family union including zero.");
        require(a.source.sequence == source.sequence &&
                    a.source.frame_ticks == source.frame_ticks && source.artwork.assets == bank &&
                    a.source.anchors[1].x == 80,
                "Analysis changed sequence, timing, actors or source ownership.");
        std::string error;
        auto tampered = a.alternatives[2];
        tampered.frames[1][0].x++;
        require(!verify_animation_alternative(source, tampered, error),
                "Misaligned anchor accepted.");
        tampered = a.alternatives[2];
        auto changed = std::make_shared<AssetBank>(*tampered.artwork.assets);
        for (auto &p : changed->data.palettes)
            p.rgb555[1] ^= 31;
        tampered.artwork.assets = changed;
        require(!verify_animation_alternative(source, tampered, error), "Changed color accepted.");
        tampered = a.alternatives[2];
        tampered.frames[0].push_back(tampered.frames[0][0]);
        require(!verify_animation_alternative(source, tampered, error),
                "Opaque overlapping pieces accepted.");
        // Nonuniform anchors and dimensions must align in actor space, not image space.
        auto varied = source;
        varied.frames[1].anchor_x = -3;
        varied.frames[1].anchor_y = 8;
        require(analyze_animation(varied).error.empty(),
                "Signed frame anchors broke reconstruction.");
        // Identical and X/Y mirrored poses should share payload, preserving each pose's anchor.
        auto mirror = source;
        auto mb = std::make_shared<AssetBank>(*bank);
        mb->data.images.resize(4);
        mirror.frames.resize(4);
        for (int k = 0; k < 4; k++) {
            auto im = bank->data.images[0];
            im.idx = k;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    im.pix[y * 32 + x] =
                        bank->data.images[0]
                            .pix[((k & 2) ? 31 - y : y) * 32 + ((k & 1) ? 31 - x : x)];
            mb->data.images[k] = im;
            mirror.frames[k] = {k, 0, k - 2, -k, "FLIP" + std::to_string(k)};
        }
        mirror.artwork.assets = mb;
        mirror.sequence = {0, 1, 2, 3, 0};
        auto mirrored = analyze_animation(mirror);
        require(mirrored.error.empty() &&
                    mirrored.alternatives[0].artwork.assets->data.images.size() == 1,
                "Whole-frame XY reuse was missed.");
        // An extra repeated sequence step cannot inflate ROM savings.
        auto repeated = source;
        repeated.sequence.insert(repeated.sequence.end(), 20, 0);
        auto r = analyze_animation(repeated);
        require(r.baseline_bits == a.baseline_bits &&
                    r.alternatives[2].video_bits == a.alternatives[2].video_bits,
                "Sequence repetitions counted as stored frames.");
        auto bad = source;
        bad.sequence[0] = 1000;
        require(!analyze_animation(bad).error.empty(), "Invalid sequence accepted.");
        bad = source;
        auto broken = std::make_shared<AssetBank>(*bank);
        broken->data.images[0].pix.pop_back();
        bad.artwork.assets = broken;
        require(!analyze_animation(bad).error.empty(), "Truncated pixels accepted.");
        OptimizeProgress cancel;
        cancel.cancel = true;
        require(analyze_animation(source, &cancel).cancelled, "Cancellation ignored.");
        // Transparent frames and opaque black must remain distinct through every representation.
        auto blank = source;
        auto bb = std::make_shared<AssetBank>(*bank);
        std::fill(bb->data.images[0].pix.begin(), bb->data.images[0].pix.end(), 0);
        bb->data.images[1].pix[0] = 31;
        blank.artwork.assets = bb;
        require(analyze_animation(blank).error.empty(),
                "Blank/opaque-black reconstruction failed.");
        // Odd-width sources and padded proposals must use comparable storage assumptions.
        auto odd = source;
        auto ob = std::make_shared<AssetBank>(*bank);
        ob->data.images.resize(1);
        ob->data.images[0].w = 21;
        ob->data.images[0].h = 8;
        ob->data.images[0].pix.assign(21 * 8, 31);
        odd.artwork.assets = ob;
        odd.frames.resize(1);
        odd.sequence = {0, 0};
        auto oa = analyze_animation(odd);
        require(oa.error.empty() && oa.baseline_bits == oa.alternatives[0].video_bits,
                "Padding manufactured a false compression saving.");
        // Families retain their distinct palette ownership, even with identical index payloads.
        auto families = source;
        auto fb = std::make_shared<AssetBank>(*bank);
        fb->data.palettes.push_back(fb->data.palettes[0]);
        fb->data.palettes[1].rgb555[1] ^= 0x7fff;
        families.frames[1].palette = 1;
        families.artwork.assets = fb;
        auto fa = analyze_animation(families);
        require(fa.error.empty() && fa.alternatives[1].artwork.assets->data.palettes.size() == 2,
                "Distinct animation palettes were conflated.");
        tampered = a.alternatives[1];
        auto transparent = std::make_shared<AssetBank>(*tampered.artwork.assets);
        bool cleared_black = false;
        for (const auto &piece : tampered.frames[0]) {
            auto &image = transparent->data.images[piece.image];
            const auto &colors = transparent->data.palettes[piece.palette];
            for (auto &pixel : image.pix)
                if (pixel && !colors.rgb555[pixel]) {
                    pixel = 0;
                    cleared_black = true;
                    break;
                }
            if (cleared_black)
                break;
        }
        tampered.artwork.assets = transparent;
        require(cleared_black && !verify_animation_alternative(source, tampered, error),
                "Opaque black was treated as transparency.");
        // Two related poses still benefit when an unrelated third pose destroys the
        // all-frame intersection. Charge the entire bank, not just a tempting local pair.
        auto subset = source;
        auto sb = std::make_shared<AssetBank>(*bank);
        std::fill(sb->data.images[2].pix.begin(), sb->data.images[2].pix.end(), 17);
        subset.artwork.assets = sb;
        auto sa = analyze_animation(subset);
        require(sa.error.empty() && sa.alternatives.size() == 5, sa.error);
        const auto &selected = sa.alternatives[3];
        require(selected.verified && selected.shared_groups.size() == 1 &&
                    selected.shared_groups[0] == std::vector<size_t>({0, 1}) &&
                    selected.frames[2].size() == 1 &&
                    selected.video_bits < sa.alternatives[1].video_bits &&
                    selected.video_bits + selected.recipe_bytes * 8 <
                        sa.alternatives[1].video_bits + sa.alternatives[1].recipe_bytes * 8,
                "Selective pair missed, unrelated pose split, or overhead ate savings.");
        require(a.alternatives[3].shared_groups.size() == 1 &&
                    a.alternatives[3].shared_groups[0].size() == 3,
                "A profitable pair did not grow into a three-frame group.");
        require(mirrored.alternatives[3].video_bits <= mirrored.alternatives[1].video_bits &&
                    mirrored.alternatives[3].recipe_bytes <= mirrored.alternatives[1].recipe_bytes,
                "Selective sharing damaged existing whole-frame reuse.");
        for (const auto &g : fa.alternatives[3].shared_groups)
            for (auto frame : g)
                require(families.frames[frame].palette == families.frames[g[0]].palette,
                        "Selective sharing crossed palette families.");
        // Exhausting the bounded search must still return a verified conservative result.
        auto capped = odd;
        capped.frames.clear();
        for (int i = 0; i < 33; i++)
            capped.frames.push_back({0, 0, 0, 0, "LIMIT" + std::to_string(i)});
        capped.sequence = {0, 32};
        OptimizeProgress progress;
        auto ca = analyze_animation(capped, &progress);
        require(ca.error.empty() && ca.alternatives[3].verified &&
                    ca.alternatives[3].search_limited &&
                    ca.alternatives[3].search_candidates == 128 &&
                    ca.alternatives[3].video_bits == ca.alternatives[1].video_bits &&
                    ca.alternatives[3].shared_groups.empty() && progress.done == progress.total,
                "Search cap failed or created unprofitable splits.");
        auto deterministic = analyze_animation(subset);
        require(animation_analysis_report(sa) == animation_analysis_report(deterministic),
                "Selective grouping is not deterministic.");
        // A shared middle band needs two simultaneous cuts. Different top/bottom poses
        // must stay exact, including transparent holes and signed actor-space boundaries.
        auto strips = source;
        auto strip_bank = std::make_shared<AssetBank>(*bank);
        strip_bank->data.images.resize(2);
        for (int f = 0; f < 2; f++) {
            auto &im = strip_bank->data.images[f];
            im.w = im.h = 64;
            im.pix.assign(4096, 0);
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++) {
                    int pattern = (x * 13 + y * 17) % 3;
                    im.pix[y * 64 + x] =
                        y >= 16 && y < 48 ? (pattern ? 31 : 1) : (pattern == f ? 17 : 1);
                }
            im.pix[(4 + f) * 64 + 3] = 0;
        }
        strips.artwork.assets = strip_bank;
        strips.frames.resize(2);
        strips.sequence = {0, 1, 0};
        strips.frames[0].anchor_y = strips.frames[1].anchor_y = 20;
        auto strip_analysis = analyze_animation(strips);
        require(strip_analysis.error.empty(), strip_analysis.error);
        const auto &bands = strip_analysis.alternatives[4];
        require(bands.verified && bands.horizontal_bands && bands.max_pieces == 3 &&
                    bands.band_cuts[0] == std::vector<int>({-4, 28}) &&
                    bands.band_cuts[1] == bands.band_cuts[0] &&
                    bands.video_bits < strip_analysis.alternatives[1].video_bits &&
                    bands.video_bits + bands.recipe_bytes * 8 <
                        strip_analysis.alternatives[1].video_bits +
                            strip_analysis.alternatives[1].recipe_bytes * 8,
                "Middle-band sharing did not preserve cuts or save after overhead.");
        require(verify_animation_alternative(strips, bands, error), error);
        auto offset_strips = strips;
        offset_strips.frames[1].anchor_x += 100;
        auto offset_analysis = analyze_animation(offset_strips);
        require(offset_analysis.error.empty() &&
                    offset_analysis.alternatives[4].search_candidates == 0,
                "Row matching ignored actor-relative X positions.");
        auto family_strips = strips;
        auto family_bank = std::make_shared<AssetBank>(*strip_bank);
        family_bank->data.palettes.push_back(family_bank->data.palettes[0]);
        family_bank->data.palettes[1].rgb555[31] = 31;
        family_strips.artwork.assets = family_bank;
        family_strips.frames[1].palette = 1;
        auto family_analysis = analyze_animation(family_strips);
        require(family_analysis.error.empty() &&
                    family_analysis.alternatives[4].search_candidates == 0,
                "Band search crossed source palette ownership.");
        require(mirrored.alternatives[4].video_bits <= mirrored.alternatives[1].video_bits &&
                    mirrored.alternatives[4].recipe_bytes <= mirrored.alternatives[1].recipe_bytes,
                "Band search damaged whole-frame reuse.");
        if (argc >= 3) {
            Document doc;
            require(doc.load(argv[1], error), error);
            auto preview = load_animation_preview(doc, argv[2]);
            require(preview.ready(), preview.notice);
            auto real = analyze_animation(preview);
            require(real.error.empty() && real.alternatives.size() == 5, real.error);
            std::cout << animation_analysis_report(real);
        }
        std::cout << "Animation reuse, family palettes, anchored reconstruction, transparency, "
                     "invalid input and cancellation passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
