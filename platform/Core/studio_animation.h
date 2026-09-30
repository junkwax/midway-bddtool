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
    bool manual_sequence = false; // IMG selection order; no runtime driver is inferred.
    bool ready() const { return !sequence.empty() && !anchors.empty(); }
    size_t frame_at(double seconds) const;
    Rect rect(size_t actor, size_t step, Point camera) const;
    double draw_rank(const Document &document) const;
};
// Recognizes the Forest tree animator in the selected checkout. Unsupported sources
// produce an explanation and no speculative animation. All reads are local and read-only.
AnimationPreview load_animation_preview(const Document &document, const std::string &game_root);
struct AnimationLibraryImage {
    std::string label, problem;
    int width = 0, height = 0, palette = 0, anchor_x = 0, anchor_y = 0;
};
struct AnimationLibrary {
    std::string path, error;
    std::vector<AnimationLibraryImage> images;
};
// Directory inspection does not decode pixels. Selected frames are reread and validated
// when loaded; they never enter the stage document or its runtime animation overlay.
AnimationLibrary inspect_animation_library(const std::string &path);
AnimationPreview load_animation_selection(const std::string &path,
                                          const std::vector<std::string> &labels,
                                          int preview_ticks = 5);
} // namespace studio
