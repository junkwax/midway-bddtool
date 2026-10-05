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
    std::string build_script = "build.py";
    std::vector<GameExportFile> files;
    // Read dependencies reviewed with the package, never installed as output files.
    std::vector<GameExportFile> dependencies;
};
// Preparation only writes a new package directory. Applying verifies both sides before writing.
bool prepare_game_export(const Document &document, const std::string &game_root,
                         const std::string &package_folder, GameExport &result, std::string &error,
                         const std::string &stage_label = {});
bool apply_game_export(GameExport &package, std::string &error);
struct ExportFreshness {
    std::string root, folder, checked_at;
    uint64_t revision = 0;
    bool applied = false, complete = false;
    size_t files_checked = 0;
    std::vector<Issue> issues;
    bool matches() const { return complete && issues.empty(); }
};
// Read-only byte comparisons of the reviewed files/dependencies. Before Apply,
// includes staged outputs and original checkout bytes; after Apply, checks the
// installed bytes and dependencies. Not a ROM/generated-table freshness proof.
ExportFreshness inspect_game_export(const GameExport &package);
bool game_export_context_matches(const GameExport &package, uint64_t revision,
                                 const std::string &root, const std::string &requested_label);
} // namespace studio
