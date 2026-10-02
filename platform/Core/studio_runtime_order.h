#pragma once
#include "Core/studio_document.h"

namespace studio {
struct RuntimeOrderCheck {
    bool checked = false, equivalent = false;
    size_t layers = 0, changed_samples = 0;
    std::string report, error;
};
// Static per-layer RGB555 comparison using MKDISP Z/Y insertion priority and
// both X arrival directions for equal keys. Hidden editor objects still export.
// This is a bounded model, not an emulator or proof for all camera histories.
RuntimeOrderCheck compare_runtime_order(const State &before, const State &after);
} // namespace studio
