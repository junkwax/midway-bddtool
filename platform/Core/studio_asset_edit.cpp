#include "Core/studio_document.h"
#include "Core/studio_animation.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <map>

namespace studio {
namespace {
bool usable(const BddCoreImage &im, int count) {
    return std::all_of(im.pix.begin(), im.pix.end(), [=](uint8_t p) { return p < count; });
}
bool palette_fits(const State &s, int palette, int count) {
    std::set<int> placed;
    for (const auto &p : s.objects) if (p.object.fl == palette) placed.insert(p.object.ii);
    for (size_t i = 0; i < s.assets->data.images.size(); ++i) {
        const auto &im = s.assets->data.images[i];
        bool used = i < s.assets->default_palettes.size() && s.assets->default_palettes[i] == palette;
        used |= placed.count(im.idx) != 0;
        if (used && !usable(im, count)) return false;
    }
    return true;
}
}

bool make_raster_asset(const std::string &name, int w, int h, const uint8_t *rgba,
                       AssetBank &output, std::string &error) {
    output = {}; error.clear();
    if (!rgba || w < 1 || h < 1 || w > 4096 || h > 4096) {
        error = "Image dimensions must be between 1 and 4096 pixels."; return false;
    }
    std::map<uint16_t, uint8_t> colors;
    auto color_at = [&](size_t i) {
        return bdd_core_argb_to_rgb555(0xff000000u | (uint32_t(rgba[i * 4]) << 16) |
            (uint32_t(rgba[i * 4 + 1]) << 8) | rgba[i * 4 + 2]);
    };
    for (size_t i = 0; i < size_t(w) * h; ++i) if (rgba[i * 4 + 3] >= 128) {
        colors.emplace(color_at(i), 0);
        if (colors.size() > 255) {
            error = "More than 255 opaque RGB555 colors. Reduce the source palette before importing."; return false;
        }
    }
    BddCorePalette pal{}; pal.count = 1;
    std::string label = name.empty() ? "IMPORTED" : name;
    for (char &c : label)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) c = '_';
    std::snprintf(pal.name, sizeof pal.name, "%s", label.c_str());
    for (auto &entry : colors) {
        int i = pal.count++; entry.second = uint8_t(i);
        pal.rgb555[i] = entry.first; pal.argb[i] = bdd_core_rgb555_to_argb(entry.first);
    }
    BddCoreImage im{}; im.w = w; im.h = h; im.pix.resize(size_t(w) * h);
    for (size_t i = 0; i < im.pix.size(); ++i)
        if (rgba[i * 4 + 3] >= 128) im.pix[i] = colors.at(color_at(i));
    BddImageMetadata meta{};
    std::snprintf(meta.label, sizeof meta.label, "%s", name.c_str());
    output.data.images.push_back(std::move(im)); output.data.palettes.push_back(pal);
    output.metadata.push_back(meta); output.default_palettes.push_back(0);
    return true;
}

std::shared_ptr<const AssetBank> prepare_asset_import(const State &state, const AssetBank &input,
                                                     bool reuse_palettes, std::string &error) {
    error.clear();
    auto fail = [&](const char *message) -> std::shared_ptr<const AssetBank> { error = message; return {}; };
    if (!state.assets || input.data.images.empty() || input.data.images.size() > 128 ||
        input.default_palettes.size() != input.data.images.size() ||
        input.metadata.size() != input.data.images.size()) return fail("Invalid import batch; select 1 to 128 images.");
    if (state.assets->data.images.size() + input.data.images.size() > BDD_CORE_MAX_IMAGES)
        return fail("Image limit reached.");
    for (const auto &p : input.data.palettes)
        if (p.count < 1 || p.count > 256 || !std::memchr(p.name, 0, sizeof p.name)) return fail("Invalid imported palette.");
    for (size_t i = 0; i < input.data.images.size(); ++i) {
        const auto &im = input.data.images[i]; int palette = input.default_palettes[i];
        if (im.w < 1 || im.h < 1 || im.w > 4096 || im.h > 4096 || im.pix.size() != size_t(im.w) * im.h ||
            palette < 0 || palette >= (int)input.data.palettes.size() || !usable(im, input.data.palettes[palette].count))
            return fail("Invalid imported pixels or palette reference.");
    }
    auto bank = std::make_shared<AssetBank>(*state.assets);
    int next = 0;
    for (const auto &im : bank->data.images) {
        if (im.idx < 0 || im.idx > 65535) return fail("An existing image ID is outside the supported range.");
        next = std::max(next, im.idx + 1);
    }
    if (next + input.data.images.size() > 65536) return fail("No free image IDs.");
    std::vector<int> palettes;
    for (const auto &pal : input.data.palettes) {
        int found = -1;
        if (reuse_palettes) for (size_t i = 0; i < bank->data.palettes.size(); ++i) {
            const auto &existing = bank->data.palettes[i];
            if (existing.count == pal.count && std::equal(existing.rgb555, existing.rgb555 + pal.count, pal.rgb555) &&
                std::equal(existing.argb, existing.argb + pal.count, pal.argb)) { found = (int)i; break; }
        }
        if (found < 0) {
            if (bank->data.palettes.size() >= BDD_CORE_MAX_PALS) return fail("Palette limit reached.");
            found = (int)bank->data.palettes.size(); bank->data.palettes.push_back(pal);
        }
        palettes.push_back(found);
    }
    for (size_t i = 0; i < input.data.images.size(); ++i) {
        auto im = input.data.images[i]; im.idx = next++;
        auto meta = input.metadata[i]; meta.idx = im.idx;
        bank->data.images.push_back(std::move(im)); bank->metadata.push_back(meta);
        bank->default_palettes.push_back(palettes[input.default_palettes[i]]);
    }
    return bank;
}

