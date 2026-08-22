#pragma once

#include <cstddef>
#include <cstdint>

namespace smedley::game_state::detail
{
    constexpr uint32_t max_modifiers_per_province = 1024;

    template <typename Reader>
    bool ReadProvinceModifierMask(uintptr_t province, const uintptr_t *definitions, uint32_t definition_count,
                                  Reader &&read, uint32_t *mask, uint32_t *iteration_guard) noexcept
    {
        if (province == 0 || definitions == nullptr || definition_count == 0 || definition_count > 32
            || mask == nullptr || iteration_guard == nullptr) return false;

        uintptr_t head = 0;
        uintptr_t tail = 0;
        int32_t declared_count = 0;
        uint8_t guard = 0;
        if (!read(province + 0xc8, &head, sizeof(head)) || !read(province + 0xcc, &tail, sizeof(tail))
            || !read(province + 0xd0, &declared_count, sizeof(declared_count))
            || !read(province + 0xd4, &guard, sizeof(guard)) || declared_count < 0
            || static_cast<uint32_t>(declared_count) > max_modifiers_per_province) return false;
        if ((declared_count == 0) != (head == 0 && tail == 0)) return false;

        uint32_t value = 0;
        uint32_t traversed = 0;
        uintptr_t previous = 0;
        uintptr_t node = head;
        while (node != 0 && traversed < max_modifiers_per_province) {
            uintptr_t modifier = 0;
            uintptr_t definition = 0;
            uintptr_t linked_previous = 0;
            uintptr_t next = 0;
            if (!read(node, &modifier, sizeof(modifier)) || modifier == 0
                || !read(node + 0x4, &linked_previous, sizeof(linked_previous)) || linked_previous != previous
                || !read(modifier + 0x8, &definition, sizeof(definition))
                || !read(node + 0x8, &next, sizeof(next))) return false;
            for (uint32_t index = 0; index < definition_count; ++index) {
                if (definition == definitions[index]) value |= 1u << index;
            }
            ++traversed;
            previous = node;
            node = next;
        }
        if (node != 0 || traversed != static_cast<uint32_t>(declared_count) || previous != tail) return false;
        *mask = value;
        *iteration_guard = guard;
        return true;
    }
}
