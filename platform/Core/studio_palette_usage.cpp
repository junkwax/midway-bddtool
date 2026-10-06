#include "Core/studio_palette_usage.h"
#include <algorithm>

namespace studio {
PaletteUsage analyze_palette_usage(const BddCoreImage &image) {
    PaletteUsage out;
    for (uint8_t index : image.pix) ++out.pixels[index];
    out.transparent_pixels = out.pixels[0];
    for (int i = 1; i < 256; ++i) if (out.pixels[i]) {
        ++out.used_colors; out.opaque_pixels += out.pixels[i]; out.max_index = i;
        if (i >= 16) out.blocks_4bpp.push_back(i);
        if (i >= 4) out.blocks_2bpp.push_back(i);
    }
    out.current_bpp = bdd_core_load2_bpp_for_max_pixel(out.max_index);
    // A dense remap reserves zero for transparency, so N opaque colors need indices 1..N.
    out.compact_bpp = bdd_core_load2_bpp_for_max_pixel(out.used_colors);
    if (out.current_bpp > out.compact_bpp)
        out.raw_saving_bytes = (uint64_t(image.pix.size()) * (out.current_bpp - out.compact_bpp) + 7) / 8;
    return out;
}
} // namespace studio
