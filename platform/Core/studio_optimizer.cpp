#include "Core/studio_optimizer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstdio>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace studio {
namespace {
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Box {
    int x, y, w, h;
};
struct Tile {
    Box box{};
    BddCoreImage image;
    std::vector<int> original_indices;
    std::string key;
    bool fx = false, fy = false;
    int role = 0;
};
std::string key(const BddCoreImage &im) {
    return std::to_string(im.w) + ":" + std::to_string(im.h) + ":" +
           std::string((const char *)im.pix.data(), im.pix.size());
}
// Storage model for aligned background tiles, auto BPP, zero compression enabled,
// and 16-bit alignment. This is an estimate, not a LOAD2 packing receipt.
uint64_t bits(const BddCoreImage &im, bool compressed = true) {
    int bpp = bdd_core_load2_bpp_for_max_pixel(bdd_core_image_max_pixel(im.pix.data(), im.w, im.h));
    uint64_t raw = (uint64_t)im.w * im.h * bpp;
    if (!compressed || im.w <= 10 || im.w % 4)
        return (raw + 15) & ~15ULL;
    std::vector<std::pair<int, int>> margins;
    std::array<int64_t, 4> lead{}, trail{};
    for (int y = 0; y < im.h; y++) {
        int l = 0, r = 0;
        bool leading = true;
        for (int x = 0; x < im.w; x++) {
            int p = im.pix[(size_t)y * im.w + x];
            if (leading) {
                if (l < 120) {
                    if (!p)
                        l++;
                    else
                        leading = false;
                } else
                    leading = false;
            } else if (x > im.w - 120) {
                if (!p)
                    r++;
                else
                    r = 0;
            }
        }
        margins.emplace_back(l, r);
        for (int k = 0; k < 4; k++) {
            int f = 1 << k;
            lead[k] += l - std::min(15, l / f) * f;
            trail[k] += r - std::min(15, r / f) * f;
        }
    }
    int lf = 1 << (std::min_element(lead.begin(), lead.end()) - lead.begin());
    int tf = 1 << (std::min_element(trail.begin(), trail.end()) - trail.begin());
    uint64_t encoded = (uint64_t)im.h * 8;
    for (auto m : margins) {
        int l = std::min(15, m.first / lf), r = std::min(15, m.second / tf);
        int x0 = l * lf, x1 = im.w - 1 - r * tf;
        if (x1 - x0 + 1 < 10) {
            int missing = 10 - (x1 - x0 + 1);
            if (missing > x0)
                missing -= x0;
            else
                x0 = missing;
            l = (l * lf - x0) / lf;
            x0 = l * lf;
            if (x1 - x0 + 1 < 10)
                r = (r * tf - missing) / tf;
        }
        int span = im.w - l * lf - r * tf;
        if (l < 0 || r < 0 || span < 0 || span > im.w)
            return (raw + 15) & ~15ULL;
        encoded += (uint64_t)span * bpp;
    }
    return (std::min(raw, encoded) + 15) & ~15ULL;
}
Tile tile(const BddCoreImage &source, Box box, bool compact) {
    // Crop only empty margins; preserve background width alignment.
    int xmin = box.w, xmax = -1, ymin = box.h, ymax = -1;
    for (int y = 0; y < box.h; y++)
        for (int x = 0; x < box.w; x++)
            if (source.pix[(size_t)(box.y + y) * source.w + box.x + x]) {
                xmin = std::min(xmin, x);
                xmax = std::max(xmax, x);
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
            }
    Tile best;
    if (xmax < 0)
        return best;
    int left = xmin / 4 * 4, right = std::min(box.w, (xmax + 4) / 4 * 4);
    box = {box.x + left, box.y + ymin, right - left, ymax - ymin + 1};
    for (int flip = 0; flip < 4; flip++) {
        Tile t;
        t.box = box;
        t.fx = (flip & 1) != 0;
        t.fy = (flip & 2) != 0;
        t.image.w = box.w;
        t.image.h = box.h;
        t.image.flags = source.flags;
        t.original_indices = {0};
        std::array<int, 256> map{};
        int next = 1;
        if (!compact) {
            t.original_indices.resize(256);
            std::iota(t.original_indices.begin(), t.original_indices.end(), 0);
        }
        for (int y = 0; y < box.h; y++)
            for (int x = 0; x < box.w; x++) {
                auto p = source.pix[(size_t)(box.y + (t.fy ? box.h - 1 - y : y)) * source.w +
                                    box.x + (t.fx ? box.w - 1 - x : x)];
                if (compact && p) {
                    if (!map[p]) {
                        map[p] = next++;
                        t.original_indices.push_back(p);
                    }
                    p = (uint8_t)map[p];
                }
                t.image.pix.push_back(p);
            }
        t.key = key(t.image);
        if (flip == 0 || t.key < best.key)
            best = std::move(t);
    }
    return best;
}
std::string palette_key(const BddCorePalette &p) {
    return std::string((const char *)p.rgb555, p.count * sizeof(uint16_t));
}
BddCorePalette remap(const BddCorePalette &p, const Tile &t, bool compact) {
    if (!compact)
        return p;
    BddCorePalette out{};
    out.count = (int)t.original_indices.size();
    for (int i = 0; i < out.count; i++) {
        require(t.original_indices[i] < p.count, "Artwork uses a missing palette entry.");
        out.rgb555[i] = p.rgb555[t.original_indices[i]];
        out.argb[i] = i ? bdd_core_rgb555_to_argb(out.rgb555[i]) : 0;
    }
    return out;
}
struct Candidate {
    std::vector<Tile> tiles;
    int64_t score = INT64_MAX;
    uint64_t new_bits = 0;
};
struct Search {
    const BddCoreImage &image;
    const std::vector<int> &palettes;
    const AssetBank &bank;
    const std::map<std::string, int> &shared;
    const OptimizeOptions &options;
    OptimizeProgress *progress;
    int uses, added_objects;
    void checkpoint() const {
        if (progress && progress->cancel)
            throw std::runtime_error("Scan cancelled.");
    }
    Candidate evaluate(const std::vector<Box> &boxes) const {
        checkpoint();
        Candidate c;
        std::set<std::string> unique, new_palettes;
        for (const auto &p : bank.data.palettes)
            new_palettes.insert(palette_key(p));
        int initial_pals = (int)new_palettes.size();
        for (auto b : boxes) {
            auto t = tile(image, b, options.compact_palettes);
            if (t.image.pix.empty())
                continue;
            if (t.image.w > 248 || t.image.w % 4 || (uint64_t)t.image.w * t.image.h > 65500)
                return c;
            if (!shared.count(t.key) && unique.insert(t.key).second)
                c.new_bits += bits(t.image);
            for (int pi : palettes)
                new_palettes.insert(
                    palette_key(remap(bank.data.palettes[pi], t, options.compact_palettes)));
            c.tiles.push_back(std::move(t));
        }
        int extra = ((int)c.tiles.size() - 1) * uses;
        if (c.tiles.empty() || extra + added_objects > options.max_added_objects ||
            (int)c.tiles.size() > options.max_pieces ||
            (int)bank.data.palettes.size() + (int)new_palettes.size() - initial_pals >
                std::max(options.max_palettes, (int)bank.data.palettes.size()))
            return Candidate{};
        int penalty = options.policy == 0 ? 0 : options.policy == 1 ? 24 : 128;
        c.score = (int64_t)c.new_bits + (int64_t)std::max(0, extra) * penalty * 8 +
                  (int64_t)((int)new_palettes.size() - initial_pals) * 16;
        return c;
    }
    Candidate run() const {
        std::vector<Box> partition{{0, 0, image.w, image.h}};
        Candidate best = evaluate(partition);
        auto consider = [&](const std::vector<Box> &boxes) {
            auto c = evaluate(boxes);
            if (c.score < best.score) {
                best = std::move(c);
                partition = boxes;
            }
        };
        // Include thirds, fifths, etc.; the number of repeated groups need not be a power of two.
        for (int nx = 1; nx <= options.max_pieces; nx++)
            for (int ny = 1; ny <= options.max_pieces / nx; ny++) {
                if (nx * ny > options.max_pieces || image.w / 4 < nx || image.h < ny)
                    continue;
                std::vector<Box> boxes;
                for (int y = 0; y < ny; y++)
                    for (int x = 0; x < nx; x++)
                        boxes.push_back(
                            {4 * (x * (image.w / 4) / nx), y * image.h / ny,
                             4 * (((x + 1) * (image.w / 4) / nx) - (x * (image.w / 4) / nx)),
                             (y + 1) * image.h / ny - y * image.h / ny});
                consider(boxes);
            }
        // Constant-size groups with an independent leftover edge. Equal subdivisions alone
        // cannot expose a 24-pixel motif repeated across a 104-pixel image, for example.
        for (int axis = 0; axis < 2; axis++) {
            int length = axis ? image.h : image.w, unit = axis ? 1 : 4;
            for (int period = unit; period < length; period += unit) {
                if ((length + period - 1) / period > options.max_pieces)
                    continue;
                std::vector<Box> boxes;
                for (int start = 0; start < length; start += period) {
                    int span = std::min(period, length - start);
                    boxes.push_back(axis ? Box{0, start, image.w, span}
                                         : Box{start, 0, span, image.h});
                }
                consider(boxes);
            }
        }
        // Empty corridors can reveal several islands only after more than one cut.
        // Follow them even if an intermediate split does not yet improve the score.
        // Keep both seeds: a regular partition can expose gaps hidden by a solid base.
        auto regular = partition;
        for (auto boxes : {std::vector<Box>{{0, 0, image.w, image.h}}, regular}) {
            while (boxes.size() < (size_t)options.max_pieces) {
                checkpoint();
                int best_area = 0, chosen = -1;
                Box first{}, second{};
                for (size_t i = 0; i < boxes.size(); i++) {
                    const auto b = boxes[i];
                    for (int axis = 0; axis < 2; axis++) {
                        int unit = axis ? 1 : 4, length = axis ? b.h : b.w;
                        std::vector<bool> occupied(length / unit, false);
                        for (int y = 0; y < b.h; y++)
                            for (int x = 0; x < b.w; x++)
                                if (image.pix[(size_t)(b.y + y) * image.w + b.x + x])
                                    occupied[(axis ? y : x) / unit] = true;
                        int lo = 0;
                        while (lo < (int)occupied.size() && !occupied[lo])
                            lo++;
                        for (int a = lo + 1; a < (int)occupied.size(); a++) {
                            if (occupied[a])
                                continue;
                            int end = a;
                            while (end < (int)occupied.size() && !occupied[end])
                                end++;
                            if (end == (int)occupied.size())
                                break; // Outer margins are already cropped by tile().
                            int area = (end - a) * unit * (axis ? b.w : b.h);
                            if (area > best_area) {
                                best_area = area;
                                chosen = (int)i;
                                first = second = b;
                                if (axis) {
                                    first.h = a * unit;
                                    second.y += end * unit;
                                    second.h -= end * unit;
                                } else {
                                    first.w = a * unit;
                                    second.x += end * unit;
                                    second.w -= end * unit;
                                }
                            }
                            a = end;
                        }
                    }
                }
                if (chosen < 0)
                    break;
                boxes[chosen] = first;
                boxes.push_back(second);
                consider(boxes);
            }
        }
        // Greedy, bounded partition search. Deep examines every aligned X and every Y cut.
        for (int round = 1; round < options.max_pieces; round++) {
            checkpoint();
            auto chosen = partition;
            int64_t previous = best.score;
            for (size_t part = 0; part < partition.size(); part++) {
                auto b = partition[part];
                for (int axis = 0; axis < 2; axis++) {
                    int length = axis ? b.h : b.w, unit = axis ? 1 : 4;
                    int step = options.deep ? unit : std::max(unit, ((length / 12) / unit) * unit);
                    for (int cut = unit; cut < length; cut += step) {
                        auto boxes = partition;
                        Box a = b, tail = b;
                        if (axis) {
                            a.h = cut;
                            tail.y += cut;
                            tail.h -= cut;
                        } else {
                            a.w = cut;
                            tail.x += cut;
                            tail.w -= cut;
                        }
                        boxes[part] = a;
                        boxes.push_back(tail);
                        auto c = evaluate(boxes);
                        if (c.score < best.score) {
                            best = std::move(c);
                            chosen = std::move(boxes);
                        }
                    }
                }
            }
            if (best.score >= previous)
                break;
            partition = std::move(chosen);
            if (partition.size() >= (size_t)options.max_pieces)
                break;
        }
        return best;
    }
};
const BddCoreImage &get_image(const State &s, int id) {
    for (const auto &im : s.assets->data.images)
        if (im.idx == id)
            return im;
    throw std::runtime_error("Missing optimizer image reference.");
}
uint32_t pixel(const BddCoreImage &im, const BddCorePalette &pal, int x, int y) {
    auto p = im.pix[(size_t)y * im.w + x];
    require(p < pal.count, "Invalid palette reference during reconstruction.");
    return p ? 0x10000u | pal.rgb555[p] : 0;
}
std::shared_ptr<AssetBank> pattern_bank(const State &state, const PatternOptions &options,
                                        uint64_t &changed, uint64_t &silhouette, int &uses) {
    require(state.assets && state.has_bdb, "Open a paired BDB/BDD stage first.");
    const auto &source = get_image(state, options.image);
    require(source.w > 0 && source.h > 0 && source.w % 4 == 0 &&
                source.pix.size() == (size_t)source.w * source.h,
            "Pattern artwork must have valid, four-pixel-aligned geometry.");
    require(options.mode >= PatternMode::RepeatX && options.mode <= PatternMode::MirrorY,
            "Unknown pattern mode.");
    for (const auto &m : state.assets->metadata)
        if (m.idx == options.image)
            require(!(m.lod_ref || m.anix || m.aniy || m.anix2 || m.aniy2 || m.aniz2 || m.frm ||
                      m.opals || m.pttblnum),
                    "Animation/LOD artwork needs a consumer-specific review.");
    std::set<int> palettes;
    uses = 0;
    for (const auto &p : state.objects)
        if (p.object.ii == options.image) {
            require(p.plane >= 0 && p.plane < (int)state.planes.size() && !p.locked &&
                        !state.planes[p.plane].locked,
                    "Unlock and assign every placement of this image before editing it.");
            require(p.object.fl >= 0 && p.object.fl < (int)state.assets->data.palettes.size(),
                    "Artwork uses a missing palette.");
            palettes.insert(p.object.fl);
            uses++;
        }
    require(uses > 0, "Choose an image placed in this stage.");
    bool vertical = options.mode == PatternMode::RepeatY || options.mode == PatternMode::MirrorY;
    bool repeat = options.mode == PatternMode::RepeatX || options.mode == PatternMode::RepeatY;
    int length = vertical ? source.h : source.w;
    if (repeat) {
        require(options.span > 0 && options.span <= length && options.offset >= 0 &&
                    options.offset <= length - options.span,
                "The source group must fit inside the image.");
        require(vertical || (options.span % 4 == 0 && options.offset % 4 == 0),
                "Horizontal groups and offsets must align to four pixels.");
    }
    auto bank = std::make_shared<AssetBank>(*state.assets);
    auto &edited = *std::find_if(bank->data.images.begin(), bank->data.images.end(),
                                 [&](const auto &im) { return im.idx == source.idx; });
    changed = silhouette = 0;
    for (int y = 0; y < source.h; y++)
        for (int x = 0; x < source.w; x++) {
            int p = vertical ? y : x, from;
            if (repeat) {
                from = p % options.span;
                if (options.alternate_flip && (p / options.span) % 2)
                    from = options.span - 1 - from;
                from += options.offset;
            } else
                from = options.use_far_side ? std::max(p, length - 1 - p)
                                            : std::min(p, length - 1 - p);
            int sx = vertical ? x : from, sy = vertical ? from : y;
            auto before = source.pix[(size_t)y * source.w + x];
            auto after = source.pix[(size_t)sy * source.w + sx];
            edited.pix[(size_t)y * source.w + x] = after;
            silhouette += (before == 0) != (after == 0);
            bool different = false;
            for (int pi : palettes) {
                const auto &pal = state.assets->data.palettes[pi];
                different |= pixel(source, pal, x, y) != pixel(source, pal, sx, sy);
            }
            changed += different;
        }
    return bank;
}
} // namespace

