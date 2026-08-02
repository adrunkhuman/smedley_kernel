#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry_plugin
{
    constexpr size_t max_factory_flow_records = 2048;

    struct FactorySettlementProbeRecord
    {
        const void *factory = nullptr;
        uint32_t pool = 0;
        std::array<int64_t, 64> quantity_raw{};
    };

    bool InstallFactoryConsumptionProbe(std::string *error);
    bool UninstallFactoryConsumptionProbe(std::string *error);
    bool DrainFactoryConsumptionProbe(FactorySettlementProbeRecord *records, size_t capacity,
                                      uint32_t *count, uint64_t *dropped);
}
