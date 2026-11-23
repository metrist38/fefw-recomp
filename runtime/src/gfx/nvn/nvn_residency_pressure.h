// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

#include "video_budget_sample.h"

namespace nxrt::nvn::be {

struct ResidencyPressure {
    int live_percent = 0;
    int os_percent = 0;
    int block_percent = 0;
    int pressure_percent = 0;
    // Drives the shorter idle time and emergency cooldown. Retained blocks alone cannot make a sweep urgent.
    int urgency_percent = 0;
    uint64_t target_bytes = 0;
};

inline ResidencyPressure residency_pressure(const gfx::VideoBudgetSample& heap, int threshold, bool integrated) {
    if (!heap.budget) return {};
    const auto percent = [budget = heap.budget](uint64_t bytes) { return static_cast<int>(bytes * 100 / budget); };
    const uint64_t free_in_blocks = heap.block_bytes - std::min(heap.block_bytes, heap.allocation_bytes);
    const uint64_t live_bytes = heap.usage - std::min(heap.usage, free_in_blocks);
    ResidencyPressure p;
    p.live_percent = percent(live_bytes);
    p.os_percent = percent(heap.usage);
    p.block_percent = percent(heap.block_bytes);
    const int os_trigger = std::min(100, std::max(threshold + 5, 85));
    p.urgency_percent = p.os_percent >= os_trigger ? std::max(p.live_percent, p.os_percent) : p.live_percent;
    // On an integrated GPU, the allocator's retained blocks take up shared RAM even below the usual raw-usage trigger.
    // Compare like for like: the block bytes and budget describe the same heap, unlike the all-heaps driver allocation gauge.
    p.pressure_percent = integrated ? std::max(p.os_percent, p.block_percent) : p.urgency_percent;
    if (p.pressure_percent >= threshold)
        p.target_bytes = std::min<uint64_t>(heap.budget / 100 * static_cast<uint64_t>(p.pressure_percent - threshold + 5), 512ull << 20);
    return p;
}

}  // namespace nxrt::nvn::be
