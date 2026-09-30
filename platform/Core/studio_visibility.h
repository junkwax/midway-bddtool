#pragma once
#include "Core/studio_optimizer.h"

namespace studio {
struct VisibilityOptions {
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
};
struct VisibilityImage {
    int image = 0;
    // Source-pixel classes: 0 transparent, 1 possibly visible, 2 always outside,
    // 3 permanently covered (or covered/outside across uses), 4 protected/unresolved.
    std::vector<uint8_t> pixels;
    uint64_t outside = 0, covered = 0, retained = 0;
    bool trimmed = false;
};
struct VisibilityPlan {
    State before, after;
    VisibilityOptions options;
    OptimizeBudget baseline, proposed;
    std::vector<VisibilityImage> images;
    std::vector<std::string> notes;
    std::string error;
    bool analyzed = false, verified = false, cancelled = false;
    int changed_images = 0;
};
// Analytic conservative proof over the entire continuous camera rectangle, not frame sampling.
// Occlusion is proved only between static bound layers with equal horizontal parallax.
VisibilityPlan analyze_visibility(const State &state, const VisibilityOptions &options,
                                  OptimizeProgress *progress = nullptr);
bool verify_visibility(const VisibilityPlan &plan, std::string &error);
std::string visibility_report(const VisibilityPlan &plan);
} // namespace studio
