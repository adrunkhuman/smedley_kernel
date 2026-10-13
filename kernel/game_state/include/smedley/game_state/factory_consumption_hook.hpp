#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <smedley/game_state/references.hpp>

namespace smedley::game_state
{
    constexpr size_t max_factory_flow_records = 2048;

    struct FactorySettlementHookRecord
    {
        FactoryRef factory{};
        uint32_t pool = 0;
        std::array<int64_t, 64> quantity_raw{};
    };

    bool InstallFactoryConsumptionHook(std::string *error);
    bool UninstallFactoryConsumptionHook(std::string *error);
    bool DrainFactoryConsumptionHook(FactorySettlementHookRecord *records, size_t capacity,
                                     uint32_t *count, uint64_t *dropped);
}
