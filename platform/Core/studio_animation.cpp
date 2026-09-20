#include "Core/studio_animation.h"
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
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string upper(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return s;
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
struct Assembly {
    std::vector<std::string> lines;
    std::map<std::string, size_t> labels;
    std::set<std::string> duplicates;
    explicit Assembly(const fs::path &path) {
        std::ifstream in(path);
        require(bool(in), "Cannot read " + path.u8string());
        std::string raw;
        while (std::getline(in, raw)) {
            auto s = upper(raw.substr(0, raw.find(';')));
            if (!s.empty() && s[0] == '*')
                s.clear();
            if (!s.empty() && s[0] != ' ' && s[0] != '\t' && s[0] != '\r') {
                auto end = s.find_first_of(" \t:\r");
                auto name = s.substr(0, end);
                if (!labels.emplace(name, lines.size()).second)
                    duplicates.insert(name);
                s = end == std::string::npos ? "" : s.substr(end + 1);
            }
            lines.push_back(trim(s));
        }
    }
    size_t at(const std::string &name) const {
        auto it = labels.find(name);
        require(it != labels.end() && !duplicates.count(name),
                "Missing or ambiguous animation label: " + name);
        return it->second;
    }
    size_t end(size_t start) const {
        size_t result = lines.size();
        for (const auto &p : labels)
            if (p.second > start)
                result = std::min(result, p.second);
        return result;
    }
    std::vector<std::string> block(const std::string &name) const {
        auto begin = at(name);
        return {lines.begin() + begin, lines.begin() + end(begin)};
    }
};
uint32_t number(std::string s) {
    s = trim(s);
    int base = 10;
    if (!s.empty() && s[0] == '>') {
        base = 16;
        s.erase(0, 1);
    } else if (!s.empty() && s.back() == 'H') {
        base = 16;
        s.pop_back();
    }
    size_t used = 0;
    auto value = std::stoull(s, &used, base);
    require(used == s.size() && value <= 0xffffffffULL, "Unsupported animation number: " + s);
    return (uint32_t)value;
}
void read_art(const fs::path &path, const std::vector<std::string> &names, AnimationPreview &out) {
#ifdef _WIN32
    FILE *raw = _wfopen(path.c_str(), L"rb");
#else
    FILE *raw = fopen(path.c_str(), "rb");
#endif
    std::unique_ptr<FILE, decltype(&fclose)> file(raw, fclose);
    require(raw != nullptr, "Cannot find MKBGANI.IMG in the selected checkout's data folder.");
    auto size = img_file_size_for_import(raw);
    ImgLibHeaderDisk header{};
    require(size >= (long)sizeof header && fread(&header, sizeof header, 1, raw) == 1 &&
                header.temp == 0xabcd && header.version >= 0x500 &&
                header.palcnt >= IMG_NUM_DEFAULT_PALS,
            "Invalid MKBGANI.IMG header.");
    auto palettes = header.palcnt - IMG_NUM_DEFAULT_PALS;
    uint64_t end = (uint64_t)header.oset + (uint64_t)header.imgcnt * sizeof(ImgImageDisk) +
                   (uint64_t)palettes * sizeof(ImgPaletteDisk);
    require(end <= (uint64_t)size, "Truncated MKBGANI.IMG directory.");
    std::map<std::string, ImgImageDisk> images;
    require(fseek(raw, header.oset, SEEK_SET) == 0, "Cannot read IMG directory.");
    for (unsigned i = 0; i < header.imgcnt; i++) {
        ImgImageDisk record{};
        require(fread(&record, sizeof record, 1, raw) == 1, "Truncated IMG image record.");
        char label[64];
        img_raw_name_to_upper(record.name, sizeof record.name, "", label, sizeof label);
        require(images.emplace(label, record).second ||
                    std::find(names.begin(), names.end(), label) == names.end(),
                "Duplicate animation image: " + std::string(label));
    }
    std::vector<ImgPaletteDisk> palette_records(palettes);
    require(palettes == 0 || fread(palette_records.data(), sizeof(ImgPaletteDisk), palettes, raw) ==
                                 (size_t)palettes,
            "Truncated IMG palette directory.");
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
        require(it != images.end(), "MKBGANI.IMG is missing animation frame " + name);
        const auto &record = it->second;
        int palette = (int)record.palnum - IMG_NUM_DEFAULT_PALS;
        require(palette >= 0 && palette < palettes, "Missing source palette for " + name);
        if (!palette_slots.count(palette)) {
            const auto &p = palette_records[palette];
            require(p.numc > 0 && p.numc <= 256 && (uint64_t)p.oset + p.numc * 2 <= (uint64_t)size,
                    "Invalid animation palette.");
            BddCorePalette colors{};
            colors.count = p.numc;
            require(fseek(raw, p.oset, SEEK_SET) == 0, "Cannot seek animation palette.");
            for (int i = 0; i < colors.count; i++) {
                unsigned char bytes[2];
                require(fread(bytes, 1, 2, raw) == 2, "Truncated animation palette.");
                colors.rgb555[i] = bytes[0] | (bytes[1] << 8);
                colors.argb[i] = img_pal_word_to_argb(colors.rgb555[i], i);
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
        image.pix.resize((size_t)image.w * image.h);
        require(img_decode_pixels(raw, size, &record, image.w, image.h, image.pix.data(), nullptr,
                                  nullptr) != 0,
                "Cannot decode animation frame " + name);
        int pi = palette_slots.at(palette);
        for (auto pixel : image.pix)
            require(pixel < bank->data.palettes[pi].count,
                    "Animation frame references an absent palette color.");
        int slot = (int)out.frames.size();
        out.frames.push_back({image.idx, pi, img_s16(record.anix), img_s16(record.aniy), name});
        bank->data.images.push_back(std::move(image));
        frame_slots[name] = slot;
        out.sequence.push_back(slot);
    }
    out.artwork.assets = bank;
}
} // namespace

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
