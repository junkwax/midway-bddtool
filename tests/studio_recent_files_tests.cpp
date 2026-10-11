#include "Core/studio_recent_files.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace studio;
namespace fs = std::filesystem;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        auto root = fs::u8path(argv[1]); fs::remove_all(root); fs::create_directories(root);
        RecentStages recent; std::string error;
        for (int i = 0; i < 10; ++i) recent.add((root / (std::to_string(i) + ".BDB")).u8string());
        require(recent.paths().size() == 8 && recent.paths()[0].find("9.BDB") != std::string::npos,
                "Recent list did not retain newest eight entries");
        auto duplicate = recent.paths()[3]; recent.add(duplicate);
        require(recent.paths()[0] == duplicate && recent.paths().size() == 8, "Duplicate was not moved to front");
        auto list = root / "settings/recent.txt";
        require(recent.save(list.u8string(), error), error.c_str());
        RecentStages loaded; require(loaded.load(list.u8string(), error), error.c_str());
        require(loaded.paths() == recent.paths(), "Recent list changed across save/load");
        loaded.erase(0); require(loaded.paths().size() == 7, "Recent entry was not removed");
        loaded.add("bad\npath"); require(loaded.paths().size() == 7, "Unsafe line was accepted");
        RecentStages absent; require(absent.load((root / "absent.txt").u8string(), error) && absent.paths().empty(),
                                     "Missing recent list should be empty");
        fs::remove_all(root);
        std::cout << "Recent-stage bounds, ordering and persistence passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
