// SPDX-License-Identifier: GPL-3.0-or-later
#include "nvn_residency_pressure.h"
#include "test_framework.h"
NXTEST_GROUP(nvn_residency_pressure)

namespace {

using nxrt::nvn::be::residency_pressure;
constexpr uint64_t MiB = 1ull << 20;

NXTEST(integrated_retained_blocks_trigger_below_os_threshold) {
    // Model #114's roughly 4 GiB reported usage and 4.93 GiB allocated against 6 GiB. All values here are for one heap.
    const nxrt::gfx::VideoBudgetSample heap{6144 * MiB, 4096 * MiB, 5048 * MiB, 4096 * MiB};
    const auto p = residency_pressure(heap, 80, true);
    CHECK_EQ(p.os_percent, 66);
    CHECK_EQ(p.block_percent, 82);
    CHECK_EQ(p.pressure_percent, 82);
    CHECK(p.urgency_percent < 92);
    CHECK(p.target_bytes > 0);
    CHECK_EQ(residency_pressure(heap, 80, false).target_bytes, 0u);
    CHECK_EQ(residency_pressure(heap, 83, true).target_bytes, 0u);
}

NXTEST(integrated_also_uses_driver_usage_below_desktop_trigger) {
    const nxrt::gfx::VideoBudgetSample heap{1000 * MiB, 820 * MiB, 760 * MiB, 700 * MiB};
    CHECK_EQ(residency_pressure(heap, 80, true).pressure_percent, 82);
    CHECK_EQ(residency_pressure(heap, 80, false).target_bytes, 0u);
}

NXTEST(retained_blocks_alone_do_not_shorten_idle_time) {
    const auto p = residency_pressure({1000 * MiB, 780 * MiB, 970 * MiB, 620 * MiB}, 80, true);
    CHECK_EQ(p.pressure_percent, 97);
    CHECK(p.urgency_percent < 92);
    CHECK(p.target_bytes > 0);
}

NXTEST(desktop_live_and_os_triggers) {
    const auto live = residency_pressure({1000 * MiB, 810 * MiB, 800 * MiB, 790 * MiB}, 80, false);
    CHECK_EQ(live.live_percent, 80);
    CHECK_EQ(live.target_bytes, 50 * MiB);
    const auto os = residency_pressure({6144 * MiB, 5899 * MiB, 5600 * MiB, 4000 * MiB}, 80, false);
    CHECK_EQ(os.live_percent, 69);
    CHECK_EQ(os.urgency_percent, 96);
    CHECK_EQ(os.target_bytes, 512 * MiB);
}

NXTEST(unavailable_budget_and_inconsistent_samples) {
    CHECK_EQ(residency_pressure({0, 200, 500, 100}, 80, true).target_bytes, 0u);
    CHECK_EQ(residency_pressure({1000 * MiB, 200, 500, 100}, 80, true).live_percent, 0);
    CHECK_EQ(residency_pressure({1000 * MiB, 800 * MiB, 500, 600}, 80, true).live_percent, 80);
}

}  // namespace
