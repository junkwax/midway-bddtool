#pragma once
#include "Core/studio_document.h"
#include <string>
#include <vector>

namespace studio {
struct SpriteSheetRequest {
    bool placed_only = false;
    int columns = 8;
    int padding = 2;
};
struct SpriteSheetExport {
    bool ready = false;
    int width = 0, height = 0, cell_width = 0, cell_height = 0;
    std::vector<uint8_t> rgba;
    std::string metadata, error;
};
SpriteSheetExport render_sprite_sheet(const State &state, const SpriteSheetRequest &request);
} // namespace studio
