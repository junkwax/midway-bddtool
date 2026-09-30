#include "Core/studio_animation_optimizer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
using Position = std::pair<int, int>;
using Pixels = std::map<Position, uint16_t>;
using Groups = std::vector<std::vector<size_t>>;
using Cuts = std::vector<std::vector<int>>;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
void check(OptimizeProgress *progress) {
    if (progress && progress->cancel)
        throw std::runtime_error("Analysis cancelled.");
}
std::string key(const BddCoreImage &im) {
    return std::to_string(im.w) + ":" + std::to_string(im.h) + ":" +
           std::string(im.pix.begin(), im.pix.end());
}
void validate(const AnimationPreview &s) {
    require(s.ready() && s.artwork.assets && !s.frames.empty() && s.frames.size() <= 128,
            "Load a supported animation before analyzing it (maximum 128 frames).");
    require(s.frame_ticks > 0 && s.sequence.size() <= 4096 && std::isfinite(s.scroll),
            "Invalid animation timing or projection.");
    for (auto anchor : s.anchors)
        require(std::isfinite(anchor.x) && std::isfinite(anchor.y), "Invalid actor anchor.");
    size_t total = 0;
    const auto &bank = s.artwork.assets->data;
    for (const auto &f : s.frames) {
        require(f.image >= 0 && f.image < (int)bank.images.size() && f.palette >= 0 &&
                    f.palette < (int)bank.palettes.size(),
                "Missing animation image or palette.");
        const auto &im = bank.images[f.image];
        const auto &pal = bank.palettes[f.palette];
        require(im.w > 0 && im.w <= 1024 && im.h > 0 && im.h <= 1024 &&
                    im.pix.size() == (size_t)im.w * im.h && pal.count > 0 && pal.count <= 256,
                "Invalid animation geometry or palette.");
        require(f.anchor_x >= -32768 && f.anchor_x <= 32767 && f.anchor_y >= -32768 &&
                    f.anchor_y <= 32767,
                "Invalid frame anchor.");
        total += im.pix.size();
        require(total <= 2097152, "Animation exceeds the two-million-pixel analysis limit.");
        for (auto p : im.pix)
            require(p < pal.count, "Animation index exceeds its palette.");
    }
    for (int frame : s.sequence)
        require(frame >= 0 && frame < (int)s.frames.size(), "Invalid animation sequence step.");
}
Pixels indexed_frame(const AnimationPreview &s, size_t frame,
                     const std::vector<std::array<uint8_t, 256>> &maps) {
    Pixels out;
    const auto &f = s.frames[frame];
    const auto &im = s.artwork.assets->data.images[f.image];
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            auto p = im.pix[(size_t)y * im.w + x];
            if (p)
                out[{x - f.anchor_x, y - f.anchor_y}] = maps[f.palette][p];
        }
    return out;
}
struct Builder {
    AnimationAlternative result;
    std::shared_ptr<AssetBank> bank = std::make_shared<AssetBank>();
    std::map<std::string, int> images;
    void add(size_t frame, const Pixels &pixels, int palette, bool base) {
        if (pixels.empty())
            return;
        int x0 = pixels.begin()->first.first, x1 = x0, y0 = pixels.begin()->first.second, y1 = y0;
        for (const auto &p : pixels) {
            x0 = std::min(x0, p.first.first);
            x1 = std::max(x1, p.first.first);
            y0 = std::min(y0, p.first.second);
            y1 = std::max(y1, p.first.second);
        }
        BddCoreImage im{};
        im.w = (x1 - x0 + 4) & ~3;
        im.h = y1 - y0 + 1;
        im.pix.resize((size_t)im.w * im.h);
        for (const auto &p : pixels)
            im.pix[(size_t)(p.first.second - y0) * im.w + p.first.first - x0] = (uint8_t)p.second;
        // Canonicalize all four orientations. The recipe restores the original orientation,
        // including any transparent padding introduced for the storage-model estimate.
        BddCoreImage canonical = im;
        auto best = key(im);
        auto best_bits = optimization_image_bits(im);
        int orientation = 0;
        for (int flip = 1; flip < 4; flip++) {
            auto candidate = im;
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++)
                    candidate.pix[(size_t)y * im.w + x] =
                        im.pix[(size_t)((flip & 2) ? im.h - 1 - y : y) * im.w +
                               ((flip & 1) ? im.w - 1 - x : x)];
            auto k = key(candidate);
            auto candidate_bits = optimization_image_bits(candidate);
            if (candidate_bits < best_bits || (candidate_bits == best_bits && k < best)) {
                best_bits = candidate_bits;
                best = k;
                canonical = std::move(candidate);
                orientation = flip;
            }
        }
        int slot = (int)bank->data.images.size();
        auto found = images.find(best);
        if (found != images.end()) {
            slot = found->second;
            result.reused_pieces++;
        } else {
            canonical.idx = slot;
            result.video_bits += optimization_image_bits(canonical);
            bank->data.images.push_back(std::move(canonical));
            images.emplace(std::move(best), slot);
        }
        if (orientation)
            result.flipped_pieces++;
        result.frames[frame].push_back(
            {slot, palette, x0, y0, bool(orientation & 1), bool(orientation & 2), base});
    }
};
AnimationAlternative build(const AnimationPreview &s, bool compact, bool shared,
                           OptimizeProgress *progress, const Groups *selected_groups = nullptr,
                           const Cuts *cuts = nullptr) {
    Builder b;
    b.result.name = shared    ? "Shared stationary base + frame details"
                    : compact ? "Family palette + frame reuse"
                              : "Trim + exact / mirrored frame reuse";
    b.result.palette_remapped = compact;
    b.result.frames.resize(s.frames.size());
    const auto &palettes = s.artwork.assets->data.palettes;
    std::vector<std::array<uint8_t, 256>> maps(palettes.size());
    std::vector<int> slots(palettes.size(), -1);
    std::map<int, std::vector<size_t>> families;
    for (size_t i = 0; i < s.frames.size(); i++)
        families[s.frames[i].palette].push_back(i);
    for (const auto &family : families) {
        check(progress);
        int pi = family.first;
        auto pal = palettes[pi];
        for (int j = 0; j < 256; j++)
            maps[pi][j] = (uint8_t)j;
        if (compact) {
            std::set<int> used;
            for (auto fi : family.second)
                for (auto p : s.artwork.assets->data.images[s.frames[fi].image].pix)
                    if (p)
                        used.insert(p);
            auto old = pal;
            pal = {};
            pal.count = 1;
            pal.rgb555[0] = old.rgb555[0];
            pal.argb[0] = old.argb[0];
            // Keep distinct authored indices distinct, even when RGB values coincide.
            // This is one mapping for the whole source-palette family, never per frame.
            for (auto p : used) {
                maps[pi][p] = (uint8_t)pal.count;
                pal.rgb555[pal.count] = old.rgb555[p];
                pal.argb[pal.count] = old.argb[p];
                pal.count++;
            }
        }
        slots[pi] = (int)b.bank->data.palettes.size();
        b.result.palette_bytes += pal.count * 2;
        b.bank->data.palettes.push_back(pal);
    }
    std::vector<Pixels> frames;
    for (size_t i = 0; i < s.frames.size(); i++) {
        check(progress);
        frames.push_back(indexed_frame(s, i, maps));
    }
    Groups groups;
    if (selected_groups)
        groups = *selected_groups;
    else
        for (const auto &family : families)
            groups.push_back(family.second);
    for (const auto &group : groups) {
        Pixels common;
        int palette = slots[s.frames[group.front()].palette];
        if (shared && group.size() > 1) {
            common = frames[group.front()];
            for (auto i : group) {
                check(progress);
                for (auto it = common.begin(); it != common.end();) {
                    auto p = frames[i].find(it->first);
                    if (p == frames[i].end() || p->second != it->second)
                        it = common.erase(it);
                    else
                        ++it;
                }
            }
        }
        if (!common.empty())
            b.result.shared_groups.push_back(group);
        for (auto i : group) {
            check(progress);
            auto detail = frames[i];
            for (const auto &p : common)
                detail.erase(p.first);
            b.add(i, common, palette, true);
            if (cuts) {
                std::vector<Pixels> bands((*cuts)[i].size() + 1);
                for (const auto &pixel : detail) {
                    size_t band =
                        std::upper_bound((*cuts)[i].begin(), (*cuts)[i].end(), pixel.first.second) -
                        (*cuts)[i].begin();
                    bands[band].insert(pixel);
                }
                for (const auto &band : bands)
                    b.add(i, band, palette, false);
            } else
                b.add(i, detail, palette, false);
            b.result.max_pieces = std::max(b.result.max_pieces, (int)b.result.frames[i].size());
            b.result.recipe_bytes += 4 + 14 * b.result.frames[i].size();
            if (progress && !selected_groups && !cuts)
                progress->done++;
        }
    }
    b.result.artwork.assets = b.bank;
    return std::move(b.result);
}
AnimationAlternative selective_sharing(const AnimationPreview &source,
                                       const AnimationAlternative &whole,
                                       OptimizeProgress *progress) {
    auto result = whole;
    Groups groups;
    uint64_t pixels = 0;
    for (size_t i = 0; i < source.frames.size(); i++) {
        groups.push_back({i});
        pixels += source.artwork.assets->data.images[source.frames[i].image].pix.size();
    }
    // Greedy merges are evaluated against the whole animation's deduplicated payload budget.
    // Never add a split unless both video bytes and video + assumed recipe bytes decrease.
    // Bounded work is reported, not presented as an exhaustive global optimum.
    int candidates = 0;
    bool limited = source.frames.size() > 32;
    uint64_t work = 0;
    bool exhausted = false;
    while (!exhausted) {
        auto best = result;
        auto best_groups = groups;
        bool improved = false;
        for (size_t i = 0; i < groups.size() && !exhausted; i++) {
            for (size_t j = i + 1; j < groups.size(); j++) {
                check(progress);
                if (groups[i].size() + groups[j].size() > 8 ||
                    *std::max_element(groups[i].begin(), groups[i].end()) >= 32 ||
                    *std::max_element(groups[j].begin(), groups[j].end()) >= 32 ||
                    source.frames[groups[i].front()].palette !=
                        source.frames[groups[j].front()].palette)
                    continue;
                if (candidates >= 128 || work + pixels > 32000000) {
                    limited = exhausted = true;
                    break;
                }
                auto trial_groups = groups;
                trial_groups[i].insert(trial_groups[i].end(), groups[j].begin(), groups[j].end());
                std::sort(trial_groups[i].begin(), trial_groups[i].end());
                trial_groups.erase(trial_groups.begin() + j);
                auto trial = build(source, true, true, progress, &trial_groups);
                candidates++;
                work += pixels;
                if (progress)
                    progress->done++;
                auto score = [](const AnimationAlternative &a) {
                    return a.video_bits + a.recipe_bytes * 8;
                };
                if (trial.video_bits < best.video_bits && score(trial) < score(best)) {
                    best = std::move(trial);
                    best_groups = std::move(trial_groups);
                    improved = true;
                }
            }
        }
        if (!improved)
            break;
        result = std::move(best);
        groups = std::move(best_groups);
    }
    result.name = "Selective frame groups + details";
    result.search_candidates = candidates;
    result.search_limited = limited;
    result.verified = false;
    return result;
}
AnimationAlternative horizontal_sharing(const AnimationPreview &source,
                                        const AnimationAlternative &whole,
                                        OptimizeProgress *progress) {
    auto result = whole;
    const size_t count = std::min<size_t>(32, source.frames.size());
    // Intern exact trimmed rows in actor space. Tokens include X placement and authored
    // indices; comparisons below additionally require the same source palette.
    std::map<std::string, int> tokens;
    std::vector<std::map<int, int>> rows(count);
    uint64_t pixels = 0;
    for (size_t i = 0; i < source.frames.size(); i++) {
        const auto &f = source.frames[i];
        const auto &im = source.artwork.assets->data.images[f.image];
        pixels += im.pix.size();
        if (i >= count)
            continue;
        for (int y = 0; y < im.h; y++) {
            check(progress);
            int left = 0, right = im.w;
            while (left < right && !im.pix[(size_t)y * im.w + left])
                left++;
            while (right > left && !im.pix[(size_t)y * im.w + right - 1])
                right--;
            if (left == right)
                continue;
            std::string row = std::to_string(left - f.anchor_x) + ":" +
                              std::string(im.pix.begin() + (size_t)y * im.w + left,
                                          im.pix.begin() + (size_t)y * im.w + right);
            auto token = tokens.emplace(std::move(row), (int)tokens.size());
            rows[i][y - f.anchor_y] = token.first->second;
        }
    }
    struct Band {
        size_t a, b;
        int begin, end;
    };
    std::vector<Band> proposals;
    for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++) {
            check(progress);
            if (source.frames[i].palette != source.frames[j].palette)
                continue;
            int first = 0, last = 0, length = 0;
            auto flush = [&]() {
                if (length >= 8)
                    proposals.push_back({i, j, first, last + 1});
                length = 0;
            };
            for (const auto &row : rows[i]) {
                auto match = rows[j].find(row.first);
                if (match == rows[j].end() || row.second != match->second) {
                    flush();
                    continue;
                }
                if (length && row.first != last + 1)
                    flush();
                if (!length)
                    first = row.first;
                last = row.first;
                length++;
            }
            flush();
        }
    std::stable_sort(proposals.begin(), proposals.end(), [](const Band &a, const Band &b) {
        return a.end - a.begin > b.end - b.begin;
    });
    bool limited = source.frames.size() > 32 || proposals.size() > 512;
    if (proposals.size() > 512)
        proposals.resize(512);
    Cuts cuts(source.frames.size());
    int candidates = 0;
    uint64_t work = 0;
    bool exhausted = false;
    while (!exhausted) {
        auto best = result;
        auto best_cuts = cuts;
        bool improved = false;
        for (const auto &band : proposals) {
            check(progress);
            auto trial_cuts = cuts;
            for (auto frame : {band.a, band.b}) {
                const auto &f = source.frames[frame];
                const auto &im = source.artwork.assets->data.images[f.image];
                auto &c = trial_cuts[frame];
                for (int y : {band.begin, band.end})
                    if (y > -f.anchor_y && y < im.h - f.anchor_y)
                        c.push_back(y);
                std::sort(c.begin(), c.end());
                c.erase(std::unique(c.begin(), c.end()), c.end());
            }
            if (trial_cuts == cuts || trial_cuts[band.a].size() > 7 ||
                trial_cuts[band.b].size() > 7)
                continue;
            if (candidates >= 128 || work + pixels > 32000000) {
                limited = exhausted = true;
                break;
            }
            auto trial = build(source, true, false, progress, nullptr, &trial_cuts);
            candidates++;
            work += pixels;
            if (progress)
                progress->done++;
            if (trial.video_bits < best.video_bits && trial.video_bits + trial.recipe_bytes * 8 <
                                                          best.video_bits + best.recipe_bytes * 8) {
                best = std::move(trial);
                best_cuts = std::move(trial_cuts);
                improved = true;
            }
        }
        if (!improved)
            break;
        result = std::move(best);
        cuts = std::move(best_cuts);
    }
    // Mark actual cross-frame shared pieces for inspection, including mirrored payload reuse.
    std::map<std::pair<int, int>, std::set<size_t>> uses;
    for (size_t i = 0; i < result.frames.size(); i++)
        for (const auto &p : result.frames[i])
            uses[{p.image, p.palette}].insert(i);
    std::set<std::vector<size_t>> groups;
    for (auto &frame : result.frames)
        for (auto &p : frame) {
            const auto &u = uses.at({p.image, p.palette});
            p.shared_base = u.size() > 1;
            if (p.shared_base)
                groups.insert(std::vector<size_t>(u.begin(), u.end()));
        }
    result.shared_groups.assign(groups.begin(), groups.end());
    result.name = "Shared horizontal bands";
    result.horizontal_bands = true;
    result.band_cuts = std::move(cuts);
    result.search_candidates = candidates;
    result.search_limited = limited;
    result.verified = false;
    return result;
}
} // namespace
bool verify_animation_alternative(const AnimationPreview &s, const AnimationAlternative &a,
                                  std::string &error) {
    error.clear();
    try {
        validate(s);
        require(a.artwork.assets && a.frames.size() == s.frames.size(), "Missing frame recipes.");
        const auto &bank = a.artwork.assets->data;
        for (size_t i = 0; i < s.frames.size(); i++) {
            const auto &f = s.frames[i];
            const auto &im = s.artwork.assets->data.images[f.image];
            const auto &pal = s.artwork.assets->data.palettes[f.palette];
            std::map<Position, uint32_t> expected, actual;
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++) {
                    auto p = im.pix[(size_t)y * im.w + x];
                    if (p)
                        expected[{x - f.anchor_x, y - f.anchor_y}] =
                            0x10000 | (pal.rgb555[p] & 0x7fff);
                }
            require(a.frames[i].size() <= 16, "Too many frame pieces.");
            for (const auto &piece : a.frames[i]) {
                require(piece.image >= 0 && piece.image < (int)bank.images.size() &&
                            piece.palette >= 0 && piece.palette < (int)bank.palettes.size(),
                        "Invalid piece image or palette.");
                const auto &part = bank.images[piece.image];
                const auto &colors = bank.palettes[piece.palette];
                require(part.w > 0 && part.w <= 1024 && part.h > 0 && part.h <= 1024 &&
                            part.pix.size() == (size_t)part.w * part.h && colors.count > 0 &&
                            colors.count <= 256 && piece.x >= -65536 && piece.x <= 65536 &&
                            piece.y >= -65536 && piece.y <= 65536,
                        "Invalid piece geometry.");
                for (int y = 0; y < part.h; y++)
                    for (int x = 0; x < part.w; x++) {
                        int sx = piece.flip_x ? part.w - 1 - x : x;
                        int sy = piece.flip_y ? part.h - 1 - y : y;
                        auto p = part.pix[(size_t)sy * part.w + sx];
                        require(p < colors.count, "Invalid piece color index.");
                        if (p) {
                            Position at{x + piece.x, y + piece.y};
                            require(!actual.count(at), "Frame pieces overlap opaque pixels.");
                            actual[at] = 0x10000 | (colors.rgb555[p] & 0x7fff);
                        }
                    }
            }
            require(expected == actual,
                    "Reconstructed frame differs at its animation anchor: " + f.label);
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
AnimationAnalysis analyze_animation(const AnimationPreview &s, OptimizeProgress *progress) {
    AnimationAnalysis out;
    out.source = s;
    try {
        check(progress);
        validate(s);
        if (progress) {
            progress->done = 0;
            progress->total = (int)s.frames.size() * 3 + 256;
        }
        std::set<std::string> payloads;
        std::set<int> palettes;
        for (const auto &f : s.frames) {
            const auto &im = s.artwork.assets->data.images[f.image];
            if (payloads.insert(key(im)).second) {
                // Apply the same four-pixel padding to baseline and proposals. Otherwise the
                // background model treats an odd-width source as raw and its padded proposal
                // as compressed, manufacturing a saving from incompatible assumptions.
                auto padded = im;
                padded.w = (im.w + 3) & ~3;
                padded.pix.assign((size_t)padded.w * padded.h, 0);
                for (int y = 0; y < im.h; y++)
                    std::copy_n(im.pix.begin() + (size_t)y * im.w, im.w,
                                padded.pix.begin() + (size_t)y * padded.w);
                out.baseline_bits += optimization_image_bits(padded);
            }
            if (palettes.insert(f.palette).second)
                out.baseline_palette_bytes += s.artwork.assets->data.palettes[f.palette].count * 2;
        }
        for (int mode = 0; mode < 3; mode++) {
            check(progress);
            auto a = build(s, mode > 0, mode == 2, progress);
            check(progress);
            require(verify_animation_alternative(s, a, out.error), out.error);
            a.verified = true;
            out.alternatives.push_back(std::move(a));
        }
        auto selective = selective_sharing(s, out.alternatives[1], progress);
        check(progress);
        require(verify_animation_alternative(s, selective, out.error), out.error);
        selective.verified = true;
        out.alternatives.push_back(std::move(selective));
        auto bands = horizontal_sharing(s, out.alternatives[1], progress);
        check(progress);
        require(verify_animation_alternative(s, bands, out.error), out.error);
        bands.verified = true;
        out.alternatives.push_back(std::move(bands));
        if (progress)
            progress->total = progress->done.load();
    } catch (const std::exception &e) {
        out.error = e.what();
        out.cancelled = progress && progress->cancel;
        out.alternatives.clear();
    }
    return out;
}
std::string animation_analysis_report(const AnimationAnalysis &a) {
    std::ostringstream out;
    out << "bddtool animation analysis\nSource: " << a.source.source << '\n'
        << a.source.frames.size() << " unique frame records, " << a.source.sequence.size()
        << " sequence steps, " << a.source.frame_ticks << " ticks per step, "
        << a.source.anchors.size() << " actors\n";
    if (a.source.manual_sequence)
        out << "Manual IMG comparison: selection order and chosen preview duration; "
               "runtime sequence, timing, actor placement and consumers are unverified.\n";
    if (!a.error.empty())
        out << "Analysis: " << a.error << '\n';
    out << "Baseline model: " << a.baseline_bits / 8 << " video bytes; " << a.baseline_palette_bytes
        << " palette bytes.\n";
    for (const auto &p : a.alternatives) {
        out << "\n"
            << p.name << ": " << p.video_bits / 8 << " video bytes ("
            << ((int64_t)a.baseline_bits - (int64_t)p.video_bits) / 8 << " saved), "
            << p.palette_bytes << " palette bytes; assumed recipe bytes " << p.recipe_bytes
            << "; max " << p.max_pieces << " pieces/actor; " << p.reused_pieces
            << " reused references; " << (p.verified ? "pixel verified" : "unverified") << "\n";
        if (p.search_candidates || p.search_limited || p.horizontal_bands)
            out << "  " << (p.horizontal_bands ? "Horizontal band" : "Selective")
                << " search: " << p.search_candidates << " candidates; "
                << (p.search_limited ? "work limit reached" : "greedy search completed")
                << (p.horizontal_bands
                        ? "; min 8 matching rows, max 8 pieces/frame, 512 proposed bands"
                        : "; max 8 frames/group")
                << "; first 32 frames, 128 candidate/32M input-pixel work caps.\n";
        for (const auto &group : p.shared_groups) {
            out << "  Shared artwork group:";
            for (auto frame : group)
                out << ' ' << a.source.frames[frame].label;
            out << '\n';
        }
        for (size_t i = 0; i < p.frames.size(); i++) {
            out << "  " << a.source.frames[i].label << ':';
            for (const auto &r : p.frames[i])
                out << " [image " << r.image << ", palette " << r.palette << ", at " << r.x << ','
                    << r.y << (r.flip_x ? ", flip X" : "") << (r.flip_y ? ", flip Y" : "")
                    << (r.shared_base ? ", shared" : ", detail") << ']';
            if (p.horizontal_bands && i < p.band_cuts.size() && !p.band_cuts[i].empty()) {
                out << " Y cuts:";
                for (int y : p.band_cuts[i])
                    out << ' ' << y;
            }
            out << '\n';
        }
    }
    out << "\nAlternatives are separate, not additive. Estimates use the background auto-BPP, "
           "zero-compression, 16-bit-alignment model, with four-pixel width padding applied to "
           "both "
           "baseline and proposals; not actual animation LOD directives or ROM "
           "receipts.\n"
           "Recipe overhead assumes 14 bytes/piece + 4 bytes/frame; runtime code/object RAM is "
           "unmeasured.\n"
           "The loaded comparison sequence, ticks, anchors and scroll remain unchanged. Random "
           "idle "
           "pauses are omitted. Repeated steps and actor instances are not duplicate ROM "
           "payloads.\n"
           "Verification covers decoded RGB555 and transparency for the loaded palettes only. "
           "Palette cycling, alternate palettes, shared consumers and compiled records need "
           "review.\n"
           "Read-only: no IMG, LOD, ASM or BDB writes. Multipart/flip/palette proposals need a "
           "reviewed "
           "runtime adapter, packed-art comparison and in-game validation before export.\n";
    return out.str();
}
} // namespace studio
