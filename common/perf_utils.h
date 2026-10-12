#pragma once

#include <cstdint>

namespace Common {
/// Read a monotonic hardware timestamp counter and return it as a uint64_t.
///
/// On x86-64 this reads the CPU Time Stamp Counter via the RDTSC instruction.
/// On AArch64 (e.g. Apple Silicon) this reads the virtual count register
/// CNTVCT_EL0. Both are cheap, monotonically increasing counters suitable for
/// relative latency measurement. Note the two counters tick at different rates
/// (CPU clock vs. a fixed system-counter frequency), so the absolute value is
/// only meaningful as a delta within a single run on a single machine.
inline auto rdtsc() noexcept -> uint64_t {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
    uint64_t cnt;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(cnt));
    return cnt;
#else
#error "Common::rdtsc(): unsupported architecture (expected x86-64 or AArch64)"
#endif
}
}  // namespace Common

/// Start latency measurement using rdtsc(). Creates a variable called TAG in the local scope.
#define START_MEASURE(TAG) const auto TAG = Common::rdtsc()

/// End latency measurement using rdtsc(). Expects a variable called TAG to already exist in the local scope.
#define END_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto end = Common::rdtsc();                                                       \
        LOGGER.log("% RDTSC " #TAG " %\n", Common::getCurrentTimeStr(&time_str_), (end - TAG)); \
    } while (false)
