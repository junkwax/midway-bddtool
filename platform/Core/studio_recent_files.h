#pragma once
#include <string>
#include <vector>

namespace studio {

class RecentStages {
    std::vector<std::string> paths_;
public:
    const std::vector<std::string> &paths() const { return paths_; }
    bool load(const std::string &file, std::string &error);
    bool save(const std::string &file, std::string &error) const;
    void add(const std::string &path);
    void erase(size_t index);
};

} // namespace studio
