#pragma once
#include "Core/studio_document.h"

namespace studio {
struct GamePlaneChange {
    std::string module;
    int slot = 0, x = 0, y = 0, center_origin = 0;
    double scroll = 1;
};
struct AssemblyExport {
    std::string text, label, report;
    std::vector<GamePlaneChange> planes;
};
// Pure transformation of an existing stage definition; never patches unrelated labels.
bool export_game_assembly(const Document &document, const std::string &assembly,
                          AssemblyExport &result, std::string &error,
                          const std::string &stage_label = {});

struct GameExportFile {
    std::string relative, before, after;
    bool existed = false;
};
struct GameExport {
    std::string root, folder, report, label, requested_label;
    uint64_t revision = 0;
    bool applied = false;
    std::vector<GameExportFile> files;
};
// Preparation only writes a new package directory. Applying verifies both sides before writing.
bool prepare_game_export(const Document &document, const std::string &game_root,
                         const std::string &package_folder, GameExport &result, std::string &error,
                         const std::string &stage_label = {});
bool apply_game_export(GameExport &package, std::string &error);
} // namespace studio
