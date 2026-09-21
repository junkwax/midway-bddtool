#include "Core/studio_rom_receipt.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
namespace fs = std::filesystem;
void check(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string read(const fs::path &path, size_t limit = 32 * 1024 * 1024) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    check(in && in.tellg() >= 0 && (uint64_t)in.tellg() <= limit,
          "Missing or oversized file: " + path.u8string());
    std::string data((size_t)in.tellg(), '\0');
    in.seekg(0);
    if (!data.empty())
        in.read(data.data(), (std::streamsize)data.size());
    check((bool)in, "Could not read " + path.u8string());
    return data;
}
uint64_t hash(const std::string &data) {
    uint64_t value = 14695981039346656037ULL;
    for (unsigned char c : data)
        value = (value ^ c) * 1099511628211ULL;
    return value; // Change identifier, not a cryptographic attestation.
}
uint32_t u32(const std::string &data, size_t pos) {
    check(pos <= data.size() && data.size() - pos >= 4, "Truncated IRW header.");
    uint32_t value = 0;
    for (int i = 0; i < 4; i++)
        value |= (uint32_t)(uint8_t)data[pos + i] << (8 * i);
    return value;
}
std::string without_comments(const std::string &source) {
    std::string out;
    bool comment = false, escape = false;
    char quote = 0;
    for (char c : source) {
        if (c == '\n') {
            comment = false;
            out += c;
            continue;
        }
        if (comment)
            continue;
        if (!quote && c == '#') {
            comment = true;
            continue;
        }
        if (quote) {
            if (!escape && c == quote)
                quote = 0;
        } else if (c == '\'' || c == '"')
            quote = c;
        out += c;
        if (quote && c == '\\' && !escape)
            escape = true;
        else
            escape = false;
    }
    return out;
}
std::string literal(const std::string &source, const std::string &name, char left, char right) {
    std::regex assignments("\\b" + name + "\\s*=(?!=)");
    check(std::distance(std::sregex_iterator(source.begin(), source.end(), assignments),
                        std::sregex_iterator()) == 1 &&
              !std::regex_search(source, std::regex("\\b" + name + "\\s*\\[[^\\]]+\\]\\s*=(?!=)")),
          "Packing declaration is reassigned: " + name);
    std::regex declaration("(^|\\n)[ \\t]*" + name + "[ \\t]*=[ \\t]*\\" + left);
    std::smatch match;
    check(std::regex_search(source, match, declaration),
          "Unsupported makevrom declaration: " + name);
    size_t start = (size_t)match.position() + match.length(), end = source.find(right, start);
    check(end != std::string::npos, "Unclosed declaration: " + name);
    size_t line_end = source.find('\n', end);
    check(source.substr(end + 1, line_end == std::string::npos ? line_end : line_end - end - 1)
                  .find_first_not_of(" \t\r") == std::string::npos,
          "Computed declaration is unsupported: " + name);
    check(
        !std::regex_search(
            source,
            std::regex(name +
                       "\\s*(\\+=|\\.\\s*(append|extend|insert|remove|pop|update|clear)\\s*\\()")),
        "Dynamic packing declarations are unsupported: " + name);
    return source.substr(start, end - start);
}
void residue_ok(std::string body, const std::regex &entries) {
    body = std::regex_replace(body, entries, "");
    check(body.find_first_not_of(" \t\r\n,") == std::string::npos,
          "Computed packing declarations are unsupported; a literal packing layout is required.");
}
std::map<std::string, uint64_t> dictionary(const std::string &source, const std::string &name) {
    auto body = literal(source, name, '{', '}');
    std::regex entry("[\"']([A-Za-z0-9_.-]+)[\"']\\s*:\\s*(0[xX][0-9a-fA-F]+|[0-9]+)");
    residue_ok(body, entry);
    std::map<std::string, uint64_t> values;
    for (std::sregex_iterator i(body.begin(), body.end(), entry), end; i != end; ++i)
        check(values.emplace((*i)[1].str(), std::stoull((*i)[2].str(), nullptr, 0)).second,
              "Duplicate packing filename.");
    return values;
}
void budgets(RomReceipt &receipt) {
    check(!receipt.payloads.empty() && receipt.payloads.size() <= 4096,
          "Receipt has no payloads or too many payloads.");
    std::set<std::string> names;
    for (const auto &p : receipt.payloads) {
        check(std::regex_match(p.name, std::regex("[A-Za-z0-9_.-]+\\.[Ii][Rr][Ww]")) &&
                  names.insert(p.name).second,
              "Invalid or duplicate receipt payload name.");
        check(p.bank == 0 || p.bank == 1, "Invalid video bank.");
        uint64_t lo = p.bank ? 0x800000 : 0, hi = p.bank ? 0xc00000 : 0x800000;
        check(p.offset >= lo && p.offset <= hi && p.bytes <= hi - p.offset,
              "Payload is outside its video bank: " + p.name);
    }
    for (int bank = 0; bank < 2; bank++) {
        std::vector<RomPayload> ranges;
        for (auto p : receipt.payloads)
            if (p.bank == bank && p.bytes)
                ranges.push_back(p);
        std::sort(ranges.begin(), ranges.end(),
                  [](const auto &a, const auto &b) { return a.offset < b.offset; });
        uint64_t cursor = bank ? 0x800000 : 0, hi = bank ? 0xc00000 : 0x800000;
        RomBankBudget budget;
        for (const auto &p : ranges) {
            check(p.offset >= cursor, "Overlapping video payload: " + p.name);
            budget.largest_gap = std::max(budget.largest_gap, p.offset - cursor);
            budget.free += p.offset - cursor;
            budget.used += p.bytes;
            cursor = p.offset + p.bytes;
        }
        budget.free += hi - cursor;
        budget.largest_gap = std::max(budget.largest_gap, hi - cursor);
        receipt.banks[bank] = budget;
    }
}
} // namespace

