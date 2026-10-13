#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <smedley/game_state/references.hpp>

namespace smedley::game_state
{
    constexpr size_t max_factory_sales_records = 8192;

    struct FactorySalesHookRecord
    {
        FactoryRef factory{};
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
