#include "pop_identity_core.hpp"

#include <limits>

namespace telemetry_plugin
{
    namespace
    {
        bool ValidSequence(const PopIdentityState *states, size_t count)
        {
            if (count != 0 && states == nullptr) return false;
            for (size_t index = 0; index < count; ++index) {
                if (states[index].pop_id < 0 || states[index].province_id < 0
                    || states[index].pop_type_id < 0 || states[index].size < 0
                    || (index != 0 && states[index - 1].pop_id >= states[index].pop_id)) return false;
            }
            return true;
        }

        bool Increment(uint32_t *value)
        {
            if (*value == (std::numeric_limits<uint32_t>::max)()) return false;
            ++*value;
            return true;
        }
    }

    bool DiffPopIdentities(const PopIdentityState *previous, size_t previous_count,
                           const PopIdentityState *current, size_t current_count,
                           PopIdentityChange *changes, size_t capacity, size_t *change_count,
                           PopIdentityDiff *diff)
    {
        if (change_count == nullptr || diff == nullptr || (capacity != 0 && changes == nullptr)
            || !ValidSequence(previous, previous_count) || !ValidSequence(current, current_count)) return false;
        *change_count = 0;
        *diff = {};
        size_t previous_index = 0;
        size_t current_index = 0;
        const auto append = [&](PopObservationKind kind, const PopIdentityState &before,
                                const PopIdentityState &after) {
            if (*change_count >= capacity) return false;
            changes[(*change_count)++] = {kind, before, after};
            return true;
        };
        while (previous_index < previous_count || current_index < current_count) {
            if (current_index == current_count
                || (previous_index < previous_count
                    && previous[previous_index].pop_id < current[current_index].pop_id)) {
                if (!append(PopObservationKind::Disappeared, previous[previous_index], {})
                    || !Increment(&diff->disappeared)) return false;
                ++previous_index;
                continue;
            }
            if (previous_index == previous_count
                || current[current_index].pop_id < previous[previous_index].pop_id) {
                if (!append(PopObservationKind::Appeared, {}, current[current_index])
                    || !Increment(&diff->appeared)) return false;
                ++current_index;
                continue;
            }
            const auto &before = previous[previous_index++];
            const auto &after = current[current_index++];
            if (before.province_id != after.province_id || before.pop_type_id != after.pop_type_id
                || before.country_key != after.country_key) {
                if (!append(PopObservationKind::ScopeChanged, before, after)
                    || !Increment(&diff->scope_changed)) return false;
            } else if (!Increment(&diff->unchanged)) return false;
        }
        return true;
    }
}
