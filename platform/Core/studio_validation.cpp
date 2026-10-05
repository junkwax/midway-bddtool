#include "Core/studio_document.h"
#include <algorithm>
#include <map>
#include <set>

namespace studio {
std::vector<Issue> Document::validate() const {
    std::vector<Issue> issues;
    auto add = [&](IssueGroup group, bool error, std::string message, std::string next,
                   ObjectId object = 0, int slot = -1, int palette = -1, int plane = -1) {
        issues.push_back({std::move(message), object, error, group, std::move(next), slot, palette, plane});
    };
    if (!state_.assets) {
        add(IssueGroup::References, true, "Artwork bank is missing", "Reopen the BDD with its companion files.");
        return issues;
    }
    const auto &bank = *state_.assets;
    const auto &data = bank.data;
    std::map<int, int> slots;
    std::map<int, ObjectId> first_use;
    std::map<std::pair<int, int>, ObjectId> palette_uses;
    std::set<int> used_palettes;
    for (size_t i = 0; i < data.images.size(); ++i) {
        const auto &im = data.images[i];
        if (im.idx < 0 || im.idx > 65535 || !slots.emplace(im.idx, (int)i).second)
            add(IssueGroup::References, true, "Image " + std::to_string(im.idx) + ": invalid or duplicate ID",
                "Restore a valid source BDD. IDs must be unique and fit the file's 16-bit field.", 0, (int)i);
    }
    for (const auto &p : state_.objects) {
        auto found = slots.find(p.object.ii);
        int slot = found == slots.end() ? -1 : found->second;
        std::string label = "Placement " + std::to_string(p.id) + ": ";
        if (slot < 0)
            add(IssueGroup::References, true, label + "missing image " + std::to_string(p.object.ii),
                "Restore the source artwork, or replace/remove this placement deliberately.", p.id);
        else {
            first_use.emplace(p.object.ii, p.id);
            palette_uses.emplace(std::make_pair(slot, p.object.fl), p.id);
        }
        if (p.plane < 0 || p.plane >= (int)state_.planes.size())
            add(IssueGroup::References, true, label + "no valid layer assignment",
                "Select the intended layer in the Stage inspector. Save cannot guess ownership.", p.id, slot);
        if (p.object.fl < 0 || p.object.fl >= (int)data.palettes.size())
            add(IssueGroup::References, true, label + "missing palette " + std::to_string(p.object.fl),
                "Choose an existing palette in the Stage inspector or restore the original palette.", p.id, slot);
        else
            used_palettes.insert(p.object.fl);
    }
    for (size_t i = 0; i < data.palettes.size(); ++i)
        if (data.palettes[i].count < 1 || data.palettes[i].count > 256)
            add(IssueGroup::Artwork, true, "Palette " + std::to_string(i) + ": invalid color count",
                "Restore a palette with 1 to 256 colors before editing or exporting.", 0, -1, (int)i);

    for (size_t i = 0; i < data.images.size(); ++i) {
        const auto &im = data.images[i];
        auto use = first_use.find(im.idx);
        ObjectId object = use == first_use.end() ? 0 : use->second;
        std::string label = "Image " + std::to_string(im.idx) + ": ";
        if (i < bank.metadata.size() && bank.metadata[i].label[0])
            label = std::string(bank.metadata[i].label) + " (#" + std::to_string(im.idx) + "): ";
        if (im.w < 1 || im.h < 1 || im.w > 4096 || im.h > 4096 ||
            im.pix.size() != size_t(im.w) * size_t(im.h)) {
            add(IssueGroup::Artwork, true, label + "invalid dimensions or pixel storage",
                "Reimport valid artwork. Save will refuse malformed pixel data.", object, (int)i);
            continue; // Never pass a malformed buffer to the core pixel readers.
        }
        int max_pixel = bdd_core_image_max_pixel(im.pix.data(), im.w, im.h);
        int default_palette = i < bank.default_palettes.size() ? bank.default_palettes[i] : -1;
        if (default_palette < 0 || default_palette >= (int)data.palettes.size()) {
            // Imported BDDs may have no palette at all; this still needs review in Assets.
            add(IssueGroup::References, true, label + "missing default palette",
                "Restore the source palette or import the artwork with its palette.", object, (int)i);
        } else {
            // Check both the Assets preview and every distinct placed palette variant.
            palette_uses.emplace(std::make_pair((int)i, default_palette), object);
        }
        if (im.w > BDD_CORE_MK2_RUNTIME_WIDEST_BLOCK)
            add(IssueGroup::Load2, object != 0, label + "width exceeds MK2's 250-pixel scan window",
                "Review trim/split proposals in Optimize. A wide block may disappear during game scrolling.", object, (int)i);
        if (im.w % BDD_CORE_MK2_BG_WIDTH_ALIGN)
            add(IssueGroup::Load2, object != 0, label + "width is not a multiple of 4",
                "Prepare artwork with a width divisible by 4 before game export; LOAD2 can shear tight rows. Save does not pad pixels.", object, (int)i);
        auto bytes = bdd_core_load2_estimated_block_bytes(im.pix.data(), im.w, im.h, nullptr);
        if (bytes > BDD_CORE_MK2_LOAD2_MAX_DATA_BYTES)
            add(IssueGroup::Load2, false, label + "uncompressed estimate " + std::to_string(bytes) + " B exceeds LOAD2's 65500 B block budget",
                "Review subdivision/BPP savings in Optimize and check the actual packed build size; this estimate excludes compression.", object, (int)i);
        for (auto it = palette_uses.lower_bound({(int)i, -1}); it != palette_uses.end() && it->first.first == (int)i; ++it) {
            int pal = it->first.second;
            if (pal < 0 || pal >= (int)data.palettes.size()) continue;
            int count = data.palettes[pal].count;
            if (count >= 1 && count <= 256 && max_pixel >= count)
                add(IssueGroup::Artwork, true, label + "pixel index " + std::to_string(max_pixel) +
                    " exceeds palette " + std::to_string(pal) + " (" + std::to_string(count) + " colors)",
                    "Inspect this palette variant and restore the intended colors or indices. No automatic recoloring is applied.",
                    it->second, (int)i, pal);
        }
    }
    auto cap = [&](size_t count, int limit, const char *name) {
        if (count > (size_t)limit)
            add(IssueGroup::Load2, true, std::string(name) + ": " + std::to_string(count) + " exceeds LOAD2's " + std::to_string(limit) + " limit",
                "Review references and export requirements before reducing the source. Unplaced artwork may still have external uses.");
    };
    cap(data.images.size(), BDD_CORE_MK2_LOAD2_MAX_IMAGE_HEADERS, "Image headers");
    cap(data.palettes.size(), BDD_CORE_MK2_LOAD2_MAX_STAGE_PALETTES, "Stage palettes");
    if (state_.has_bdb) cap(state_.planes.size(), BDD_CORE_MK2_LOAD2_MAX_MODULES, "Layers/modules");
    if (used_palettes.size() > BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS)
        add(IssueGroup::Load2, false, "Static placements use " + std::to_string(used_palettes.size()) + " palettes; MK2 has 35 dynamic background slots",
            "Review palette sharing and in-game allocation. This excludes animation, actors and runtime allocation order.");
    if (state_.has_bdb)
        for (size_t i = 0; i < state_.planes.size(); ++i)
            if (!state_.planes[i].bound)
                add(IssueGroup::Layers, false, state_.planes[i].name + ": no game-plane binding",
                    "Review layer transforms and the target checkout's stage setup. Composition is estimated until the runtime mapping is known.",
                    0, -1, -1, (int)i);
    std::stable_sort(issues.begin(), issues.end(), [](const Issue &a, const Issue &b) {
        if (a.error != b.error) return a.error > b.error;
        return a.group < b.group;
    });
    return issues;
}
} // namespace studio
