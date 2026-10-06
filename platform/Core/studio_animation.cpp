#include "Core/studio_animation.h"
#include "Core/studio_assembly.h"
#include "Core/img_format.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
namespace fs = std::filesystem;
using namespace assembly;
struct ImgDirectory {
    std::unique_ptr<FILE, decltype(&fclose)> file{nullptr, fclose};
    long size = 0;
    std::vector<std::pair<std::string, ImgImageDisk>> images;
    std::vector<ImgPaletteDisk> palettes;
    explicit ImgDirectory(const fs::path &path) {
#ifdef _WIN32
        file.reset(_wfopen(path.c_str(), L"rb"));
#else
        file.reset(fopen(path.c_str(), "rb"));
#endif
        require(bool(file), "Cannot open IMG library: " + path.u8string());
        auto raw = file.get();
        size = img_file_size_for_import(raw);
        ImgLibHeaderDisk header{};
        require(size >= (long)sizeof header && fread(&header, sizeof header, 1, raw) == 1 &&
                    header.temp == 0xabcd && header.version >= 0x500 &&
                    header.palcnt >= IMG_NUM_DEFAULT_PALS && header.oset >= sizeof header,
                "Invalid IMG library header.");
        auto count = header.palcnt - IMG_NUM_DEFAULT_PALS;
        uint64_t end = (uint64_t)header.oset + (uint64_t)header.imgcnt * sizeof(ImgImageDisk) +
                       (uint64_t)count * sizeof(ImgPaletteDisk);
        require(end <= (uint64_t)size, "Truncated IMG directory.");
        require(fseek(raw, header.oset, SEEK_SET) == 0, "Cannot read IMG directory.");
        for (unsigned i = 0; i < header.imgcnt; i++) {
            ImgImageDisk record{};
            require(fread(&record, sizeof record, 1, raw) == 1, "Truncated IMG image record.");
            char label[64];
            img_raw_name_to_upper(record.name, sizeof record.name, "", label, sizeof label);
            images.emplace_back(label, record);
        }
        palettes.resize(count);
        require(!count ||
                    fread(palettes.data(), sizeof(ImgPaletteDisk), count, raw) == (size_t)count,
                "Truncated IMG palette directory.");
    }
    std::string problem(const ImgImageDisk &r) const {
        int pi = (int)r.palnum - IMG_NUM_DEFAULT_PALS;
        if (!r.w || r.w > 1024 || !r.h || r.h > 1024)
            return "Unsupported dimensions (maximum 1024 per axis).";
        if (pi < 0 || pi >= (int)palettes.size())
            return "External/default palette is not available in this IMG.";
        const auto &p = palettes[pi];
        if (!p.numc || p.numc > 256 || (uint64_t)p.oset + p.numc * 2 > (uint64_t)size)
            return "Invalid palette data.";
        if (r.oset >= (uint64_t)size)
            return "Invalid pixel offset.";
        return {};
    }
};
void read_art(const fs::path &path, const std::vector<std::string> &names, AnimationPreview &out, bool opaque_zero = false) {
    ImgDirectory directory(path);
    auto raw = directory.file.get();
    auto size = directory.size;
    const auto &palette_records = directory.palettes;
    int palettes = (int)palette_records.size();
    std::map<std::string, ImgImageDisk> images;
    for (const auto &entry : directory.images)
        require(images.emplace(entry.first, entry.second).second ||
                    std::find(names.begin(), names.end(), entry.first) == names.end(),
                "Duplicate animation image: " + entry.first);
    size_t total_pixels = 0;
    auto bank = std::make_shared<AssetBank>();
    std::map<int, int> palette_slots;
    std::map<std::string, int> frame_slots;
    for (const auto &name : names) {
        auto existing = frame_slots.find(name);
        if (existing != frame_slots.end()) {
            out.sequence.push_back(existing->second);
            continue;
        }
        auto it = images.find(name);
        require(it != images.end(), "IMG library is missing animation frame " + name);
        const auto &record = it->second;
        auto problem = directory.problem(record);
        require(problem.empty(), name + ": " + problem);
        int palette = (int)record.palnum - IMG_NUM_DEFAULT_PALS;
        require(palette >= 0 && palette < palettes, "Missing source palette for " + name);
        if (!palette_slots.count(palette)) {
            const auto &p = palette_records[palette];
            require(p.numc > 0 && p.numc <= 256 && (uint64_t)p.oset + p.numc * 2 <= (uint64_t)size,
                    "Invalid animation palette.");
            BddCorePalette colors{};
            img_raw_name_to_upper(p.name, sizeof p.name, "IMG_PALETTE", colors.name, sizeof colors.name);
            colors.count = p.numc;
            require(fseek(raw, p.oset, SEEK_SET) == 0, "Cannot seek animation palette.");
            for (int i = 0; i < colors.count; i++) {
                unsigned char bytes[2];
                require(fread(bytes, 1, 2, raw) == 2, "Truncated animation palette.");
                colors.rgb555[i] = bytes[0] | (bytes[1] << 8);
                colors.argb[i] = img_pal_word_to_argb(colors.rgb555[i], i);
            }
            if (opaque_zero) {
                require(colors.count < 256, "Opaque floor zero needs a free palette entry (maximum 255 source colors).");
                colors.rgb555[colors.count] = colors.rgb555[0];
                colors.argb[colors.count++] = img_pal_word_to_argb_opaque(colors.rgb555[0]);
            }
            palette_slots[palette] = (int)bank->data.palettes.size();
            bank->data.palettes.push_back(colors);
        }
        BddCoreImage image{};
        image.idx = (int)bank->data.images.size();
        image.w = std::max(3, (int)record.w);
        image.h = record.h;
        require(image.w <= 4096 && image.h > 0 && image.h <= 4096 &&
                    (uint64_t)image.w * image.h <= 2097152,
                "Invalid animation dimensions.");
        total_pixels += (size_t)image.w * image.h;
        require(total_pixels <= 2097152, "Selected frames exceed the two-million-pixel limit.");
        image.pix.resize((size_t)image.w * image.h);
        require(img_decode_pixels(raw, size, &record, image.w, image.h, image.pix.data(), nullptr,
                                  nullptr) != 0,
                "Cannot decode animation frame " + name);
        int pi = palette_slots.at(palette);
        for (auto pixel : image.pix)
            require(pixel < bank->data.palettes[pi].count - (opaque_zero ? 1 : 0),
                    "Animation frame references an absent palette color.");
        if (opaque_zero) {
            // A second decode distinguishes stored zero from transparent trimmed margins.
            std::vector<uint8_t> coverage(image.pix.size(), 1);
            if (record.flags & 0x80) {
                uint8_t solid[256]; std::fill(std::begin(solid), std::end(solid), uint8_t(1));
                require(img_decode_pixels(raw, size, &record, image.w, image.h, coverage.data(), solid, nullptr) != 0,
                        "Cannot decode floor coverage.");
            }
            for (size_t i = 0; i < image.pix.size(); ++i)
                if (!image.pix[i] && coverage[i]) image.pix[i] = uint8_t(bank->data.palettes[pi].count - 1);
        }
        int slot = (int)out.frames.size();
        out.frames.push_back({image.idx, pi, img_s16(record.anix), img_s16(record.aniy), name});
        BddImageMetadata meta{};
        meta.idx = image.idx;
        meta.anix = img_s16(record.anix); meta.aniy = img_s16(record.aniy);
        meta.anix2 = img_s16(record.anix2); meta.aniy2 = img_s16(record.aniy2);
        meta.aniz2 = img_s16(record.aniz2); meta.frm = record.frm;
        meta.pttblnum = record.pttblnum; meta.opals = record.opals;
        std::snprintf(meta.label, sizeof meta.label, "%s", name.c_str());
        std::snprintf(meta.source, sizeof meta.source, "%s", path.filename().u8string().c_str());
        bank->metadata.push_back(meta);
        bank->default_palettes.push_back(pi);
        bank->data.images.push_back(std::move(image));
        frame_slots[name] = slot;
        out.sequence.push_back(slot);
    }
    out.artwork.assets = bank;
}
} // namespace

