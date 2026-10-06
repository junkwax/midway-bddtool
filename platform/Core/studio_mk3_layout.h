#pragma once
#include "Core/studio_document.h"

namespace studio {
struct Mk3Layout {
    std::vector<Plane> planes;
    int start_x = 0, start_y = 0, ground = 0;
    uint64_t revision = 0;
    std::shared_ptr<const AssetBank> assets;
    std::string source, label, error, report;
    // Source positions retained for a read-only floor preview from the same definition.
    size_t floor_line = 0;
    std::string display_list;
    std::vector<double> scroll_rates;
    bool valid() const { return error.empty() && !planes.empty(); }
};
// Read-only MK3 MKBT.ASM import. No implicit search of unrelated checkouts.
Mk3Layout read_mk3_layout(const Document &document, const std::string &path);
} // namespace studio