State pattern_source(const State &state, int plane, std::string &error) {
    if (plane < 0)
        return state;
    try {
        require(state.assets && plane < (int)state.planes.size() && !state.planes[plane].locked,
                "Choose an unlocked layer.");
        std::vector<Placement> objects;
        std::set<int> replaced;
        int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN, next_image = 0;
        for (const auto &im : state.assets->data.images)
            next_image = std::max(next_image, im.idx + 1);
        require(next_image < 65535, "No image ID available for a layer pattern.");
        for (const auto &p : state.objects)
            if (p.plane == plane) {
                require(!p.locked && !p.hidden,
                        "Layer patterns require visible, unlocked placements.");
                if (!objects.empty())
                    require(p.object.fl == objects.front().object.fl &&
                                (p.object.wx & ~0x30) == (objects.front().object.wx & ~0x30),
                            "Choose a layer whose artwork uses one palette and draw mode.");
                const auto &im = get_image(state, p.object.ii);
                require(im.w > 0 && im.h > 0 && im.pix.size() == (size_t)im.w * im.h,
                        "Layer artwork has invalid geometry.");
                if (!objects.empty())
                    require(im.flags == get_image(state, objects.front().object.ii).flags,
                            "Layer artwork has different image flags.");
                for (const auto &m : state.assets->metadata)
                    if (m.idx == im.idx)
                        require(!(m.lod_ref || m.anix || m.aniy || m.anix2 || m.aniy2 || m.aniz2 ||
                                  m.frm || m.opals || m.pttblnum),
                                "Animation/LOD artwork cannot be combined into a layer pattern.");
                left = std::min(left, p.object.depth);
                top = std::min(top, p.object.sy);
                right = std::max(right, p.object.depth + im.w);
                bottom = std::max(bottom, p.object.sy + im.h);
                objects.push_back(p);
                replaced.insert(im.idx);
            }
        require(!objects.empty(), "This layer has no artwork.");
        int w = (right - left + 3) / 4 * 4, h = bottom - top;
        require(w > 0 && h > 0 && w <= 4096 && h <= 4096 && (uint64_t)w * h <= 1000000,
                "Layer pattern extent is too large (limit: 4096 per axis, one million pixels).");
        std::stable_sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) {
            return a.object.order < b.object.order;
        });
        BddCoreImage merged;
        merged.idx = next_image;
        merged.w = w;
        merged.h = h;
        merged.flags = get_image(state, objects.front().object.ii).flags;
        merged.pix.resize((size_t)w * h);
        for (const auto &p : objects) {
            const auto &im = get_image(state, p.object.ii);
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++) {
                    auto value = im.pix[(size_t)((p.object.wx & 0x20) ? im.h - 1 - y : y) * im.w +
                                        ((p.object.wx & 0x10) ? im.w - 1 - x : x)];
                    if (value)
                        merged
                            .pix[(size_t)(p.object.sy - top + y) * w + p.object.depth - left + x] =
                            value;
                }
        }
        State out = state;
        auto bank = std::make_shared<AssetBank>(*state.assets);
        out.objects.erase(std::remove_if(out.objects.begin(), out.objects.end(),
                                         [&](const auto &p) { return p.plane == plane; }),
                          out.objects.end());
        for (const auto &p : out.objects)
            replaced.erase(p.object.ii); // Shared with another layer: keep its original payload.
        for (size_t i = bank->data.images.size(); i-- > 0;)
            if (replaced.count(bank->data.images[i].idx)) {
                bank->data.images.erase(bank->data.images.begin() + i);
                bank->default_palettes.erase(bank->default_palettes.begin() + i);
            }
        bank->data.images.push_back(std::move(merged));
        bank->default_palettes.push_back(objects.front().object.fl);
        BddImageMetadata metadata{};
        metadata.idx = next_image;
        std::snprintf(metadata.label, sizeof metadata.label, "PATTERN_%d", next_image);
        bank->metadata.push_back(metadata);
        auto placement = objects.front();
        placement.object.ii = next_image;
        placement.object.wx &= ~0x30;
        placement.object.depth = left;
        placement.object.sy = top;
        out.objects.push_back(placement);
        std::stable_sort(out.objects.begin(), out.objects.end(), [](const auto &a, const auto &b) {
            return a.object.order < b.object.order;
        });
        out.assets = std::move(bank);
        return out;
    } catch (const std::exception &e) {
        error = e.what();
        return {};
    }
}