AnimationLibrary inspect_animation_library(const std::string &path) {
    AnimationLibrary out;
    out.path = path;
    try {
        ImgDirectory directory(fs::u8path(path));
        std::map<std::string, int> counts;
        for (const auto &entry : directory.images)
            counts[entry.first]++;
        for (const auto &entry : directory.images) {
            const auto &r = entry.second;
            auto problem = directory.problem(r);
            if (entry.first.empty())
                problem = "Unnamed image.";
            else if (counts[entry.first] != 1)
                problem = "Duplicate image label.";
            out.images.push_back({entry.first, problem, r.w, r.h,
                                  (int)r.palnum - IMG_NUM_DEFAULT_PALS, img_s16(r.anix),
                                  img_s16(r.aniy)});
        }
    } catch (const std::exception &e) {
        out.error = e.what();
        out.images.clear();
    }
    return out;
}
AnimationPreview load_animation_selection(const std::string &path,
                                          const std::vector<std::string> &labels,
                                          int preview_ticks, bool opaque_zero) {
    AnimationPreview out;
    try {
        require(!labels.empty() && labels.size() <= 128, "Select between 1 and 128 frame entries.");
        require(preview_ticks >= 1 && preview_ticks <= 60, "Preview duration must be 1..60 ticks.");
        std::vector<std::string> names;
        for (const auto &label : labels) {
            require(!label.empty(), "An image label is empty.");
            names.push_back(upper(label));
        }
        read_art(fs::u8path(path), names, out, opaque_zero);
        out.anchors.push_back({0, 0});
        out.frame_ticks = preview_ticks;
        out.manual_sequence = true;
        out.source = path;
        out.notice = "Manual IMG comparison: selection order and preview timing only. "
                     "IMG frame anchors are preserved; runtime sequence, actors, palette changes "
                     "and driver behavior are not verified.";
    } catch (const std::exception &e) {
        out = {};
        out.notice = e.what();
    }
    return out;
}
size_t AnimationPreview::frame_at(double seconds) const {
    if (sequence.empty() || !std::isfinite(seconds) || seconds < 0)
        return 0;
    // An authoring loop at 60 preview ticks/s. The game's random idle pauses are omitted.
    return (size_t)std::fmod(std::floor(seconds * 60.0 / std::max(1, frame_ticks)),
                             (double)sequence.size());
}
Rect AnimationPreview::rect(size_t actor, size_t step, Point camera) const {
    if (!ready() || actor >= anchors.size())
        return {};
    const auto &frame = frames[sequence[step % sequence.size()]];
    const auto &im = artwork.assets->data.images[frame.image];
    return {anchors[actor].x - frame.anchor_x - camera.x * scroll,
            anchors[actor].y - frame.anchor_y - camera.y, (double)im.w, (double)im.h};
}
double AnimationPreview::draw_rank(const Document &document) const {
    std::vector<int> ranks;
    for (const auto &p : document.state().planes)
        if (std::find(background_modules.begin(), background_modules.end(), upper(p.source.name)) !=
            background_modules.end())
            ranks.push_back(p.rank);
    std::sort(ranks.begin(), ranks.end());
    if (ranks.empty())
        return 10000;
    if (backgrounds_before == 0)
        return ranks.front() - .5;
    if (backgrounds_before >= ranks.size())
        return ranks.back() + .5;
    return (ranks[backgrounds_before - 1] + ranks[backgrounds_before]) / 2.0;
}
AnimationPreview load_animation_preview(const Document &document, const std::string &game_root) {
    AnimationPreview out;
    try {
        if (game_root.empty() || !document.state().has_bdb) {
            out.notice = "Choose a game checkout in Build & Check to load runtime animations.";
            return out;
        }
        auto root = fs::u8path(game_root);
        auto path = root / "src" / "BGND.ASM";
        if (!fs::is_regular_file(path))
            path = root / "src-refactor" / "src" / "BGND.ASM";
        Assembly source(path);
        if (!source.labels.count("FOREST_MOD")) {
            out.notice = "No supported runtime animation in this checkout.";
            return out;
        }
        std::string calla, dlists, rates;
        int count = 0, slot = 0;
        std::map<int, std::string> modules;
        std::regex long_op(R"(\.LONG\s+([A-Z_0-9]+))");
        std::smatch m;
        for (const auto &line : source.block("FOREST_MOD")) {
            if (!std::regex_match(line, m, long_op))
                continue;
            std::string value = m[1];
            count++;
            if (count == 1)
                calla = value;
            if (count == 2)
                rates = value;
            if (count == 3)
                dlists = value;
            if (count <= 4)
                continue;
            if (value == "0" || value == "0FFFFFFFFH")
                break;
            ++slot;
            if (value == "SKIP_BAKMOD")
                continue;
            require(value.size() > 4 && value.substr(value.size() - 4) == "BMOD",
                    "Unsupported Forest module list.");
            modules[slot] = value.substr(0, value.size() - 4);
        }
        std::set<std::string> names;
        for (const auto &p : document.state().planes)
            names.insert(upper(p.source.name));
        if (modules.empty() || !std::all_of(modules.begin(), modules.end(),
                                            [&](const auto &p) { return names.count(p.second); })) {
            out.notice = "Runtime animation preview currently supports the Forest tree faces.";
            return out;
        }
        auto calls = source.block(calla);
        require(std::any_of(calls.begin(), calls.end(),
                            [](const auto &line) {
                                return std::regex_match(
                                    line, std::regex(R"(CREATE\s+PID_BANI\s*,\s*TREE_ANIMATOR)"));
                            }),
                "Forest does not spawn the supported tree animator.");
        auto start = source.at("TREE_ANIMATOR"), stop = source.at("TRIPLE_FRAMEW");
        require(stop > start && stop - start < 160, "Unsupported tree animator body.");
        std::string sequence;
        bool tick_found = false, driver_found = false, pending = false;
        Point anchor;
        for (size_t i = start; i < stop; i++) {
            const auto &line = source.lines[i];
            if (std::regex_match(line, m, std::regex(R"(MOVI\s+([^,]+),\s*A4)"))) {
                uint32_t xy = number(m[1]);
                anchor = {(double)img_s16((unsigned short)xy),
                          (double)img_s16((unsigned short)(xy >> 16))};
                pending = true;
            } else if (std::regex_match(line, std::regex(R"(CALLR\s+MAKE_A_MAD_TREE)"))) {
                require(pending, "Tree position is not a supported literal.");
                out.anchors.push_back(anchor);
                pending = false;
            }
            if (std::regex_match(line, m, std::regex(R"(MOVI\s+(A_[A-Z_0-9]+),\s*A9)")))
                sequence = m[1];
            if (std::regex_match(line, m, std::regex(R"(MOVK\s+([^,]+),\s*A0)"))) {
                out.frame_ticks = (int)number(m[1]);
                tick_found = true;
            }
            if (std::regex_match(line, std::regex(R"(JSRP\s+TRIPLE_FRAMEW)")))
                driver_found = true;
        }
        require(out.anchors.size() == 3 && tick_found && driver_found && out.frame_ticks > 0 &&
                    out.frame_ticks <= 60,
                "Unsupported Forest positions or animation timing.");
        int actor_slot = 0;
        for (const auto &line : source.block("MAKE_A_MAD_TREE"))
            if (std::regex_match(line, m, std::regex(R"(MOVI\s+BAKLST([1-8]),\s*B4)")))
                actor_slot = std::stoi(m[1]);
        require(actor_slot > 0 && !modules.count(actor_slot),
                "Unsupported tree actor insertion list.");
        std::vector<uint32_t> scrolls;
        for (const auto &line : source.block(rates))
            if (std::regex_match(line, m, std::regex(R"(\.LONG\s+([^,]+))")))
                scrolls.push_back(number(m[1]));
        require(scrolls.size() == 9 && scrolls[8 - actor_slot] == 0x20000,
                "Forest actor scroll is not the supported 1x world projection.");
        bool actor_found = false;
        std::set<int> seen;
        for (const auto &line : source.block(dlists))
            if (std::regex_match(line, m,
                                 std::regex(R"(\.LONG\s+BAKLST([1-8]),\s*WORLDTLX[0-8]*\+16)"))) {
                int s = std::stoi(m[1]);
                require(seen.insert(s).second, "Repeated Forest display-list slot.");
                if (s == actor_slot)
                    actor_found = true;
                if (modules.count(s)) {
                    if (!actor_found)
                        out.backgrounds_before++;
                    out.background_modules.push_back(modules.at(s));
                }
            }
        require(actor_found && out.background_modules.size() == modules.size(),
                "Missing Forest display-list entries.");
        std::vector<std::string> frames;
        bool terminated = false;
        for (const auto &line : source.block(sequence)) {
            if (line.empty())
                continue;
            require(std::regex_match(line, m, long_op), "Unsupported animation frame directive.");
            if (m[1] == "0") {
                terminated = true;
                break;
            }
            std::string name = m[1];
            require(name.rfind("TREEANI", 0) == 0 && frames.size() < 128,
                    "Unsupported tree animation frame or opcode.");
            frames.push_back(name);
        }
        require(terminated && !frames.empty(), "Unterminated or empty tree animation.");
        read_art(root / "data" / "MKBGANI.IMG", frames, out);
        out.source = path.u8string();
        out.notice = "Forest faces: source positions and frame offsets. Roar loops at 60 preview "
                     "ticks/s; random game pauses are omitted.";
    } catch (const std::exception &e) {
        out = {};
        out.notice = e.what();
    }
    return out;
}
} // namespace studio
