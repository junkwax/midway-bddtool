#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace studio {
struct PackedImageFingerprint {
    int width = 0, height = 0;
    uint64_t pixels = 0;
};
struct PackedStageArtwork {
    std::string stage, table;
    uint64_t source = 0;
    std::vector<PackedImageFingerprint> images;
};
struct PackedArtwork {
    bool valid = false;
    std::string error;
    std::vector<PackedStageArtwork> stages;
};
struct ArtworkComparison {
    bool available = false;
    size_t unchanged_images = 0, regressions = 0, review_stages = 0;
    std::string report;
};
// MK7 static backgrounds only. Uses the verified IRW payload, never executes game scripts.
// Evidence files must be re-read by the receipt owner to detect concurrent builds.
PackedArtwork capture_packed_artwork(const std::string &root, const std::string &payload,
                                     uint64_t irw_base,
                                     std::vector<std::pair<std::string, std::string>> &evidence);
void validate_packed_artwork(const PackedArtwork &artwork);
ArtworkComparison compare_packed_artwork(const PackedArtwork &before, const PackedArtwork &after);
} // namespace studio
