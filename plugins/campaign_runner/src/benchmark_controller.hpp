#pragma once

#include <cstdint>
#include <optional>

namespace campaign_runner
{
    enum class BenchmarkAction { Continue, Complete, Fail };

    struct BenchmarkObservation
    {
        bool idler_available = false;
        std::optional<int> date_raw;
        int pause_state = -1;
        bool game_over = false;
        bool observer_invariants_valid = true;
        uint64_t monotonic_us = 0;
    };

    struct BenchmarkDecision
    {
        BenchmarkAction action = BenchmarkAction::Continue;
        const char *reason = nullptr;
    };

    class BenchmarkController
    {
    public:
        static constexpr int raw_units_per_day = 24;

        bool Begin(int start_date_raw, std::optional<int> requested_days, std::optional<int> requested_target_raw,
                   int timeout_seconds, uint64_t start_monotonic_us, const char **error);
        BenchmarkDecision Observe(const BenchmarkObservation &observation);
        bool active() const { return active_; }
        int start_date_raw() const { return start_date_raw_; }
        int target_date_raw() const { return target_date_raw_; }
        int requested_days() const { return requested_days_; }
        uint64_t start_monotonic_us() const { return start_monotonic_us_; }

    private:
        bool active_ = false;
        int start_date_raw_ = 0;
        int target_date_raw_ = 0;
        int requested_days_ = 0;
        int timeout_seconds_ = 0;
        uint64_t start_monotonic_us_ = 0;
        int previous_date_raw_ = 0;
        bool target_pause_observed_ = false;
    };
}
