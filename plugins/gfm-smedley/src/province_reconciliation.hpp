#pragma once

#include <cstdint>

namespace gfm_smedley
{
    constexpr uint32_t slave_trade_modifier_mask = (1u << 4) - 1;
    constexpr int32_t reconciliation_interval_raw = 30 * 24;

    struct ReconciliationSchedule
    {
        int32_t last_observed_date_raw = 0;
        int32_t last_reconciliation_date_raw = 0;
        bool observed = false;
        bool reconciled = false;
    };

    struct ProvinceModifierSnapshot
    {
        uint64_t province = 0;
        uint64_t owner = 0;
        uint32_t present_modifier_mask = 0;
    };

    struct CountryPolicySnapshot
    {
        uint64_t country = 0;
        uint32_t no_slavery_selected = 0;
    };

    struct ProvinceModifierRemoval
    {
        uint64_t province = 0;
        uint32_t modifier_mask = 0;
    };

    enum class ReconciliationPlanStatus : uint8_t
    {
        success,
        invalid_argument,
        invalid_record,
        duplicate_country,
        missing_country_policy,
        output_too_small,
    };

    ReconciliationPlanStatus PlanSlaveTradeReconciliation(
        const ProvinceModifierSnapshot *provinces,
        uint32_t province_count,
        const CountryPolicySnapshot *countries,
        uint32_t country_count,
        ProvinceModifierRemoval *removals,
        uint32_t removal_capacity,
        uint32_t *removal_count) noexcept;

    bool ReconciliationDue(int32_t date_raw, ReconciliationSchedule *schedule) noexcept;
}
