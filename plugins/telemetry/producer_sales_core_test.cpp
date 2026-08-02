#include "producer_sales_core.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace telemetry_plugin
{
    TEST(ProducerSalesCoreTest, ReconcilesProductionInventoryAndSales)
    {
        ProducerSale sale{};
        ASSERT_TRUE(ReconcileProducerSale(10479, 307920, 27007, 1441937000, &sale));
        EXPECT_EQ(sale.opening_inventory_raw, 10479);
        EXPECT_EQ(sale.produced_raw, 307920);
        EXPECT_EQ(sale.sold_raw, 291392);
        EXPECT_EQ(sale.closing_inventory_raw, 27007);
        EXPECT_EQ(sale.proceeds_raw, 1441937000);
    }

    TEST(ProducerSalesCoreTest, RejectsInvalidAndOverflowingAccounts)
    {
        ProducerSale sale{};
        EXPECT_FALSE(ReconcileProducerSale(-1, 1, 0, 0, &sale));
        EXPECT_FALSE(ReconcileProducerSale(1, 1, 3, 0, &sale));
        EXPECT_FALSE(ReconcileProducerSale((std::numeric_limits<int64_t>::max)(), 1, 0, 0, &sale));
        EXPECT_FALSE(ReconcileProducerSale(0, 0, 0, -1, &sale));
        EXPECT_FALSE(ReconcileProducerSale(0, 0, 0, 0, nullptr));
    }
}
