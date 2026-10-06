#include "Core/studio_view_compare.h"
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        auto d = Document::demo(); auto scene = d.scene(); require(!scene.empty(), "Demo has no placements");
        Point camera{scene[0].rect.x - 20, scene[0].rect.y - 20};
        auto before = render_camera_view(d.state(), camera);
        require(before.width == kViewWidth && before.height == kViewHeight, "Camera view has the wrong size");
        bool covered = false;
        for (size_t p = 3; p < before.rgba.size(); p += 4) covered |= before.rgba[p] != 0;
        require(covered, "Camera view rendered no artwork");
        auto same = diff_views(before, render_camera_view(d.state(), camera));
        require(!same.any() && same.compared == uint64_t(kViewWidth) * kViewHeight, "Identical views reported changes");

        auto revision = d.state().revision;
        d.move({scene[0].id}, 7, 0); require(d.state().revision != revision, "Fixture move failed");
        auto moved = diff_views(before, render_camera_view(d.state(), camera));
        require(moved.any() && moved.left <= moved.right && moved.top <= moved.bottom, "Moved placement was not detected");
        require(moved.left >= 0 && moved.right < kViewWidth && moved.bottom < kViewHeight, "Change bounds escape the view");

        BddCorePalette pal{}; pal.count = 3; pal.rgb555[1] = 0x7c00; pal.rgb555[2] = 0x03e0;
        BddCoreImage small{}; small.w = 2; small.h = 1; small.pix = {1, 2};
        BddCoreImage wide{}; wide.w = 3; wide.h = 2; wide.pix = {1, 1, 0, 0, 0, 2};
        auto changed = diff_views(render_indexed_image(small, pal), render_indexed_image(wide, pal));
        require(changed.width == 3 && changed.height == 2, "Size change was not unioned");
        // (1,0) is recolored and (2,1) is added; (0,0) and the transparent pixels are unchanged.
        require(changed.changed == 2 && changed.left == 1 && changed.right == 2 && changed.bottom == 1,
                "Replacement difference counted the wrong pixels");
        const uint8_t *added = &changed.rgba[(1 * 3 + 2) * 4];
        require(added[1] > added[0], "Added pixels are not marked green");
        std::cout << "Camera view render, identical/moved views and resized replacement diff passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
