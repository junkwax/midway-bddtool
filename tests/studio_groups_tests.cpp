#include "Core/studio_document.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace studio;
namespace fs = std::filesystem;
void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
using Footprint = std::tuple<int, int, int, int, bool, bool, int>; // x, y, w, h, flip x, flip y, image
std::vector<Footprint> footprints(const Document &d, const std::vector<ObjectId> &ids, int dx = 0, int dy = 0) {
    std::vector<Footprint> out;
    for (const auto &item : d.scene())
        if (std::find(ids.begin(), ids.end(), item.id) != ids.end())
            out.emplace_back((int)item.rect.x - dx, (int)item.rect.y - dy, (int)item.rect.w, (int)item.rect.h,
                             item.hflip, item.vflip, d.object(item.id)->object.ii);
    std::sort(out.begin(), out.end());
    return out;
}
int main() {
    try {
        auto d = Document::demo(); auto scene = d.scene(); require(scene.size() >= 2, "Demo needs two placements");
        std::vector<ObjectId> ids = {scene[0].id, scene[1].id};
        double left = std::min(scene[0].rect.x, scene[1].rect.x), top = std::min(scene[0].rect.y, scene[1].rect.y);
        std::string error;
        require(!d.add_group(ids, "   ", error), "Blank group name accepted");
        require(d.add_group(ids, " Pillar pair ", error), error);
        require(d.state().groups.size() == 1 && d.state().groups[0].name == "Pillar pair", "Group name was not trimmed");
        require(!d.add_group(ids, "Pillar pair", error), "Duplicate group name accepted");
        require(d.undo() && d.state().groups.empty() && d.redo() && d.state().groups.size() == 1, "Group save undo/redo failed");
        const auto group = d.state().groups[0];

        // Placing at the captured origin reproduces the original footprint exactly.
        GroupPlacement at; at.origin = {left, top};
        auto copy = d.place_group(0, at, error); require(copy.size() == group.items.size(), error);
        require(footprints(d, copy) == footprints(d, ids), "Placed group does not match its source");
        require(d.undo() && !d.object(copy[0]) && d.redo(), "Group placement was not one undo step");

        // Mirroring reflects positions inside the group box and toggles every flip.
        GroupPlacement mirrored = at; mirrored.mirror_x = true;
        auto flipped = d.place_group(0, mirrored, error); require(!flipped.empty(), error);
        auto expected = footprints(d, ids);
        for (auto &f : expected) {
            std::get<0>(f) = int(left) + group.width - (std::get<0>(f) - int(left)) - std::get<2>(f);
            std::get<4>(f) = !std::get<4>(f);
        }
        std::sort(expected.begin(), expected.end());
        require(footprints(d, flipped) == expected, "Mirrored group footprint is wrong");

        // Alternating repeats step by width plus gap and mirror every other copy.
        GroupPlacement run = at; run.copies = 3; run.gap = 4; run.alternate = true; run.origin.y += 300;
        auto repeated = d.place_group(0, run, error); require(repeated.size() == 3 * group.items.size(), error);
        size_t n = group.items.size();
        std::vector<ObjectId> first(repeated.begin(), repeated.begin() + n), second(repeated.begin() + n, repeated.begin() + 2 * n),
            third(repeated.begin() + 2 * n, repeated.end());
        int step = group.width + 4;
        require(footprints(d, third, 2 * step, 0) == footprints(d, first), "Third copy is not two steps along");
        auto second_expected = footprints(d, flipped, -step, -300);
        require(footprints(d, second) == second_expected, "Second copy was not mirrored in place");

        // Locked layers reject the whole placement without changing the document.
        int plane = group.items[0].plane; auto flags = d.state().planes[plane];
        require(d.set_plane_flags(plane, flags.hidden, true), "Could not lock layer");
        auto revision = d.state().revision;
        require(d.place_group(0, at, error).empty() && d.state().revision == revision, "Locked layer accepted a group");
        require(d.set_plane_flags(plane, flags.hidden, false), "Could not unlock layer");

        // Groups survive save and reopen with every field intact.
        auto root = fs::temp_directory_path() / "bddtool-studio-groups-test";
        fs::remove_all(root); fs::create_directories(root);
        auto file = (root / "groups.BDB").u8string();
        require(d.save(file, error), "Save: " + error);
        Document reopened; require(reopened.load(file, error), "Reopen: " + error);
        require(reopened.state().groups.size() == 1, "Group did not reopen");
        const auto &back = reopened.state().groups[0];
        require(back.name == group.name && back.width == group.width && back.height == group.height &&
                back.items.size() == group.items.size(), "Reopened group header changed");
        for (size_t i = 0; i < back.items.size(); ++i) {
            const auto &a = back.items[i], &b = group.items[i];
            require(a.image == b.image && a.palette == b.palette && a.plane == b.plane && a.x == b.x && a.y == b.y &&
                    a.flip_x == b.flip_x && a.flip_y == b.flip_y && a.attributes == b.attributes, "Reopened group item changed");
        }
        require(reopened.remove_group(0) && reopened.state().groups.empty() && reopened.undo() &&
                reopened.state().groups.size() == 1, "Group delete undo failed");
        fs::remove_all(root);
        std::cout << "Group save, exact placement, mirroring, alternating repeats, lock protection and reopen passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
