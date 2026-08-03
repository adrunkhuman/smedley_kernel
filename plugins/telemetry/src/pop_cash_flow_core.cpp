#include "pop_cash_flow_core.hpp"

#include <array>
#include <limits>

namespace telemetry_plugin
{
    namespace
    {
        constexpr std::array<const char *, pop_cash_flow_count> names{
            "needs", "welfare", "salary", "expenses", "events", "projects", "bank", "interest"};

        bool Add(int64_t value, int64_t *sum)
        {
            if ((value > 0 && *sum > (std::numeric_limits<int64_t>::max)() - value)
                || (value < 0 && *sum < (std::numeric_limits<int64_t>::min)() - value)) return false;
            *sum += value;
            return true;
        }

        bool Subtract(int64_t left, int64_t right, int64_t *difference)
        {
            if ((right > 0 && left < (std::numeric_limits<int64_t>::min)() + right)
                || (right < 0 && left > (std::numeric_limits<int64_t>::max)() + right)) return false;
            *difference = left - right;
            return true;
        }
    }

    const char *PopCashFlowName(size_t index)
    {
        return index < names.size() ? names[index] : "unknown";
    }

    bool ReconcilePopCashFlow(int64_t opening_money_raw, int64_t closing_money_raw,
                              const std::array<int64_t, pop_cash_flow_count> &posted_raw,
                              const std::array<int64_t, pop_cash_flow_count> &money_delta_raw,
                              PopCashFlowAccount *account)
    {
        if (account == nullptr || opening_money_raw < 0 || closing_money_raw < 0) return false;
        PopCashFlowAccount value{};
        value.opening_money_raw = opening_money_raw;
        value.closing_money_raw = closing_money_raw;
        for (size_t index = 0; index < pop_cash_flow_count; ++index) {
            if (!Add(posted_raw[index], &value.posted_raw)
                || !Add(money_delta_raw[index], &value.money_delta_raw)) return false;
        }
        int64_t observed_delta = 0;
        if (!Subtract(closing_money_raw, opening_money_raw, &observed_delta)
            || !Subtract(observed_delta, value.money_delta_raw, &value.residual_raw)) return false;
        *account = value;
        return true;
    }
}
