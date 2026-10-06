#include "Core/studio_view_compare.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio {
namespace {
void put(std::vector<uint8_t> &rgba, size_t p, uint32_t argb) {
    rgba[p] = uint8_t(argb >> 16); rgba[p + 1] = uint8_t(argb >> 8);
    rgba[p + 2] = uint8_t(argb); rgba[p + 3] = 255;
}
const uint8_t *pixel(const RgbaView &view, int x, int y) {
    static const uint8_t empty[4] = {};
    if (x >= view.width || y >= view.height) return empty;
    return &view.rgba[(size_t(y) * view.width + x) * 4];
}
} // namespace

RgbaView render_camera_view(const State &state, Point camera, int width, int height) {
    RgbaView out;
    if (width <= 0 || height <= 0) return out;
    out.width = width; out.height = height;
    out.rgba.assign(size_t(width) * height * 4, 0);
    if (!state.assets) return out;
    for (const auto &item : scene_items(state, camera)) {
        if (item.image_slot >= state.assets->data.images.size() || item.palette < 0 ||
            item.palette >= (int)state.assets->data.palettes.size()) continue;
        const auto &im = state.assets->data.images[item.image_slot];
        const auto &pal = state.assets->data.palettes[item.palette];
        if (im.w <= 0 || im.h <= 0 || im.pix.size() < size_t(im.w) * im.h) continue;
        int dx = (int)std::llround(item.rect.x), dy = (int)std::llround(item.rect.y);
        int x0 = std::max(0, -dx), y0 = std::max(0, -dy);
        int x1 = std::min(im.w, width - dx), y1 = std::min(im.h, height - dy);
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            int sx = item.hflip ? im.w - 1 - x : x, sy = item.vflip ? im.h - 1 - y : y;
            unsigned index = im.pix[size_t(sy) * im.w + sx];
            if (!index || index >= (unsigned)pal.count) continue;
            put(out.rgba, (size_t(dy + y) * width + dx + x) * 4, bdd_core_rgb555_to_argb(pal.rgb555[index]));
        }
    }
    return out;
}

RgbaView render_indexed_image(const BddCoreImage &image, const BddCorePalette &palette) {
    RgbaView out;
    if (image.w <= 0 || image.h <= 0 || image.pix.size() < size_t(image.w) * image.h) return out;
    out.width = image.w; out.height = image.h;
    out.rgba.assign(size_t(image.w) * image.h * 4, 0);
    for (size_t i = 0; i < size_t(image.w) * image.h; ++i) {
        unsigned index = image.pix[i];
        if (index && index < (unsigned)palette.count)
            put(out.rgba, i * 4, bdd_core_rgb555_to_argb(palette.rgb555[index]));
    }
    return out;
}

ViewDifference diff_views(const RgbaView &before, const RgbaView &after) {
    ViewDifference out;
    out.width = std::max(before.width, after.width);
    out.height = std::max(before.height, after.height);
    if (out.width <= 0 || out.height <= 0) { out.width = out.height = 0; return out; }
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    out.left = out.width; out.top = out.height;
    for (int y = 0; y < out.height; ++y) for (int x = 0; x < out.width; ++x) {
        const uint8_t *a = pixel(before, x, y), *b = pixel(after, x, y);
        // Transparent pixels match regardless of their stored color.
        bool same = (!a[3] && !b[3]) || std::memcmp(a, b, 4) == 0;
        uint8_t *o = &out.rgba[(size_t(y) * out.width + x) * 4];
        ++out.compared;
        if (same) {
            if (b[3]) {
                uint8_t gray = uint8_t((b[0] * 77 + b[1] * 150 + b[2] * 29) * 2 / 5 >> 8); // Luma at 40%.
                o[0] = o[1] = o[2] = gray; o[3] = 255;
            }
            continue;
        }
        ++out.changed;
        out.left = std::min(out.left, x); out.top = std::min(out.top, y);
        out.right = std::max(out.right, x); out.bottom = std::max(out.bottom, y);
        // Magenta: color changed. Green: artwork added. Red: artwork removed.
        if (a[3] && b[3]) { o[0] = 255; o[1] = 64; o[2] = 255; }
        else if (b[3]) { o[0] = 64; o[1] = 230; o[2] = 96; }
        else { o[0] = 255; o[1] = 72; o[2] = 64; }
        o[3] = 255;
    }
    if (!out.changed) { out.left = out.top = 0; out.right = out.bottom = -1; }
    return out;
}

} // namespace studio
