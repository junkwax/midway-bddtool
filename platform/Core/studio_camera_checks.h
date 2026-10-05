#pragma once
#include "Core/studio_optimizer.h"

namespace studio {
struct CameraCheckOptions {
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    int step = 16, reserve = BDD_CORE_MK2_DISPLAY_OBJECT_RUNTIME_RESERVE;
    bool operator==(const CameraCheckOptions &other) const;
};
struct CameraCheck {
    State before;
    CameraCheckOptions options;
    int samples = 0, peak_objects = 0, peak_palettes = 0, unresolved = 0;
    double widest_gap = 0;
    Point object_camera, palette_camera, gap_camera;
    std::vector<Issue> issues;
    std::string error;
    bool complete = false, cancelled = false;
};
// Samples projected static bounds in a 400x254 viewport. Includes editor-hidden
// placements, which are exported. This is not pixel coverage or a runtime proof.
CameraCheck check_camera_range(const State &state, const CameraCheckOptions &options,
                               OptimizeProgress *progress = nullptr);
bool camera_check_current(const CameraCheck &check, const State &state,
                          const CameraCheckOptions &options);
std::string camera_check_report(const CameraCheck &check);
} // namespace studio
