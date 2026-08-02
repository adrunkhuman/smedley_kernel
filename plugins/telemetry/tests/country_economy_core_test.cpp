#include "country_economy_core.hpp"

#include <gtest/gtest.h>

namespace
{
    telemetry_plugin::CountryEconomyDay Day(const char *tag, int64_t population, long double nominal,
                                             bool complete = true)
    {
        telemetry_plugin::CountryEconomyDay day;
        std::copy_n(tag, 3, day.country_tag.begin());
        day.population = population;
        day.nominal[0] = nominal;
        day.real[0] = nominal / 2;
        day.complete = complete;
        return day;
    }
}

TEST(CountryEconomyAccumulatorTest, AggregatesAndBoundsFixedDayPeriods)
{
    telemetry_plugin::CountryEconomyAccumulator accumulator;
    constexpr int start = 59'883'384;
    EXPECT_EQ(accumulator.ObserveDate(start, smedley::telemetry::CaptureCadence::FixedDays, 3),
        telemetry_plugin::EconomyDateTransition::First);
    EXPECT_TRUE(accumulator.AddDay(start, Day("ENG", 10, 2)));
    EXPECT_EQ(accumulator.ObserveDate(start + 24, smedley::telemetry::CaptureCadence::FixedDays, 3),
        telemetry_plugin::EconomyDateTransition::SamePeriod);
    EXPECT_TRUE(accumulator.AddDay(start + 24, Day("ENG", 20, 3, false)));
    EXPECT_EQ(accumulator.ObserveDate(start + 48, smedley::telemetry::CaptureCadence::FixedDays, 3),
        telemetry_plugin::EconomyDateTransition::SamePeriod);
    EXPECT_TRUE(accumulator.AddDay(start + 48, Day("ENG", 30, 4)));
    EXPECT_EQ(accumulator.ObserveDate(start + 72, smedley::telemetry::CaptureCadence::FixedDays, 3),
        telemetry_plugin::EconomyDateTransition::NewPeriod);
    ASSERT_EQ(accumulator.period_count(), 1u);
    EXPECT_EQ(accumulator.observed_days(), 3u);
    EXPECT_EQ(accumulator.expected_days(), 3u);
    EXPECT_EQ(accumulator.period(0).observation_days, 3u);
    EXPECT_EQ(accumulator.period(0).invalid_days, 1u);
    EXPECT_EQ(accumulator.period(0).population_sum, 60);
    EXPECT_EQ(accumulator.period(0).nominal[0], 9);
}

TEST(CountryEconomyAccumulatorTest, DetectsCalendarBoundariesAndRegression)
{
    telemetry_plugin::CountryEconomyAccumulator accumulator;
    constexpr int january_31 = 59'884'080;
    constexpr int february_1 = january_31 + 24;
    EXPECT_EQ(accumulator.ObserveDate(january_31, smedley::telemetry::CaptureCadence::Monthly, 1),
        telemetry_plugin::EconomyDateTransition::First);
    EXPECT_TRUE(accumulator.AddDay(january_31, Day("FRA", 100, 1)));
    EXPECT_EQ(accumulator.ObserveDate(february_1, smedley::telemetry::CaptureCadence::Monthly, 1),
        telemetry_plugin::EconomyDateTransition::NewPeriod);
    EXPECT_EQ(accumulator.expected_days(), 31u);
    EXPECT_EQ(accumulator.ObserveDate(january_31 - 24, smedley::telemetry::CaptureCadence::Monthly, 1),
        telemetry_plugin::EconomyDateTransition::Regression);
}

TEST(CountryEconomyAccumulatorTest, RejectsDuplicateCountryDays)
{
    telemetry_plugin::CountryEconomyAccumulator accumulator;
    constexpr int start = 59'883'384;
    ASSERT_EQ(accumulator.ObserveDate(start, smedley::telemetry::CaptureCadence::Daily, 1),
        telemetry_plugin::EconomyDateTransition::First);
    EXPECT_TRUE(accumulator.AddDay(start, Day("PRU", 10, 1)));
    EXPECT_FALSE(accumulator.AddDay(start, Day("PRU", 10, 1)));
}

TEST(CountryEconomyAccumulatorTest, RetainsARecoverablePeriodAcrossDateGaps)
{
    telemetry_plugin::CountryEconomyAccumulator accumulator;
    constexpr int start = 59'883'384;
    ASSERT_EQ(accumulator.ObserveDate(start, smedley::telemetry::CaptureCadence::Weekly, 1),
        telemetry_plugin::EconomyDateTransition::First);
    EXPECT_TRUE(accumulator.AddDay(start, Day("USA", 10, 1)));
    EXPECT_EQ(accumulator.ObserveDate(start + 48, smedley::telemetry::CaptureCadence::Weekly, 1),
        telemetry_plugin::EconomyDateTransition::SamePeriod);
    EXPECT_TRUE(accumulator.AddDay(start + 48, Day("USA", 10, 1)));
    EXPECT_EQ(accumulator.observed_days(), 2u);
    EXPECT_EQ(accumulator.expected_days(), 7u);
}
