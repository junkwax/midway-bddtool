#pragma once
#include "Core/studio_optimizer.h"
#include <vector>

namespace studio {
// STAGE ART BUDGET, measured with generated test stages in a fight on the board-timed
// emulator (mk2-main tools/mame_hw/stagebudget.lua, 2026-11-18). A visible block is
// charged for its full programmed rectangle, including transparent pixels:
//   cost us = max(floor, start + width * height * 0.06)
// Up to about 10.3 ms a frame stays under 1% late frames on both boards; about 11.2 ms is
// where late frames become visible.  Fighters, floor, shadows and HUD are already allowed for.
struct StageBudgetBoard {
    const char *name;
    double floor_us, start_us;
};
// Source BDDs lack final packing information. Use the zero-compressed rate
// for the estimate (the measured raw rate is 0.051).
inline constexpr double kStageArtCompressedUsPerPixel = 0.060, kStageArtRawUsPerPixel = 0.051;
inline constexpr double kStageArtWarnMs = 10.3, kStageArtOverMs = 11.2;
inline constexpr StageBudgetBoard kStageBudgetBoards[2] = {{"T-unit", 62, 12}, {"Wolf", 40, 10}};
inline double stage_block_cost_us(const StageBudgetBoard &board, double width, double height,
                                  bool all_raw = false) {
    double pixel_us = all_raw ? kStageArtRawUsPerPixel : kStageArtCompressedUsPerPixel;
    double cost = board.start_us + width * height * pixel_us;
    return cost > board.floor_us ? cost : board.floor_us;
}
struct StageBudgetPeak {
    double ms = 0;
    Point camera;
    struct Block { int image = -1, width = 0, height = 0; double ms = 0; };
    std::vector<Block> largest;   // dearest visible blocks at that camera
};
struct CameraCheckOptions {
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    int step = 16, reserve = BDD_CORE_MK2_DISPLAY_OBJECT_RUNTIME_RESERVE;
    bool all_raw = false;
    bool operator==(const CameraCheckOptions &other) const;
};
struct CameraCheck {
    State before;
    CameraCheckOptions options;
    int samples = 0, peak_objects = 0, peak_palettes = 0, unresolved = 0;
    double widest_gap = 0;
    Point object_camera, palette_camera, gap_camera;
    StageBudgetPeak budget[2];    // kStageBudgetBoards order
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
