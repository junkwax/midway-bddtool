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
        request.placed_only = false; request.columns = 0; request.padding = 99;
        require(render_sprite_sheet(state, request).ready, "Safe option clamping failed");
        std::cout << "Artwork sheet layout, metadata, palettes, transparency and visibility passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
