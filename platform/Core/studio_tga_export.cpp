#include "Core/studio_tga_export.h"
#include <algorithm>

namespace studio {
IndexedTgaExport encode_indexed_tga(const BddCoreImage &image, const BddCorePalette &palette) {
    IndexedTgaExport out;
    if (image.w <= 0 || image.h <= 0 || image.w > 65535 || image.h > 65535) {
        out.error = "Indexed TGA dimensions must be between 1 and 65535 pixels."; return out;
    }
    int colors = std::clamp(palette.count, 0, 256);
    if (colors < 1) { out.error = "The selected palette has no colors."; return out; }
    size_t pixels = size_t(image.w) * image.h;
    if (image.pix.size() < pixels) { out.error = "The selected artwork has incomplete pixel data."; return out; }
    for (size_t i = 0; i < pixels; ++i) if (image.pix[i] >= colors) {
        out.error = "Artwork uses a color outside the selected palette."; return out;
    }
    out.bytes.assign(18 + size_t(colors) * 2 + pixels, 0);
    auto *h = out.bytes.data();
    h[1] = 1; // color map present
    h[2] = 1; // uncompressed color-mapped image
    h[5] = uint8_t(colors); h[6] = uint8_t(colors >> 8); h[7] = 16;
    h[12] = uint8_t(image.w); h[13] = uint8_t(image.w >> 8);
    h[14] = uint8_t(image.h); h[15] = uint8_t(image.h >> 8);
    h[16] = 8; h[17] = 0x20; // 8-bit indices, top-left origin
    size_t at = 18;
    for (int i = 0; i < colors; ++i) {
        h[at++] = uint8_t(palette.rgb555[i]); h[at++] = uint8_t(palette.rgb555[i] >> 8);
    }
    std::copy_n(image.pix.begin(), pixels, out.bytes.begin() + at);
    out.ready = true; return out;
}
} // namespace studio
