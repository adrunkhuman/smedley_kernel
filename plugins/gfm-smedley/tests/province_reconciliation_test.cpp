#include "province_reconciliation.hpp"

#include <gtest/gtest.h>

#include <array>

TEST(GfmProvinceReconciliationTest, RemovesEveryManagedStageForQualifyingOwners)
{
    constexpr std::array countries{
        gfm_smedley::CountryPolicySnapshot{10, 1},
        gfm_smedley::CountryPolicySnapshot{20, 0},
    };
    constexpr std::array provinces{
        gfm_smedley::ProvinceModifierSnapshot{101, 10, 1u << 2},
        gfm_smedley::ProvinceModifierSnapshot{102, 20, 1u << 1},
        gfm_smedley::ProvinceModifierSnapshot{103, 10, 0},
        gfm_smedley::ProvinceModifierSnapshot{104, 0, 1u << 3},
        gfm_smedley::ProvinceModifierSnapshot{105, 10, 1u << 0},
    };
    std::array<gfm_smedley::ProvinceModifierRemoval, 2> removals{};
    uint32_t count = 0;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  provinces.data(), provinces.size(), countries.data(), countries.size(),
                  removals.data(), removals.size(), &count),
        gfm_smedley::ReconciliationPlanStatus::success);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(removals[0].province, 101u);
    EXPECT_EQ(removals[0].modifier_mask, gfm_smedley::slave_trade_modifier_mask);
    EXPECT_EQ(removals[1].province, 105u);
    EXPECT_EQ(removals[1].modifier_mask, gfm_smedley::slave_trade_modifier_mask);
}

TEST(GfmProvinceReconciliationTest, FailsClosedWhenOwnerPolicyIsMissing)
{
    constexpr std::array provinces{
        gfm_smedley::ProvinceModifierSnapshot{101, 10, 1},
        gfm_smedley::ProvinceModifierSnapshot{102, 30, 1},
    };
    constexpr std::array countries{gfm_smedley::CountryPolicySnapshot{10, 1}};
    std::array removals{gfm_smedley::ProvinceModifierRemoval{999, 7}};
    uint32_t count = 99;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  provinces.data(), provinces.size(), countries.data(), countries.size(),
                  removals.data(), removals.size(), &count),
        gfm_smedley::ReconciliationPlanStatus::missing_country_policy);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(removals[0].province, 999u);
    EXPECT_EQ(removals[0].modifier_mask, 7u);
}

TEST(GfmProvinceReconciliationTest, RejectsDuplicateOrInvalidCountryRecords)
{
    constexpr std::array duplicates{
        gfm_smedley::CountryPolicySnapshot{10, 1},
        gfm_smedley::CountryPolicySnapshot{10, 0},
    };
    constexpr std::array invalid{gfm_smedley::CountryPolicySnapshot{10, 2}};
    uint32_t count = 99;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  nullptr, 0, duplicates.data(), duplicates.size(), nullptr, 0, &count),
        gfm_smedley::ReconciliationPlanStatus::duplicate_country);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  nullptr, 0, invalid.data(), invalid.size(), nullptr, 0, &count),
        gfm_smedley::ReconciliationPlanStatus::invalid_record);
    EXPECT_EQ(count, 0u);
}

TEST(GfmProvinceReconciliationTest, RejectsUnknownModifierBits)
{
    constexpr std::array provinces{
        gfm_smedley::ProvinceModifierSnapshot{101, 10, 1u << 4},
    };
    constexpr std::array countries{gfm_smedley::CountryPolicySnapshot{10, 1}};
    uint32_t count = 99;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  provinces.data(), provinces.size(), countries.data(), countries.size(),
                  nullptr, 0, &count),
        gfm_smedley::ReconciliationPlanStatus::invalid_record);
    EXPECT_EQ(count, 0u);
}

TEST(GfmProvinceReconciliationTest, RejectsDuplicateProvinceRecordsWithoutPartialPlans)
{
    constexpr std::array countries{
        gfm_smedley::CountryPolicySnapshot{10, 1},
        gfm_smedley::CountryPolicySnapshot{20, 0},
    };
    constexpr std::array duplicate_sets{
        std::array{
            gfm_smedley::ProvinceModifierSnapshot{101, 10, 1},
            gfm_smedley::ProvinceModifierSnapshot{101, 10, 1},
        },
        std::array{
            gfm_smedley::ProvinceModifierSnapshot{101, 10, 1},
            gfm_smedley::ProvinceModifierSnapshot{101, 20, 2},
        },
    };

    for (const auto &provinces : duplicate_sets) {
        std::array removals{gfm_smedley::ProvinceModifierRemoval{999, 7}};
        uint32_t count = 99;
        EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                      provinces.data(), provinces.size(), countries.data(), countries.size(),
                      removals.data(), removals.size(), &count),
            gfm_smedley::ReconciliationPlanStatus::invalid_record);
        EXPECT_EQ(count, 0u);
        EXPECT_EQ(removals[0].province, 999u);
        EXPECT_EQ(removals[0].modifier_mask, 7u);
    }
}

TEST(GfmProvinceReconciliationTest, PreflightsOutputCapacityWithoutPartialPlans)
{
    constexpr std::array provinces{
        gfm_smedley::ProvinceModifierSnapshot{101, 10, 1},
        gfm_smedley::ProvinceModifierSnapshot{102, 10, 2},
    };
    constexpr std::array countries{gfm_smedley::CountryPolicySnapshot{10, 1}};
    std::array removals{gfm_smedley::ProvinceModifierRemoval{999, 7}};
    uint32_t count = 99;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  provinces.data(), provinces.size(), countries.data(), countries.size(),
                  removals.data(), removals.size(), &count),
        gfm_smedley::ReconciliationPlanStatus::output_too_small);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(removals[0].province, 999u);
    EXPECT_EQ(removals[0].modifier_mask, 7u);
}

TEST(GfmProvinceReconciliationTest, AcceptsEmptySnapshots)
{
    uint32_t count = 99;

    EXPECT_EQ(gfm_smedley::PlanSlaveTradeReconciliation(
                  nullptr, 0, nullptr, 0, nullptr, 0, &count),
        gfm_smedley::ReconciliationPlanStatus::success);
    EXPECT_EQ(count, 0u);
}

TEST(GfmProvinceReconciliationTest, ReconcilesOnFirstObservationAndEveryThirtyDays)
{
    gfm_smedley::ReconciliationSchedule schedule{};

    EXPECT_TRUE(gfm_smedley::ReconciliationDue(1000, &schedule));
    EXPECT_FALSE(gfm_smedley::ReconciliationDue(1000, &schedule));
    EXPECT_FALSE(gfm_smedley::ReconciliationDue(1000 + 30 * 24 - 1, &schedule));
    EXPECT_TRUE(gfm_smedley::ReconciliationDue(1000 + 30 * 24, &schedule));
}

TEST(GfmProvinceReconciliationTest, DateRegressionStartsANewCadence)
{
    gfm_smedley::ReconciliationSchedule schedule{};

    ASSERT_TRUE(gfm_smedley::ReconciliationDue(2000, &schedule));
    EXPECT_TRUE(gfm_smedley::ReconciliationDue(1000, &schedule));
    EXPECT_FALSE(gfm_smedley::ReconciliationDue(1001, &schedule));
    EXPECT_FALSE(gfm_smedley::ReconciliationDue(1000, nullptr));
}
