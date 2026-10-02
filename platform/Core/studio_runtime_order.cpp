#include "Core/studio_runtime_order.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace studio {
namespace {
void need(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Item {
    const Placement *placement;
    const BddCoreImage *image;
    const BddCorePalette *palette;
    int64_t x, y;
};
std::vector<Item> items(const State &s, int plane) {
    need(s.assets != nullptr, "Missing runtime artwork assets.");
    std::map<int, const BddCoreImage *> images;
    for (const auto &im : s.assets->data.images)
        need(images.emplace(im.idx, &im).second, "Duplicate runtime image ID.");
    std::vector<Item> out;
    for (const auto &p : s.objects) {
        need(p.plane >= -1 && p.plane < (int)s.planes.size(), "Invalid runtime layer.");
        if (p.plane != plane)
            continue;
        auto im = images.find(p.object.ii);
        need(im != images.end(), "Missing runtime image.");
        need(p.object.fl >= 0 && p.object.fl < (int)s.assets->data.palettes.size(),
             "Missing runtime palette.");
        const auto &image = *im->second;
        const auto &palette = s.assets->data.palettes[p.object.fl];
        need(image.w > 0 && image.h > 0 && image.w <= 4096 && image.h <= 4096 &&
                 image.pix.size() == (uint64_t)image.w * image.h,
             "Invalid runtime image dimensions.");
        need(palette.count > 0 && palette.count <= 256, "Invalid runtime palette size.");
        int64_t x = (int64_t)p.object.depth + p.runtime_dx, y = p.object.sy;
        if (plane >= 0) {
            const auto &layer = s.planes[plane];
            x += (int64_t)layer.x - layer.source.x1;
            y += (int64_t)layer.y - layer.source.y1;
        }
        out.push_back({&p, &image, &palette, x, y});
    }
    return out;
}
std::vector<uint32_t> render(std::vector<Item> objects, int64_t x1, int64_t y1, size_t width,
                             size_t height, bool reverse) {
    std::stable_sort(objects.begin(), objects.end(), [=](const Item &a, const Item &b) {
        const auto ak = std::make_pair(a.placement->object.wx >> 8, a.y);
        const auto bk = std::make_pair(b.placement->object.wx >> 8, b.y);
        if (ak != bk)
            return ak < bk;
        const auto at = std::make_pair(a.x, a.placement->object.order);
        const auto bt = std::make_pair(b.x, b.placement->object.order);
        return reverse ? at > bt : at < bt;
    });
    std::vector<uint32_t> out(width * height);
    for (const auto &p : objects) {
        const auto &im = *p.image;
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                int sx = p.placement->object.wx & 16 ? im.w - 1 - x : x;
                int sy = p.placement->object.wx & 32 ? im.h - 1 - y : y;
                auto v = im.pix[(size_t)sy * im.w + sx];
                if (!v)
                    continue;
                need(v < p.palette->count, "Runtime image uses a missing palette entry.");
                out[(size_t)(p.y - y1 + y) * width + (size_t)(p.x - x1 + x)] =
                    0x10000 | p.palette->rgb555[v];
            }
    }
    return out;
}
} // namespace
RuntimeOrderCheck compare_runtime_order(const State &before, const State &after) {
    RuntimeOrderCheck result;
    try {
        need(before.planes.size() == after.planes.size(), "Runtime layer count changed.");
        uint64_t work = 0;
        std::ostringstream details;
        for (int plane = -1; plane < (int)before.planes.size(); ++plane) {
            if (plane >= 0) {
                const auto &a = before.planes[plane], &b = after.planes[plane];
                need(std::string(a.source.name) == b.source.name && a.scroll == b.scroll &&
                         a.rank == b.rank,
                     "Runtime layer binding, scroll or rank changed.");
            }
            auto a = items(before, plane), b = items(after, plane);
            if (a.empty() && b.empty())
                continue;
            bool first = true;
            int64_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            for (const auto *list : {&a, &b})
                for (const auto &p : *list) {
                    if (first) {
                        x1 = x2 = p.x;
                        y1 = y2 = p.y;
                        first = false;
                    }
                    x1 = std::min(x1, p.x);
                    y1 = std::min(y1, p.y);
                    x2 = std::max(x2, p.x + p.image->w);
                    y2 = std::max(y2, p.y + p.image->h);
                    work += (uint64_t)p.image->w * p.image->h;
                }
            need(x2 - x1 <= 16000000 && y2 - y1 <= 16000000 &&
                     (uint64_t)(x2 - x1) * (y2 - y1) <= 16000000,
                 "Runtime layer exceeds the 16-million-pixel comparison limit.");
            work += (uint64_t)(x2 - x1) * (y2 - y1) * 2;
            need(work <= 128000000, "Runtime comparison exceeds its pixel-work limit.");
            size_t changed = 0;
            for (bool reverse : {false, true}) {
                auto old = render(a, x1, y1, (size_t)(x2 - x1), (size_t)(y2 - y1), reverse);
                auto now = render(b, x1, y1, (size_t)(x2 - x1), (size_t)(y2 - y1), reverse);
                for (size_t i = 0; i < old.size(); ++i)
                    changed += old[i] != now[i];
            }
            ++result.layers;
            result.changed_samples += changed;
            if (changed)
                details << (plane < 0 ? "Unassigned artwork" : before.planes[plane].source.name)
                        << ": " << changed << " changed pixel samples across two arrival orders.\n";
        }
        result.checked = true;
        result.equivalent = result.changed_samples == 0;
        result.report = "Runtime Z/Y order: " + std::to_string(result.layers) +
                        " layer(s), two X arrival orders; " +
                        std::to_string(result.changed_samples) + " changed pixel samples.\n" +
                        details.str() +
                        "Static RGB555 model only; camera history, IMG actors, packed output and "
                        "object/DMA limits still require build/emulator checks.\n";
        if (!result.equivalent)
            result.error = "Runtime drawing order changes overlapping artwork.\n" + details.str();
    } catch (const std::exception &e) {
        result.error = e.what();
        result.report = "Runtime drawing-order comparison unavailable: " + result.error + "\n";
    }
    return result;
}
} // namespace studio
