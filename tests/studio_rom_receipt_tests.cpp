#include "Core/studio_rom_receipt.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace studio;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &data) {
    std::ofstream out(path, std::ios::binary);
    out << data;
    require((bool)out, "Fixture write failed");
}
void u32(std::string &s, size_t p, uint32_t value) {
    for (int i = 0; i < 4; i++)
        s[p + i] = (char)(value >> (i * 8));
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch directory");
        auto root = fs::u8path(argv[1]) /
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root / "data");
        fs::create_directories(root / "rom");
        std::ostringstream script;
        script
            << "IROM_BASE = 0x02000000\nIROM_BANK1_HIGH_BASE = 0x04000000\nVIDEO_SIZE = 0xc00000\n"
               "IRW_HEADER_SIZE = 68\nIRW_RECORD_HEADER_SIZE = 24\nBANK_OFFSETS = {0: 0x000000, 1: "
               "0x800000}\n"
               "FILE_BANK = {'A.IRW': 0, 'B.IRW': 1}\nFILE_BASE_OVERRIDE = {}\n"
               "irw_files = ['A.IRW', 'B.IRW'] # shipped files only\nMAME_ROMS = [\n";
        for (int group = 0; group < 3; group++)
            for (int lane = 0; lane < 4; lane++)
                script << "('chip" << group << lane << ".bin', " << group * 0x400000
                       << ", 1048576, " << lane << "),\n";
        script << "]\n";
        auto config = script.str();
        write(root / "makevrom.py", config);
        auto fixture = [&](int b_size, bool chips = true) {
            std::string a(68, 0), b(68, 0), extra(24, 0);
            u32(a, 44, 0x02000080);
            u32(a, 48, 64);
            u32(a, 60, 0xffff);
            a.append(64, (char)0xff);
            u32(b, 44, 0x04000100);
            u32(b, 48, b_size);
            u32(b, 60, 1);
            b.append(b_size, 'b');
            u32(extra, 4, 12);
            b += extra;
            b.append(12, 'c');
            write(root / "data/A.IRW", a);
            write(root / "data/B.IRW", b);
            if (chips) {
                std::string flat(0xc00000, (char)0xff);
                flat.replace(0x800020, b_size, std::string(b_size, 'b'));
                flat.replace(0x800020 + b_size, 12, std::string(12, 'c'));
                for (int group = 0; group < 3; group++)
                    for (int lane = 0; lane < 4; lane++) {
                        std::string data(0x100000, 0);
                        for (size_t p = 0; p < data.size(); p++)
                            data[p] = flat[group * 0x400000 + p * 4 + lane];
                        write(root / "rom" /
                                  ("chip" + std::to_string(group) + std::to_string(lane) + ".bin"),
                              data);
                    }
            }
        };
        fixture(20);
        auto before = capture_rom_receipt(root.u8string());
        require(before.valid, before.error);
        require(before.banks[0].used == 64 && before.banks[1].used == 32,
                "IRW bytes/continuations miscounted (including 0xff payload)");
        fixture(8, false);
        require(!capture_rom_receipt(root.u8string()).valid, "Stale chips accepted");
        fixture(8);
        auto after = capture_rom_receipt(root.u8string(), true);
        require(after.valid, after.error);
        require(after.banks[1].used == 20 && after.banks[1].free == before.banks[1].free + 12,
                "Measured delta is incorrect");
        std::string error;
        auto receipt = root / "baseline.romreceipt";
        require(save_rom_receipt(before, receipt.u8string(), error), error);
        auto loaded = load_rom_receipt(receipt.u8string());
        require(loaded.valid && loaded.banks[1].used == 32, loaded.error);
        auto report = compare_rom_receipts(loaded, after);
        require(report.find("B.IRW: 32 -> 20 B (12 saved)") != std::string::npos,
                "Per-payload comparison missing");
        write(root / "makevrom.py", config + "irw_files.append('STAGED.IRW')\n");
        require(!capture_rom_receipt(root.u8string()).valid, "Dynamic packing list accepted");
        write(root / "makevrom.py", config);
        write(root / "data/B.IRW", "truncated");
        require(!capture_rom_receipt(root.u8string()).valid, "Truncated IRW accepted");
        fixture(8);
        std::string overlap(68, 0);
        u32(overlap, 44, 0x02000080);
        u32(overlap, 48, 8);
        u32(overlap, 60, 0);
        overlap.append(8, 'x');
        write(root / "data/B.IRW", overlap);
        require(!capture_rom_receipt(root.u8string()).valid, "Overlapping payloads accepted");
        write(receipt, "BDDROM 1\n\"root\" \"0\" 1 0\n1\n\"BAD.IRW\" 1 0 4294967295 0\n");
        require(!load_rom_receipt(receipt.u8string()).valid,
                "Invalid serialized bank range accepted");
        if (argc >= 3) {
            auto actual = capture_rom_receipt(argv[2]);
            std::cout << "Local capture: " << (actual.valid ? "VERIFIED" : actual.error) << '\n';
            if (actual.valid) {
                require(save_rom_receipt(actual, (root / "local.romreceipt").u8string(), error),
                        error);
                std::cout << compare_rom_receipts(actual, actual);
            }
        }
        std::cout << "ROM receipts: chip identity, continuations, stale output, overlaps, exact "
                     "deltas and persistence passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
