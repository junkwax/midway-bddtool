#include "Core/studio_palette_file.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace studio {
std::string encode_palette_file(const BddCorePalette &palette) {
    std::ostringstream out;
    int count = std::clamp(palette.count, 1, 256);
    out << "bddtool-rgb555-palette 1\nname " << std::quoted(std::string(palette.name))
        << "\ncount " << count << "\ncolors\n" << std::hex << std::uppercase << std::setfill('0');
    for (int i = 0; i < count; ++i) out << std::setw(4) << unsigned(palette.rgb555[i]) << '\n';
    return out.str();
}
bool decode_palette_file(const std::string &text, BddCorePalette &palette, std::string &error) {
    std::istringstream in(text); std::string token, name; int version = 0, count = 0;
    if (!(in >> token >> version) || token != "bddtool-rgb555-palette" || version != 1) {
        error = "This is not a supported bddtool RGB555 palette file."; return false;
    }
    if (!(in >> token) || token != "name" || !(in >> std::quoted(name)) || name.empty() || name.size() > 63) {
        error = "Palette name must contain 1-63 characters."; return false;
    }
    if (!(in >> token >> count) || token != "count" || count < 1 || count > 256) {
        error = "Palette color count must be between 1 and 256."; return false;
    }
    if (!(in >> token) || token != "colors") { error = "Palette colors section is missing."; return false; }
    BddCorePalette parsed{}; std::snprintf(parsed.name, sizeof parsed.name, "%s", name.c_str()); parsed.count = count;
    for (int i = 0; i < count; ++i) {
        std::string word;
        if (!(in >> word) || word.size() != 4 || !std::all_of(word.begin(), word.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            error = "Palette must contain exactly one four-digit RGB555 word per color."; return false;
        }
        unsigned value = 0; std::istringstream hex(word); hex >> std::hex >> value;
        parsed.rgb555[i] = uint16_t(value); parsed.argb[i] = i ? bdd_core_rgb555_to_argb(parsed.rgb555[i]) : 0;
    }
    if (in >> token) { error = "Palette file contains unexpected data after its colors."; return false; }
    palette = parsed; error.clear(); return true;
}
} // namespace studio
