#pragma once
#include "Core/bdd_core.h"

namespace studio {
struct PaletteAdjustment {
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
};
BddCorePalette adjust_palette(const BddCorePalette &source, const PaletteAdjustment &adjustment);
BddCorePalette blend_palette(const BddCorePalette &source, const BddCorePalette &target, float amount);
} // namespace studio
