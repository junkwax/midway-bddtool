#include "Core/studio_sprite_sheet.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace studio {
namespace {
std::string json(const char *value) {
    std::string out;
    for (const unsigned char c : std::string(value ? value : "")) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c >= 32) out += char(c);
    }
    return out;
}
}
SpriteSheetExport render_sprite_sheet(const State &state, const SpriteSheetRequest &request) {
    SpriteSheetExport out;
    if (!state.assets) { out.error = "No artwork is loaded."; return out; }
    int columns = std::clamp(request.columns, 1, 64);
    int padding = std::clamp(request.padding, 0, 32);
    std::unordered_map<size_t, int> placed_palettes;
    for (const auto &item : scene_items(state)) if (!placed_palettes.count(item.image_slot))
        placed_palettes[item.image_slot] = item.palette;
    std::vector<size_t> slots;
    for (size_t i = 0; i < state.assets->data.images.size(); ++i)
        if (!request.placed_only || placed_palettes.count(i)) slots.push_back(i);
    if (slots.empty()) { out.error = request.placed_only ? "No visible placed artwork is available." : "The artwork library is empty."; return out; }
    int max_w = 1, max_h = 1;
    for (size_t slot : slots) {
        const auto &im = state.assets->data.images[slot];
        if (im.w <= 0 || im.h <= 0 || im.pix.size() < size_t(im.w) * im.h) { out.error = "Artwork has invalid pixel data."; return out; }
        max_w = std::max(max_w, im.w); max_h = std::max(max_h, im.h);
    }
    columns = std::min(columns, (int)slots.size());
    int rows = ((int)slots.size() + columns - 1) / columns;
    out.cell_width = max_w + padding * 2; out.cell_height = max_h + padding * 2;
    uint64_t width = uint64_t(out.cell_width) * columns, height = uint64_t(out.cell_height) * rows;
    if (width > 8192 || height > 8192 || width * height > 32000000) {
        out.error = "Artwork sheet exceeds the 8192-pixel or 32-million-pixel export limit."; return out;
    }
    out.width = (int)width; out.height = (int)height;
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    std::ostringstream meta;
    meta << "{\n  \"format\": \"bddtool-artwork-sheet\",\n  \"version\": 1,\n  \"stage\": \"" << json(state.name.c_str())
         << "\",\n  \"size\": [" << out.width << ", " << out.height << "],\n  \"cellSize\": [" << out.cell_width << ", " << out.cell_height
         << "],\n  \"padding\": " << padding << ",\n  \"images\": [\n";
    for (size_t n = 0; n < slots.size(); ++n) {
        size_t slot = slots[n]; const auto &im = state.assets->data.images[slot];
        int palette = placed_palettes.count(slot) ? placed_palettes[slot] :
            (slot < state.assets->default_palettes.size() ? state.assets->default_palettes[slot] : 0);
        if (palette < 0 || palette >= (int)state.assets->data.palettes.size()) { out.error = "Artwork has an invalid default palette."; out.rgba.clear(); return out; }
        const auto &pal = state.assets->data.palettes[palette];
        int cell_x = int(n % columns) * out.cell_width, cell_y = int(n / columns) * out.cell_height;
        int x0 = cell_x + padding + (max_w - im.w) / 2, y0 = cell_y + padding + (max_h - im.h) / 2;
        for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) {
            unsigned index = im.pix[size_t(y) * im.w + x]; if (!index) continue;
            if (index >= (unsigned)pal.count || index >= 256) { out.error = "Artwork uses a color outside its assigned palette."; out.rgba.clear(); return out; }
            uint32_t color = bdd_core_rgb555_to_argb(pal.rgb555[index]);
            size_t p = (size_t(y0 + y) * out.width + x0 + x) * 4;
            out.rgba[p] = uint8_t(color >> 16); out.rgba[p + 1] = uint8_t(color >> 8);
            out.rgba[p + 2] = uint8_t(color); out.rgba[p + 3] = 255;
        }
        const char *label = slot < state.assets->metadata.size() ? state.assets->metadata[slot].label : "";
        meta << "    {\"id\": " << im.idx << ", \"label\": \"" << json(label) << "\", \"palette\": " << palette
             << ", \"rect\": [" << x0 << ", " << y0 << ", " << im.w << ", " << im.h << "], \"cell\": ["
             << cell_x << ", " << cell_y << ", " << out.cell_width << ", " << out.cell_height << "]}"
             << (n + 1 == slots.size() ? "\n" : ",\n");
    }
    meta << "  ]\n}\n"; out.metadata = meta.str(); out.ready = true;
    return out;
}
} // namespace studio
