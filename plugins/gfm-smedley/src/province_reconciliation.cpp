#include "province_reconciliation.hpp"

namespace gfm_smedley
{
    namespace
    {
        const CountryPolicySnapshot *FindCountry(
            const CountryPolicySnapshot *countries, uint32_t count, uint64_t country) noexcept
        {
            for (uint32_t index = 0; index < count; ++index) {
                if (countries[index].country == country) return &countries[index];
            }
            return nullptr;
        }

        ReconciliationPlanStatus ValidateCountries(
            const CountryPolicySnapshot *countries, uint32_t count) noexcept
        {
            for (uint32_t index = 0; index < count; ++index) {
                if (countries[index].country == 0 || countries[index].no_slavery_selected > 1) {
                    return ReconciliationPlanStatus::invalid_record;
                }
                for (uint32_t prior = 0; prior < index; ++prior) {
                    if (countries[prior].country == countries[index].country) {
                        return ReconciliationPlanStatus::duplicate_country;
                    }
                }
            }
            return ReconciliationPlanStatus::success;
        }
    }

    ReconciliationPlanStatus PlanSlaveTradeReconciliation(
        const ProvinceModifierSnapshot *provinces,
        uint32_t province_count,
        const CountryPolicySnapshot *countries,
        uint32_t country_count,
        ProvinceModifierRemoval *removals,
        uint32_t removal_capacity,
        uint32_t *removal_count) noexcept
    {
        if (removal_count == nullptr) return ReconciliationPlanStatus::invalid_argument;
        *removal_count = 0;
        if ((province_count != 0 && provinces == nullptr)
            || (country_count != 0 && countries == nullptr)
            || (removal_capacity != 0 && removals == nullptr)) {
            return ReconciliationPlanStatus::invalid_argument;
        }

        const auto country_status = ValidateCountries(countries, country_count);
        if (country_status != ReconciliationPlanStatus::success) return country_status;

        uint32_t required = 0;
        for (uint32_t index = 0; index < province_count; ++index) {
            const auto &province = provinces[index];
            if (province.province == 0
                || (province.present_modifier_mask & ~slave_trade_modifier_mask) != 0) {
                return ReconciliationPlanStatus::invalid_record;
            }
            for (uint32_t prior = 0; prior < index; ++prior) {
                if (provinces[prior].province == province.province) {
                    return ReconciliationPlanStatus::invalid_record;
                }
            }
            if (province.present_modifier_mask == 0 || province.owner == 0) continue;
            const auto *owner = FindCountry(countries, country_count, province.owner);
            if (owner == nullptr) return ReconciliationPlanStatus::missing_country_policy;
            if (owner->no_slavery_selected != 0) ++required;
        }
        if (required > removal_capacity) return ReconciliationPlanStatus::output_too_small;

        uint32_t output = 0;
        for (uint32_t index = 0; index < province_count; ++index) {
            const auto &province = provinces[index];
            if (province.present_modifier_mask == 0 || province.owner == 0) continue;
            const auto *owner = FindCountry(countries, country_count, province.owner);
            if (owner->no_slavery_selected == 0) continue;
            removals[output++] = {province.province, slave_trade_modifier_mask};
        }
        *removal_count = output;
        return ReconciliationPlanStatus::success;
    }

    bool ReconciliationDue(int32_t date_raw, ReconciliationSchedule *schedule) noexcept
    {
        if (schedule == nullptr) return false;
        const bool regressed = schedule->observed && date_raw < schedule->last_observed_date_raw;
        schedule->last_observed_date_raw = date_raw;
        schedule->observed = true;
        if (regressed) schedule->reconciled = false;
        if (schedule->reconciled
            && static_cast<int64_t>(date_raw) - schedule->last_reconciliation_date_raw
                < reconciliation_interval_raw) return false;
        schedule->last_reconciliation_date_raw = date_raw;
        schedule->reconciled = true;
        return true;
    }
}
