#include "benchmark_controller.hpp"

#include <limits>

namespace campaign_runner
{
    bool BenchmarkController::Begin(int start_date_raw, std::optional<int> requested_days,
                                    std::optional<int> requested_target_raw, int timeout_seconds,
                                    uint64_t start_monotonic_us, const char **error)
    {
        active_ = false;
        if (error != nullptr) *error = nullptr;
        auto fail = [&](const char *message) {
            if (error != nullptr) *error = message;
            return false;
        };
        if (requested_days.has_value() == requested_target_raw.has_value() || timeout_seconds < 1 || timeout_seconds > 86400) {
            return fail("invalid benchmark condition");
        }
        int64_t target = requested_target_raw.value_or(0);
        if (requested_days) {
            if (*requested_days < 1 || *requested_days > 1000000) {
                return fail("run days must be from 1 through 1000000");
            }
            target = static_cast<int64_t>(start_date_raw) + static_cast<int64_t>(*requested_days) * raw_units_per_day;
        }
        if (target < (std::numeric_limits<int>::min)() || target > (std::numeric_limits<int>::max)()
            || target <= start_date_raw || (target - start_date_raw) % raw_units_per_day != 0) {
            return fail("benchmark target must advance from the start date on a 24-raw-unit boundary");
        }
        active_ = true;
        start_date_raw_ = start_date_raw;
        target_date_raw_ = static_cast<int>(target);
        requested_days_ = static_cast<int>((target - start_date_raw) / raw_units_per_day);
        timeout_seconds_ = timeout_seconds;
        start_monotonic_us_ = start_monotonic_us;
        previous_date_raw_ = start_date_raw;
        target_pause_observed_ = false;
        return true;
    }

    BenchmarkDecision BenchmarkController::Observe(const BenchmarkObservation &observation)
    {
        if (!active_) return {};
        auto fail = [&](const char *reason) {
            active_ = false;
            return BenchmarkDecision{BenchmarkAction::Fail, reason};
        };
        if (!observation.idler_available || !observation.date_raw) return fail("idler_unavailable");
        if (observation.pause_state != 0 && observation.pause_state != 1) return fail("invalid_pause_state");
        if (observation.game_over) return fail("game_over");
        if (!observation.observer_invariants_valid) return fail("observer_invariant_failed");
        if (*observation.date_raw < previous_date_raw_) return fail("date_regressed");
        previous_date_raw_ = *observation.date_raw;
        if (*observation.date_raw > target_date_raw_) return fail("date_overshoot");
        const uint64_t timeout_us = static_cast<uint64_t>(timeout_seconds_) * 1000000ull;
        const bool timed_out = observation.monotonic_us < start_monotonic_us_
            || observation.monotonic_us - start_monotonic_us_ >= timeout_us;
        if (*observation.date_raw == target_date_raw_) {
            if (observation.pause_state == 1 && target_pause_observed_) {
                active_ = false;
                return {BenchmarkAction::Complete, nullptr};
            }
            if (timed_out) return fail("timeout");
            if (observation.pause_state == 1) {
                target_pause_observed_ = true;
            } else {
                target_pause_observed_ = false;
            }
            return {};
        }
        if (observation.pause_state == 1) return fail("unexpected_pause");
        if (timed_out) return fail("timeout");
        return {};
    }
}