PatternPlan preview_pattern(const Document &document, const PatternOptions &options) {
    PatternPlan plan;
    plan.before = document.state();
    plan.options = options;
    try {
        plan.source = pattern_source(plan.before, options.plane, plan.error);
        require(plan.source.assets != nullptr, plan.error);
        auto effective = options;
        if (options.plane >= 0)
            effective.image = plan.source.assets->data.images.back().idx;
        auto edited = document;
        edited.state_ = plan.source;
        edited.state_.assets = pattern_bank(plan.source, effective, plan.changed_pixels,
                                            plan.silhouette_pixels, plan.uses);
        if (options.plane >= 0)
            plan.uses = (int)std::count_if(plan.before.objects.begin(), plan.before.objects.end(),
                                           [&](const auto &p) { return p.plane == options.plane; });
        OptimizeOptions packing;
        packing.compact_palettes = false;
        packing.source_image = effective.image;
        packing.max_pieces = 16;
        packing.max_added_objects = 256;
        packing.policy = 1;
        plan.packing = find_lossless_savings(edited, packing);
        require(plan.packing.verified, plan.packing.error);
        for (const auto &im : plan.packing.after.assets->data.images)
            if (im.idx == effective.image ||
                std::none_of(plan.source.assets->data.images.begin(),
                             plan.source.assets->data.images.end(),
                             [&](const auto &original) { return original.idx == im.idx; }))
                require(im.w <= 248 && im.w % 4 == 0 && (uint64_t)im.w * im.h <= 65500,
                        "This group cannot be packed within 16 pieces. Try a larger repeat group.");
        plan.valid = verify_pattern(plan, plan.error);
    } catch (const std::exception &e) {
        plan.error = e.what();
    }
    return plan;
}

PatternPlan suggest_pattern(const Document &document, const PatternOptions &options) {
    try {
        require(options.mode == PatternMode::RepeatX || options.mode == PatternMode::RepeatY,
                "Group suggestions are available for X/Y repetition.");
        std::string error;
        auto source = pattern_source(document.state(), options.plane, error);
        require(source.assets != nullptr, error);
        auto candidate = options, best = options;
        if (options.plane >= 0)
            candidate.image = source.assets->data.images.back().idx;
        const auto &im = get_image(source, candidate.image);
        bool vertical = options.mode == PatternMode::RepeatY;
        int unit = vertical ? 1 : 4, length = vertical ? im.h : im.w;
        require(options.span > 0 && options.span <= length,
                "The source group must fit inside the image.");
        int positions = (length - options.span) / unit;
        int stride = std::max(1, (positions + 127) / 128);
        uint64_t best_score = UINT64_MAX;
        // Bounded source-window sampling. Prefer fewer changed pixels, with additional
        // weight for silhouette changes; this is a suggestion, not an aesthetic proof.
        for (int position = 0;; position = std::min(positions, position + stride)) {
            candidate.offset = position * unit;
            uint64_t changed = 0, silhouette = 0;
            int uses = 0;
            pattern_bank(source, candidate, changed, silhouette, uses);
            uint64_t score = changed + silhouette * 2;
            if (score < best_score) {
                best_score = score;
                best.offset = candidate.offset;
            }
            if (position == positions)
                break;
        }
        return preview_pattern(document, best);
    } catch (const std::exception &e) {
        PatternPlan failed;
        failed.before = document.state();
        failed.options = options;
        failed.error = e.what();
        return failed;
    }
}

