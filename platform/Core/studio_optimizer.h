#pragma once
#include "Core/studio_document.h"
#include <atomic>
#include <map>

namespace studio {
struct OptimizeOptions {
    bool deep = false, compact_palettes = true;
    bool palette_reuse_only = false; // Canonical whole-image reuse; no extra placements.
    int max_pieces = 8, max_added_objects = 64, max_palettes = 45;
    // 0: prioritize bytes, 1: balanced, 2: prioritize fewer placements.
    int policy = 1;
    int source_image = -1; // -1: whole stage; otherwise restrict changes to this image.
};
struct OptimizeProgress {
    std::atomic<bool> cancel{false};
    std::atomic<int> done{0}, total{0};
};
struct OptimizeBudget {
    uint64_t video_bits = 0, raw_bits = 0, table_bytes = 0, palette_bytes = 0;
    int images = 0, palettes = 0, objects = 0;
};
struct OptimizePiece {
    int x = 0, y = 0, w = 0, h = 0, image = 0;
    bool flip_x = false, flip_y = false;
    int role = 0; // 0: region, 1: shared base, 2: unique detail.
    std::map<int, int> palettes;
};
struct OptimizeChange {
    int source_image = 0, uses = 0;
    uint64_t before_bits = 0, added_bits = 0;
    bool reindexed = false;
    bool residual = false;
    int equivalent_indices = 0;
    std::vector<OptimizePiece> pieces;
};
struct OptimizationPlan {
    State before, after;
    OptimizeOptions options;
    OptimizeBudget baseline, proposed;
    std::vector<OptimizeChange> changes;
    std::vector<std::string> notes;
    std::string error;
    bool verified = false, cancelled = false;
};
struct PaletteMerge {
    int source = -1, target = -1;
    std::string source_name, target_name;
    int placements = 0, defaults = 0;
};
struct PaletteConsolidation {
    State before, after;
    OptimizeBudget baseline, proposed;
    std::vector<int> remap;
    std::vector<PaletteMerge> merges;
    std::string error;
    bool verified = false;
};
OptimizeBudget optimization_budget(const State &state);
uint64_t optimization_image_bits(const BddCoreImage &image);
OptimizationPlan find_lossless_savings(const Document &document, const OptimizeOptions &options,
                                       OptimizeProgress *progress = nullptr);
OptimizationPlan find_shared_savings(const Document &document, const OptimizeOptions &options,
                                     OptimizeProgress *progress = nullptr);
bool verify_optimization(const OptimizationPlan &plan, std::string &error);
std::string optimization_report(const OptimizationPlan &plan);
struct OptimizeRegion {
    int image = 0, change = 0,
        kinds = 0; // 1 blank trim, 2 lower BPP, 4 shared, 8 mirror, 16 detail.
    Rect rect;
    int before_bpp = 0, after_bpp = 0;
};
std::vector<OptimizeRegion> optimization_regions(const OptimizationPlan &plan);
PaletteConsolidation find_exact_palette_consolidation(const Document &document);
bool verify_palette_consolidation(const PaletteConsolidation &plan, std::string &error);
std::string palette_consolidation_report(const PaletteConsolidation &plan);

enum class PatternMode { RepeatX, RepeatY, MirrorX, MirrorY };
struct PatternOptions {
    int image = -1;
    int plane = -1; // -1: one image; otherwise compose this layer into a source strip.
    PatternMode mode = PatternMode::RepeatX;
    int offset = 0, span = 16;
    bool alternate_flip = false, use_far_side = false;
};
// Deliberate artwork edits are separate from verified lossless representations.
struct PatternPlan {
    State before, source;
    PatternOptions options;
    OptimizationPlan packing;
    uint64_t changed_pixels = 0, silhouette_pixels = 0;
    int uses = 0;
    bool valid = false;
    std::string error;
};
PatternPlan preview_pattern(const Document &document, const PatternOptions &options);
PatternPlan suggest_pattern(const Document &document, const PatternOptions &options);
struct PatternSearch {
    State before;
    OptimizeBudget baseline;
    PatternOptions options;
    std::vector<PatternPlan> proposals;
    int sampled = 0, packed = 0;
    bool cancelled = false;
    std::string error;
};
// Bounded search over sizes, offsets, alternating flips and mirrored sides on the chosen axis.
// Results are deliberately changed artwork; each result still needs visual review.
PatternSearch discover_patterns(const Document &document, const PatternOptions &options,
                                OptimizeProgress *progress = nullptr);
bool verify_pattern(const PatternPlan &plan, std::string &error);
State pattern_source(const State &state, int plane, std::string &error);
} // namespace studio
