#pragma once
#include "Core/bdd_core.h"
#include <string>
#include <vector>

namespace studio {
struct IndexedTgaExport {
    bool ready = false;
    std::vector<uint8_t> bytes;
    std::string error;
};
// Encodes the BDD indices and RGB555 words directly as an uncompressed,
// top-origin, color-mapped TGA suitable for lossless round trips.
IndexedTgaExport encode_indexed_tga(const BddCoreImage &image, const BddCorePalette &palette);
} // namespace studio
