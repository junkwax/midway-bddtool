#include "Core/studio_document.h"
#include <algorithm>
#include <cmath>

namespace studio {

bool Document::add_group(const std::vector<ObjectId> &ids, const std::string &raw_name,
                         std::string &error) {
    error.clear();
    if (active_) { error = "Finish the current edit before saving a group."; return false; }
    auto first = raw_name.find_first_not_of(" \t"), last = raw_name.find_last_not_of(" \t");
    std::string name = first == std::string::npos ? "" : raw_name.substr(first, last - first + 1);
    if (name.empty()) { error = "Name the group."; return false; }
    if (name.size() > 48) { error = "Group names are limited to 48 characters."; return false; }
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) {
        error = "Group names cannot contain control characters."; return false;
    }
    if (std::any_of(state_.groups.begin(), state_.groups.end(),
                    [&](const ArtworkGroup &g) { return g.name == name; })) {
        error = "A group with that name already exists."; return false;
    }
    if (state_.groups.size() >= (size_t)kMaxGroups) { error = "This stage already has the maximum of 64 groups."; return false; }
    std::vector<SceneItem> items;
    for (const auto &item : scene())
        if (std::find(ids.begin(), ids.end(), item.id) != ids.end()) items.push_back(item);
    if (items.empty()) { error = "Select visible artwork to save as a group."; return false; }
    if (items.size() > (size_t)kMaxGroupItems) { error = "Groups are limited to 256 placements."; return false; }
    double left = items.front().rect.x, top = items.front().rect.y;
    double right = left + items.front().rect.w, bottom = top + items.front().rect.h;
    for (const auto &item : items) {
        left = std::min(left, item.rect.x); top = std::min(top, item.rect.y);
        right = std::max(right, item.rect.x + item.rect.w);
        bottom = std::max(bottom, item.rect.y + item.rect.h);
    }
    ArtworkGroup group;
    group.name = name;
    group.width = (int)std::llround(right - left);
    group.height = (int)std::llround(bottom - top);
    for (const auto &item : items) { // scene() is already back-to-front.
        const auto &p = state_.objects[item.object_index];
        GroupItem g;
        g.image = p.object.ii; g.palette = p.object.fl; g.plane = p.plane;
        g.x = (int)std::llround(item.rect.x - left); g.y = (int)std::llround(item.rect.y - top);
        g.flip_x = (p.object.wx & 0x10) != 0; g.flip_y = (p.object.wx & 0x20) != 0;
        g.attributes = p.object.wx & ~0x30;
        group.items.push_back(g);
    }
    begin("Save artwork group");
    state_.groups.push_back(std::move(group));
    touch();
    commit();
    return true;
}

bool Document::remove_group(size_t index) {
    if (active_ || index >= state_.groups.size()) return false;
    begin("Delete artwork group");
    state_.groups.erase(state_.groups.begin() + (std::ptrdiff_t)index);
    touch();
    commit();
    return true;
}

std::vector<ObjectId> Document::place_group(size_t index, const GroupPlacement &o, std::string &error) {
    error.clear();
    std::vector<ObjectId> ids;
    if (active_) { error = "Finish the current edit before placing a group."; return ids; }
    if (index >= state_.groups.size()) { error = "That group no longer exists."; return ids; }
    const auto &group = state_.groups[index];
    if (o.copies < 1 || o.copies > kMaxGroupCopies) { error = "Choose between 1 and 64 copies."; return ids; }
    if (std::abs(o.gap) > 4096 || !std::isfinite(o.origin.x) || !std::isfinite(o.origin.y) ||
        std::abs(o.origin.x) > 100000 || std::abs(o.origin.y) > 100000) {
        error = "The group position or gap is out of range."; return ids;
    }
    if (o.target_plane < -1 || o.target_plane >= (int)state_.planes.size()) {
        error = "Choose an existing layer."; return ids;
    }
    for (const auto &item : group.items) {
        int plane = o.target_plane >= 0 ? o.target_plane : item.plane;
        if (plane < 0 || plane >= (int)state_.planes.size()) {
            error = "The group has artwork without a saved layer. Choose a target layer."; return ids;
        }
        if (state_.planes[plane].locked) { error = "Unlock " + state_.planes[plane].name + " to place this group."; return ids; }
        if (!image(item.image)) { error = "The group references artwork that no longer exists."; return ids; }
        if (item.palette < 0 || item.palette >= (int)state_.assets->data.palettes.size()) {
            error = "The group references a palette that no longer exists."; return ids;
        }
    }
    if (state_.objects.size() + group.items.size() * (size_t)o.copies > BDD_CORE_MAX_OBJECTS) {
        error = "Placing this many copies would exceed the stage object limit."; return ids;
    }
    int order = 0;
    for (const auto &p : state_.objects) order = std::max(order, p.object.order + 1);
    begin("Place artwork group");
    for (int copy = 0; copy < o.copies; ++copy) {
        bool odd = o.alternate && copy % 2;
        bool mirror_x = o.mirror_x != (odd && !o.vertical), mirror_y = o.mirror_y != (odd && o.vertical);
        double step = copy * double((o.vertical ? group.height : group.width) + o.gap);
        double ox = o.origin.x + (o.vertical ? 0 : step), oy = o.origin.y + (o.vertical ? step : 0);
        for (const auto &item : group.items) {
            const auto *im = image(item.image);
            int plane = o.target_plane >= 0 ? o.target_plane : item.plane;
            const auto &layer = state_.planes[plane];
            // Mirroring reflects each item inside the group's box and flips its pixels.
            int x = mirror_x ? group.width - item.x - im->w : item.x;
            int y = mirror_y ? group.height - item.y - im->h : item.y;
            bool fx = item.flip_x != mirror_x, fy = item.flip_y != mirror_y;
            Placement p;
            p.id = next_id_++;
            p.plane = plane;
            p.object.ii = item.image;
            p.object.fl = item.palette;
            p.object.wx = (item.attributes & ~0x30) | (fx ? 0x10 : 0) | (fy ? 0x20 : 0);
            p.object.depth = (int)std::llround(ox + x) - layer.x + layer.source.x1;
            p.object.sy = (int)std::llround(oy + y) - layer.y + layer.source.y1;
            p.object.order = order++;
            state_.objects.push_back(p);
            ids.push_back(p.id);
        }
    }
    state_.has_bdb = true;
    touch();
    commit();
    return ids;
}

} // namespace studio
