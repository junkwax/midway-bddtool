#pragma once
#include "Core/bdd_core.h"
#include <array>
#include <cstdint>
#include <vector>

namespace studio {
struct PaletteUsage {
    std::array<uint64_t, 256> pixels{};
    uint64_t transparent_pixels = 0, opaque_pixels = 0, raw_saving_bytes = 0;
    int used_colors = 0, max_index = 0, current_bpp = 1, compact_bpp = 1;
    std::vector<int> blocks_4bpp, blocks_2bpp;
};
PaletteUsage analyze_palette_usage(const BddCoreImage &image);
} // namespace studio
