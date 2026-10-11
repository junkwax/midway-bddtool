#pragma once
#include "Core/bdd_core.h"
#include <cstdint>
#include <string>

namespace studio {

bool palette_cycle_preview(const BddCorePalette &source, int first, int last,
                           uint64_t steps, BddCorePalette &out, std::string &error);

} // namespace studio
