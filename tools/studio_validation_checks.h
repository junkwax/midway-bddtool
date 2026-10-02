#pragma once
#include "Core/studio_document.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>

// Private integration checks, not an emulator or a general export guarantee.
namespace studio::validation {
inline void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline std::vector<uint32_t> layer(const State &state, int index, const BddCoreModule &box,
                                   bool reverse_ties) {
    int w = box.x2 - box.x1 + 1, h = box.y2 - box.y1 + 1;
    check(w > 0 && h > 0 && uint64_t(w) * h < 16000000, "Layer too large");
    std::vector<uint32_t> out(size_t(w) * h);
    std::vector<Placement> objects;
    for (const auto &p : state.objects)
        if (p.plane == index)
            objects.push_back(p);
    std::stable_sort(objects.begin(), objects.end(), [=](const auto &a, const auto &b) {
        // MKDISP.ASM insobj_v sorts on Z, then Y, inserting new equal-key
        // objects before existing ones. Camera arrival can change tie order.
        auto ak = std::make_pair(a.object.wx >> 8, a.object.sy);
        auto bk = std::make_pair(b.object.wx >> 8, b.object.sy);
        if (ak != bk)
            return ak < bk;
        auto at = std::tie(a.object.depth, a.object.order);
        auto bt = std::tie(b.object.depth, b.object.order);
        return reverse_ties ? at > bt : at < bt;
    });
    for (const auto &p : objects) {
        auto found =
            std::find_if(state.assets->data.images.begin(), state.assets->data.images.end(),
                         [&](const auto &im) { return im.idx == p.object.ii; });
        check(found != state.assets->data.images.end(), "Missing image");
        const auto &im = *found;
        const auto &pal = state.assets->data.palettes.at(p.object.fl);
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                int v = im.pix[size_t(p.object.wx & 32 ? im.h - 1 - y : y) * im.w +
                               (p.object.wx & 16 ? im.w - 1 - x : x)];
                if (!v)
                    continue;
                check(v < pal.count, "Invalid palette");
                int dx = p.object.depth - box.x1 + x, dy = p.object.sy - box.y1 + y;
                check(dx >= 0 && dx < w && dy >= 0 && dy < h, "Layer bounds changed");
                out[size_t(dy) * w + dx] = 0x10000 | pal.rgb555[v];
            }
    }
    return out;
}
inline bool protects_unaligned(const std::vector<BddCoreImage> &images, int image_id) {
    // LOAD2's zcom_analysis reads four-pixel row strides even for
    // unaligned BDDs. Preserve the preceding buffer contents until
    // an aligned image large enough to overwrite them is reached.
    bool protected_context = false;
    bool after_source = false;
    size_t overwritten = 0;
    for (const auto &im : images) {
        if (im.idx == image_id) {
            after_source = true;
            continue;
        }
        if (!after_source)
            continue;
        size_t bytes = size_t(im.w) * im.h;
        if ((im.w & 3) && size_t((im.w + 3) & ~3) * im.h > std::max(bytes, overwritten))
            protected_context = true;
        overwritten = std::max(overwritten, bytes);
    }
    return protected_context;
}
} // namespace studio::validation
