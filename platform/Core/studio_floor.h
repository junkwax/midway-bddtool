#pragma once
#include "Core/studio_animation.h"

namespace studio {
// External floor reference; never part of Document, save, export or ROM budgets.
struct FloorPreview {
    AnimationPreview layout; // Shares display-list placement and immutable artwork ownership.
    std::string label, source, notice;
    int screen_y = 0;
    bool ready() const { return layout.artwork.assets && !layout.artwork.assets->data.images.empty(); }
    Rect rect(const State &stage, Point camera) const;
};
FloorPreview load_floor_preview(const Document &document, const std::string &game_root);
} // namespace studio
