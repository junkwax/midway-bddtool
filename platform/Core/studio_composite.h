#pragma once

#include "Core/studio_document.h"
#include <string>
#include <vector>

namespace studio {

enum class CompositeScope { Selection, Layer, Stage };

struct CompositeRequest {
    CompositeScope scope = CompositeScope::Stage;
    std::vector<ObjectId> selection;
    int plane = -1;
    bool include_backdrop = false;
};

struct CompositeExport {
    bool ready = false;
    int width = 0, height = 0;
    int origin_x = 0, origin_y = 0;
    std::vector<uint8_t> rgba;
    std::string metadata;
    std::string error;
};

// Renders the visible, resolved authoring scene without changing the document.
CompositeExport render_composite(const State &state, const CompositeRequest &request);

} // namespace studio
