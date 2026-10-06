#include "Core/studio_image_replace.h"
#include <algorithm>
#include <unordered_map>

namespace studio {
namespace {
bool dimensions(int w, int h, std::string &error) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || uint64_t(w) * h > 16000000) {
        error = "Replacement dimensions exceed 4096 pixels or 16 million pixels."; return false;
    }
    return true;
}
}
ReplacementPixels prepare_rgba_replacement(int w, int h, const uint8_t *rgba,
                                           const BddCorePalette &palette) {
    ReplacementPixels out; out.width = w; out.height = h;
    if (!rgba || !dimensions(w, h, out.error)) return out;
    int count = std::clamp(palette.count, 0, 256);
    std::unordered_map<uint16_t, uint8_t> indices;
    for (int i = 1; i < count; ++i) indices.emplace(uint16_t(palette.rgb555[i] & 0x7fff), uint8_t(i));
    out.pixels.resize(size_t(w) * h);
    for (size_t i = 0; i < out.pixels.size(); ++i) {
        if (rgba[i * 4 + 3] < 128) { out.pixels[i] = 0; continue; }
        uint32_t color = 0xff000000u | (uint32_t(rgba[i * 4]) << 16) |
                         (uint32_t(rgba[i * 4 + 1]) << 8) | rgba[i * 4 + 2];
        auto found = indices.find(bdd_core_argb_to_rgb555(color));
        if (found == indices.end()) {
            out.error = "Replacement contains an opaque color that is not in the selected palette.";
            out.pixels.clear(); return out;
        }
        out.pixels[i] = found->second;
    }
    out.ready = true; return out;
}
ReplacementPixels prepare_indexed_tga_replacement(const std::vector<uint8_t> &b,
                                                   const BddCorePalette &palette) {
    ReplacementPixels out;
    if (b.size() < 18 || b[1] != 1 || b[2] != 1 || (b[7] != 15 && b[7] != 16) || b[16] != 8) {
        out.error = "TGA must be an uncompressed 8-bit indexed image with a 15/16-bit palette."; return out;
    }
    int id = b[0], first = b[3] | (b[4] << 8), colors = b[5] | (b[6] << 8);
    int w = b[12] | (b[13] << 8), h = b[14] | (b[15] << 8);
    out.width = w; out.height = h;
    if (!dimensions(w, h, out.error) || first != 0 || colors < 1 || colors > 256) {
        if (out.error.empty()) out.error = "TGA palette must begin at index zero and contain 1-256 colors."; return out;
    }
    size_t palette_at = 18 + size_t(id), pixels_at = palette_at + size_t(colors) * 2, count = size_t(w) * h;
    if (pixels_at > b.size() || count > b.size() - pixels_at) { out.error = "TGA file is truncated."; return out; }
    int target_count = std::clamp(palette.count, 0, 256);
    out.pixels.assign(b.begin() + pixels_at, b.begin() + pixels_at + count);
    bool top = (b[17] & 0x20) != 0;
    if (!top) for (int y = 0; y < h / 2; ++y)
        for (int x = 0; x < w; ++x) std::swap(out.pixels[size_t(y) * w + x], out.pixels[size_t(h - 1 - y) * w + x]);
    for (uint8_t index : out.pixels) {
        if (index >= colors || index >= target_count) { out.error = "TGA uses an index outside the selected palette."; out.pixels.clear(); return out; }
        uint16_t word = uint16_t(b[palette_at + size_t(index) * 2] | (b[palette_at + size_t(index) * 2 + 1] << 8));
        if ((word & 0x7fff) != (palette.rgb555[index] & 0x7fff)) {
            out.error = "TGA palette colors do not match the selected document palette."; out.pixels.clear(); return out;
        }
    }
    out.ready = true; return out;
}
} // namespace studio
