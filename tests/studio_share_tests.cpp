#include "Core/studio_share.h"
#include "libs/stb_image.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool value, const std::string &error) { if (!value) throw std::runtime_error(error); }
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Expected scratch folder");
        auto root = fs::u8path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root);
        auto d = Document::demo(); std::string error;
        require(d.save((root / "source.BDB").u8string(), error), error);
        auto source = fs::file_size(root / "source.BDB"); auto time = fs::last_write_time(root / "source.BDB");
        auto id = d.state().objects.front().id;
        d.move({id}, 7, 3);
        auto revision = d.state().revision; auto path = d.path(); auto assets = d.state().assets;
        ShareCredits credits{"Test | Author", "<script> & description", "Own art", "Test license", "Not tested", "TEST_STAGE"};
        auto bundle = build_stage_share(d, credits, (root / "bundle").u8string());
        require(bundle.ready, bundle.error);
        require(d.dirty() && d.state().revision == revision && d.path() == path && d.state().assets == assets, "Share changed save point/state");
        require(fs::file_size(root / "source.BDB") == source && fs::last_write_time(root / "source.BDB") == time, "Share overwrote original");
        require(fs::exists(bundle.zip) && fs::exists(root / "bundle/TEST_STAGE.bddstudio") && fs::exists(root / "bundle/TEST_STAGE.BDD.meta"), "Missing companions/archive");
        Document reopened; require(reopened.load((root / "bundle/TEST_STAGE.BDB").u8string(), error), error);
        auto expected = d.scene(), actual = reopened.scene(); require(expected.size() == actual.size(), "Placement count changed");
        for (size_t i = 0; i < expected.size(); ++i)
            require(expected[i].rect.x == actual[i].rect.x && expected[i].rect.y == actual[i].rect.y && expected[i].palette == actual[i].palette,
                    "Bundle lost current unsaved layout");
        int w, h, channels;
        auto *png = stbi_load((root / "bundle/arena_game.png").u8string().c_str(), &w, &h, &channels, 4);
        require(png && w == 400 && h == 254, "Invalid camera PNG"); stbi_image_free(png);
        require(bundle.markdown.find("&lt;script&gt;") != std::string::npos && bundle.markdown.find("Test \\| Author") != std::string::npos,
                "Markup not escaped");
        require(bundle.submission_url.find("issues/new?") != std::string::npos && bundle.submission_url.find("stage-submission") != std::string::npos, "Submission draft URL absent");
        auto size = fs::file_size(bundle.zip);
        require(!build_stage_share(d, credits, (root / "bundle").u8string()).ready && fs::file_size(bundle.zip) == size, "Existing bundle overwritten");
        credits.page_name = "../escape";
        require(!build_stage_share(d, credits, (root / "bad-name").u8string()).ready && !fs::exists(root / "bad-name"), "Unsafe name accepted");
        credits.page_name = "HOME";
        require(!build_stage_share(d, credits, (root / "reserved").u8string()).ready, "Wiki home can be overwritten");
        credits.page_name = "TEST"; credits.license.clear();
        require(!build_stage_share(d, credits, (root / "no-license").u8string()).ready, "Missing licensing context accepted");
        require(d.undo(), "Share broke undo history");
        std::cout << "Share snapshot, companions, PNG, draft URL, escaping and failure isolation passed: " << bundle.folder << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
