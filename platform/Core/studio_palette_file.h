#pragma once
#include "Core/bdd_core.h"
#include <string>

namespace studio {
std::string encode_palette_file(const BddCorePalette &palette);
bool decode_palette_file(const std::string &text, BddCorePalette &palette, std::string &error);
} // namespace studio
