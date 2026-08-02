#pragma once

#include <cstddef>
#include <cstdint>

namespace telemetry_plugin
{
    constexpr uint32_t max_world_countries = 512;

    enum SnapshotFlag : uint32_t
    {
        SNAPSHOT_COUNTRY_LIMIT = 1u << 0,
        SNAPSHOT_COLLECTION_FAILED = 1u << 1,
        SNAPSHOT_DUPLICATE_POP = 1u << 2,
        SNAPSHOT_POP_UNREADABLE = 1u << 3,
        SNAPSHOT_SUM_OVERFLOW = 1u << 4,
    };

    struct EconomicSnapshot
    {
        int32_t date_raw = 0;
        uint32_t snapshot_flags = 0;
        uint32_t collection_flags = 0;
        uint32_t credit_flags = 0;
        uint32_t country_count = 0;
        uint32_t state_count = 0;
        uint32_t province_count = 0;
        uint32_t pop_count = 0;
        uint32_t positive_money_pops = 0;
        uint32_t positive_savings_pops = 0;
        uint32_t countries_with_negative_treasury = 0;
        uint32_t countries_with_creditors = 0;
        uint32_t creditor_count = 0;
        uint32_t creditors_was_paid = 0;
        int64_t treasury_observed_raw = 0;
        int64_t pop_money_observed_raw = 0;
        int64_t pop_savings_observed_raw = 0;
        int64_t bank_interest_accumulator_raw = 0;
        int64_t state_savings_candidate_raw = 0;
        int64_t state_interest_candidate_raw = 0;
        int64_t creditor_interest_candidate_raw = 0;
        int64_t creditor_debt_candidate_raw = 0;
        uint64_t collection_us = 0;

        bool complete() const { return snapshot_flags == 0 && collection_flags == 0; }
    };

    void AddEconomicValue(int64_t value, int64_t *total, uint32_t *flags,
                          uint32_t overflow_flag = SNAPSHOT_SUM_OVERFLOW);
    int64_t UtilizationBasisPoints(uint32_t value, uint32_t limit);
    bool SortUniqueNonnegativeIds(int32_t *ids, size_t count);
}
