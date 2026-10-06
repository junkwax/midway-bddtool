#include "Core/studio_palette_adjust.h"
#include <algorithm>
#include <cmath>

namespace studio {
BddCorePalette blend_palette(const BddCorePalette &source, const BddCorePalette &target, float amount) {
    BddCorePalette out = source; amount = std::clamp(amount, 0.0f, 1.0f);
    int count = std::min(std::clamp(source.count, 0, 256), std::clamp(target.count, 0, 256));
    for (int i = 1; i < count; ++i) {
        uint16_t a = source.rgb555[i], b = target.rgb555[i];
        auto mix = [&](int shift) {
            float av = float((a >> shift) & 31), bv = float((b >> shift) & 31);
            return std::clamp((int)std::lround(av + (bv - av) * amount), 0, 31);
        };
        out.rgb555[i] = uint16_t((a & 0x8000) | mix(0) | (mix(5) << 5) | (mix(10) << 10));
        out.argb[i] = bdd_core_rgb555_to_argb(out.rgb555[i]);
    }
    return out;
}
BddCorePalette adjust_palette(const BddCorePalette &source, const PaletteAdjustment &adjustment) {
    BddCorePalette out = source;
    const float brightness = std::clamp(adjustment.brightness, -1.0f, 1.0f);
    const float contrast = std::clamp(adjustment.contrast, 0.0f, 2.0f);
    const float saturation = std::clamp(adjustment.saturation, 0.0f, 2.0f);
    const int count = std::clamp(source.count, 0, 256);
    for (int i = 1; i < count; ++i) {
        uint32_t color = bdd_core_rgb555_to_argb(source.rgb555[i]);
        float r = float((color >> 16) & 255) / 255.0f;
        float g = float((color >> 8) & 255) / 255.0f;
        float b = float(color & 255) / 255.0f;
        float luma = r * .2126f + g * .7152f + b * .0722f;
        r = luma + (r - luma) * saturation;
        g = luma + (g - luma) * saturation;
        b = luma + (b - luma) * saturation;
        r = (r - .5f) * contrast + .5f + brightness;
        g = (g - .5f) * contrast + .5f + brightness;
        b = (b - .5f) * contrast + .5f + brightness;
        auto byte = [](float value) { return (uint32_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f); };
        uint32_t argb = 0xff000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
        out.rgb555[i] = uint16_t((source.rgb555[i] & 0x8000) | bdd_core_argb_to_rgb555(argb));
        out.argb[i] = bdd_core_rgb555_to_argb(out.rgb555[i]);
    }
    return out;
}
} // namespace studio
