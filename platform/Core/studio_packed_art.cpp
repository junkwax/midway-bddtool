#include "Core/studio_packed_art.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
void need(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
uint64_t fingerprint(const std::string &s, uint64_t value = 14695981039346656037ULL) {
    for (unsigned char c : s)
        value = (value ^ c) * 1099511628211ULL;
    return value; // Change detector, not a cryptographic attestation.
}
std::string clean(std::string s) {
    s = s.substr(0, s.find(';'));
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
uint64_t number(std::string token) {
    token = clean(token);
    need(!token.empty() && token[0] != '-', "Invalid background header number");
    int radix = 10;
    if (token.back() == 'h' || token.back() == 'H') {
        token.pop_back();
        radix = 16;
    } else if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0)
        radix = 16;
    size_t used = 0;
    auto value = std::stoull(token, &used, radix);
    need(used == token.size() && value <= 0xffffffffULL, "Unsupported background header number");
    return value;
}
struct Header {
    int w, h;
    uint64_t address, control;
};
std::map<std::string, std::vector<Header>> headers(const std::string &text) {
    std::map<std::string, std::vector<Header>> result;
    std::istringstream in(text);
    std::string line, table;
    std::vector<uint64_t> values;
    auto finish = [&] {
        if (table.empty())
            return;
        need(!values.empty() && values.size() % 4 == 0 && values.size() <= 4 * 65536,
             "Incomplete or oversized background table: " + table);
        auto &rows = result.at(table);
        for (size_t i = 0; i < values.size(); i += 4) {
            need(values[i] > 0 && values[i] <= 4096 && values[i + 1] > 0 && values[i + 1] <= 4096 &&
                     values[i + 3] <= 65535,
                 "Unsupported background image geometry/control");
            rows.push_back({(int)values[i], (int)values[i + 1], values[i + 2], values[i + 3]});
        }
        table.clear();
        values.clear();
    };
    const std::regex label("([A-Za-z_][A-Za-z0-9_]*HDRS|HDRS):");
    while (std::getline(in, line)) {
        line = clean(line);
        if (line.empty())
            continue;
        std::smatch match;
        if (std::regex_match(line, match, label)) {
            finish();
            table = match[1].str();
            need(result.emplace(table, std::vector<Header>{}).second, "Duplicate background table");
        } else if (!table.empty()) {
            std::istringstream row(line);
            std::string op, operands, token;
            row >> op;
            if (op != ".word" && op != ".long") {
                finish();
                continue;
            }
            std::getline(row, operands);
            std::istringstream fields(operands);
            while (std::getline(fields, token, ','))
                values.push_back(number(token));
        }
    }
    finish();
    need(!result.empty(), "No static background headers found");
    return result;
}
PackedImageFingerprint decode(const std::string &payload, uint64_t base, const Header &h) {
    need(h.address >= base + 0x2000000ULL, "Background address precedes MK7 payload");
    uint64_t bit = h.address - base - 0x2000000ULL;
    auto bits = [&](unsigned count) {
        need(bit <= payload.size() * 8ULL && count <= payload.size() * 8ULL - bit,
             "Background pixels extend outside MK7 payload");
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i, ++bit)
            value |= (((unsigned char)payload[(size_t)(bit / 8)] >> (bit % 8)) & 1) << i;
        return value;
    };
    unsigned bpp = (unsigned)(h.control >> 12) & 7;
    if (!bpp)
        bpp = 8;
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < h.h; ++y) {
        unsigned lead = 0, trail = 0;
        if (h.control & 0x80) {
            auto row = bits(8);
            lead = (row & 15) << ((h.control >> 8) & 3);
            trail = (row >> 4) << ((h.control >> 10) & 3);
        }
        need(lead + trail <= (unsigned)h.w, "Invalid compressed background row");
        for (unsigned x = 0; x < (unsigned)h.w; ++x) {
            unsigned value = x < lead || x >= (unsigned)h.w - trail ? 0 : bits(bpp);
            hash = (hash ^ value) * 1099511628211ULL;
        }
    }
    return {h.w, h.h, hash};
}
} // namespace

void validate_packed_artwork(const PackedArtwork &art) {
    need(art.valid || art.stages.empty(), "Unchecked artwork contains fingerprints");
    if (!art.valid)
        return;
    need(!art.stages.empty() && art.stages.size() <= 512, "Invalid artwork stage count");
    std::set<std::string> names, tables;
    size_t images = 0;
    for (const auto &stage : art.stages) {
        need(std::regex_match(stage.stage, std::regex("[A-Za-z0-9_]{1,64}")) &&
                 std::regex_match(stage.table, std::regex("[A-Za-z_][A-Za-z0-9_]{0,64}")) &&
                 names.insert(stage.stage).second && tables.insert(stage.table).second,
             "Invalid or duplicate artwork stage/table");
        need(!stage.images.empty(), "Empty artwork table");
        images += stage.images.size();
        need(images <= 65536, "Too many artwork fingerprints");
        for (const auto &im : stage.images)
            need(im.width > 0 && im.width <= 4096 && im.height > 0 && im.height <= 4096,
                 "Invalid artwork dimensions");
    }
}

