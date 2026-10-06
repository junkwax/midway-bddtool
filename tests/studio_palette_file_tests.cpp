#include "Core/studio_palette_file.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        BddCorePalette source{}; std::strcpy(source.name, "CAVE WATER"); source.count = 4;
        source.rgb555[0] = 0; source.rgb555[1] = 0x001f; source.rgb555[2] = 0x83e0; source.rgb555[3] = 0x7c00;
        auto text = encode_palette_file(source); BddCorePalette decoded{}; std::string error;
        require(decode_palette_file(text, decoded, error), error.c_str());
        require(std::string(decoded.name) == source.name && decoded.count == source.count, "Palette identity changed");
        for (int i = 0; i < source.count; ++i) require(decoded.rgb555[i] == source.rgb555[i], "RGB555 word changed");
        require(decoded.argb[0] == 0 && decoded.argb[1] == bdd_core_rgb555_to_argb(0x001f), "ARGB preview was not rebuilt");
        require(!decode_palette_file("JASC-PAL\n0100\n", decoded, error), "Foreign format accepted");
        auto broken = text; broken.replace(broken.find("001F"), 4, "ZZZZ");
        require(!decode_palette_file(broken, decoded, error), "Invalid color accepted");
        require(!decode_palette_file(text + "extra\n", decoded, error), "Trailing data accepted");
        std::cout << "RGB555 palette file identity and malformed-input validation passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
