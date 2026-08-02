#include "country_economy_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace telemetry_plugin
{
    namespace
    {
        constexpr std::array<uint32_t, 12> month_days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        bool SameTag(const std::array<char, 4> &left, const std::array<char, 4> &right)
        {
            return left == right;
        }
    }

    int64_t CountryEconomyAccumulator::PeriodKey(int32_t date_raw, smedley::telemetry::CaptureCadence cadence,
                                                  int fixed_days, int32_t anchor_raw)
    {
        if (cadence == smedley::telemetry::CaptureCadence::Daily) return date_raw / 24;
        if (cadence == smedley::telemetry::CaptureCadence::FixedDays
            || cadence == smedley::telemetry::CaptureCadence::Weekly) {
            const int days = cadence == smedley::telemetry::CaptureCadence::Weekly ? 7 : fixed_days;
            return (date_raw / 24 - anchor_raw / 24) / days;
        }
        const auto date = smedley::telemetry::DecodeClausewitzDate(date_raw);
        if (!date) return (std::numeric_limits<int64_t>::min)();
        if (cadence == smedley::telemetry::CaptureCadence::Monthly) {
            return static_cast<int64_t>(date->year) * 12 + date->month - 1;
        }
        return date->year;
    }

    uint32_t CountryEconomyAccumulator::ExpectedDays(int32_t date_raw,
                                                      smedley::telemetry::CaptureCadence cadence, int fixed_days)
    {
        if (cadence == smedley::telemetry::CaptureCadence::Daily) return 1;
        if (cadence == smedley::telemetry::CaptureCadence::Weekly) return 7;
        if (cadence == smedley::telemetry::CaptureCadence::FixedDays) return static_cast<uint32_t>(fixed_days);
        if (cadence == smedley::telemetry::CaptureCadence::Yearly) return 365;
        const auto date = smedley::telemetry::DecodeClausewitzDate(date_raw);
        return date && date->month >= 1 && date->month <= 12 ? month_days[date->month - 1] : 0;
    }

    EconomyDateTransition CountryEconomyAccumulator::ObserveDate(
        int32_t date_raw, smedley::telemetry::CaptureCadence cadence, int fixed_days)
    {
        if (!active_) {
            StartPeriod(date_raw, cadence, fixed_days);
            return EconomyDateTransition::First;
        }
        if (date_raw < last_observed_raw_) return EconomyDateTransition::Regression;
        if (date_raw == last_observed_raw_) return EconomyDateTransition::SamePeriod;
        const int64_t key = PeriodKey(date_raw, cadence, fixed_days, anchor_raw_);
        return key == period_key_ ? EconomyDateTransition::SamePeriod : EconomyDateTransition::NewPeriod;
    }

    void CountryEconomyAccumulator::StartPeriod(int32_t date_raw,
                                                 smedley::telemetry::CaptureCadence cadence, int fixed_days)
    {
        if (!active_ || date_raw < last_observed_raw_) anchor_raw_ = date_raw;
        for (size_t index = 0; index < period_count_; ++index) periods_[index] = {};
        period_count_ = 0;
        period_start_raw_ = date_raw;
        period_end_raw_ = date_raw;
        last_observed_raw_ = date_raw;
        period_key_ = PeriodKey(date_raw, cadence, fixed_days, anchor_raw_);
        observed_days_ = 0;
        expected_days_ = ExpectedDays(date_raw, cadence, fixed_days);
        active_ = true;
    }

    bool CountryEconomyAccumulator::AddDay(int32_t date_raw, const CountryEconomyDay &day)
    {
        if (!active_ || date_raw < period_start_raw_ || date_raw % 24 != period_start_raw_ % 24
            || day.country_tag[0] == '\0') return false;
        if (date_raw != last_observed_raw_) {
            if (date_raw < last_observed_raw_) return false;
            last_observed_raw_ = date_raw;
            period_end_raw_ = date_raw;
            ++observed_days_;
        } else if (observed_days_ == 0) {
            observed_days_ = 1;
        }
        size_t index = 0;
        while (index < period_count_ && !SameTag(periods_[index].country_tag, day.country_tag)) ++index;
        if (index == period_count_) {
            if (period_count_ >= periods_.size()) return false;
            periods_[index].country_tag = day.country_tag;
            ++period_count_;
        }
        auto &period = periods_[index];
        if (period.last_date_raw == date_raw) return false;
        for (size_t component = 0; component < country_economy_component_count; ++component) {
            if (!std::isfinite(day.nominal[component]) || !std::isfinite(day.real[component])
                || !std::isfinite(period.nominal[component] + day.nominal[component])
                || !std::isfinite(period.real[component] + day.real[component])) return false;
            period.nominal[component] += day.nominal[component];
            period.real[component] += day.real[component];
        }
        if (!std::isfinite(period.population_sum + day.population)) return false;
        period.population_sum += day.population;
        if (period.unsettled_factory_candidates > (std::numeric_limits<uint64_t>::max)()
                - day.unsettled_factory_candidates) return false;
        period.unsettled_factory_candidates += day.unsettled_factory_candidates;
        ++period.observation_days;
        if (!day.complete) ++period.invalid_days;
        period.last_date_raw = date_raw;
        return true;
    }

    void CountryEconomyAccumulator::Reset() noexcept
    {
        for (size_t index = 0; index < period_count_; ++index) periods_[index] = {};
        period_count_ = 0;
        anchor_raw_ = 0;
        period_start_raw_ = 0;
        period_end_raw_ = 0;
        last_observed_raw_ = 0;
        period_key_ = 0;
        observed_days_ = 0;
        expected_days_ = 0;
        active_ = false;
    }

    const char *CountryEconomyComponentName(CountryEconomyComponent component)
    {
        switch (component) {
        case CountryEconomyComponent::Factory: return "factory";
        case CountryEconomyComponent::Rgo: return "rgo";
        case CountryEconomyComponent::Artisan: return "artisan";
        }
        return "unknown";
    }
}
