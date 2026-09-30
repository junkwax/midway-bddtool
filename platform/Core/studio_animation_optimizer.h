#pragma once
#include "Core/studio_animation.h"
#include "Core/studio_optimizer.h"

namespace studio {
struct AnimationPiece {
    int image = 0, palette = 0, x = 0, y = 0;
    bool flip_x = false, flip_y = false, shared_base = false;
};
struct AnimationAlternative {
    std::string name;
    State artwork;
    std::vector<std::vector<AnimationPiece>> frames;
    uint64_t video_bits = 0, palette_bytes = 0, recipe_bytes = 0;
    int max_pieces = 0, reused_pieces = 0, flipped_pieces = 0;
    bool verified = false, palette_remapped = false;
    std::vector<std::vector<size_t>> shared_groups;
    int search_candidates = 0;
    bool search_limited = false, horizontal_bands = false;
    std::vector<std::vector<int>> band_cuts; // Actor-relative Y boundaries per frame.
};
struct AnimationAnalysis {
    AnimationPreview source;
    uint64_t baseline_bits = 0, baseline_palette_bytes = 0;
    std::vector<AnimationAlternative> alternatives;
    std::string error;
    bool cancelled = false;
};
// Read-only representations of the decoded source animation. These are not game export plans.
AnimationAnalysis analyze_animation(const AnimationPreview &source,
                                    OptimizeProgress *progress = nullptr);
bool verify_animation_alternative(const AnimationPreview &source,
                                  const AnimationAlternative &alternative, std::string &error);
std::string animation_analysis_report(const AnimationAnalysis &analysis);
} // namespace studio
