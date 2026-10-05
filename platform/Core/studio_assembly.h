#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Small literal-only parser shared by read-only runtime previews.
namespace studio::assembly {
namespace fs = std::filesystem;
inline void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
inline std::string upper(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return s;
}
inline std::string trim(std::string s) {
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
                "Missing or ambiguous assembly label: " + name);
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
inline uint32_t number(std::string s) {
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
} // namespace studio::assembly
