#pragma once

#include <cstddef>
#include <cstdint>

namespace telemetry_plugin
{
    enum class PopObservationKind : uint8_t
    {
        Appeared,
        Disappeared,
        ScopeChanged,
    };

    struct PopIdentityState
    {
        int32_t pop_id = -1;
        int32_t province_id = -1;
        int32_t pop_type_id = -1;
        int32_t size = 0;
        uint32_t country_key = 0;
    };

    struct PopIdentityChange
    {
        PopObservationKind kind = PopObservationKind::Appeared;
        PopIdentityState previous;
        PopIdentityState current;
    };

    struct PopIdentityDiff
    {
        uint32_t appeared = 0;
        uint32_t disappeared = 0;
        uint32_t scope_changed = 0;
        uint32_t unchanged = 0;
    };

    bool DiffPopIdentities(const PopIdentityState *previous, size_t previous_count,
                           const PopIdentityState *current, size_t current_count,
                           PopIdentityChange *changes, size_t capacity, size_t *change_count,
                           PopIdentityDiff *diff);
}