bool Document::import_img(const std::string &path, const std::vector<std::string> &labels,
                          std::string &error, std::vector<int> &ids) {
    error.clear();
    ids.clear();
    if (active_) { error = "Finish the current edit before importing."; return false; }
    const auto decoded = load_animation_selection(path, labels);
    if (!decoded.ready()) { error = decoded.notice; return false; }
    return import_assets(*decoded.artwork.assets, true, error, ids);
}

bool Document::import_assets(const AssetBank &input, bool reuse_palettes,
                             std::string &error, std::vector<int> &ids) {
    ids.clear();
    if (active_) { error = "Finish the current edit before importing."; return false; }
    auto bank = prepare_asset_import(state_, input, reuse_palettes, error);
    if (!bank) return false;
    for (size_t i = state_.assets->data.images.size(); i < bank->data.images.size(); ++i)
        ids.push_back(bank->data.images[i].idx);
    begin("Import artwork"); state_.assets = bank; touch(); commit();
    return true;
}

bool Document::set_palette(int palette, const BddCorePalette &colors, std::string &error) {
    error.clear();
    if (active_ || palette < 0 || palette >= (int)state_.assets->data.palettes.size() ||
        colors.count < 1 || colors.count > 256) { error = "Invalid palette or unfinished edit."; return false; }
    if (!palette_fits(state_, palette, colors.count)) {
        error = "Artwork still uses colors beyond this palette size."; return false;
    }
    auto pal = colors;
    pal.name[sizeof pal.name - 1] = 0;
    for (char *c = pal.name; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_')) *c = '_';
    for (int i = 0; i < pal.count; ++i)
        pal.argb[i] = i ? bdd_core_rgb555_to_argb(pal.rgb555[i]) : 0;
    const auto &old = state_.assets->data.palettes[palette];
    if (old.count == pal.count && std::string(old.name) == pal.name &&
        std::equal(old.rgb555, old.rgb555 + pal.count, pal.rgb555)) return true;
    auto bank = std::make_shared<AssetBank>(*state_.assets);
    bank->data.palettes[palette] = pal;
    begin("Edit palette"); state_.assets = bank; touch(); commit(); error.clear(); return true;
}

bool Document::copy_palette_for_image(int id, int palette, std::string &error, int &result) {
    error.clear();
    size_t slot = 0;
    const auto *im = image(id, &slot);
    if (active_ || !im || palette < 0 || palette >= (int)state_.assets->data.palettes.size() ||
        state_.assets->data.palettes.size() >= BDD_CORE_MAX_PALS) {
        error = "Cannot copy this palette (invalid artwork, palette limit or unfinished edit)."; return false;
    }
    if (!usable(*im, state_.assets->data.palettes[palette].count)) {
        error = "This palette does not cover the artwork's pixel indices."; return false;
    }
    auto bank = std::make_shared<AssetBank>(*state_.assets);
    auto pal = bank->data.palettes[palette];
    result = (int)bank->data.palettes.size();
    std::snprintf(pal.name, sizeof pal.name, "%.48s_copy%d", bank->data.palettes[palette].name, result);
    bank->data.palettes.push_back(pal);
    bank->default_palettes.resize(bank->data.images.size(), 0);
    bank->default_palettes[slot] = result;
    begin("Copy artwork palette");
    for (auto &p : state_.objects)
        if (p.object.ii == id && p.object.fl == palette)
            p.object.fl = result;
    state_.assets = bank; touch(); commit(); error.clear(); return true;
}

bool Document::set_image_pixels(int id, const std::vector<uint8_t> &pixels, std::string &error) {
    error.clear();
    size_t slot = 0; const auto *im = image(id, &slot);
    if (active_ || !im || pixels.size() != im->pix.size()) {
        error = "Pixel dimensions changed or another edit is unfinished."; return false;
    }
    std::set<int> palettes;
    if (slot < state_.assets->default_palettes.size()) palettes.insert(state_.assets->default_palettes[slot]);
    for (const auto &p : state_.objects) if (p.object.ii == id) palettes.insert(p.object.fl);
    for (int i : palettes)
        if (i < 0 || i >= (int)state_.assets->data.palettes.size() ||
            std::any_of(pixels.begin(), pixels.end(), [&](uint8_t p) { return p >= state_.assets->data.palettes[i].count; })) {
            error = "A pixel exceeds a palette used by this artwork. Choose a valid color index."; return false;
        }
    if (pixels == im->pix) return true;
    auto bank = std::make_shared<AssetBank>(*state_.assets);
    bank->data.images[slot].pix = pixels;
    begin("Edit block pixels"); state_.assets = bank; touch(); commit(); error.clear(); return true;
}
} // namespace studio
