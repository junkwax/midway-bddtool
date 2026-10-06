#include "Core/studio_floor.h"
#include "Core/studio_assembly.h"
#include "Core/studio_mk3_layout.h"
#include <cmath>
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
std::shared_ptr<const AssetBank> raw_floor(const fs::path &bin, const std::string &label,
        const std::string &palette_label, const std::vector<std::string> &palette_words, int height) {
    require(!palette_words.empty(), "Missing floor palette.");
    int count = (int)number(palette_words.front());
    require(count > 0 && count <= 64 && palette_words.size() == size_t(count + 1), "Unsupported floor palette.");
    auto bank = std::make_shared<AssetBank>();
    BddCorePalette palette{}; palette.count = count + 1;
    std::snprintf(palette.name, sizeof palette.name, "%s", palette_label.c_str());
    // Raw floors have opaque index zero; reserve a new transparent zero for authoring.
    for (int i = 0; i < count; ++i) {
        auto value = number(palette_words[i + 1]); require(value <= 65535, "Invalid floor palette word.");
        palette.rgb555[i + 1] = (uint16_t)value;
        palette.argb[i + 1] = bdd_core_rgb555_to_argb((uint16_t)value);
    }
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
    BddImageMetadata meta{}; std::snprintf(meta.label, sizeof meta.label, "%s", label.c_str());
    std::snprintf(meta.source, sizeof meta.source, "%s", bin.filename().u8string().c_str());
    bank->data.images.push_back(std::move(im)); bank->data.palettes.push_back(palette);
    bank->metadata.push_back(meta); bank->default_palettes.push_back(0);
    return bank;
}
}

Rect FloorPreview::rect(const State &stage, Point camera) const {
    if (!ready()) return {};
    const auto &im = layout.artwork.assets->data.images.front();
    // setup_floor_info centers the raw texture 400 pixels into its 1200-pixel rows.
    return {double(stage.start_x) * scroll + screen_x - camera.x * scroll,
            double(stage.start_y + screen_y) - camera.y, double(im.w), double(im.h)};
}

FloorLibraryDiscovery discover_floor_libraries(const std::vector<std::string> &folders, const std::string &selected_path) {
    FloorLibraryDiscovery out;
    std::set<std::string> seen_files, seen_folders;
    const std::regex floor_filename(R"(MKFLOOR[^.]*\.IMG)");
    auto canonical = [](const fs::path &path) {
        std::error_code ec;
        auto resolved = fs::weakly_canonical(path, ec);
        auto key = (ec ? path.lexically_normal() : resolved).generic_u8string();
#ifdef _WIN32
        key = upper(key);
#endif
        return key;
    };
    auto add = [&](const fs::path &path) {
        if (!seen_files.insert(canonical(path)).second) return;
        if (out.paths.size() >= 128) { out.notice = "Showing the first 128 nearby floor libraries. Use Open IMG for another file."; return; }
        out.paths.push_back(path.u8string());
    };
    for (const auto &folder : folders) {
        if (folder.empty()) continue;
        auto directory = fs::u8path(folder);
        if (!seen_folders.insert(canonical(directory)).second) continue;
        std::error_code ec;
        if (!fs::exists(directory, ec) && !ec) continue;
        std::vector<fs::path> matches;
        fs::directory_iterator it(directory, ec), end;
        size_t scanned = 0;
        while (!ec && it != end) {
            if (++scanned > 8192) { out.notice = "Folder scan stopped at 8192 entries. Use Open IMG for another file."; break; }
            const auto &path = it->path();
            if (std::regex_match(upper(path.filename().u8string()), floor_filename) && it->is_regular_file(ec))
                matches.push_back(path);
            if (!ec) it.increment(ec);
        }
        if (ec) out.notice = "Could not read a nearby folder. Use Open IMG to choose a library.";
        std::sort(matches.begin(), matches.end(), [](const fs::path &a, const fs::path &b) {
            auto an = upper(a.filename().u8string()), bn = upper(b.filename().u8string());
            if ((an == "MKFLOORS.IMG") != (bn == "MKFLOORS.IMG")) return an == "MKFLOORS.IMG";
            return an < bn;
        });
        for (const auto &path : matches) add(path);
    }
    if (!selected_path.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(selected_path), ec)) add(fs::u8path(selected_path));
    }
    return out;
}

