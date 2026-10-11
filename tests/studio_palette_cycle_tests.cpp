#include "Core/studio_palette_cycle.h"
#include <iostream>
#include <stdexcept>

using namespace studio;
void need(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        BddCorePalette p{}; p.count = 5;
        for (int i = 0; i < p.count; ++i) { p.rgb555[i] = (uint16_t)i; p.argb[i] = 100 + i; }
        BddCorePalette out{}; std::string error;
        need(palette_cycle_preview(p, 1, 4, 1, out, error), error.c_str());
        need(out.rgb555[0] == 0 && out.rgb555[1] == 4 && out.rgb555[2] == 1 &&
             out.argb[4] == 103 && p.rgb555[1] == 1, "Cycle direction or source immutability changed");
        need(palette_cycle_preview(p, 1, 4, 5, out, error) && out.rgb555[1] == 4,
             "Cycle wrapping changed");
        need(!palette_cycle_preview(p, 0, 3, 1, out, error) &&
             !palette_cycle_preview(p, 2, 5, 1, out, error), "Unsafe cycle range accepted");
        std::cout << "Palette cycle range, wrap and source isolation passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
