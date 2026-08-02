#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry_plugin
{
    constexpr size_t max_factory_sales_records = 8192;

    struct FactorySalesHookRecord
    {
        const void *factory = nullptr;
        int64_t proceeds_raw = 0;
        int64_t produced_raw = 0;
        int64_t opening_inventory_raw = 0;
        int64_t closing_inventory_raw = 0;
    };

    bool InstallFactorySalesHook(std::string *error);
    bool UninstallFactorySalesHook(std::string *error);
    bool DrainFactorySalesHook(FactorySalesHookRecord *records, size_t capacity,
                               uint32_t *count, uint64_t *dropped);
}
