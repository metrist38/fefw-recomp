// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace nxrt::gfx {

// A single sample of the largest device-local heap. VMA uses allocator estimates when VK_EXT_memory_budget is unavailable.
struct VideoBudgetSample {
    uint64_t budget = 0;
    uint64_t usage = 0;
    uint64_t block_bytes = 0;
    uint64_t allocation_bytes = 0;
};

VideoBudgetSample video_budget_sample();

}  // namespace nxrt::gfx
