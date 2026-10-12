#include "common/time_utils.h"

#include <string>

#include "gtest/gtest.h"

namespace {

TEST(TimeUtils, GetCurrentTimeStrPopulatesBuffer) {
    std::string time_str;
    const std::string& result = Common::getCurrentTimeStr(&time_str);
    // Should return a reference to the same buffer it was handed.
    EXPECT_EQ(&result, &time_str);
    EXPECT_FALSE(time_str.empty());
}

}  // namespace
