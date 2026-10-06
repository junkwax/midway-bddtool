#pragma once
#include "Core/studio_animation.h"

namespace studio {
// External floor reference; never part of Document, save, export or ROM budgets.
struct FloorPreview {
    AnimationPreview layout; // Shares display-list placement and immutable artwork ownership.
    std::string label, source, notice;
    int screen_y = 0;
    double screen_x = -400;
    double scroll = 1;
    bool mk3 = false;
    std::string palette_source;
    bool ready() const { return layout.artwork.assets && !layout.artwork.assets->data.images.empty(); }
    Rect rect(const State &stage, Point camera) const;
};
FloorPreview load_floor_preview(const Document &document, const std::string &game_root);
FloorPreview load_mk3_floor_preview(const Document &document, const std::string &mkbt_path,
                                  const std::string &asset_directory = "");
struct FloorLibraryEntry {
    std::string label, problem;
    std::vector<std::string> strips;
    int width = 0, height = 0;
};
struct FloorLibrary {
    std::string path, error;
    std::vector<FloorLibraryEntry> entries;
};
struct FloorLibraryDiscovery {
    std::vector<std::string> paths;
    std::string notice;
};
// Only immediate, explicitly supplied folders; no checkout or recursive search.
FloorLibraryDiscovery discover_floor_libraries(const std::vector<std::string> &folders,
                                               const std::string &selected_path = "");
enum class FloorMatch { Unavailable, Exact, DifferentSize, DifferentPixels };
struct FloorComparison {
    FloorMatch match = FloorMatch::Unavailable;
    size_t different_pixels = 0, total_pixels = 0;
};
// Compares decoded colors and transparency, independent of palette indices/labels.
FloorComparison compare_floor_pixels(const FloorPreview &candidate, const FloorPreview &runtime);
// Numbered strips are suggested groups, not proof of runtime use or assembly.
FloorLibrary inspect_floor_library(const std::string &path);
FloorPreview load_floor_library_entry(const FloorLibrary &library, size_t entry,
                                     const FloorPreview &runtime);
} // namespace studio
