#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace telemetry_plugin
{
    constexpr size_t pop_cash_flow_count = 8;

    struct PopCashFlowAccount
    {
        int64_t opening_money_raw = 0;
        int64_t closing_money_raw = 0;
        int64_t posted_raw = 0;
        int64_t money_delta_raw = 0;
        int64_t residual_raw = 0;
    };

    const char *PopCashFlowName(size_t index);
    bool ReconcilePopCashFlow(int64_t opening_money_raw, int64_t closing_money_raw,
                              const std::array<int64_t, pop_cash_flow_count> &posted_raw,
                              const std::array<int64_t, pop_cash_flow_count> &money_delta_raw,
                              PopCashFlowAccount *account);
}
