#include "economic_capture_core.hpp"

#include <algorithm>
#include <limits>

namespace telemetry_plugin
{
    void AddEconomicValue(int64_t value, int64_t *total, uint32_t *flags,
                          uint32_t overflow_flag)
    {
        if ((value > 0 && *total > (std::numeric_limits<int64_t>::max)() - value)
            || (value < 0 && *total < (std::numeric_limits<int64_t>::min)() - value)) {
            *flags |= overflow_flag;
            return;
        }
        *total += value;
    }

    int64_t UtilizationBasisPoints(uint32_t value, uint32_t limit)
    {
        if (limit == 0) return 0;
        return static_cast<int64_t>(value) * 10000 / limit;
    }

    bool SortUniqueNonnegativeIds(int32_t *ids, size_t count)
    {
        if (count == 0) return true;
        if (ids == nullptr) return false;
        std::sort(ids, ids + count);
        return ids[0] >= 0 && std::adjacent_find(ids, ids + count) == ids + count;
    }
}
