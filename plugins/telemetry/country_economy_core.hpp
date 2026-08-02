#pragma once

#include "telemetry_core.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace telemetry_plugin
{
    constexpr size_t max_country_economy_slots = 512;
    constexpr size_t country_economy_component_count = 3;

    enum class CountryEconomyComponent : uint8_t
    {
        Factory,
        Rgo,
        Artisan,
    };

    struct CountryEconomyDay
    {
        std::array<char, 4> country_tag{};
        std::array<long double, country_economy_component_count> nominal{};
        std::array<long double, country_economy_component_count> real{};
        int64_t population = 0;
        uint32_t unsettled_factory_candidates = 0;
        bool complete = true;
    };

    struct CountryEconomyPeriod
    {
        std::array<char, 4> country_tag{};
        std::array<long double, country_economy_component_count> nominal{};
        std::array<long double, country_economy_component_count> real{};
        long double population_sum = 0;
        uint32_t observation_days = 0;
        uint32_t invalid_days = 0;
        uint64_t unsettled_factory_candidates = 0;
        int32_t last_date_raw = 0;
    };

    enum class EconomyDateTransition : uint8_t
    {
        First,
        SamePeriod,
        NewPeriod,
        Regression,
    };

    class CountryEconomyAccumulator
    {
    public:
        EconomyDateTransition ObserveDate(int32_t date_raw, smedley::telemetry::CaptureCadence cadence,
                                          int fixed_days);
        void StartPeriod(int32_t date_raw, smedley::telemetry::CaptureCadence cadence, int fixed_days);
        bool AddDay(int32_t date_raw, const CountryEconomyDay &day);
        void Reset() noexcept;

        const CountryEconomyPeriod &period(size_t index) const { return periods_[index]; }
        size_t period_count() const { return period_count_; }
        int32_t period_start_raw() const { return period_start_raw_; }
        int32_t period_end_raw() const { return period_end_raw_; }
        uint32_t observed_days() const { return observed_days_; }
        uint32_t expected_days() const { return expected_days_; }
        bool active() const { return active_; }

    private:
        static int64_t PeriodKey(int32_t date_raw, smedley::telemetry::CaptureCadence cadence,
                                 int fixed_days, int32_t anchor_raw);
        static uint32_t ExpectedDays(int32_t date_raw, smedley::telemetry::CaptureCadence cadence,
                                     int fixed_days);

        std::array<CountryEconomyPeriod, max_country_economy_slots> periods_{};
        size_t period_count_ = 0;
        int32_t anchor_raw_ = 0;
        int32_t period_start_raw_ = 0;
        int32_t period_end_raw_ = 0;
        int32_t last_observed_raw_ = 0;
        int64_t period_key_ = 0;
        uint32_t observed_days_ = 0;
        uint32_t expected_days_ = 0;
        bool active_ = false;
    };

    const char *CountryEconomyComponentName(CountryEconomyComponent component);
}
