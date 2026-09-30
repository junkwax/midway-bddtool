#pragma once
#include "Core/studio_game_export.h"

namespace studio {
bool seed_cave_runtime(Document &document, const std::string &root, std::string &error);
bool prepare_cave_export(const Document &document, const std::string &root,
                         const std::string &folder, GameExport &result, std::string &error);
} // namespace studio
