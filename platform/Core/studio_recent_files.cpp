#include "Core/studio_recent_files.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace studio {
namespace fs = std::filesystem;

static std::string normalized(const std::string &text) {
    if (text.empty() || text.find_first_of("\r\n") != std::string::npos || text.size() > 4096)
        return {};
    std::error_code ec;
    auto path = fs::absolute(fs::u8path(text), ec);
    if (ec) return {};
    return path.lexically_normal().u8string();
}
static std::string key(std::string text) {
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
#endif
    return text;
}

void RecentStages::add(const std::string &path) {
    auto clean = normalized(path);
    if (clean.empty()) return;
    auto match = key(clean);
    paths_.erase(std::remove_if(paths_.begin(), paths_.end(), [&](const auto &p) {
        return key(p) == match;
    }), paths_.end());
    paths_.insert(paths_.begin(), std::move(clean));
    if (paths_.size() > 8) paths_.resize(8);
}

void RecentStages::erase(size_t index) {
    if (index < paths_.size()) paths_.erase(paths_.begin() + index);
}

bool RecentStages::load(const std::string &file, std::string &error) {
    paths_.clear(); error.clear();
    std::ifstream in(fs::u8path(file), std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!fs::exists(fs::u8path(file), ec)) return true;
        error = "Could not read the recent-stage list."; return false;
    }
    std::string line; std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lines.size() < 64) lines.push_back(line);
    }
    if (!in.eof()) { paths_.clear(); error = "Could not finish reading the recent-stage list."; return false; }
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) add(*it);
    return true;
}

bool RecentStages::save(const std::string &file, std::string &error) const {
    error.clear(); auto path = fs::u8path(file), temp = path; temp += ".tmp";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create the settings folder: " + ec.message(); return false; }
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto &entry : paths_) out << entry << '\n';
        if (!out) { error = "Could not write the recent-stage list."; fs::remove(temp, ec); return false; }
    }
    fs::remove(path, ec); ec.clear();
    fs::rename(temp, path, ec);
    if (ec) { error = "Could not replace the recent-stage list: " + ec.message(); fs::remove(temp, ec); return false; }
    return true;
}

} // namespace studio