FloorComparison compare_floor_pixels(const FloorPreview &candidate, const FloorPreview &runtime) {
    auto valid = [](const FloorPreview &floor) {
        if (!floor.ready()) return false;
        const auto &bank = *floor.layout.artwork.assets;
        const auto &im = bank.data.images.front();
        if (bank.data.palettes.empty() || im.w < 1 || im.w > 4096 || im.h < 1 || im.h > 254 ||
            im.pix.size() != size_t(im.w) * im.h) return false;
        int count = bank.data.palettes.front().count;
        return count > 0 && count <= 256 && std::all_of(im.pix.begin(), im.pix.end(), [count](uint8_t index) { return index < count; });
    };
    if (!valid(candidate) || !valid(runtime)) return {};
    const auto &a = *candidate.layout.artwork.assets, &b = *runtime.layout.artwork.assets;
    const auto &ai = a.data.images.front(), &bi = b.data.images.front();
    if (ai.w != bi.w || ai.h != bi.h) return {FloorMatch::DifferentSize};
    FloorComparison out; out.match = FloorMatch::Exact; out.total_pixels = ai.pix.size();
    for (size_t i = 0; i < ai.pix.size(); ++i) {
        uint32_t ac = a.data.palettes.front().argb[ai.pix[i]], bc = b.data.palettes.front().argb[bi.pix[i]];
        if (!(ac >> 24)) ac = 0;
        if (!(bc >> 24)) bc = 0;
        if (ac != bc) ++out.different_pixels;
    }
    if (out.different_pixels) out.match = FloorMatch::DifferentPixels;
    return out;
}

FloorLibrary inspect_floor_library(const std::string &path) {
    FloorLibrary out; out.path = path;
    auto library = inspect_animation_library(path);
    out.error = library.error;
    if (!out.error.empty()) return out;
    std::map<std::string, std::vector<std::pair<int, AnimationLibraryImage>>> groups;
    for (const auto &image : library.images) {
        std::smatch m; int number = 0; std::string key = image.label;
        if (std::regex_match(image.label, m, std::regex(R"((.*[^0-9])([0-9]{1,3}))"))) {
            number = std::stoi(m[2]);
            if (number > 0 && number <= 128) key = m[1]; else number = 0;
        }
        // Keep an unnumbered image distinct from numbered strips of the same name.
        groups[key + (number ? "#strips" : "#image")].push_back({number, image});
    }
    for (auto &group : groups) {
        auto &parts = group.second;
        std::stable_sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        FloorLibraryEntry entry;
        entry.label = parts.size() > 1 ? group.first.substr(0, group.first.rfind('#')) : parts.front().second.label;
        int expected = 1;
        for (const auto &part : parts) {
            const auto &im = part.second;
            entry.strips.push_back(im.label); entry.width = std::min(4097, entry.width + im.width); entry.height = std::max(entry.height, im.height);
            if (!im.problem.empty()) entry.problem = im.label + ": " + im.problem;
            if (parts.size() > 1 && part.first != expected++) entry.problem = "Numbered strips must be unique and consecutive, starting at 1.";
            if (im.width < 3) entry.problem = "Floor strips must be at least 3 pixels wide.";
        }
        if (entry.width > 4096 || entry.height > 254 || entry.strips.size() > 128)
            entry.problem = "Floor exceeds 4096 x 254 or 128 strips.";
        out.entries.push_back(std::move(entry));
    }
    return out;
}

