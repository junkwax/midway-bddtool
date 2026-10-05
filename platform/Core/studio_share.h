#pragma once
#include "Core/studio_document.h"

namespace studio {
inline constexpr const char *project_url = "https://github.com/junkwax/midway-bddtool";
inline constexpr const char *wiki_url = "https://github.com/junkwax/midway-bddtool/wiki";
struct ShareCredits {
    std::string author, description, sources, license, tested, page_name;
    bool operator==(const ShareCredits &o) const {
        return author == o.author && description == o.description && sources == o.sources && license == o.license && tested == o.tested && page_name == o.page_name;
    }
};
struct ShareBundle {
    std::string folder, zip, page, markdown, submission_url, error;
    uint64_t revision = 0;
    std::shared_ptr<const AssetBank> assets;
    bool ready = false;
    ShareCredits credits;
};
// Writes a new local directory only. Snapshot Save preserves the caller's save
// point/history. Never opens a browser, submits an issue, or touches a checkout.
ShareBundle build_stage_share(const Document &document, const ShareCredits &credits,
                              const std::string &new_folder);
} // namespace studio
