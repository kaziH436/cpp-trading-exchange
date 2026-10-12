#include "common/perf_utils.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace {

TEST(PerfUtils, RdtscReturnsNonZero) {
    EXPECT_GT(Common::rdtsc(), 0u);
}

TEST(PerfUtils, RdtscIsNonDecreasing) {
    const uint64_t first = Common::rdtsc();
    const uint64_t second = Common::rdtsc();
    EXPECT_GE(second, first);
}

TEST(PerfUtils, RdtscAdvancesAcrossWork) {
    const uint64_t start = Common::rdtsc();
    // Do some work the optimizer can't elide so the counter must advance.
    volatile uint64_t sink = 0;
    for (int i = 0; i < 100000; ++i) sink += static_cast<uint64_t>(i);
    const uint64_t end = Common::rdtsc();
    EXPECT_GT(end, start);
}

}  // namespace
