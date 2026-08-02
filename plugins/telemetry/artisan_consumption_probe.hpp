#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry_plugin
{
    constexpr size_t max_artisan_flow_records = 8192;

    struct ArtisanSettlementProbeRecord
    {
        const void *pop = nullptr;
        uint32_t pool = 0;
        std::array<int64_t, 64> quantity_raw{};
    };

    bool InstallArtisanConsumptionProbe(const uint32_t *country_keys, size_t country_count, std::string *error);
    bool UninstallArtisanConsumptionProbe(std::string *error);
    bool DrainArtisanConsumptionProbe(ArtisanSettlementProbeRecord *records, size_t capacity,
                                      uint32_t *count, uint64_t *dropped);
}
