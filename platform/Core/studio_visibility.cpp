#include "Core/studio_visibility.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace studio {
namespace {
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool runtime_image(const AssetBank &bank, int id) {
    for (const auto &m : bank.metadata)
        if (m.idx == id && (m.frm || m.opals || m.pttblnum || m.lod_ref || m.anix || m.aniy ||
                            m.anix2 || m.aniy2 || m.aniz2))
            return true;
    return false;
}
bool static_item(const State &s, const Placement &p) {
    return p.plane >= 0 && p.plane < (int)s.planes.size() && !p.hidden &&
           !s.planes[p.plane].hidden && s.planes[p.plane].bound &&
           std::isfinite(s.planes[p.plane].scroll) && !runtime_image(*s.assets, p.object.ii);
}
bool same_state(const State &a, const State &b) {
    if (std::tie(a.name, a.header, a.world_w, a.world_h, a.depth, a.start_x, a.start_y, a.ground,
                 a.has_bdb, a.revision) != std::tie(b.name, b.header, b.world_w, b.world_h, b.depth,
                                                    b.start_x, b.start_y, b.ground, b.has_bdb,
                                                    b.revision) ||
        a.runtime_profile != b.runtime_profile || a.planes.size() != b.planes.size() ||
        a.objects.size() != b.objects.size() || !a.assets || !b.assets)
        return false;
    for (size_t i = 0; i < a.planes.size(); i++) {
        const auto &p = a.planes[i], &q = b.planes[i];
        if (std::tie(p.name, p.x, p.y, p.scroll, p.rank, p.bound, p.hidden, p.locked) !=
                std::tie(q.name, q.x, q.y, q.scroll, q.rank, q.bound, q.hidden, q.locked) ||
            std::memcmp(&p.source, &q.source, sizeof p.source))
            return false;
    }
    for (size_t i = 0; i < a.objects.size(); i++) {
        const auto &p = a.objects[i], &q = b.objects[i];
        if (std::tie(p.id, p.plane, p.hidden, p.locked, p.runtime_dx) !=
                std::tie(q.id, q.plane, q.hidden, q.locked, q.runtime_dx) ||
            std::memcmp(&p.object, &q.object, sizeof p.object))
            return false;
    }
    const auto &x = *a.assets, &y = *b.assets;
    if (x.default_palettes != y.default_palettes || x.original_metadata != y.original_metadata ||
        x.original_metadata_count != y.original_metadata_count ||
        x.metadata.size() != y.metadata.size() || x.data.images.size() != y.data.images.size() ||
        x.data.palettes.size() != y.data.palettes.size() || x.data.path != y.data.path ||
        x.data.error != y.data.error)
        return false;
    for (size_t i = 0; i < x.metadata.size(); i++)
        if (std::memcmp(&x.metadata[i], &y.metadata[i], sizeof(BddImageMetadata)))
            return false;
    for (size_t i = 0; i < x.data.palettes.size(); i++)
        if (std::memcmp(&x.data.palettes[i], &y.data.palettes[i], sizeof(BddCorePalette)))
            return false;
    for (size_t i = 0; i < x.data.images.size(); i++) {
        const auto &p = x.data.images[i], &q = y.data.images[i];
        if (std::tie(p.idx, p.w, p.h, p.flags, p.pix) != std::tie(q.idx, q.w, q.h, q.flags, q.pix))
            return false;
    }
    return true;
}
} // namespace