PatternSearch discover_patterns(const Document &document, const PatternOptions &options,
                                OptimizeProgress *progress) {
    PatternSearch result;
    result.before = document.state();
    result.options = options;
    try {
        auto checkpoint = [&]() {
            if (progress && progress->cancel)
                throw std::runtime_error("Pattern search cancelled.");
        };
        checkpoint();
        std::string error;
        auto source = pattern_source(result.before, options.plane, error);
        require(source.assets != nullptr, error);
        auto effective = options;
        if (options.plane >= 0)
            effective.image = source.assets->data.images.back().idx;
        const auto &im = get_image(source, effective.image);
        bool vertical =
            options.mode == PatternMode::RepeatY || options.mode == PatternMode::MirrorY;
        int unit = vertical ? 1 : 4, length = vertical ? im.h : im.w;
        // Validate geometry, palette references and every consumer once before the sampled scan.
        auto identity = effective;
        identity.mode = vertical ? PatternMode::RepeatY : PatternMode::RepeatX;
        identity.offset = 0;
        identity.span = length;
        identity.alternate_flip = false;
        uint64_t changed = 0, silhouette = 0;
        int uses = 0;
        pattern_bank(source, identity, changed, silhouette, uses);
        std::array<std::array<bool, 256>, 256> different{};
        std::set<int> palettes;
        for (const auto &p : source.objects)
            if (p.object.ii == effective.image)
                palettes.insert(p.object.fl);
        for (int a = 0; a < 256; a++)
            for (int b = 0; b < 256; b++) {
                different[a][b] = (a == 0) != (b == 0);
                for (int pi : palettes) {
                    const auto &pal = source.assets->data.palettes[pi];
                    if (a && b && a < pal.count && b < pal.count)
                        different[a][b] = different[a][b] || pal.rgb555[a] != pal.rgb555[b];
                }
            }
        std::vector<size_t> samples;
        size_t count = std::min<size_t>(4096, im.pix.size());
        for (size_t i = 0; i < count; i++) {
            size_t begin = i * im.pix.size() / count, end = (i + 1) * im.pix.size() / count;
            // One deterministic sample per stratum avoids repeatedly sampling the same phase.
            samples.push_back(begin + ((i * 2654435761ULL + 1013904223ULL) % (end - begin)));
        }
        struct Candidate {
            PatternOptions options;
            double distortion = 0, fraction = 0;
        };
        auto score = [&](const PatternOptions &o) {
            checkpoint();
            result.sampled++;
            uint64_t difference = 0;
            bool repeat = o.mode == PatternMode::RepeatX || o.mode == PatternMode::RepeatY;
            for (auto pos : samples) {
                int x = (int)(pos % im.w), y = (int)(pos / im.w), p = vertical ? y : x;
                int from;
                if (repeat) {
                    from = p % o.span;
                    if (o.alternate_flip && (p / o.span) % 2)
                        from = o.span - 1 - from;
                    from += o.offset;
                } else
                    from =
                        o.use_far_side ? std::max(p, length - 1 - p) : std::min(p, length - 1 - p);
                auto a = im.pix[pos];
                auto b = im.pix[vertical ? (size_t)from * im.w + x : (size_t)y * im.w + from];
                difference += different[a][b] + 2 * ((a == 0) != (b == 0));
            }
            return Candidate{o, (double)difference / count, repeat ? (double)o.span / length : .5};
        };
        std::set<int> sizes;
        auto add_size = [&](int size) {
            size = size / unit * unit;
            if (size >= unit && size <= length / 2 && size * 16 >= length)
                sizes.insert(size);
        };
        add_size(options.span);
        for (int divisions = 2; divisions <= 16; divisions++)
            add_size((length / divisions + unit - 1) / unit * unit);
        for (int size = unit; size <= length / 2; size *= 2)
            add_size(size);
        if (progress) {
            progress->done = 0;
            progress->total = (int)sizes.size() * 2 + 2 + 12;
        }
        std::vector<Candidate> candidates;
        for (int size : sizes)
            for (int flip = 0; flip < 2; flip++) {
                auto o = effective;
                o.mode = identity.mode;
                o.span = size;
                o.alternate_flip = flip != 0;
                int positions = (length - size) / unit;
                int stride = std::max(1, (positions + 31) / 32);
                Candidate best;
                best.distortion = std::numeric_limits<double>::infinity();
                for (int pos = 0;; pos = std::min(positions, pos + stride)) {
                    o.offset = pos * unit;
                    auto candidate = score(o);
                    if (candidate.distortion < best.distortion)
                        best = candidate;
                    if (pos == positions)
                        break;
                }
                candidates.push_back(best);
                if (progress)
                    ++progress->done;
            }
        for (int side = 0; side < 2; side++) {
            auto o = effective;
            o.mode = vertical ? PatternMode::MirrorY : PatternMode::MirrorX;
            o.use_far_side = side != 0;
            candidates.push_back(score(o));
            if (progress)
                ++progress->done;
        }
        // Keep four candidates from each preference, then do full-resolution packing/verification.
        std::set<size_t> shortlist;
        for (double weight : {0.0, .5, 2.0}) {
            std::vector<size_t> order(candidates.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return candidates[a].distortion + weight * candidates[a].fraction <
                       candidates[b].distortion + weight * candidates[b].fraction;
            });
            for (size_t i = 0; i < std::min<size_t>(4, order.size()); i++)
                shortlist.insert(order[i]);
        }
        result.baseline = optimization_budget(result.before);
        for (size_t index : shortlist) {
            checkpoint();
            auto o = candidates[index].options;
            o.image = options.image; // Layer composition resolves its temporary ID in preview.
            auto plan = preview_pattern(document, o);
            result.packed++;
            if (progress)
                ++progress->done;
            if (plan.valid && plan.packing.proposed.video_bits < result.baseline.video_bits)
                result.proposals.push_back(std::move(plan));
        }
        checkpoint();
        // Keep the tradeoffs: no survivor is worse in bytes, changed pixels, silhouette AND
        // objects.
        auto dominates = [](const PatternPlan &a, const PatternPlan &b) {
            const auto &ab = a.packing.proposed, &bb = b.packing.proposed;
            return ab.video_bits <= bb.video_bits && a.changed_pixels <= b.changed_pixels &&
                   a.silhouette_pixels <= b.silhouette_pixels && ab.objects <= bb.objects &&
                   (ab.video_bits < bb.video_bits || a.changed_pixels < b.changed_pixels ||
                    a.silhouette_pixels < b.silhouette_pixels || ab.objects < bb.objects);
        };
        std::vector<PatternPlan> frontier;
        for (const auto &p : result.proposals)
            if (std::none_of(result.proposals.begin(), result.proposals.end(),
                             [&](const auto &other) { return dominates(other, p); })) {
                bool duplicate = std::any_of(frontier.begin(), frontier.end(), [&](const auto &q) {
                    return q.changed_pixels == p.changed_pixels &&
                           q.silhouette_pixels == p.silhouette_pixels &&
                           q.packing.proposed.video_bits == p.packing.proposed.video_bits &&
                           q.packing.proposed.objects == p.packing.proposed.objects &&
                           get_image(q.packing.before, q.packing.options.source_image).pix ==
                               get_image(p.packing.before, p.packing.options.source_image).pix;
                });
                if (!duplicate)
                    frontier.push_back(p);
            }
        std::stable_sort(frontier.begin(), frontier.end(), [](const auto &a, const auto &b) {
            if (a.changed_pixels != b.changed_pixels)
                return a.changed_pixels < b.changed_pixels;
            if (a.silhouette_pixels != b.silhouette_pixels)
                return a.silhouette_pixels < b.silhouette_pixels;
            return a.packing.proposed.video_bits < b.packing.proposed.video_bits;
        });
        result.proposals = std::move(frontier);
        if (progress)
            progress->done = progress->total.load();
    } catch (const std::exception &e) {
        result.error = e.what();
        result.cancelled = progress && progress->cancel;
        result.proposals.clear();
    }
    return result;
}

