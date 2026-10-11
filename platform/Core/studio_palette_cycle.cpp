#include "Core/studio_palette_cycle.h"
#include <algorithm>

namespace studio {

bool palette_cycle_preview(const BddCorePalette &source, int first, int last,
                           uint64_t steps, BddCorePalette &out, std::string &error) {
    error.clear();
    if (source.count < 1 || source.count > 256 || first < 1 || last < first ||
        last >= source.count) {
        error = "Palette cycle must use opaque entries within this palette.";
        return false;
    }
    out = source;
    const int length = last - first + 1;
    steps %= (uint64_t)length;
    if (!steps) return true;
    std::rotate(out.rgb555 + first, out.rgb555 + last + 1 - steps, out.rgb555 + last + 1);
    std::rotate(out.argb + first, out.argb + last + 1 - steps, out.argb + last + 1);
    return true;
}

} // namespace studio
