#include "Core/studio_palette_usage.h"
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        BddCoreImage image{}; image.w = 4; image.h = 2;
        image.pix = {0, 1, 1, 17, 0, 3, 17, 3};
        auto usage = analyze_palette_usage(image);
        require(usage.transparent_pixels == 2 && usage.opaque_pixels == 6, "Pixel totals are wrong");
        require(usage.used_colors == 3 && usage.max_index == 17, "Index totals are wrong");
        require(usage.current_bpp == 5 && usage.compact_bpp == 2, "BPP widths are wrong");
        require(usage.blocks_4bpp == std::vector<int>{17}, "4 BPP blocker is wrong");
        require(usage.blocks_2bpp == std::vector<int>{17}, "2 BPP blockers are wrong");
        require(usage.pixels[1] == 2 && usage.pixels[3] == 2 && usage.pixels[17] == 2, "Usage counts are wrong");
        require(usage.raw_saving_bytes == 3, "Raw savings estimate is wrong");
        BddCoreImage dense{}; dense.w = 4; dense.h = 1; dense.pix = {0, 1, 2, 3};
        auto minimal = analyze_palette_usage(dense);
        require(minimal.current_bpp == 2 && minimal.compact_bpp == 2 && minimal.raw_saving_bytes == 0,
                "Dense palette reported false savings");
        std::cout << "Palette usage counts, BPP blockers and dense-remap estimate passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
