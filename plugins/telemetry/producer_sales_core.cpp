#include "producer_sales_core.hpp"

#include <limits>

namespace telemetry_plugin
{
    bool ReconcileProducerSale(int64_t opening_inventory_raw, int64_t produced_raw,
                               int64_t closing_inventory_raw, int64_t proceeds_raw,
                               ProducerSale *sale) noexcept
    {
        if (sale == nullptr || opening_inventory_raw < 0 || produced_raw < 0
            || closing_inventory_raw < 0 || proceeds_raw < 0
            || opening_inventory_raw > (std::numeric_limits<int64_t>::max)() - produced_raw) return false;
        const int64_t available = opening_inventory_raw + produced_raw;
        if (closing_inventory_raw > available) return false;
        *sale = {opening_inventory_raw, produced_raw, available - closing_inventory_raw,
            closing_inventory_raw, proceeds_raw};
        return true;
    }
}
