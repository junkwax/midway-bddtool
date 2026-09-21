#pragma once
#include "Core/studio_optimizer.h"

namespace studio {
struct ArtAuditEntry {
    bool palette = false;
    int id = 0, placements = 0, defaults = 0, duplicate_of = -1;
    uint64_t estimated_bytes = 0;
    std::string label, status;
    std::vector<std::string> evidence;
};
struct ArtAudit {
    State before;
    std::string root, graph_path, error;
    std::vector<ArtAuditEntry> entries;
    std::vector<std::string> notes;
    int source_files = 0, unplaced_images = 0, unplaced_palettes = 0;
    uint64_t unplaced_video_bytes = 0;
    bool cancelled = false;
};
// Read-only evidence gathering. No classification here authorizes deletion or palette renumbering.
ArtAudit audit_art(const Document &document, const std::string &root,
                   OptimizeProgress *progress = nullptr);
std::string art_audit_report(const ArtAudit &audit);
} // namespace studio
