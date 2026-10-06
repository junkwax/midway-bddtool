#include "Core/studio_palette_adjust.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() {
    try {
        BddCorePalette source{}; std::strcpy(source.name, "TEST"); source.count = 4;
        source.rgb555[0] = 0; source.argb[0] = 0;
        source.rgb555[1] = bdd_core_argb_to_rgb555(0xffff0000);
        source.rgb555[2] = uint16_t(0x8000 | bdd_core_argb_to_rgb555(0xff4080c0));
        source.rgb555[3] = bdd_core_argb_to_rgb555(0xffc0c0c0);
        for (int i = 1; i < source.count; ++i) source.argb[i] = bdd_core_rgb555_to_argb(source.rgb555[i]);
        auto identity = adjust_palette(source, {});
        require(std::memcmp(source.rgb555, identity.rgb555, sizeof source.rgb555) == 0, "Identity changed RGB555 colors");
        PaletteAdjustment mono; mono.saturation = 0;
        auto gray = adjust_palette(source, mono);
        uint32_t c = gray.argb[1]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        require(std::abs(r - g) <= 8 && std::abs(g - b) <= 8, "Zero saturation did not produce RGB555 gray");
        require(gray.rgb555[0] == source.rgb555[0] && gray.argb[0] == source.argb[0], "Transparent index changed");
        PaletteAdjustment bright; bright.brightness = 1;
        auto white = adjust_palette(source, bright);
        require((white.rgb555[1] & 0x7fff) == 0x7fff, "Brightness did not clamp to white");
        require((white.rgb555[2] & 0x8000) != 0, "Palette high bit was lost");
        require(source.rgb555[1] != white.rgb555[1], "Source palette was mutated");
        BddCorePalette target = source; target.count = 3;
        target.rgb555[1] = 0x03e0; target.rgb555[2] = 0x001f;
        auto zero_blend = blend_palette(source, target, 0);
        auto full_blend = blend_palette(source, target, 1);
        require(zero_blend.rgb555[1] == source.rgb555[1], "Zero blend changed source");
        require((full_blend.rgb555[1] & 0x7fff) == target.rgb555[1], "Full blend missed target color");
        require((full_blend.rgb555[2] & 0x8000) != 0, "Blend lost source high bit");
        require(full_blend.count == source.count && full_blend.rgb555[3] == source.rgb555[3], "Blend changed source shape or unmatched color");
        auto half_blend = blend_palette(source, target, .5f);
        require(half_blend.rgb555[1] != source.rgb555[1] && half_blend.rgb555[1] != target.rgb555[1], "Half blend did not interpolate");
        std::cout << "RGB555 palette identity, saturation, brightness, transparency and flag preservation passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
