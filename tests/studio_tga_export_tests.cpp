#include "Core/studio_tga_export.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        BddCoreImage image{}; image.idx = 42; image.w = 3; image.h = 2; image.pix = {0, 1, 2, 2, 1, 0};
        BddCorePalette palette{}; palette.count = 3;
        palette.rgb555[0] = 0; palette.rgb555[1] = 0x001f; palette.rgb555[2] = 0xfc00;
        auto encoded = encode_indexed_tga(image, palette); require(encoded.ready, "Valid indexed TGA failed");
        const auto &b = encoded.bytes;
        require(b.size() == 18 + 6 + image.pix.size(), "TGA size is wrong");
        require(b[1] == 1 && b[2] == 1 && b[7] == 16 && b[16] == 8 && b[17] == 0x20, "TGA format header is wrong");
        require((b[12] | (b[13] << 8)) == image.w && (b[14] | (b[15] << 8)) == image.h, "TGA dimensions changed");
        require(b[18] == 0 && b[19] == 0 && b[20] == 0x1f && b[21] == 0, "RGB555 palette changed");
        require(b[22] == 0 && b[23] == 0xfc, "RGB555 high bit changed");
        require(std::equal(image.pix.begin(), image.pix.end(), b.begin() + 24), "Palette indices changed");
        image.pix[0] = 3; require(!encode_indexed_tga(image, palette).ready, "Out-of-palette index accepted");
        image.pix.pop_back(); require(!encode_indexed_tga(image, palette).ready, "Truncated pixels accepted");
        std::cout << "Indexed TGA header, RGB555 words, pixels and validation passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