bool verify_pattern(const PatternPlan &plan, std::string &error) {
    try {
        uint64_t changed = 0, silhouette = 0;
        int uses = 0;
        auto source = pattern_source(plan.before, plan.options.plane, error);
        require(source.assets != nullptr, error);
        auto effective = plan.options;
        if (effective.plane >= 0)
            effective.image = source.assets->data.images.back().idx;
        auto expected = pattern_bank(source, effective, changed, silhouette, uses);
        if (effective.plane >= 0)
            uses = (int)std::count_if(plan.before.objects.begin(), plan.before.objects.end(),
                                      [&](const auto &p) { return p.plane == effective.plane; });
        require(changed == plan.changed_pixels && silhouette == plan.silhouette_pixels &&
                    uses == plan.uses,
                "Pattern change statistics do not match the preview.");
        const auto &painted = plan.packing.before.assets;
        require(painted && painted->data.images.size() == expected->data.images.size() &&
                    painted->data.palettes.size() == expected->data.palettes.size(),
                "Pattern preview changed unrelated assets.");
        for (size_t i = 0; i < expected->data.images.size(); i++) {
            const auto &a = expected->data.images[i], &b = painted->data.images[i];
            require(a.idx == b.idx && a.flags == b.flags && a.w == b.w && a.h == b.h &&
                        a.pix == b.pix,
                    "Pattern pixels do not match the selected source group.");
        }
        OptimizationPlan guard;
        guard.before = source;
        guard.before.assets = expected;
        guard.after = plan.packing.before;
        require(verify_optimization(guard, error), error);
        for (const auto &c : plan.packing.changes)
            require(c.source_image == effective.image && !c.reindexed,
                    "Pattern packing changed unrelated artwork or palette indices.");
        require(plan.packing.verified && verify_optimization(plan.packing, error), error);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

OptimizeBudget optimization_budget(const State &state) {
    OptimizeBudget b;
    if (!state.assets)
        return b;
    std::set<std::string> unique;
    for (const auto &im : state.assets->data.images)
        if (unique.insert(key(im)).second) {
            b.video_bits += bits(im);
            b.raw_bits += bits(im, false);
        }
    b.images = (int)state.assets->data.images.size();
    b.palettes = (int)state.assets->data.palettes.size();
    b.objects = (int)state.objects.size();
    b.table_bytes =
        (uint64_t)b.images * 10 + (uint64_t)b.objects * 8 + (uint64_t)state.planes.size() * 8;
    for (const auto &p : state.assets->data.palettes)
        b.palette_bytes += p.count * 2 + 2;
    return b;
}
OptimizationPlan find_lossless_savings(const Document &document, const OptimizeOptions &options,
                                       OptimizeProgress *progress) {
    OptimizationPlan plan;
    plan.before = document.state();
    plan.after = plan.before;
    plan.options = options;
    try {
        require(plan.before.assets && plan.before.has_bdb,
                "Open a paired BDB/BDD stage to optimize placements.");
        require(options.max_pieces >= 1 && options.max_pieces <= 16 &&
                    options.max_added_objects >= 0 && options.max_palettes >= 1 &&
                    options.max_palettes <= 256,
                "Invalid optimization limits.");
        plan.baseline = optimization_budget(plan.before);
        auto bank = std::make_shared<AssetBank>(*plan.before.assets);
        std::map<std::string, int> shared, pals;
        for (size_t i = 0; i < bank->data.palettes.size(); i++)
            pals.emplace(palette_key(bank->data.palettes[i]), (int)i);
        std::map<int, std::vector<const Placement *>> uses;
        for (const auto &obj : plan.before.objects)
            uses[obj.object.ii].push_back(&obj);
        std::vector<const BddCoreImage *> sources;
        int next_id = 0, added = 0;
        for (const auto &im : plan.before.assets->data.images) {
            sources.push_back(&im);
            next_id = std::max(next_id, im.idx + 1);
        }
        std::stable_sort(sources.begin(), sources.end(),
                         [](auto a, auto b) { return bits(*a) > bits(*b); });
        if (progress)
            progress->total = (int)sources.size();
        for (const auto *source : sources) {
            if (progress && progress->cancel) {
                plan.cancelled = true;
                break;
            }
            if (progress)
                ++progress->done;
            auto id = source->idx;
            if (options.source_image >= 0 && id != options.source_image)
                continue;
            auto &placements = uses[id];
            std::string reason;
            if (placements.empty())
                continue; // Not proof of absence from external references.
            if (source->w % 4 || source->w <= 0 || source->h <= 0 ||
                source->pix.size() != (size_t)source->w * source->h)
                reason = "source width/geometry is not LOAD2-aligned";
            for (const auto &m : bank->metadata)
                if (m.idx == id && (m.lod_ref || m.anix || m.aniy || m.anix2 || m.aniy2 ||
                                    m.aniz2 || m.frm || m.opals || m.pttblnum))
                    reason = "animation/LOD metadata requires a consumer-specific review";
            std::set<int> used_palettes;
            for (auto p : placements) {
                if (p->plane < 0 || p->plane >= (int)plan.before.planes.size() || p->locked ||
                    (p->plane >= 0 && p->plane < (int)plan.before.planes.size() &&
                     plan.before.planes[p->plane].locked))
                    reason = "unassigned or locked placement";
                if (p->object.fl < 0 || p->object.fl >= (int)bank->data.palettes.size())
                    reason = "missing palette";
                else
                    used_palettes.insert(p->object.fl);
            }
            if (!reason.empty()) {
                plan.notes.push_back("Image " + std::to_string(id) + ": skipped (" + reason + ").");
                continue;
            }
            std::vector<int> palettes(used_palettes.begin(), used_palettes.end());
            Search search{
                *source, palettes, *bank, shared, options, progress, (int)placements.size(), added};
            auto candidate = search.run();
            bool compact = options.compact_palettes;
            if (compact) {
                auto keep_options = options;
                keep_options.compact_palettes = false;
                Search keep{*source,
                            palettes,
                            *bank,
                            shared,
                            keep_options,
                            progress,
                            (int)placements.size(),
                            added};
                auto preserved = keep.run();
                if (preserved.score <= candidate.score) {
                    candidate = std::move(preserved);
                    compact = false;
                }
            }
            // Neutral canonicalization seeds reuse for later whole-image mirrors.
            bool seed =
                !compact && candidate.tiles.size() == 1 && candidate.new_bits == bits(*source);
            if (candidate.score == INT64_MAX || (candidate.new_bits >= bits(*source) && !seed))
                continue;
            int penalty = options.policy == 0 ? 0 : options.policy == 1 ? 24 : 128;
            if (candidate.score >= (int64_t)bits(*source) && penalty && !seed)
                continue;
            auto tentative = std::make_shared<AssetBank>(*bank);
            auto next_shared = shared, next_pals = pals;
            int next = next_id;
            OptimizeChange change;
            change.source_image = id;
            change.uses = (int)placements.size();
            change.before_bits = bits(*source);
            change.added_bits = candidate.new_bits;
            change.reindexed = compact;
            for (const auto &t : candidate.tiles) {
                OptimizePiece p;
                p.x = t.box.x;
                p.y = t.box.y;
                p.w = t.box.w;
                p.h = t.box.h;
                p.flip_x = t.fx;
                p.flip_y = t.fy;
                if (next_shared.count(t.key))
                    p.image = next_shared.at(t.key);
                else {
                    auto im = t.image;
                    im.idx = next++;
                    p.image = im.idx;
                    next_shared[t.key] = im.idx;
                    tentative->data.images.push_back(std::move(im));
                    BddImageMetadata m{};
                    m.idx = p.image;
                    std::snprintf(m.label, sizeof m.label, "OPT_%d", p.image);
                    tentative->metadata.push_back(m);
                    tentative->default_palettes.push_back(palettes.front());
                }
                for (int pi : palettes) {
                    if (!compact) {
                        p.palettes[pi] = pi;
                        continue;
                    }
                    auto pal = remap(bank->data.palettes[pi], t, compact);
                    auto pk = palette_key(pal);
                    if (!next_pals.count(pk)) {
                        int slot = (int)tentative->data.palettes.size();
                        std::snprintf(pal.name, sizeof pal.name, "OPT%03d", slot);
                        next_pals[pk] = slot;
                        tentative->data.palettes.push_back(pal);
                    }
                    p.palettes[pi] = next_pals.at(pk);
                }
                change.pieces.push_back(std::move(p));
            }
            auto pos = std::find_if(tentative->data.images.begin(), tentative->data.images.end(),
                                    [&](const auto &im) { return im.idx == id; });
            auto offset = pos - tentative->data.images.begin();
            tentative->data.images.erase(pos);
            if (offset < (ptrdiff_t)tentative->default_palettes.size())
                tentative->default_palettes.erase(tentative->default_palettes.begin() + offset);
            if (tentative->data.images.size() > BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS ||
                next > 65536) {
                plan.notes.push_back("Image " + std::to_string(id) +
                                     ": skipped (image header limit).");
                continue;
            }
            added += ((int)change.pieces.size() - 1) * change.uses;
            bank = std::move(tentative);
            shared = std::move(next_shared);
            pals = std::move(next_pals);
            next_id = next;
            plan.changes.push_back(std::move(change));
        }
        require(!plan.cancelled, "Scan cancelled.");
        plan.after.assets = bank;
        plan.after.objects.clear();
        ObjectId next_object = 1;
        for (const auto &o : plan.before.objects)
            next_object = std::max(next_object, o.id + 1);
        for (const auto &old : plan.before.objects) {
            auto change =
                std::find_if(plan.changes.begin(), plan.changes.end(),
                             [&](const auto &c) { return c.source_image == old.object.ii; });
            if (change == plan.changes.end()) {
                plan.after.objects.push_back(old);
                continue;
            }
            const auto &im = get_image(plan.before, old.object.ii);
            bool first = true;
            for (const auto &p : change->pieces) {
                Placement obj = old;
                if (!first)
                    obj.id = next_object++;
                first = false;
                obj.object.depth += (old.object.wx & 0x10) ? im.w - p.x - p.w : p.x;
                obj.object.sy += (old.object.wx & 0x20) ? im.h - p.y - p.h : p.y;
                obj.object.ii = p.image;
                obj.object.fl = p.palettes.at(old.object.fl);
                obj.object.wx ^= (p.flip_x ? 0x10 : 0) | (p.flip_y ? 0x20 : 0);
                plan.after.objects.push_back(obj);
            }
        }
        std::stable_sort(
            plan.after.objects.begin(), plan.after.objects.end(),
            [](const auto &a, const auto &b) { return a.object.order < b.object.order; });
        for (size_t i = 0; i < plan.after.objects.size(); i++)
            plan.after.objects[i].object.order = (int)i;
        // Asset-tray palette defaults must match each newly generated tile.
        for (size_t i = 0; i < bank->data.images.size(); i++)
            for (const auto &o : plan.after.objects)
                if (o.object.ii == bank->data.images[i].idx) {
                    bank->default_palettes[i] = o.object.fl;
                    break;
                }
        plan.proposed = optimization_budget(plan.after);
        require(plan.after.objects.size() <= BDD_CORE_MAX_OBJECTS &&
                    plan.after.objects.size() <= BDD_CORE_MK2_LOAD2_MAX_BLOCKS,
                "Proposed stage exceeds the LOAD2 block limit.");
        if (plan.proposed.video_bits >= plan.baseline.video_bits) {
            plan.changes.clear();
            plan.after = plan.before;
            plan.proposed = plan.baseline;
            plan.notes.push_back(
                "No net modeled saving after exact-payload sharing. No changes proposed.");
        }
        plan.verified = verify_optimization(plan, plan.error);
    } catch (const std::exception &e) {
        plan.error = e.what();
        plan.verified = false;
        if (progress && progress->cancel)
            plan.cancelled = true;
    }
    return plan;
}
OptimizationPlan find_shared_savings(const Document &document, const OptimizeOptions &options,
                                     OptimizeProgress *progress) {
    OptimizationPlan plan;
    plan.before = plan.after = document.state();
    plan.options = options;
    plan.options.compact_palettes = false;
    try {
        require(plan.before.assets && plan.before.has_bdb, "Open a paired stage first.");
        require(options.max_pieces >= 1 && options.max_pieces <= 16 &&
                    options.max_added_objects >= 0,
                "Invalid shared-piece limits.");
        plan.baseline = optimization_budget(plan.before);
        auto bank = std::make_shared<AssetBank>(*plan.before.assets);
        auto checkpoint = [&]() {
            if (progress && progress->cancel)
                throw std::runtime_error("Scan cancelled.");
        };
        std::map<int, std::vector<const Placement *>> uses;
        for (const auto &p : plan.before.objects)
            uses[p.object.ii].push_back(&p);
        std::vector<const BddCoreImage *> sources;
        int next_id = 0;
        for (const auto &im : bank->data.images) {
            next_id = std::max(next_id, im.idx + 1);
            bool ok = im.w > 0 && im.w <= 248 && im.w % 4 == 0 && im.h > 0 &&
                      (uint64_t)im.w * im.h <= 65500 && im.pix.size() == (size_t)im.w * im.h &&
                      !uses[im.idx].empty();
            for (auto p : uses[im.idx])
                ok &= p->plane >= 0 && p->plane < (int)plan.before.planes.size() && !p->locked &&
                      !plan.before.planes[p->plane].locked && p->object.fl >= 0 &&
                      p->object.fl < (int)bank->data.palettes.size();
            for (const auto &m : bank->metadata)
                if (m.idx == im.idx && (m.lod_ref || m.anix || m.aniy || m.anix2 || m.aniy2 ||
                                        m.aniz2 || m.frm || m.opals || m.pttblnum))
                    ok = false;
            // Keep pointers in the immutable source snapshot, not the bank being edited.
            if (ok)
                sources.push_back(&get_image(plan.before, im.idx));
        }
        struct Pair {
            int a = 0, b = 0, extra = 0;
            int64_t saving = 0;
            std::vector<Tile> first, second;
        };
        std::vector<Pair> proposals;
        int pair_limit = options.deep ? 4096 : 1024, visited = 0;
        if (progress) {
            progress->done = 0;
            progress->total =
                std::min(pair_limit,
                         (int)(sources.size() * (sources.size() - (sources.empty() ? 0 : 1)) / 2));
        }
        for (size_t ai = 0; ai < sources.size(); ai++)
            for (size_t bi = ai + 1; bi < sources.size() && visited < pair_limit; bi++) {
                checkpoint();
                visited++;
                if (progress)
                    ++progress->done;
                const auto &a = *sources[ai], &b = *sources[bi];
                if (a.flags != b.flags ||
                    (options.source_image >= 0 && a.idx != options.source_image &&
                     b.idx != options.source_image))
                    continue;
                Pair best;
                // Exact 8x4 anchors vote for translations. Blank windows cannot create matches.
                auto anchor = [](const BddCoreImage &im, int x, int y, int flip) {
                    std::string value;
                    int opaque = 0;
                    for (int yy = 0; yy < 4; yy++)
                        for (int xx = 0; xx < 8; xx++) {
                            auto p =
                                im.pix[(size_t)((flip & 2) ? im.h - 1 - y - yy : y + yy) * im.w +
                                       ((flip & 1) ? im.w - 1 - x - xx : x + xx)];
                            value.push_back((char)p);
                            opaque += p != 0;
                        }
                    return opaque >= 8 ? value : std::string{};
                };
                std::unordered_map<std::string, std::vector<std::pair<int, int>>> anchors;
                int stride = options.deep ? 4 : 8;
                for (int y = 0; y + 4 <= a.h; y += stride)
                    for (int x = 0; x + 8 <= a.w; x += stride) {
                        auto k = anchor(a, x, y, 0);
                        if (!k.empty() && anchors[k].size() < 4)
                            anchors[k].emplace_back(x, y);
                    }
                for (int flip = 0; flip < 4; flip++) {
                    checkpoint();
                    std::map<std::pair<int, int>, int> votes;
                    for (int y = 0; y + 4 <= b.h; y += stride)
                        for (int x = 0; x + 8 <= b.w; x += stride) {
                            auto found = anchors.find(anchor(b, x, y, flip));
                            if (found != anchors.end())
                                for (auto p : found->second)
                                    votes[{x - p.first, y - p.second}]++;
                        }
                    std::vector<std::pair<int, std::pair<int, int>>> ranked;
                    for (const auto &vote : votes)
                        ranked.emplace_back(vote.second, vote.first);
                    std::sort(ranked.rbegin(), ranked.rend());
                    std::vector<std::pair<int, int>> offsets{{0, 0}};
                    for (size_t i = 0; i < ranked.size() && i < 4; i++)
                        if (ranked[i].second != offsets.front())
                            offsets.push_back(ranked[i].second);
                    for (auto offset : offsets) {
                        BddCoreImage common_a = a, common_b = b, detail_a = a, detail_b = b;
                        std::fill(common_a.pix.begin(), common_a.pix.end(), 0);
                        std::fill(common_b.pix.begin(), common_b.pix.end(), 0);
                        int matched = 0;
                        for (int y = 0; y < a.h; y++)
                            for (int x = 0; x < a.w; x++) {
                                int bx = x + offset.first, by = y + offset.second;
                                if (bx < 0 || by < 0 || bx >= b.w || by >= b.h)
                                    continue;
                                if (flip & 1)
                                    bx = b.w - 1 - bx;
                                if (flip & 2)
                                    by = b.h - 1 - by;
                                size_t ap = (size_t)y * a.w + x, bp = (size_t)by * b.w + bx;
                                if (a.pix[ap] && a.pix[ap] == b.pix[bp]) {
                                    common_a.pix[ap] = common_b.pix[bp] = a.pix[ap];
                                    detail_a.pix[ap] = detail_b.pix[bp] = 0;
                                    matched++;
                                }
                            }
                        if (matched < 32)
                            continue;
                        Pair pair;
                        pair.a = a.idx;
                        pair.b = b.idx;
                        auto parts = [](const BddCoreImage &common, const BddCoreImage &detail) {
                            std::vector<Tile> out;
                            auto c = tile(common, {0, 0, common.w, common.h}, false);
                            c.role = 1;
                            auto d = tile(detail, {0, 0, detail.w, detail.h}, false);
                            d.role = 2;
                            if (!c.image.pix.empty())
                                out.push_back(std::move(c));
                            if (!d.image.pix.empty())
                                out.push_back(std::move(d));
                            return out;
                        };
                        pair.first = parts(common_a, detail_a);
                        pair.second = parts(common_b, detail_b);
                        if (pair.first.empty() || pair.second.empty() ||
                            pair.first[0].key != pair.second[0].key ||
                            pair.first.size() > (size_t)options.max_pieces ||
                            pair.second.size() > (size_t)options.max_pieces)
                            continue;
                        pair.extra = ((int)pair.first.size() - 1) * (int)uses[a.idx].size() +
                                     ((int)pair.second.size() - 1) * (int)uses[b.idx].size();
                        if (pair.extra > options.max_added_objects)
                            continue;
                        std::set<std::string> unique;
                        uint64_t cost = 0;
                        for (const auto &list : {pair.first, pair.second})
                            for (const auto &t : list)
                                if (unique.insert(t.key).second)
                                    cost += bits(t.image);
                        int penalty = options.policy == 0 ? 0 : options.policy == 1 ? 24 : 128;
                        pair.saving = (int64_t)bits(a) + (key(a) == key(b) ? 0 : (int64_t)bits(b)) -
                                      (int64_t)cost - (int64_t)pair.extra * penalty * 8;
                        if (pair.saving > best.saving)
                            best = std::move(pair);
                    }
                }
                if (best.saving > 0) {
                    proposals.push_back(std::move(best));
                    std::sort(proposals.begin(), proposals.end(),
                              [](const auto &x, const auto &y) { return x.saving > y.saving; });
                    if (proposals.size() > 128)
                        proposals.resize(128);
                }
            }
        struct Family {
            std::map<int, std::vector<Tile>> members;
            int extra = 0;
            int64_t saving = 0;
        };
        auto score_family = [&](Family &family) {
            std::set<std::string> originals, pieces;
            int64_t before = 0, after = 0;
            family.extra = 0;
            for (const auto &member : family.members) {
                const auto &im = get_image(plan.before, member.first);
                if (originals.insert(key(im)).second)
                    before += bits(im);
                family.extra += ((int)member.second.size() - 1) * (int)uses[im.idx].size();
                for (const auto &part : member.second)
                    if (pieces.insert(part.key).second)
                        after += bits(part.image);
            }
            int penalty = options.policy == 0 ? 0 : options.policy == 1 ? 24 : 128;
            family.saving = before - after - (int64_t)family.extra * penalty * 8;
        };
        std::vector<Family> families;
        for (const auto &seed : proposals) {
            checkpoint();
            Family family;
            family.members[seed.a] = seed.first;
            family.members[seed.b] = seed.second;
            score_family(family);
            families.push_back(family); // Keep the pair if a larger family exceeds remaining caps.
            for (const auto &other : proposals) {
                if (other.first.front().key != seed.first.front().key ||
                    get_image(plan.before, other.a).flags != get_image(plan.before, seed.a).flags)
                    continue;
                auto extended = family;
                extended.members.emplace(other.a, other.first);
                extended.members.emplace(other.b, other.second);
                if (extended.members.size() == family.members.size() || extended.members.size() > 8)
                    continue;
                score_family(extended);
                if (extended.extra <= options.max_added_objects && extended.saving > family.saving)
                    family = std::move(extended);
            }
            if (family.members.size() > 2)
                families.push_back(std::move(family));
        }
        std::stable_sort(families.begin(), families.end(),
                         [](const auto &a, const auto &b) { return a.saving > b.saving; });
        std::set<int> consumed;
        std::map<std::string, int> shared;
        int added = 0;
        int family_count = 0, largest_family = 0;
        for (const auto &family : families) {
            checkpoint();
            if (added + family.extra > options.max_added_objects ||
                std::any_of(family.members.begin(), family.members.end(),
                            [&](const auto &member) { return consumed.count(member.first) != 0; }))
                continue;
            int worst_new = 0;
            for (const auto &member : family.members)
                worst_new += (int)member.second.size();
            if (bank->data.images.size() + worst_new - family.members.size() >
                    BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS ||
                next_id + worst_new > 65536)
                continue;
            family_count++;
            largest_family = std::max(largest_family, (int)family.members.size());
            for (const auto &member : family.members) {
                int id = member.first;
                const auto &tiles = member.second;
                OptimizeChange change;
                change.source_image = id;
                change.uses = (int)uses[id].size();
                change.before_bits = bits(get_image(plan.before, id));
                change.residual = true;
                for (const auto &t : tiles) {
                    OptimizePiece piece;
                    piece.x = t.box.x;
                    piece.y = t.box.y;
                    piece.w = t.box.w;
                    piece.h = t.box.h;
                    piece.flip_x = t.fx;
                    piece.flip_y = t.fy;
                    piece.role = t.role;
                    if (!shared.count(t.key)) {
                        auto im = t.image;
                        im.idx = next_id++;
                        shared[t.key] = im.idx;
                        change.added_bits += bits(im);
                        bank->data.images.push_back(im);
                        bank->default_palettes.push_back(uses[id].front()->object.fl);
                        BddImageMetadata m{};
                        m.idx = im.idx;
                        std::snprintf(m.label, sizeof m.label, "SHARED_%d", m.idx);
                        bank->metadata.push_back(m);
                    }
                    piece.image = shared.at(t.key);
                    for (auto use : uses[id])
                        piece.palettes[use->object.fl] = use->object.fl;
                    change.pieces.push_back(std::move(piece));
                }
                auto pos = std::find_if(bank->data.images.begin(), bank->data.images.end(),
                                        [&](const auto &im) { return im.idx == id; });
                bank->default_palettes.erase(bank->default_palettes.begin() +
                                             (pos - bank->data.images.begin()));
                bank->data.images.erase(pos);
                consumed.insert(id);
                plan.changes.push_back(std::move(change));
            }
            added += family.extra;
        }
        plan.after.assets = bank;
        plan.after.objects.clear();
        ObjectId next_object = 1;
        for (const auto &p : plan.before.objects)
            next_object = std::max(next_object, p.id + 1);
        for (const auto &old : plan.before.objects) {
            auto change =
                std::find_if(plan.changes.begin(), plan.changes.end(),
                             [&](const auto &c) { return c.source_image == old.object.ii; });
            if (change == plan.changes.end()) {
                plan.after.objects.push_back(old);
                continue;
            }
            const auto &im = get_image(plan.before, old.object.ii);
            bool first = true;
            for (const auto &piece : change->pieces) {
                auto p = old;
                if (!first)
                    p.id = next_object++;
                first = false;
                p.object.depth += (old.object.wx & 0x10) ? im.w - piece.x - piece.w : piece.x;
                p.object.sy += (old.object.wx & 0x20) ? im.h - piece.y - piece.h : piece.y;
                p.object.wx ^= (piece.flip_x ? 0x10 : 0) | (piece.flip_y ? 0x20 : 0);
                p.object.ii = piece.image;
                plan.after.objects.push_back(p);
            }
        }
        std::stable_sort(
            plan.after.objects.begin(), plan.after.objects.end(),
            [](const auto &a, const auto &b) { return a.object.order < b.object.order; });
        for (size_t i = 0; i < plan.after.objects.size(); i++)
            plan.after.objects[i].object.order = (int)i;
        require(plan.after.objects.size() <= BDD_CORE_MK2_LOAD2_MAX_BLOCKS,
                "Shared pieces exceed the LOAD2 block limit.");
        plan.proposed = optimization_budget(plan.after);
        if (plan.proposed.video_bits >= plan.baseline.video_bits) {
            plan.after = plan.before;
            plan.proposed = plan.baseline;
            plan.changes.clear();
        }
        plan.notes.push_back("Shared-base search: exact index matches, X/Y flips and sampled 8x4 "
                             "translation anchors; up to " +
                             std::to_string(pair_limit) +
                             " pairs; families of up to 8 images sharing an identical base. " +
                             std::to_string(family_count) + " groups selected; largest family " +
                             std::to_string(largest_family) +
                             ". Unique opaque details retained. Rectangles may overlap; "
                             "opaque pixels must not.");
        plan.verified = verify_optimization(plan, plan.error);
    } catch (const std::exception &e) {
        plan.error = e.what();
        if (progress && progress->cancel)
            plan.cancelled = true;
    }
    return plan;
}

bool verify_optimization(const OptimizationPlan &plan, std::string &error) {
    try {
        require(plan.before.assets && plan.after.assets, "Missing optimization assets.");
        require(plan.before.planes.size() == plan.after.planes.size(),
                "Optimization changed layer count.");
        for (size_t i = 0; i < plan.before.planes.size(); i++) {
            const auto &a = plan.before.planes[i], &b = plan.after.planes[i];
            require(a.x == b.x && a.y == b.y && a.scroll == b.scroll && a.rank == b.rank &&
                        a.hidden == b.hidden && a.locked == b.locked &&
                        a.source.x1 == b.source.x1 && a.source.y1 == b.source.y1,
                    "Optimization changed layer projection.");
        }
        std::vector<Placement> expected;
        ObjectId next = 1;
        for (const auto &o : plan.before.objects)
            next = std::max(next, o.id + 1);
        for (const auto &old : plan.before.objects) {
            auto it = std::find_if(plan.changes.begin(), plan.changes.end(),
                                   [&](const auto &c) { return c.source_image == old.object.ii; });
            if (it == plan.changes.end()) {
                expected.push_back(old);
                const auto &a = get_image(plan.before, old.object.ii),
                           &b = get_image(plan.after, old.object.ii);
                require(a.w == b.w && a.h == b.h && a.pix == b.pix,
                        "Unchanged artwork was modified.");
                continue;
            }
            const auto &im = get_image(plan.before, old.object.ii);
            bool first = true;
            for (const auto &p : it->pieces) {
                Placement n = old;
                if (!first)
                    n.id = next++;
                first = false;
                n.object.depth += (old.object.wx & 0x10) ? im.w - p.x - p.w : p.x;
                n.object.sy += (old.object.wx & 0x20) ? im.h - p.y - p.h : p.y;
                n.object.wx ^= (p.flip_x ? 0x10 : 0) | (p.flip_y ? 0x20 : 0);
                n.object.ii = p.image;
                n.object.fl = p.palettes.at(old.object.fl);
                expected.push_back(n);
            }
        }
        std::stable_sort(expected.begin(), expected.end(), [](const auto &a, const auto &b) {
            return a.object.order < b.object.order;
        });
        require(expected.size() == plan.after.objects.size(),
                "Optimization changed placement coverage.");
        for (size_t i = 0; i < expected.size(); i++) {
            auto &a = expected[i];
            const auto &b = plan.after.objects[i];
            require(a.id == b.id && a.plane == b.plane && a.hidden == b.hidden &&
                        a.locked == b.locked && a.object.ii == b.object.ii &&
                        a.object.fl == b.object.fl && a.object.wx == b.object.wx &&
                        a.object.depth == b.object.depth && a.object.sy == b.object.sy &&
                        (plan.changes.empty() || b.object.order == (int)i),
                    "Optimization changed placement, flips or draw order.");
        }
        require(plan.after.assets->data.palettes.size() >= plan.before.assets->data.palettes.size(),
                "Original palettes removed.");
        for (size_t i = 0; i < plan.before.assets->data.palettes.size(); i++)
            require(palette_key(plan.before.assets->data.palettes[i]) ==
                        palette_key(plan.after.assets->data.palettes[i]),
                    "Original palette modified.");
        for (const auto &change : plan.changes) {
            const auto &original = get_image(plan.before, change.source_image);
            std::set<int> palettes;
            for (const auto &o : plan.before.objects)
                if (o.object.ii == change.source_image)
                    palettes.insert(o.object.fl);
            for (int pi : palettes) {
                const auto &pal = plan.before.assets->data.palettes.at(pi);
                std::vector<uint32_t> reconstructed((size_t)original.w * original.h, 0);
                std::vector<bool> covered(reconstructed.size(), false);
                for (const auto &p : change.pieces) {
                    const auto &im = get_image(plan.after, p.image);
                    require(p.x >= 0 && p.y >= 0 && p.x + p.w <= original.w &&
                                p.y + p.h <= original.h && im.w == p.w && im.h == p.h,
                            "Optimization piece is out of bounds.");
                    const auto &colors = plan.after.assets->data.palettes.at(p.palettes.at(pi));
                    for (int y = 0; y < p.h; y++)
                        for (int x = 0; x < p.w; x++) {
                            size_t i = (size_t)(p.y + y) * original.w + p.x + x;
                            auto value = pixel(im, colors, p.flip_x ? p.w - 1 - x : x,
                                               p.flip_y ? p.h - 1 - y : y);
                            if (!change.residual || value) {
                                require(!covered[i], "Overlapping opaque optimization pixels.");
                                covered[i] = true;
                            }
                            if (value)
                                reconstructed[i] = value;
                        }
                }
                for (int y = 0; y < original.h; y++)
                    for (int x = 0; x < original.w; x++)
                        require(reconstructed[(size_t)y * original.w + x] ==
                                    pixel(original, pal, x, y),
                                "Reconstruction changed a color or transparency.");
            }
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
std::string optimization_report(const OptimizationPlan &p) {
    std::ostringstream out;
    out << "bddtool lossless optimization\nStage: " << p.before.name << "\n";
    out << "Video data estimate: " << (p.baseline.video_bits + 7) / 8 << " -> "
        << (p.proposed.video_bits + 7) / 8 << " bytes\n";
    out << "Table estimate: " << p.baseline.table_bytes << " -> " << p.proposed.table_bytes
        << " bytes\n";
    out << "Palette data estimate: " << p.baseline.palette_bytes << " -> "
        << p.proposed.palette_bytes << " bytes\n";
    out << "Images: " << p.baseline.images << " -> " << p.proposed.images
        << "; palettes: " << p.baseline.palettes << " -> " << p.proposed.palettes
        << "; placements: " << p.baseline.objects << " -> " << p.proposed.objects << "\n";
    out << "Exact reconstruction: " << (p.verified ? "PASS" : "NOT VERIFIED") << "\n";
    for (const auto &c : p.changes) {
        std::set<int> tiles;
        uint64_t covered = 0;
        for (const auto &piece : c.pieces) {
            tiles.insert(piece.image);
            covered += (uint64_t)piece.w * piece.h;
        }
        const auto &original = get_image(p.before, c.source_image);
        out << "Image " << c.source_image << ": " << c.pieces.size() << " pieces, " << c.uses
            << " uses, " << tiles.size() << " unique tiles, "
            << (c.residual ? "shared base + unique details"
                           : std::to_string((uint64_t)original.w * original.h - covered) +
                                 " transparent pixels trimmed")
            << "\n";
    }
    for (const auto &n : p.notes)
        out << n << '\n';
    out << p.error
        << "\nModel: auto BPP, zero compression on, 16-bit image alignment; exact payload "
           "duplicates counted once.\n"
           "BPP, compression policy, checksum folds and bank allocation must be verified with the "
           "selected game's LOAD2 build.\n"
           "Palette copies prove static RGB555 pixels only; cycling/swapping/index-sensitive game "
           "palettes need separate review.\n"
           "This bounded greedy search is not an exhaustive optimum. Runtime peak objects/DMA and "
           "bank headroom are not measured.\n";
    return out.str();
}
std::vector<OptimizeRegion> optimization_regions(const OptimizationPlan &plan) {
    std::vector<OptimizeRegion> regions;
    if (!plan.verified)
        return regions;
    std::map<int, int> uses;
    for (const auto &c : plan.changes)
        for (const auto &p : c.pieces)
            uses[p.image]++;
    for (size_t ci = 0; ci < plan.changes.size(); ci++) {
        const auto &c = plan.changes[ci];
        const auto &original = get_image(plan.before, c.source_image);
        int before_bpp = bdd_core_load2_bpp_for_max_pixel(
            bdd_core_image_max_pixel(original.pix.data(), original.w, original.h));
        std::vector<bool> stored(original.pix.size());
        for (const auto &p : c.pieces) {
            const auto &im = get_image(plan.after, p.image);
            int bpp = bdd_core_load2_bpp_for_max_pixel(
                bdd_core_image_max_pixel(im.pix.data(), im.w, im.h));
            int kinds = (bpp < before_bpp ? 2 : 0) | (uses[p.image] > 1 ? 4 : 0) |
                        (uses[p.image] > 1 && (p.flip_x || p.flip_y) ? 8 : 0) |
                        (p.role == 2 ? 16 : 0);
            if (kinds)
                regions.push_back({c.source_image,
                                   (int)ci,
                                   kinds,
                                   {(double)p.x, (double)p.y, (double)p.w, (double)p.h},
                                   before_bpp,
                                   bpp});
            for (int y = p.y; y < p.y + p.h; y++)
                for (int x = p.x; x < p.x + p.w; x++)
                    stored[(size_t)y * original.w + x] = true;
        }
        // Merge matching omitted row spans vertically into exact crop rectangles.
        std::map<std::pair<int, int>, size_t> active;
        for (int y = 0; y < original.h; y++) {
            std::map<std::pair<int, int>, size_t> next;
            for (int x = 0; x < original.w; x++) {
                if (stored[(size_t)y * original.w + x])
                    continue;
                int start = x;
                while (x + 1 < original.w && !stored[(size_t)y * original.w + x + 1])
                    x++;
                auto span = std::make_pair(start, x - start + 1);
                if (active.count(span)) {
                    auto index = active.at(span);
                    regions[index].rect.h++;
                    next[span] = index;
                } else {
                    next[span] = regions.size();
                    regions.push_back({c.source_image,
                                       (int)ci,
                                       1,
                                       {(double)start, (double)y, (double)span.second, 1},
                                       before_bpp,
                                       0});
                }
            }
            active = std::move(next);
        }
    }
    return regions;
}
} // namespace studio
