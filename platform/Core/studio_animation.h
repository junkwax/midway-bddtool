#pragma once
#include "Core/studio_document.h"

namespace studio {
struct AnimationFrame {
    int image = 0, palette = 0, anchor_x = 0, anchor_y = 0;
    std::string label;
};
// Preview-only assets and runtime placements never enter Document or its save/export path.
struct AnimationPreview {
    State artwork;
    std::vector<AnimationFrame> frames;
    std::vector<int> sequence;
    std::vector<Point> anchors;
    std::vector<std::string> background_modules;
    size_t backgrounds_before = 0;
    int frame_ticks = 5;
    double scroll = 1;
    std::string source, notice;
    bool ready() const { return !sequence.empty() && !anchors.empty(); }
    size_t frame_at(double seconds) const;
    Rect rect(size_t actor, size_t step, Point camera) const;
    double draw_rank(const Document &document) const;
};
// Recognizes the Forest tree animator in the selected checkout. Unsupported sources
// produce an explanation and no speculative animation. All reads are local and read-only.
AnimationPreview load_animation_preview(const Document &document, const std::string &game_root);
} // namespace studio