PackedArtwork capture_packed_artwork(const std::string &root, const std::string &payload,
                                     uint64_t base,
                                     std::vector<std::pair<std::string, std::string>> &evidence) {
    PackedArtwork art;
    try {
        auto read = [&](const std::string &relative) {
            auto path = std::filesystem::u8path(root) / relative;
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            need(in && in.tellg() >= 0 && in.tellg() <= 32 * 1024 * 1024,
                 "Missing or oversized artwork input: " + relative);
            std::string data((size_t)in.tellg(), '\0');
            in.seekg(0);
            if (!data.empty())
                in.read(data.data(), (std::streamsize)data.size());
            need((bool)in, "Cannot read artwork input: " + relative);
            evidence.emplace_back(path.u8string(), data);
            return data;
        };
        auto rows = headers(read("tmp/load2/BGNDTBL.MK7"));
        std::istringstream lod(read("data/MK7MIL.LOD"));
        std::string line;
        std::set<std::string> found;
        uint64_t pixels = 0;
        while (std::getline(lod, line)) {
            line = clean(line);
            if (line.rfind("BBB>", 0) != 0)
                continue;
            auto name = clean(line.substr(4));
            need(std::regex_match(name, std::regex("[A-Za-z0-9_]{1,64}")),
                 "Unsupported BBB input in MK7MIL.LOD");
            auto bdb = read("data/" + name + ".BDB"), bdd = read("data/" + name + ".BDD");
            std::istringstream meta(bdb);
            std::string title;
            meta >> title;
            need(title.size() >= 4, "Unsupported short background name");
            auto table = title.substr(4) + "HDRS"; // LOAD2's nametail(name) = name + 4.
            need(rows.count(table) && found.insert(table).second,
                 "Missing or ambiguous background table for " + name);
            PackedStageArtwork stage;
            stage.stage = name;
            stage.table = table;
            stage.source = fingerprint(bdd, fingerprint(std::string(1, '\0'), fingerprint(bdb)));
            std::istringstream bdd_header(bdd.substr(0, bdd.find('\n')));
            size_t count = 0;
            bdd_header >> count;
            need(bdd_header && count == rows.at(table).size(), "BDD/header count differs: " + name);
            for (const auto &h : rows.at(table)) {
                pixels += (uint64_t)h.w * h.h;
                need(pixels <= 64000000, "Static background decode exceeds capture limit");
                stage.images.push_back(decode(payload, base, h));
            }
            art.stages.push_back(std::move(stage));
        }
        need(found.size() == rows.size(), "Unmapped static background tables; coverage incomplete");
        art.valid = true;
        validate_packed_artwork(art);
    } catch (const std::exception &e) {
        art.valid = false;
        art.stages.clear();
        art.error = e.what();
    }
    return art;
}

ArtworkComparison compare_packed_artwork(const PackedArtwork &before, const PackedArtwork &after) {
    ArtworkComparison result;
    std::ostringstream out;
    if (!before.valid || !after.valid) {
        result.report = "Static artwork comparison unavailable. Capture new receipts with MK7 "
                        "background tables and source pairs present.\n";
        return result;
    }
    result.available = true;
    std::map<std::string, const PackedStageArtwork *> a, b;
    std::set<std::string> names;
    for (const auto &s : before.stages) {
        a[s.stage] = &s;
        names.insert(s.stage);
    }
    for (const auto &s : after.stages) {
        b[s.stage] = &s;
        names.insert(s.stage);
    }
    for (const auto &name : names) {
        auto x = a.find(name), y = b.find(name);
        if (x == a.end() || y == b.end() || x->second->source != y->second->source) {
            ++result.review_stages;
            out << name << ": source changed or stage added/removed; visual review required.\n";
            continue;
        }
        const auto &old = *x->second, &now = *y->second;
        size_t changed = 0;
        if (old.table != now.table || old.images.size() != now.images.size()) {
            ++changed;
        } else
            for (size_t i = 0; i < old.images.size(); ++i) {
                const auto &p = old.images[i], &q = now.images[i];
                if (p.width != q.width || p.height != q.height || p.pixels != q.pixels)
                    ++changed;
                else
                    ++result.unchanged_images;
            }
        result.regressions += changed;
        if (changed)
            out << "REGRESSION: " << name << ": " << changed
                << " image(s) or table shape changed despite unchanged source.\n";
    }
    std::ostringstream heading;
    heading << "Static background pixels: " << result.unchanged_images << " unchanged; "
            << result.regressions << " regression(s); " << result.review_stages
            << " stage(s) need visual review.\n";
    result.report =
        heading.str() + out.str() +
        "Covers MK7 static background indices and dimensions. Palettes, program bindings, "
        "IMG animations and gameplay remain separate checks. Source hashes do not prove build "
        "freshness.\n";
    return result;
}
} // namespace studio
