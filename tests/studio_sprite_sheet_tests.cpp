#include "Core/studio_sprite_sheet.h"
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() {
    try {
        auto document = Document::demo(); const auto &state = document.state();
        SpriteSheetRequest request; request.columns = 2; request.padding = 3;
        auto all = render_sprite_sheet(state, request);
        require(all.ready && all.width > 0 && all.height > 0, "Full artwork sheet failed");
        require(all.cell_width >= 7 && all.cell_height >= 7, "Padding was not included");
        require(all.metadata.find("bddtool-artwork-sheet") != std::string::npos, "Sheet metadata missing");
        for (const auto &im : state.assets->data.images)
            require(all.metadata.find("\"id\": " + std::to_string(im.idx)) != std::string::npos, "Artwork omitted from full sheet");
        request.placed_only = true; auto placed = render_sprite_sheet(state, request);
        require(placed.ready && placed.rgba.size() == size_t(placed.width) * placed.height * 4, "Placed sheet failed");
        bool opaque = false, transparent = false;
        for (size_t i = 3; i < placed.rgba.size(); i += 4) { opaque |= placed.rgba[i] == 255; transparent |= placed.rgba[i] == 0; }
        require(opaque && transparent, "Sheet lost artwork or transparency");
        State hidden = state; for (auto &p : hidden.objects) p.hidden = true;
        require(!render_sprite_sheet(hidden, request).ready, "Hidden-only placed sheet was accepted");
        const int first = state.assets->data.images[0].idx, second = state.assets->data.images[1].idx,
                  third = state.assets->data.images[2].idx;
        request.image_ids = {third, first, third};
        auto selection = render_sprite_sheet(hidden, request);
        auto metadata_id = [](int id) { return "\"id\": " + std::to_string(id) + ","; };
        require(selection.ready && selection.width == 2 * selection.cell_width &&
                selection.height == selection.cell_height, "Selected sheet has the wrong number of cells");
        require(selection.metadata.find(metadata_id(first)) < selection.metadata.find(metadata_id(third)) &&
                selection.metadata.find(metadata_id(third)) != std::string::npos &&
                selection.metadata.find(metadata_id(second)) == std::string::npos,
                "Selected IDs were not exported once in library order");
        request.image_ids = {first, -1};
        require(!render_sprite_sheet(state, request).ready, "Stale selection was silently exported");
        request.image_ids = std::vector<int>{};
        require(!render_sprite_sheet(state, request).ready, "Empty selection fell back to the entire library");
        request.image_ids.reset();
        request.placed_only = false; request.columns = 0; request.padding = 99;
        require(render_sprite_sheet(state, request).ready, "Safe option clamping failed");
        std::cout << "Artwork sheet layout, metadata, palettes, transparency and visibility passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
