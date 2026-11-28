// SPDX-License-Identifier: GPL-3.0-or-later
// System tick and high-resolution sleep. Pattern from smo-recomp runtime/os/os_threads.c sleep_ns and SuperMarioWonderRecomp kernel.cpp system_tick.
#include <windows.h>

#include "nxrt/kernel.h"
#include "nxrt/game.h"
#include "nxrt/tt.h"
#include "kernel_internal.h"

namespace nxrt::kernel {

namespace {
std::atomic<bool> g_paused{false};
std::atomic<uint32_t> g_pause_owner_tid{0};
std::atomic<uint64_t> g_paused_qpc_total{0};
std::atomic<uint64_t> g_pause_start_qpc{0};
SRWLOCK g_pause_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_pause_cv = CONDITION_VARIABLE_INIT;

uint64_t raw_qpc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}
}  // namespace

void set_paused(bool paused, uint32_t owner_tid) {
    AcquireSRWLockExclusive(&g_pause_lock);
    const bool was = g_paused.load(std::memory_order_relaxed);
    if (paused && !was) {
        g_pause_start_qpc.store(raw_qpc(), std::memory_order_relaxed);
        g_pause_owner_tid.store(owner_tid, std::memory_order_relaxed);
        g_paused.store(true, std::memory_order_release);
    } else if (!paused && was) {
        const uint64_t now = raw_qpc();
        const uint64_t start = g_pause_start_qpc.load(std::memory_order_relaxed);
        if (now > start) {
            g_paused_qpc_total.fetch_add(now - start, std::memory_order_relaxed);
        }
        g_pause_owner_tid.store(0, std::memory_order_relaxed);
        g_paused.store(false, std::memory_order_release);
        WakeAllConditionVariable(&g_pause_cv);
    }
    ReleaseSRWLockExclusive(&g_pause_lock);
}

bool is_paused() { return g_paused.load(std::memory_order_acquire); }

void wait_if_paused() {
    if (!g_paused.load(std::memory_order_acquire)) return;
    const uint32_t owner = g_pause_owner_tid.load(std::memory_order_relaxed);
    if (owner != 0 && GetCurrentThreadId() == owner) return;
    AcquireSRWLockExclusive(&g_pause_lock);
    while (g_paused.load(std::memory_order_acquire)) {
        SleepConditionVariableSRW(&g_pause_cv, &g_pause_lock, INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&g_pause_lock);
}

uint64_t tick_hz() {
    static const uint64_t hz = game::info().platform == game::Platform::Switch2 ? kTickHzSwitch2 : kTickHzSwitch1;
    return hz;
}

uint64_t system_tick() {
    if (uint64_t vt; tt::virtual_tick(&vt)) return vt;      // deterministic mode: the virtual clock (test tooling)
    static const uint64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    uint64_t v = 0;
    if (!g_paused.load(std::memory_order_acquire)) {
        const uint64_t q = raw_qpc();
        const uint64_t off = g_paused_qpc_total.load(std::memory_order_relaxed);
        v = q > off ? q - off : 0;
    } else {
        AcquireSRWLockShared(&g_pause_lock);
        const uint64_t q = g_paused.load(std::memory_order_relaxed) ? g_pause_start_qpc.load(std::memory_order_relaxed) : raw_qpc();
        const uint64_t off = g_paused_qpc_total.load(std::memory_order_relaxed);
        v = q > off ? q - off : 0;
        ReleaseSRWLockShared(&g_pause_lock);
    }
    const uint64_t hz = tick_hz();
    return (v / freq) * hz + (v % freq) * hz / freq;     // split so the multiplication cannot overflow
}

// SleepThread takes nanoseconds and the console timer is good to a few microseconds. Sleep() counts whole milliseconds on a
// 1 ms or 15.6 ms grid, so the wait runs on a high-resolution waitable timer (100 ns units); Sleep() is only the fallback.
// A zero or negative span yields, as on the console.
namespace {
void sleep_once(int64_t ns) {
    static thread_local HANDLE timer = nullptr;
    static thread_local bool no_timer = false;
    if (ns <= 0) {
        yield_host();
        return;
    }
    // Short sleeps (polls such as Pokopia's GPU-fence wait, 100 us a round) overshoot to about half a millisecond on the timer: yield until the
    // deadline instead. Longer ones keep the timer (no busy waiting while a thread idles).
    if (ns <= 200000) {
        LARGE_INTEGER f, s, n;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&s);
        const int64_t ticks = ns * f.QuadPart / 1000000000;
        do {
            yield_host();
            QueryPerformanceCounter(&n);
        } while (n.QuadPart - s.QuadPart < ticks);
        return;
    }
    if (!timer && !no_timer) {
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer) no_timer = true;
    }
    LARGE_INTEGER due;
    due.QuadPart = -((ns + 99) / 100);
    if (!timer || !SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        Sleep(static_cast<DWORD>((ns + 999999) / 1000000));
    else
        WaitForSingleObject(timer, INFINITE);
}
}  // namespace

// On the thread set_blocked_hook names, a sleep longer than 10 ms is cut into 10 ms slices with the hook run between them, as in the kernel
// waits: Pokopia's main thread sleeps in SleepThread while the software keyboard applet is open (the freeze report's main stack).
void sleep_ns(int64_t ns) {
    if (tt::virtual_sleep(ns)) return;                      // deterministic mode: time advances, the thread only yields
    constexpr int64_t kSlice = 10000000;
    void (*fn)() = ns > kSlice ? detail::blocked_hook_here() : nullptr;
    if (!fn) return sleep_once(ns);
    const auto qpc_ns = [] {
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        return static_cast<int64_t>(c.QuadPart / f.QuadPart * 1000000000 + c.QuadPart % f.QuadPart * 1000000000 / f.QuadPart);
    };
    const int64_t until = qpc_ns() + ns;
    for (int64_t rem = ns; rem > 0; rem = until - qpc_ns()) {
        if (rem <= kSlice) return sleep_once(rem);
        sleep_once(kSlice);
        fn();
    }
}

}  // namespace nxrt::kernel
