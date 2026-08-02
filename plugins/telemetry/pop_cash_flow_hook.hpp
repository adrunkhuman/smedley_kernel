#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry_plugin
{
    constexpr size_t pop_cash_flow_component_count = 8;
    constexpr size_t max_pop_cash_flow_records = 100000;

    struct PopCashFlowHookRecord
    {
        const void *pop = nullptr;
        std::array<int64_t, pop_cash_flow_component_count> posted_raw{};
        std::array<int64_t, pop_cash_flow_component_count> money_delta_raw{};
        uint32_t call_count = 0;
        uint32_t clamped_call_count = 0;
    };

    struct PopCashFlowHookStats
    {
        uint64_t calls = 0;
        uint64_t invalid_index = 0;
        uint64_t table_full = 0;
        uint64_t overflow = 0;
        uint64_t output_overflow = 0;

        bool complete() const
        {
            return invalid_index == 0 && table_full == 0 && overflow == 0 && output_overflow == 0;
        }
    };

    bool InstallPopCashFlowHook(std::string *error);
    bool UninstallPopCashFlowHook(std::string *error);
    bool DrainPopCashFlowHook(PopCashFlowHookRecord *records, size_t capacity,
                              uint32_t *count, PopCashFlowHookStats *stats);
}
