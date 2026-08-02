#include "economic_capture_core.hpp"

#include <gtest/gtest.h>

#include <array>

TEST(EconomicCaptureCoreTest, ValidatesUniqueNonnegativePopIds)
{
    std::array<int32_t, 4> ids{9, 2, 7, 4};
    EXPECT_TRUE(telemetry_plugin::SortUniqueNonnegativeIds(ids.data(), ids.size()));
    EXPECT_EQ(ids, (std::array<int32_t, 4>{2, 4, 7, 9}));

    std::array<int32_t, 3> duplicate{5, 3, 5};
    EXPECT_FALSE(telemetry_plugin::SortUniqueNonnegativeIds(duplicate.data(), duplicate.size()));

    std::array<int32_t, 2> negative{1, -1};
    EXPECT_FALSE(telemetry_plugin::SortUniqueNonnegativeIds(negative.data(), negative.size()));
    EXPECT_TRUE(telemetry_plugin::SortUniqueNonnegativeIds(nullptr, 0));
    EXPECT_FALSE(telemetry_plugin::SortUniqueNonnegativeIds(nullptr, 1));
}
