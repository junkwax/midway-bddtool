#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Core/studio_optimizer.h"

namespace studio {

struct LibraryAuditRow {
    std::string file, scope, best_mode, unverified_modes, detail;
    bool active = false;
    int64_t baseline_bytes = -1, preserve_saved = -1, best_saved = -1;
    int extra_objects = -1, extra_table_bytes = -1, extra_palette_bytes = -1, palettes_after = -1;
};

struct LibraryAuditReport {
    std::string folder;
    std::vector<LibraryAuditRow> rows;
    int64_t preserve_total = 0, best_total = 0; // Active build relationships only.
    size_t incomplete = 0;
};

// Reads the summary.csv produced by tools/audit_bdd_library.py. This is a
// report viewer only; it never scans, applies, or saves game assets.
bool load_library_audit(const std::string &folder, LibraryAuditReport &out, std::string &error);
// Copies one frozen input and its same-stem companions to a new review folder.
// Returns the copied BDD path. The report and its inputs are never modified.
bool prepare_library_audit_review(const LibraryAuditReport &report, const LibraryAuditRow &row,
                                  const std::string &destination, std::string &bdd_path, std::string &error);
// Recreates the bounded settings used by studio_savings_audit for a report mode.
// engine_mode is 0 for per-image lossless search and 1 for shared-base search.
bool library_audit_optimize_settings(const LibraryAuditRow &row, OptimizeOptions &options,
                                     int &engine_mode, std::string &error);

} // namespace studio
