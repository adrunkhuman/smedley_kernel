#pragma once

#include <cstdint>

namespace telemetry_plugin
{
    struct ProducerSale
    {
        int64_t opening_inventory_raw = 0;
        int64_t produced_raw = 0;
        int64_t sold_raw = 0;
        int64_t closing_inventory_raw = 0;
        int64_t proceeds_raw = 0;
    };

    bool ReconcileProducerSale(int64_t opening_inventory_raw, int64_t produced_raw,
                               int64_t closing_inventory_raw, int64_t proceeds_raw,
                               ProducerSale *sale) noexcept;
}