RomReceipt capture_rom_receipt(const std::string &root_path, bool successful) {
    RomReceipt result;
    try {
        auto root = fs::canonical(fs::u8path(root_path));
        result.root = root.u8string();
        auto script = read(root / "makevrom.py", 2 * 1024 * 1024);
        auto source = without_comments(script);
        auto constants = [&](const std::string &name, uint64_t expected) {
            std::smatch match;
            check(std::regex_search(
                      source, match,
                      std::regex("(^|\\n)" + name +
                                 "\\s*=\\s*(0[xX][0-9a-fA-F]+|[0-9]+)[ \\t]*(\\r?\\n|$)")) &&
                      std::stoull(match[2].str(), nullptr, 0) == expected,
                  "Unsupported MK2 video layout: " + name);
        };
        constants("IROM_BASE", 0x02000000);
        constants("IROM_BANK1_HIGH_BASE", 0x04000000);
        constants("VIDEO_SIZE", 0xc00000);
        constants("IRW_HEADER_SIZE", 68);
        constants("IRW_RECORD_HEADER_SIZE", 24);
        auto bank_body = literal(source, "BANK_OFFSETS", '{', '}');
        check(std::regex_match(
                  bank_body, std::regex("\\s*0\\s*:\\s*0x0+\\s*,\\s*1\\s*:\\s*0x800000\\s*,?\\s*")),
              "Unsupported bank offsets.");
        auto banks = dictionary(source, "FILE_BANK"),
             overrides = dictionary(source, "FILE_BASE_OVERRIDE");
        auto files = literal(source, "irw_files", '[', ']');
        std::regex filename("[\"']([A-Za-z0-9_.-]+\\.[Ii][Rr][Ww])[\"']");
        residue_ok(files, filename);
        std::string flat(0xc00000, (char)0xff);
        std::vector<std::pair<fs::path, std::string>> read_back;
        for (std::sregex_iterator i(files.begin(), files.end(), filename), end; i != end; ++i) {
            auto name = (*i)[1].str();
            auto path = root / "data" / name;
            auto bytes = read(path);
            check(bytes.size() >= 68, "Truncated IRW: " + name);
            uint64_t base = u32(bytes, 44), count = u32(bytes, 48), bank = u32(bytes, 60);
            if (bank > 1) {
                check(banks.count(name) && banks[name] <= 1, "Missing bank assignment: " + name);
                bank = banks[name];
            }
            std::string payload;
            if (!count && overrides.count(name)) {
                base = overrides[name];
                payload = bytes.substr(68);
            } else {
                check(count <= bytes.size() - 68, "Truncated IRW payload: " + name);
                payload = bytes.substr(68, (size_t)count);
                size_t pos = 68 + (size_t)count;
                while (pos < bytes.size()) {
                    check(bytes.size() - pos >= 24, "Unrecognized IRW tail: " + name);
                    size_t extra = u32(bytes, pos + 4);
                    check(extra > 0 && extra <= bytes.size() - pos - 24,
                          "Invalid continuation record: " + name);
                    payload.append(bytes, pos + 24, extra);
                    pos += 24 + extra;
                }
            }
            uint64_t origin = bank == 1 && base >= 0x04000000 ? 0x04000000 : 0x02000000;
            check(base >= origin && (base - origin) % 8 == 0, "Invalid IRW bit address: " + name);
            uint64_t offset = (bank ? 0x800000 : 0) + (base - origin) / 8;
            check(offset <= flat.size() && payload.size() <= flat.size() - offset,
                  "IRW exceeds video capacity: " + name);
            result.payloads.push_back({name, (int)bank, offset, payload.size(), hash(payload)});
            flat.replace((size_t)offset, payload.size(), payload);
            read_back.emplace_back(path, std::move(bytes));
        }
        budgets(
            result); // Refuse overlaps, even if later data happened to overwrite identical pixels.
        auto chips = literal(source, "MAME_ROMS", '[', ']');
        std::regex chip("\\(\\s*[\"']([A-Za-z0-9_.-]+)[\"']\\s*,\\s*(0[xX][0-9a-fA-F]+|[0-9]+)\\s*,"
                        "\\s*(0[xX][0-9a-fA-F]+|[0-9]+)\\s*,\\s*([0-3])\\s*\\)");
        residue_ok(chips, chip);
        std::set<std::pair<uint64_t, uint64_t>> lanes;
        for (std::sregex_iterator i(chips.begin(), chips.end(), chip), end; i != end; ++i) {
            uint64_t offset = std::stoull((*i)[2].str(), nullptr, 0),
                     size = std::stoull((*i)[3].str(), nullptr, 0), lane = std::stoull((*i)[4]);
            check((offset == 0 || offset == 0x400000 || offset == 0x800000) && size == 0x100000 &&
                      lanes.emplace(offset, lane).second,
                  "Unsupported or duplicate video chip lane.");
            auto path = root / "rom" / (*i)[1].str();
            auto bytes = read(path, 0x100000);
            check(bytes.size() == size, "Video chip has the wrong size: " + path.u8string());
            for (size_t p = 0; p < bytes.size(); p++)
                if (bytes[p] != flat[(size_t)offset + p * 4 + (size_t)lane])
                    throw std::runtime_error(
                        "Video chips do not match generated IRWs. Rebuild before capturing: " +
                        path.filename().u8string());
            read_back.emplace_back(path, std::move(bytes));
        }
        check(lanes.size() == 12, "All twelve video chips are required for verification.");
        // Detect build activity during capture; no timestamps or zero-filled-byte guesses.
        check(read(root / "makevrom.py", 2 * 1024 * 1024) == script,
              "Packing configuration changed during capture.");
        for (const auto &file : read_back)
            check(read(file.first) == file.second,
                  "Build output changed during capture. Try again after the build finishes.");
        result.packing_fingerprint = hash(script);
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream stamp;
        stamp << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
        result.captured = stamp.str();
        result.after_successful_build = successful;
        result.valid = true;
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}
bool save_rom_receipt(const RomReceipt &receipt, const std::string &path, std::string &error) {
    try {
        check(receipt.valid, "No verified ROM receipt to save.");
        auto validated = receipt;
        budgets(validated);
        std::ostringstream out;
        out << "BDDROM 1\n"
            << std::quoted(receipt.root) << ' ' << std::quoted(receipt.captured) << ' '
            << receipt.packing_fingerprint << ' ' << receipt.after_successful_build << '\n';
        out << receipt.payloads.size() << '\n';
        for (const auto &p : receipt.payloads)
            out << std::quoted(p.name) << ' ' << p.bank << ' ' << p.offset << ' ' << p.bytes << ' '
                << p.fingerprint << '\n';
        std::ofstream file(fs::u8path(path), std::ios::binary | std::ios::trunc);
        file << out.str();
        file.close();
        check((bool)file, "Could not write the ROM receipt.");
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
RomReceipt load_rom_receipt(const std::string &path) {
    RomReceipt result;
    try {
        std::istringstream in(read(fs::u8path(path), 1024 * 1024));
        std::string magic;
        int version = 0;
        size_t count = 0;
        in >> magic >> version >> std::quoted(result.root) >> std::quoted(result.captured) >>
            result.packing_fingerprint >> result.after_successful_build >> count;
        check(in && magic == "BDDROM" && version == 1 && count > 0 && count <= 4096,
              "Invalid ROM receipt.");
        for (size_t i = 0; i < count; i++) {
            RomPayload p;
            in >> std::quoted(p.name) >> p.bank >> p.offset >> p.bytes >> p.fingerprint;
            check((bool)in, "Truncated ROM receipt.");
            result.payloads.push_back(p);
        }
        in >> std::ws;
        check(in.eof(), "Unexpected receipt data.");
        budgets(result);
        result.valid = true;
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}
std::string compare_rom_receipts(const RomReceipt &before, const RomReceipt &after) {
    if (!before.valid || !after.valid)
        return "Capture or load two verified ROM receipts to compare builds.";
    std::ostringstream out;
    out << "Verified packed video bytes (all twelve chip lanes checked at capture)\nBefore: "
        << before.root << " @ " << before.captured << "\nAfter: " << after.root << " @ "
        << after.captured << '\n';
    if (before.packing_fingerprint != after.packing_fingerprint)
        out << "Packing configuration changed between captures.\n";
    for (int i = 0; i < 2; i++) {
        const auto &a = before.banks[i], &b = after.banks[i];
        out << "Bank " << i << ": used " << a.used << " -> " << b.used << " B; freed "
            << (int64_t)a.used - (int64_t)b.used << " B; free " << a.free << " -> " << b.free
            << " B; largest gap " << a.largest_gap << " -> " << b.largest_gap << " B\n";
    }
    std::map<std::string, RomPayload> a, b;
    for (auto p : before.payloads)
        a[p.name] = p;
    for (auto p : after.payloads)
        b[p.name] = p;
    std::set<std::string> names;
    for (const auto &p : a)
        names.insert(p.first);
    for (const auto &p : b)
        names.insert(p.first);
    for (const auto &name : names) {
        const auto &x = a[name], &y = b[name];
        if (x.name != y.name || x.bytes != y.bytes || x.offset != y.offset ||
            x.fingerprint != y.fingerprint)
            out << name << ": " << x.bytes << " -> " << y.bytes << " B ("
                << (int64_t)x.bytes - (int64_t)y.bytes << " saved)"
                << (x.offset != y.offset ? "; moved" : "")
                << (x.fingerprint != y.fingerprint ? "; payload changed" : "") << '\n';
    }
    out << "Physical gaps may be reserved by the game's slot policy. Changes cover the entire "
           "build, not only this stage.\n"
           "Source freshness, decoded-pixel identity, program tables/palettes and runtime "
           "object/DMA costs are separate checks.\n";
    return out.str();
}
} // namespace studio