FloorPreview load_floor_library_entry(const FloorLibrary &library, size_t entry_index, const FloorPreview &runtime) {
    FloorPreview out;
    try {
        require(library.error.empty(), library.error);
        require(entry_index < library.entries.size(), "Choose a floor from the IMG library.");
        const auto &entry = library.entries[entry_index];
        require(entry.problem.empty(), entry.problem);
        auto decoded = load_animation_selection(library.path, entry.strips, 5, true);
        require(decoded.ready(), decoded.notice);
        const auto &bank = *decoded.artwork.assets;
        int width = 0, height = 0;
        for (const auto &frame : decoded.frames) {
            const auto &im = bank.data.images.at(frame.image);
            width += im.w; height = std::max(height, im.h);
        }
        require(width == entry.width && height == entry.height && width > 0 && width <= 4096 && height > 0 && height <= 254,
                "IMG dimensions changed or exceed floor limits. Reopen the library.");
        std::vector<uint8_t> rgba(size_t(width) * height * 4, 0);
        int left = 0;
        for (const auto &frame : decoded.frames) {
            const auto &im = bank.data.images.at(frame.image); const auto &pal = bank.data.palettes.at(frame.palette);
            for (int y = 0; y < im.h; ++y) for (int x = 0; x < im.w; ++x) {
                auto color = pal.argb[im.pix[size_t(y) * im.w + x]];
                size_t p = (size_t(y + height - im.h) * width + left + x) * 4;
                rgba[p] = uint8_t(color >> 16); rgba[p + 1] = uint8_t(color >> 8);
                rgba[p + 2] = uint8_t(color); rgba[p + 3] = uint8_t(color >> 24);
            }
            left += im.w;
        }
        auto art = std::make_shared<AssetBank>(); std::string error;
        require(make_raster_asset(entry.label, width, height, rgba.data(), *art, error), error);
        std::snprintf(art->metadata.front().source, sizeof art->metadata.front().source, "%s", fs::u8path(library.path).filename().u8string().c_str());
        if (runtime.ready()) out = runtime;
        int bottom = runtime.ready() ? runtime.screen_y + runtime.layout.artwork.assets->data.images.front().h : 254;
        out.layout.artwork.assets = art; out.label = entry.label;
        out.screen_x = (400.0 - width) / 2; out.screen_y = bottom - height;
        out.source = library.path; out.palette_source = library.path;
        out.notice = "IMG floor comparison: numbered strips joined left to right, aligned at the bottom; stored palette zero is opaque. Runtime use, perspective skew and animation are not inferred.";
    } catch (const std::exception &e) { out = {}; out.notice = e.what(); }
    return out;
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
        auto bin = root / "data" / (out.label + ".BIN");
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
        out.layout.artwork.assets = raw_floor(bin, out.label, longs[1], palette_words, height);
        out.source = bin.u8string(); out.palette_source = path.u8string();
        out.notice = "Static game-floor reference at the authored start; camera movement uses 1x scrolling. Runtime skew, row skipping and palette effects are not simulated.";
    } catch (const std::exception &e) { out = {}; out.notice = e.what(); }
    return out;
}
FloorPreview load_mk3_floor_preview(const Document &document, const std::string &mkbt_path,
                                  const std::string &asset_directory) {
    FloorPreview out;
    try {
        auto definition = read_mk3_layout(document, mkbt_path);
        require(definition.valid(), definition.error);
        auto source_path = fs::u8path(mkbt_path);
        Assembly source(source_path);
        require(definition.floor_line < source.lines.size(), "This MK3 stage has no separate floor descriptor.");
        std::vector<std::string> descriptor;
        // MK3 appends the descriptor after the module/center_x terminator. It can
        // have its own label (street_floor_info); it is not a calla MOVI as in MK2.
        for (size_t i = definition.floor_line; i < source.lines.size() && i < definition.floor_line + 32 && descriptor.size() < 5; ++i) {
            const auto &line = source.lines[i]; if (line.empty()) continue;
            std::smatch m;
            auto op = descriptor.size() == 2 ? R"(\.WORD\s+([A-Z0-9_>]+))" : R"(\.LONG\s+([A-Z0-9_>]+))";
            require(std::regex_match(line, m, std::regex(op)), "No supported inline MK3 floor descriptor.");
            if (descriptor.empty()) require(m[1] != "0", "This MK3 stage does not define a separate runtime floor.");
            descriptor.push_back(m[1]);
        }
        require(descriptor.size() == 5 && std::regex_match(descriptor[0], std::regex("FL_[A-Z0-9_]+")),
                "No supported inline MK3 floor descriptor.");
        int height = (int)number(descriptor[2]);
        require(height > 0 && height <= 254, "Unsupported MK3 floor height.");
        std::smatch m;
        require(std::regex_match(descriptor[3], m, std::regex(R"(SCROLLX([1-8]?))")), "Unsupported MK3 floor scroll source.");
        int slot = m[1].str().empty() ? 0 : std::stoi(m[1]);
        out.scroll = definition.scroll_rates.at(8 - slot);
        require(std::isfinite(out.scroll) && std::abs(out.scroll) <= 16, "Unsupported MK3 floor scroll rate.");
        out.label = descriptor[0]; out.screen_y = 254 - height; out.mk3 = true;

        std::vector<std::string> palette_words;
        if (source.labels.count(descriptor[1])) {
            palette_words = operands(source, descriptor[1], "WORD"); out.palette_source = source_path.u8string();
        } else {
            auto palette_path = source_path.parent_path() / "BGNDPAL.ASM";
            require(fs::is_regular_file(palette_path) && fs::file_size(palette_path) <= 8 * 1024 * 1024,
                    "Floor palette " + descriptor[1] + " is missing; keep BGNDPAL.ASM beside MKBT.ASM.");
            Assembly palettes(palette_path);
            palette_words = operands(palettes, descriptor[1], "WORD"); out.palette_source = palette_path.u8string();
        }
        auto assets = asset_directory.empty() ? fs::u8path(document.path()).parent_path() : fs::u8path(asset_directory);
        auto file = out.label + ".BIN";
        std::vector<fs::path> candidates;
        if (!assets.empty()) candidates = {assets / "BINFILES" / file, assets / file, assets / "data" / file};
        auto parent = source_path.parent_path();
        candidates.insert(candidates.end(), {parent / "BINFILES" / file, parent / file, parent / "data" / file, parent.parent_path() / "data" / file});
        fs::path bin;
        for (const auto &candidate : candidates) if (fs::is_regular_file(candidate)) { bin = candidate; break; }
        require(!bin.empty(), "Missing " + file + ". Keep its BINFILES folder beside the map, or the texture beside the selected MKBT.ASM.");

        int rank = 0, floor_rank = -1; bool ended = false;
        auto start = source.at(definition.display_list);
        for (size_t i = start; i < source.lines.size() && i < start + 128; ++i) {
            const auto &line = source.lines[i]; if (line.empty()) continue;
            if (std::regex_match(line, std::regex(R"(\.LONG\s+0)"))) { ended = true; break; }
            if (std::regex_match(line, std::regex(R"(\.LONG\s+-1,\s*FLOOR_CODE)"))) {
                require(floor_rank < 0, "Repeated MK3 floor display-list slot."); floor_rank = rank;
            }
            ++rank;
        }
        require(ended && floor_rank >= 0, "MK3 stage has no supported floor display-list slot.");
        auto planes = definition.planes;
        std::sort(planes.begin(), planes.end(), [](const Plane &a, const Plane &b) { return a.rank < b.rank; });
        for (const auto &plane : planes) {
            out.layout.background_modules.push_back(upper(plane.source.name));
            if (plane.rank < floor_rank) ++out.layout.backgrounds_before;
        }
        out.layout.artwork.assets = raw_floor(bin, out.label, descriptor[1], palette_words, height);
        out.source = bin.u8string();
        out.notice = "MK3 floor reference: source palette, bottom alignment and floor scroll rate. Perspective skew, palette animation and stage callbacks are not simulated.";
    } catch (const std::exception &e) { out = {}; out.mk3 = true; out.notice = e.what(); }
    return out;
}
} // namespace studio
