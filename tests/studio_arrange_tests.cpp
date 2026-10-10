#include "Core/studio_document.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::vector<SceneItem> chosen(const Document &d, const std::vector<ObjectId> &ids) {
    auto items = d.scene();
    items.erase(std::remove_if(items.begin(), items.end(), [&](const SceneItem &i) {
        return std::find(ids.begin(), ids.end(), i.id) == ids.end();
    }), items.end()); return items;
}
const SceneItem &with_id(const std::vector<SceneItem> &items, ObjectId id) {
    auto found = std::find_if(items.begin(), items.end(), [&](const SceneItem &item) { return item.id == id; });
    if (found == items.end()) throw std::runtime_error("Selected placement disappeared");
    return *found;
}
int main() {
    try {
        auto d = Document::demo(); auto scene = d.scene(); require(scene.size() >= 3, "Demo needs three placements");
        std::vector<ObjectId> ids = {scene[0].id, scene[1].id, scene[2].id}; std::string error;
        require(d.arrange(ids, ArrangeMode::Left, error), error.c_str());
        auto aligned = chosen(d, ids); require(aligned.size() == 3, "Aligned selection changed");
        for (size_t i = 1; i < aligned.size(); ++i)
            require(aligned[i].rect.x == aligned[0].rect.x, "Resolved stage edges did not align");
        require(d.undo(), "Alignment was not undoable");
        require(d.redo(), "Alignment was not redoable");
        require(d.undo(), "Could not restore distribution input");
        d.move({ids[1]}, 13, 0); d.move({ids[2]}, 37, 0);
        require(d.arrange(ids, ArrangeMode::DistributeX, error), error.c_str());
        auto spaced = chosen(d, ids);
        std::sort(spaced.begin(), spaced.end(), [](const SceneItem &a, const SceneItem &b) { return a.rect.x < b.rect.x; });
        double gap1 = spaced[1].rect.x - (spaced[0].rect.x + spaced[0].rect.w);
        double gap2 = spaced[2].rect.x - (spaced[1].rect.x + spaced[1].rect.w);
        require(std::abs(gap1 - gap2) <= 1.0, "Horizontal gaps were not evenly distributed");
        int plane = d.object(ids[0])->plane; require(plane >= 0, "Fixture placement has no layer");
        auto flags = d.state().planes[plane]; require(d.set_plane_flags(plane, flags.hidden, true), "Could not lock layer");
        auto revision = d.state().revision;
        require(!d.arrange(ids, ArrangeMode::Top, error) && d.state().revision == revision, "Locked arrangement changed document");

        auto mirrored = Document::demo(); auto mirror_scene = mirrored.scene();
        std::vector<ObjectId> mirror_ids = {mirror_scene[0].id, mirror_scene[1].id, mirror_scene[2].id};
        auto before = chosen(mirrored, mirror_ids);
        double left = before[0].rect.x, right = before[0].rect.x + before[0].rect.w;
        for (const auto &item : before) {
            left = std::min(left, item.rect.x);
            right = std::max(right, item.rect.x + item.rect.w);
        }
        require(mirrored.arrange(mirror_ids, ArrangeMode::MirrorX, error), error.c_str());
        auto after = chosen(mirrored, mirror_ids);
        for (ObjectId id : mirror_ids) {
            const auto &was = with_id(before, id); const auto &now = with_id(after, id);
            require(std::abs(now.rect.x - (left + right - was.rect.x - was.rect.w)) <= 1.0,
                    "Group mirror did not reflect a resolved stage rectangle");
            require(now.hflip != was.hflip && now.vflip == was.vflip, "Horizontal mirror flags are incorrect");
        }
        require(mirrored.arrange(mirror_ids, ArrangeMode::MirrorX, error), error.c_str());
        auto restored = chosen(mirrored, mirror_ids);
        for (ObjectId id : mirror_ids) {
            const auto &was = with_id(before, id); const auto &now = with_id(restored, id);
            require(std::abs(now.rect.x - was.rect.x) <= 1.0 && std::abs(now.rect.y - was.rect.y) <= 1.0,
                    "Mirroring twice did not restore placement positions");
            require(now.hflip == was.hflip && now.vflip == was.vflip, "Mirroring twice did not restore flip flags");
        }
        require(mirrored.arrange(mirror_ids, ArrangeMode::MirrorY, error), error.c_str());
        auto vertical = chosen(mirrored, mirror_ids);
        for (ObjectId id : mirror_ids)
            require(with_id(vertical, id).vflip != with_id(before, id).vflip, "Vertical mirror flag is incorrect");
        require(mirrored.undo(), "Group mirror was not undoable");

        std::cout << "Resolved alignment, equal gaps, group mirror, undo/redo and lock protection passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
