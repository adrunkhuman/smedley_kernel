#include "pop_cash_flow_core.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace telemetry_plugin
{
    TEST(PopCashFlowCoreTest, ReconcilesPostedAndActualMoneyChanges)
    {
        std::array<int64_t, pop_cash_flow_count> posted{};
        std::array<int64_t, pop_cash_flow_count> actual{};
        posted[0] = -200;
        posted[2] = 150;
        actual[0] = -100;
        actual[2] = 150;
        PopCashFlowAccount account{};
        ASSERT_TRUE(ReconcilePopCashFlow(100, 150, posted, actual, &account));
        EXPECT_EQ(account.posted_raw, -50);
        EXPECT_EQ(account.money_delta_raw, 50);
        EXPECT_EQ(account.residual_raw, 0);
        EXPECT_STREQ(PopCashFlowName(0), "needs");
        EXPECT_STREQ(PopCashFlowName(7), "interest");
    }

    TEST(PopCashFlowCoreTest, PreservesUnattributedResidualAndRejectsOverflow)
    {
        std::array<int64_t, pop_cash_flow_count> posted{};
        std::array<int64_t, pop_cash_flow_count> actual{};
        actual[3] = -25;
        PopCashFlowAccount account{};
        ASSERT_TRUE(ReconcilePopCashFlow(100, 80, posted, actual, &account));
        EXPECT_EQ(account.residual_raw, 5);

        posted[0] = (std::numeric_limits<int64_t>::max)();
        posted[1] = 1;
        EXPECT_FALSE(ReconcilePopCashFlow(100, 80, posted, actual, &account));
    }
}
