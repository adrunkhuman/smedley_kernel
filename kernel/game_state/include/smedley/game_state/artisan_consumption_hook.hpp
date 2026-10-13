#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <smedley/game_state/references.hpp>

namespace smedley::game_state
{
    constexpr size_t max_artisan_flow_records = 8192;

    struct ArtisanSettlementHookRecord
    {
        PopRef pop{};
        uint32_t pool = 0;
        std::array<int64_t, 64> quantity_raw{};
    };

    bool InstallArtisanConsumptionHook(const uint32_t *country_keys, size_t country_count, std::string *error);
    bool UninstallArtisanConsumptionHook(std::string *error);
    bool DrainArtisanConsumptionHook(ArtisanSettlementHookRecord *records, size_t capacity,
                                     uint32_t *count, uint64_t *dropped);
}
