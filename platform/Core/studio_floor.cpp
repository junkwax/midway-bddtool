#include "Core/studio_floor.h"
#include "Core/studio_assembly.h"
#include <regex>
#include <sstream>

namespace studio {
namespace {
using namespace assembly;
std::vector<std::string> operands(const Assembly &source, const std::string &label, const char *op) {
    std::vector<std::string> out;
    std::regex directive(std::string("\\.") + op + R"(\s+(.+))");
    std::smatch m;
    for (const auto &line : source.block(label)) if (std::regex_match(line, m, directive)) {
        std::istringstream input(m[1]); std::string part;
        while (std::getline(input, part, ',')) out.push_back(trim(part));
    }
    return out;
}
}

Rect FloorPreview::rect(const State &stage, Point camera) const {
    if (!ready()) return {};
    const auto &im = layout.artwork.assets->data.images.front();
    // setup_floor_info centers the raw texture 400 pixels into its 1200-pixel rows.
    return {double(stage.start_x - 400) - camera.x,
            double(stage.start_y + screen_y) - camera.y, double(im.w), double(im.h)};
}

FloorPreview load_floor_preview(const Document &document, const std::string &game_root) {
    FloorPreview out;
    try {
        if (game_root.empty()) {
            out.notice = "Choose a game checkout in Build & Check, then load its floor here.";
            return out;
        }
        auto root = fs::u8path(game_root), path = root / "src" / "BGND.ASM";
        if (!fs::is_regular_file(path)) path = root / "src-refactor" / "src" / "BGND.ASM";
        Assembly source(path);
        std::set<std::string> names;
        for (const auto &p : document.state().planes) names.insert(upper(p.source.name));
        std::string selected, calla, dlists;
        std::map<int, std::string> modules;
        for (const auto &entry : source.labels) {
            if (entry.first.size() < 4 || entry.first.substr(entry.first.size() - 4) != "_MOD") continue;
            auto values = operands(source, entry.first, "LONG");
            if (values.size() < 6) continue;
            std::map<int, std::string> candidate;
            int slot = 0; bool valid = true, terminated = false;
            for (size_t i = 4; i < values.size(); ++i) {
                const auto &value = values[i];
                if (value == "0" || value == "0FFFFFFFFH") { terminated = true; break; }
                ++slot;
                if (slot > 8) { valid = false; break; }
                if (value == "SKIP_BAKMOD") continue;
                if (value.size() <= 4 || value.substr(value.size() - 4) != "BMOD") { valid = false; break; }
                auto name = value.substr(0, value.size() - 4);
                if (!names.count(name)) { valid = false; break; }
                candidate[slot] = name;
            }
            if (!valid || !terminated || candidate.empty()) continue;
            require(selected.empty(), "More than one runtime stage matches these modules; floor selection is ambiguous.");
            selected = entry.first; calla = values[0]; dlists = values[2]; modules = std::move(candidate);
        }
        require(!selected.empty(), "No supported game stage matches this map's source modules. You can still add floor artwork below.");
        std::smatch m; std::string descriptor;
        for (const auto &line : source.block(calla))
            if (std::regex_match(line, m, std::regex(R"(MOVI\s+([A-Z_0-9]+_FLOOR_INFO),\s*A0)"))) {
                require(descriptor.empty(), "More than one floor descriptor in the stage setup.");
                descriptor = m[1];
            }
        require(!descriptor.empty(), "This stage has no supported separate floor descriptor. Its floor may already be in the map.");
        auto longs = operands(source, descriptor, "LONG"), words = operands(source, descriptor, "WORD");
        require(longs.size() == 4 && words.size() == 2, "Unsupported floor descriptor layout.");
        require(std::regex_match(longs[0], std::regex("FL_[A-Z0-9_]+")), "Unsupported floor source label.");
        int height = (int)number(words[1]);
        require(height > 0 && height <= 254 && number(words[0]) <= 32767, "Unsupported floor dimensions or position.");
        out.screen_y = (int)number(words[0]); out.label = longs[0];
        // This preview intentionally shows a static reference. It does not execute floor_code,
        // skew callbacks, skipped rows, palette animation, or the runtime scroll accumulator.
        auto palette_words = operands(source, longs[1], "WORD");
        require(!palette_words.empty(), "Floor palette is missing from BGND.ASM.");
        int count = (int)number(palette_words.front());
        require(count > 0 && count <= 64 && palette_words.size() == size_t(count + 1), "Unsupported floor palette.");
        auto bank = std::make_shared<AssetBank>();
        BddCorePalette palette{}; palette.count = count + 1;
        std::snprintf(palette.name, sizeof palette.name, "%s", longs[1].c_str());
        // Raw floors have opaque index zero; reserve a new transparent zero for authoring.
        for (int i = 0; i < count; ++i) {
            auto value = number(palette_words[i + 1]); require(value <= 65535, "Invalid floor palette word.");
            palette.rgb555[i + 1] = (uint16_t)value;
            palette.argb[i + 1] = bdd_core_rgb555_to_argb((uint16_t)value);
        }
        auto bin = root / "data" / (out.label + ".BIN");
        require(fs::file_size(bin) == size_t(1200 * height * 6 / 8), "Floor BIN must contain 1200-pixel raw 6bpp rows matching the descriptor height.");
        std::ifstream in(bin, std::ios::binary);
        std::vector<uint8_t> bytes(1200 * height * 6 / 8);
        require(bool(in.read((char *)bytes.data(), (std::streamsize)bytes.size())), "Cannot read floor BIN.");
        BddCoreImage im{}; im.w = 1200; im.h = height; im.pix.resize(size_t(im.w) * im.h);
        for (size_t i = 0; i < im.pix.size(); ++i) {
            size_t bit = i * 6, offset = bit / 8;
            unsigned value = bytes[offset];
            if (offset + 1 < bytes.size()) value |= unsigned(bytes[offset + 1]) << 8;
            value = (value >> (bit % 8)) & 63;
            require(value < unsigned(count), "Floor pixels reference an absent palette color.");
            im.pix[i] = uint8_t(value + 1);
        }
        bool floor_found = false;
        std::set<int> seen_slots;
        for (const auto &line : source.block(dlists)) {
            if (std::regex_match(line, std::regex(R"(\.LONG\s+-1,\s*FLOOR_CODE)"))) {
                require(!floor_found, "Repeated floor display-list slot."); floor_found = true;
            } else if (std::regex_match(line, m, std::regex(R"(\.LONG\s+BAKLST([1-8]),\s*WORLDTLX[0-8]*\+16)"))) {
                int slot = std::stoi(m[1]);
                require(seen_slots.insert(slot).second, "Repeated background display-list slot.");
                if (modules.count(slot)) {
                    if (!floor_found) ++out.layout.backgrounds_before;
                    out.layout.background_modules.push_back(modules.at(slot));
                }
            }
        }
        require(floor_found && out.layout.background_modules.size() == modules.size(), "Unsupported floor display-list order.");
        BddImageMetadata meta{}; std::snprintf(meta.label, sizeof meta.label, "%s", out.label.c_str());
        std::snprintf(meta.source, sizeof meta.source, "%s", bin.filename().u8string().c_str());
        bank->data.images.push_back(std::move(im)); bank->data.palettes.push_back(palette);
        bank->metadata.push_back(meta); bank->default_palettes.push_back(0);
        out.layout.artwork.assets = bank; out.source = bin.u8string();
        out.notice = "Static game-floor reference at the authored start; camera movement uses 1x scrolling. Runtime skew, row skipping and palette effects are not simulated.";
    } catch (const std::exception &e) { out = {}; out.notice = e.what(); }
    return out;
}
} // namespace studio
