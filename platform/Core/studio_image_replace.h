#pragma once
#include "Core/bdd_core.h"
#include <string>
#include <vector>

namespace studio {
struct ReplacementPixels {
    bool ready = false;
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    std::string error;
};
ReplacementPixels prepare_rgba_replacement(int width, int height, const uint8_t *rgba,
                                           const BddCorePalette &palette);
ReplacementPixels prepare_indexed_tga_replacement(const std::vector<uint8_t> &bytes,
                                                   const BddCorePalette &palette);
} // namespace studio
