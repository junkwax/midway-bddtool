#include "Core/studio_image_replace.h"
#include "Core/studio_tga_export.h"
#include "Core/studio_document.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        auto document = Document::demo(); const auto &state = document.state();
        int id = state.assets->data.images.front().idx, palette_index = state.assets->default_palettes.front();
        const auto palette = state.assets->data.palettes[palette_index];
        uint32_t color = bdd_core_rgb555_to_argb(palette.rgb555[1]);
        uint8_t rgba[] = {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color), 255, 0, 0, 0, 0,
                          uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color), 255, 0, 0, 0, 0};
        auto raster = prepare_rgba_replacement(2, 2, rgba, palette);
        require(raster.ready && raster.pixels == std::vector<uint8_t>({1, 0, 1, 0}), "RGBA was not mapped exactly");
        rgba[0] ^= 0x40; require(!prepare_rgba_replacement(2, 2, rgba, palette).ready, "Foreign RGB color accepted");
        BddCoreImage tga_image{}; tga_image.w = 2; tga_image.h = 2; tga_image.pix = {0, 1, 1, 0};
        auto encoded = encode_indexed_tga(tga_image, palette); require(encoded.ready, "TGA fixture failed");
        auto indexed = prepare_indexed_tga_replacement(encoded.bytes, palette);
        require(indexed.ready && indexed.pixels == tga_image.pix, "Indexed TGA did not round trip");
        auto wrong = palette; wrong.rgb555[1] ^= 1;
        require(!prepare_indexed_tga_replacement(encoded.bytes, wrong).ready, "Mismatched TGA palette accepted");
        auto revision = document.state().revision; auto objects = document.state().objects;
        std::string error; require(document.replace_image(id, 2, 2, tga_image.pix, error), error.c_str());
        size_t slot = 0; auto *changed = document.image(id, &slot);
        require(changed && changed->w == 2 && changed->h == 2 && changed->idx == id, "Replacement lost image identity");
        require(document.state().objects.size() == objects.size() && document.state().objects.front().id == objects.front().id,
                "Replacement changed placements");
        require(document.state().revision != revision && document.undo(), "Replacement was not undoable");
        auto *restored = document.image(id); require(restored && (restored->w != 2 || restored->h != 2), "Undo did not restore artwork");
        require(document.redo() && document.image(id)->w == 2, "Redo did not restore replacement");
        // Padding must compensate normal and flipped placements differently.
        auto before_canvas = document.scene();
        require(document.resize_image_canvas(id, 2, 4, 3, 5, error), error.c_str());
        auto after_canvas = document.scene();
        for (const auto &before : before_canvas) if (before.id && document.object(before.id)->object.ii == id) {
            auto after = std::find_if(after_canvas.begin(), after_canvas.end(), [&](const SceneItem &i) { return i.id == before.id; });
            require(after != after_canvas.end(), "Canvas resize lost placement");
            require(after->rect.x == before.rect.x - (before.hflip ? 3 : 2), "Horizontal flip compensation failed");
            require(after->rect.y == before.rect.y - (before.vflip ? 5 : 4), "Vertical flip compensation failed");
        }
        require(document.resize_image_canvas(id, -2, -4, -3, -5, error), error.c_str());
        require(document.image(id)->w == 2 && document.image(id)->h == 2, "Transparent trim did not restore canvas");
        require(!document.resize_image_canvas(id, -1, 0, 0, 0, error), "Visible pixel trim was accepted");
        std::cout << "PNG mapping, indexed TGA round trip, in-place identity and undo/redo passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
