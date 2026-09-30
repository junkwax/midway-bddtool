#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace studio {
struct RomPayload {
    std::string name;
    int bank = 0;
    uint64_t offset = 0, bytes = 0, fingerprint = 0;
};
struct RomBankBudget {
    uint64_t used = 0, free = 0, largest_gap = 0;
    uint64_t reserved_free = 0, unreserved_free = 0, largest_unreserved_gap = 0;
};
struct RomSlot {
    std::string name;
    uint64_t start = 0, end = 0;
};
struct RomSlotPolicy {
    std::vector<RomSlot> slots;
    std::string error;
    bool valid = false, declared = false;
};
struct RomReceipt {
    std::string root, captured, error;
    uint64_t packing_fingerprint = 0;
    std::vector<RomPayload> payloads;
    std::vector<RomSlot> slots;
    std::array<RomBankBudget, 2> banks{};
    bool valid = false, after_successful_build = false;
    bool slots_checked = false;
};
// Reads literal CUSTOM_VIDEO_SLOTS; never executes Python. Missing declarations remain unknown.
RomSlotPolicy read_rom_slot_policy(const std::string &root);
// Reads literal MK2 makevrom declarations, generated IRWs and all twelve video chips.
// Verifies every chip byte against the reconstructed packed image; never runs a script.
RomReceipt capture_rom_receipt(const std::string &root, bool after_successful_build = false);
bool save_rom_receipt(const RomReceipt &receipt, const std::string &path, std::string &error);
RomReceipt load_rom_receipt(const std::string &path);
std::string compare_rom_receipts(const RomReceipt &before, const RomReceipt &after);
} // namespace studio
