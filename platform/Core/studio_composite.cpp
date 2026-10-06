#include "Core/studio_composite.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace studio {
namespace {
std::string json(const std::string &value) {
    std::string out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c >= 32) out += char(c);
    }
    return out;
}
bool selected(const CompositeRequest &request, ObjectId id) {
    return std::find(request.selection.begin(), request.selection.end(), id) != request.selection.end();
}
const char *scope_name(CompositeScope scope) {
    return scope == CompositeScope::Selection ? "selection" : scope == CompositeScope::Layer ? "layer" : "stage";
}
}

CompositeExport render_composite(const State &state, const CompositeRequest &request) {
    CompositeExport out;
    if (!state.assets) { out.error = "No artwork is loaded."; return out; }
    auto all = scene_items(state);
    std::vector<SceneItem> items;
    for (const auto &item : all) {
        const auto &placement = state.objects[item.object_index];
        if (request.scope == CompositeScope::Selection && !selected(request, item.id)) continue;
        if (request.scope == CompositeScope::Layer && placement.plane != request.plane) continue;
        items.push_back(item);
    }
    if (items.empty()) { out.error = "The chosen scope has no visible artwork to export."; return out; }
    double left = items.front().rect.x, top = items.front().rect.y;
    double right = left + items.front().rect.w, bottom = top + items.front().rect.h;
    for (const auto &item : items) {
        left = std::min(left, item.rect.x); top = std::min(top, item.rect.y);
        right = std::max(right, item.rect.x + item.rect.w);
        bottom = std::max(bottom, item.rect.y + item.rect.h);
    }
    out.origin_x = (int)std::floor(left); out.origin_y = (int)std::floor(top);
    out.width = (int)std::ceil(right) - out.origin_x;
    out.height = (int)std::ceil(bottom) - out.origin_y;
    if (out.width <= 0 || out.height <= 0 || out.width > 8192 || out.height > 8192 ||
        uint64_t(out.width) * uint64_t(out.height) > 32000000) {
        out.error = "Composite exceeds the 8192-pixel or 32-million-pixel export limit."; return out;
    }
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    if (request.include_backdrop && state.backdrop >= 0 && state.backdrop <= 32767) {
        uint32_t color = bdd_core_rgb555_to_argb((uint16_t)state.backdrop);
        for (size_t p = 0; p < out.rgba.size(); p += 4) {
            out.rgba[p] = uint8_t(color >> 16); out.rgba[p + 1] = uint8_t(color >> 8);
            out.rgba[p + 2] = uint8_t(color); out.rgba[p + 3] = 255;
        }
    }
    for (const auto &item : items) {
        if (item.image_slot >= state.assets->data.images.size() || item.palette < 0 ||
            item.palette >= (int)state.assets->data.palettes.size()) {
            out.error = "A placement has an invalid artwork or palette reference."; out.rgba.clear(); return out;
        }
        const auto &im = state.assets->data.images[item.image_slot];
        const auto &pal = state.assets->data.palettes[item.palette];
        if (im.w <= 0 || im.h <= 0 || im.pix.size() < size_t(im.w) * im.h) {
            out.error = "A placement has invalid pixel data."; out.rgba.clear(); return out;
        }
        int dx = (int)std::llround(item.rect.x) - out.origin_x;
        int dy = (int)std::llround(item.rect.y) - out.origin_y;
        for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) {
            int sx = item.hflip ? im.w - 1 - x : x;
            int sy = item.vflip ? im.h - 1 - y : y;
            unsigned index = im.pix[size_t(sy) * im.w + sx];
            if (!index) continue;
            if (index >= (unsigned)pal.count || index >= 256) {
                out.error = "Artwork uses a color outside its assigned palette."; out.rgba.clear(); return out;
            }
            int px = dx + x, py = dy + y;
            if (px < 0 || py < 0 || px >= out.width || py >= out.height) continue;
            uint32_t color = bdd_core_rgb555_to_argb(pal.rgb555[index]);
            size_t p = (size_t(py) * out.width + px) * 4;
            out.rgba[p] = uint8_t(color >> 16); out.rgba[p + 1] = uint8_t(color >> 8);
            out.rgba[p + 2] = uint8_t(color); out.rgba[p + 3] = 255;
        }
    }
    std::ostringstream meta;
    meta << "{\n  \"format\": \"bddtool-composite\",\n  \"version\": 1,\n  \"stage\": \"" << json(state.name)
         << "\",\n  \"scope\": \"" << scope_name(request.scope) << "\",\n  \"revision\": " << state.revision
         << ",\n  \"origin\": [" << out.origin_x << ", " << out.origin_y << "],\n  \"size\": [" << out.width << ", " << out.height
         << "],\n  \"backdropIncluded\": " << (request.include_backdrop ? "true" : "false") << ",\n  \"placements\": [\n";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i]; const auto &p = state.objects[item.object_index];
        meta << "    {\"id\": " << item.id << ", \"image\": " << p.object.ii << ", \"palette\": " << item.palette
             << ", \"layer\": " << p.plane << ", \"stage\": [" << (int)std::llround(item.rect.x) << ", "
             << (int)std::llround(item.rect.y) << "], \"relative\": [" << (int)std::llround(item.rect.x) - out.origin_x << ", "
             << (int)std::llround(item.rect.y) - out.origin_y << "], \"size\": [" << item.rect.w << ", " << item.rect.h
             << "], \"flipX\": " << (item.hflip ? "true" : "false") << ", \"flipY\": " << (item.vflip ? "true" : "false") << "}"
             << (i + 1 == items.size() ? "\n" : ",\n");
    }
    meta << "  ]\n}\n";
    out.metadata = meta.str(); out.ready = true;
    return out;
}
} // namespace studio
