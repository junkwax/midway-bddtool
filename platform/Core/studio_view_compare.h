#pragma once

#include "Core/studio_document.h"
#include <cstdint>
#include <vector>

namespace studio {

constexpr int kViewWidth = 400, kViewHeight = 254;

struct RgbaView {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // Zero alpha where no artwork covers the pixel.
};

// Renders the resolved scene as the camera sees it, using the same layer parallax as the editor.
RgbaView render_camera_view(const State &state, Point camera, int width = kViewWidth,
                            int height = kViewHeight);

// Renders one indexed image with a palette; index zero stays transparent.
RgbaView render_indexed_image(const BddCoreImage &image, const BddCorePalette &palette);

// The image dims unchanged pixels to gray and highlights changed ones.
struct ViewDifference : RgbaView {
    uint64_t changed = 0, compared = 0;
    int left = 0, top = 0, right = -1, bottom = -1; // Inclusive bounds of changed pixels.
    bool any() const { return changed != 0; }
};

// Compares two views aligned at their top-left corners. Pixels outside the smaller view count
// as transparent, so a replacement with new dimensions reports the gained or lost area.
ViewDifference diff_views(const RgbaView &before, const RgbaView &after);

} // namespace studio
