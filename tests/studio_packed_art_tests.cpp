#include "Core/studio_rom_receipt.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void need(bool ok, const std::string &error) {
    if (!ok)
        throw std::runtime_error(error);
}
void write(const fs::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
    need((bool)f, "Fixture write failed");
}
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--compare") {
            auto a = capture_rom_receipt(argv[2]), b = capture_rom_receipt(argv[3]);
            need(a.valid, a.error);
            need(b.valid, b.error);
            need(a.artwork.valid, a.artwork.error);
            need(b.artwork.valid, b.artwork.error);
            auto result = compare_packed_artwork(a.artwork, b.artwork);
            std::cout << result.report;
            return result.regressions ? 2 : 0;
        }
        need(argc == 2, "Need scratch folder");
        auto root = fs::u8path(argv[1]) /
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root / "data");
        fs::create_directories(root / "tmp/load2");
        write(root / "data/MK7MIL.LOD", ";BBB> IGNORED\nBBB> KEEP\nBBB> EDITX\n");
        write(root / "data/KEEP.BDB", "KEEP 100 100 255 1 1 1\n");
        write(root / "data/EDITX.BDB", "editx 100 100 255 1 1 1\n");
        write(root / "data/KEEP.BDD", "1\nfixture-a");
        write(root / "data/EDITX.BDD", "1\nfixture-b");
        auto table = [&](int delta) {
            write(root / "tmp/load2/BGNDTBL.MK7",
                  "HDRS:\n .word 2,1\n .long " + std::to_string(0x2000003 + delta) +
                      "\n .word 04000H\nSTOP:\n .word 0\nxHDRS:\n .word 4,1\n .long " +
                      std::to_string(0x2000010 + delta) + "\n .word 04080H\n");
        };
        std::string pixels("\xc8\x03\x11\x71", 4);
        auto capture = [&](const std::string &bytes) {
            std::vector<std::pair<std::string, std::string>> evidence;
            return capture_packed_artwork(root.u8string(), bytes, 0, evidence);
        };
        table(0);
        auto baseline = capture(pixels);
        need(baseline.valid, baseline.error);
        need(baseline.stages.size() == 2, "Missing stage or commented LOD line included");
        auto pixel_hash = [](std::initializer_list<unsigned> values) {
            uint64_t hash = 14695981039346656037ULL;
            for (auto value : values)
                hash = (hash ^ value) * 1099511628211ULL;
            return hash;
        };
        need(baseline.stages[0].images[0].pixels == pixel_hash({9, 7}) &&
                 baseline.stages[1].images[0].pixels == pixel_hash({0, 1, 7, 0}),
             "Raw or compressed pixels decoded incorrectly");
        table(0x2000000);
        std::vector<std::pair<std::string, std::string>> evidence;
        auto bank = capture_packed_artwork(root.u8string(), pixels, 0x2000000, evidence);
        need(bank.valid && compare_packed_artwork(baseline, bank).unchanged_images == 2,
             "Nonzero IRW base changed decoded pixels");
        table(8);
        auto relocated = capture(std::string(1, '\0') + pixels);
        need(relocated.valid, relocated.error);
        auto comparison = compare_packed_artwork(baseline, relocated);
        need(comparison.available && !comparison.regressions && comparison.unchanged_images == 2,
             "Bit-address relocation changed decoded identity");
        auto changed = std::string(1, '\0') + pixels;
        changed[1] = '\xc0';
        auto regression = capture(changed);
        need(regression.valid, regression.error);
        comparison = compare_packed_artwork(baseline, regression);
        need(comparison.regressions == 1 && comparison.unchanged_images == 1 &&
                 comparison.report.find("REGRESSION: KEEP") != std::string::npos,
             "One-pixel cross-stage change escaped comparison");
        write(root / "data/KEEP.BDD", "1\nintentionally edited source");
        auto edited = capture(changed);
        need(edited.valid, edited.error);
        comparison = compare_packed_artwork(baseline, edited);
        need(!comparison.regressions && comparison.review_stages == 1 &&
                 comparison.unchanged_images == 1,
             "Intentional source edit incorrectly verified or flagged as unchanged-source "
             "regression");
        need(!compare_packed_artwork({}, baseline).available, "Missing baseline marked verified");
        auto bad = changed;
        bad[3] = '\xff';
        need(!capture(bad).valid, "Invalid compressed row accepted");
        need(!capture("").valid, "Out-of-bounds pixels accepted");
        write(root / "data/KEEP.BDD", "2\nwrong count");
        need(!capture(changed).valid, "Source/header count drift accepted");
        write(root / "data/KEEP.BDD", "1\nfixture-a");
        write(root / "tmp/load2/BGNDTBL.MK7",
              "HDRS:\n .word 2,1\n .long 02000003H\n .word 04000H\n"
              "xHDRS:\n .word 8,1\n .long 02000010H\n .word 00680H\n");
        auto shifted = capture(std::string("\xc8\x03\x11\x2a\xff", 5));
        need(shifted.valid, shifted.error);
        need(shifted.stages[1].images[0].pixels == pixel_hash({0, 0, 0, 0, 42, 255, 0, 0}),
             "Eight-bit pixels or compressed lead/trail shifts decoded incorrectly");
        write(root / "data/MK7MIL.LOD", "BBB> KEEP\n");
        auto incomplete = capture(pixels);
        need(!incomplete.valid && incomplete.stages.empty(),
             "Incomplete stage coverage advertised as valid");

        RomReceipt receipt;
        receipt.valid = true;
        receipt.root = "fixture";
        receipt.payloads.push_back({"MK7MIL.IRW", 1, 0x800000, 8, 123});
        receipt.artwork = baseline;
        std::string error;
        auto path = (root / "artwork.romreceipt").u8string();
        need(save_rom_receipt(receipt, path, error), error);
        auto loaded = load_rom_receipt(path);
        need(loaded.valid, loaded.error);
        need(loaded.artwork.valid &&
                 compare_packed_artwork(baseline, loaded.artwork).unchanged_images == 2,
             "Artwork fingerprints lost in receipt roundtrip");
        receipt.artwork.stages.push_back(receipt.artwork.stages[0]);
        need(!save_rom_receipt(receipt, path, error), "Duplicate stage accepted in receipt");
        write(fs::u8path(path), "BDDROM 2\n\"root\" \"old\" 1 0\n1\n\"A.IRW\" 0 16 64 0\n0 0\n");
        loaded = load_rom_receipt(path);
        need(loaded.valid && !loaded.artwork.valid &&
                 !compare_packed_artwork(loaded.artwork, baseline).available,
             "Legacy receipt advertised decoded identity");
        std::cout << "Packed artwork checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
