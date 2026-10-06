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
        std::cout << "Resolved alignment, equal gaps, undo/redo and lock protection passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