VisibilityPlan analyze_visibility(const State &state, const VisibilityOptions &options,
                                  OptimizeProgress *progress) {
    VisibilityPlan plan;
    plan.before = plan.after = state;
    plan.options = options;
    try {
        auto checkpoint = [&]() {
            if (progress && progress->cancel)
                throw std::runtime_error("Visibility scan cancelled.");
        };
        checkpoint();
        require(state.assets && state.has_bdb, "Open a paired BDB/BDD stage.");
        require(options.min_x <= options.max_x && options.min_y <= options.max_y &&
                    options.min_x >= -100000 && options.max_x <= 100000 &&
                    options.min_y >= -100000 && options.max_y <= 100000,
                "Enter an ordered camera range within -100000 to 100000.");
        auto scene = scene_items(state);
        std::map<int, size_t> slots;
        size_t total_pixels = 0;
        for (size_t i = 0; i < state.assets->data.images.size(); i++) {
            const auto &im = state.assets->data.images[i];
            require(im.w > 0 && im.h > 0 && im.pix.size() == (size_t)im.w * im.h,
                    "Invalid artwork geometry.");
            total_pixels += im.pix.size();
            require(total_pixels <= 16000000,
                    "Visibility scan exceeds the 16-million-pixel artwork limit.");
            require(slots.emplace(im.idx, i).second, "Duplicate image ID in visibility source.");
            VisibilityImage result;
            result.image = im.idx;
            result.pixels.resize(im.pix.size());
            plan.images.push_back(std::move(result));
        }
        std::vector<int> uses(plan.images.size());
        std::vector<bool> protected_image(plan.images.size());
        bool bound = true;
        for (const auto &p : state.objects) {
            require(slots.count(p.object.ii) != 0, "A placement references missing artwork.");
            size_t slot = slots.at(p.object.ii);
            ++uses[slot];
            bool valid_plane = p.plane >= 0 && p.plane < (int)state.planes.size();
            require(!valid_plane || std::isfinite(state.planes[p.plane].scroll),
                    "Invalid layer parallax.");
            if (!valid_plane || !state.planes[p.plane].bound)
                bound = false;
            protected_image[slot] = protected_image[slot] || !static_item(state, p) || p.locked ||
                                    (valid_plane && state.planes[p.plane].locked);
            require(p.object.fl >= 0 && p.object.fl < (int)state.assets->data.palettes.size(),
                    "A placement references a missing palette.");
            const auto &im = state.assets->data.images[slot];
            const auto &pal = state.assets->data.palettes[p.object.fl];
            for (auto value : im.pix)
                require(value < pal.count, "Artwork references a missing palette entry.");
        }
        for (size_t i = 0; i < plan.images.size(); i++) {
            const auto &im = state.assets->data.images[i];
            protected_image[i] = protected_image[i] || !uses[i] || im.w % 4 || im.w > 248 ||
                                 im.pix.size() > 65500 || runtime_image(*state.assets, im.idx);
            for (size_t p = 0; p < im.pix.size(); p++)
                if (im.pix[p])
                    plan.images[i].pixels[p] = protected_image[i] ? 4 : 2;
        }
        if (progress) {
            progress->total = (int)scene.size();
            progress->done = 0;
        }
        uint64_t comparisons = 0, visited_pixels = 0;
        for (size_t si = 0; si < scene.size(); si++) {
            checkpoint();
            const auto &item = scene[si];
            const auto &obj = state.objects[item.object_index];
            const auto &im = state.assets->data.images[item.image_slot];
            auto &mask = plan.images[item.image_slot].pixels;
            if (progress)
                ++progress->done;
            if (protected_image[item.image_slot])
                continue;
            double scroll = state.planes[obj.plane].scroll;
            double dx0 = options.min_x * scroll, dx1 = options.max_x * scroll;
            require(std::isfinite(dx0) && std::isfinite(dx1), "Invalid layer parallax.");
            std::vector<const SceneItem *> covers;
            for (size_t ci = si + 1; ci < scene.size(); ci++) {
                const auto &cover = scene[ci];
                const auto &p = state.objects[cover.object_index];
                if (!static_item(state, p) || state.planes[p.plane].scroll != scroll ||
                    cover.rect.x >= item.rect.x + im.w ||
                    cover.rect.x + cover.rect.w <= item.rect.x ||
                    cover.rect.y >= item.rect.y + im.h ||
                    cover.rect.y + cover.rect.h <= item.rect.y)
                    continue;
                covers.push_back(&cover);
            }
            for (int y = 0; y < im.h; y++) {
                checkpoint();
                for (int x = 0; x < im.w; x++) {
                    require(++visited_pixels <= 100000000, "Visibility work limit exceeded; reduce "
                                                           "artwork or placements for this scan.");
                    size_t at = (size_t)y * im.w + x;
                    if (!im.pix[at] || mask[at] == 1)
                        continue; // Any visible use retains this source pixel.
                    double px = item.rect.x + (item.hflip ? im.w - 1 - x : x);
                    double py = item.rect.y + (item.vflip ? im.h - 1 - y : y);
                    // Pixel rectangles are swept continuously. Boundary-touching pixels are
                    // retained conservatively to avoid relying on a rasterizer's subpixel rounding
                    // convention.
                    bool outside = px + 1 - std::min(dx0, dx1) < 0 ||
                                   px - std::max(dx0, dx1) > 400 || py + 1 - options.min_y < 0 ||
                                   py - options.max_y > 254;
                    if (outside)
                        continue;
                    bool covered = false;
                    for (auto cover : covers) {
                        if (++comparisons > 100000000)
                            throw std::runtime_error(
                                "Occlusion work limit exceeded; narrow the stage or camera range.");
                        int cx = (int)(px - cover->rect.x), cy = (int)(py - cover->rect.y);
                        const auto &ci = state.assets->data.images[cover->image_slot];
                        if (cx < 0 || cy < 0 || cx >= ci.w || cy >= ci.h)
                            continue;
                        if (cover->hflip)
                            cx = ci.w - 1 - cx;
                        if (cover->vflip)
                            cy = ci.h - 1 - cy;
                        if (ci.pix[(size_t)cy * ci.w + cx]) {
                            covered = true;
                            break;
                        }
                    }
                    mask[at] = covered ? 3 : 1;
                }
            }
        }
        plan.baseline = plan.proposed = optimization_budget(state);
        auto bank = std::make_shared<AssetBank>(*state.assets);
        plan.after.assets = bank;
        for (size_t i = 0; i < plan.images.size(); i++) {
            checkpoint();
            auto &result = plan.images[i];
            const auto &im = state.assets->data.images[i];
            for (auto c : result.pixels) {
                result.outside += c == 2;
                result.covered += c == 3;
                result.retained += c == 1 || c == 4;
            }
            if (!bound || protected_image[i] || !(result.outside + result.covered))
                continue;
            auto masked = im;
            int left = im.w, right = -1, top = im.h, bottom = -1;
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++) {
                    size_t at = (size_t)y * im.w + x;
                    if (result.pixels[at] == 2 || result.pixels[at] == 3)
                        masked.pix[at] = 0;
                    if (masked.pix[at]) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            if (right < 0) {
                left = top = 0;
                right = 3;
                bottom = 0;
            }
            left = left / 4 * 4;
            right = std::min(im.w - 1, (right + 4) / 4 * 4 - 1);
            BddCoreImage cropped = im;
            cropped.w = right - left + 1;
            cropped.h = bottom - top + 1;
            cropped.pix.clear();
            for (int y = top; y <= bottom; y++)
                cropped.pix.insert(cropped.pix.end(), masked.pix.begin() + (size_t)y * im.w + left,
                                   masked.pix.begin() + (size_t)y * im.w + right + 1);
            bank->data.images[i] = cropped;
            auto budget = optimization_budget(plan.after);
            if (budget.video_bits >= plan.proposed.video_bits) {
                bank->data.images[i] = im;
                continue;
            }
            plan.proposed = budget;
            for (auto &p : plan.after.objects)
                if (p.object.ii == im.idx) {
                    p.object.depth += (p.object.wx & 0x10) ? im.w - left - cropped.w : left;
                    p.object.sy += (p.object.wx & 0x20) ? im.h - top - cropped.h : top;
                }
            result.trimmed = true;
            ++plan.changed_images;
        }
        plan.notes.push_back(
            "Continuous camera rectangle, 400x254 viewport. Same-parallax opaque coverage is "
            "proved analytically; different-parallax coverage is retained as unproven.");
        plan.notes.push_back(
            "All placements must agree before a shared source pixel is removed. Hidden, locked, "
            "unplaced, animation/LOD and unsupported artwork is protected.");
        plan.notes.push_back(
            "Camera limits and static layer behavior are a user-confirmed gameplay contract. "
            "Runtime actors, palette transparency changes and stage effects are not modeled.");
        if (!bound)
            plan.notes.push_back("Review only: unresolved game-plane bindings. Bind the stage "
                                 "layers before generating applicable trims.");
        if (!plan.changed_images)
            plan.notes.push_back("No net-saving trim within these proof limits. Classified pixels "
                                 "do not necessarily reduce packed bytes.");
        plan.analyzed = true;
        plan.verified = bound;
    } catch (const std::exception &e) {
        plan.error = e.what();
        plan.cancelled = progress && progress->cancel;
        plan.after = plan.before;
        plan.changed_images = 0;
        plan.verified = plan.analyzed = false;
    }
    return plan;
}
bool verify_visibility(const VisibilityPlan &plan, std::string &error) {
    auto checked = analyze_visibility(plan.before, plan.options);
    if (!plan.verified || !checked.verified || !checked.changed_images ||
        !same_state(checked.after, plan.after)) {
        error = checked.error.empty()
                    ? "Visibility proposal does not match the recomputed camera-range proof."
                    : checked.error;
        return false;
    }
    return true;
}
std::string visibility_report(const VisibilityPlan &plan) {
    std::ostringstream out;
    out << "bddtool visibility analysis\nCamera X " << plan.options.min_x << ".."
        << plan.options.max_x << ", Y " << plan.options.min_y << ".." << plan.options.max_y
        << "\nVideo estimate " << plan.baseline.video_bits / 8 << " -> "
        << plan.proposed.video_bits / 8 << " bytes\n";
    for (const auto &i : plan.images)
        out << "Image " << i.image << ": outside " << i.outside << ", covered " << i.covered
            << ", retained/protected " << i.retained << (i.trimmed ? "; trim proposed" : "")
            << '\n';
    for (const auto &note : plan.notes)
        out << note << '\n';
    if (!plan.error.empty())
        out << "INCOMPLETE: " << plan.error << '\n';
    return out.str();
}
} // namespace studio
