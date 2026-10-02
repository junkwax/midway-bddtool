#include "Core/studio_rom_receipt.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        if (argc != 3 && argc != 4)
            throw std::runtime_error("Usage: bdd_rom_verify CHECKOUT NEW_RECEIPT [BASELINE_RECEIPT]");
        if (std::filesystem::exists(std::filesystem::u8path(argv[2])))
            throw std::runtime_error("Receipt output already exists.");
        auto current = studio::capture_rom_receipt(argv[1]);
        if (!current.valid) throw std::runtime_error(current.error);
        if (!current.artwork.valid) throw std::runtime_error(current.artwork.error);
        if (argc == 4) {
            auto baseline = studio::load_rom_receipt(argv[3]);
            if (!baseline.valid) throw std::runtime_error(baseline.error);
            auto art = studio::compare_packed_artwork(baseline.artwork, current.artwork);
            std::cout << studio::compare_rom_receipts(baseline, current);
            if (!art.available || art.regressions)
                throw std::runtime_error("Unedited background comparison failed.");
        }
        std::string error;
        if (!studio::save_rom_receipt(current, argv[2], error)) throw std::runtime_error(error);
        std::cout << "Verified all twelve video chips and captured static artwork.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
