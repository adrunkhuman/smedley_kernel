#pragma once

#include "v2_304_layout.hpp"

#include <smedley/memory.hpp>

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>

namespace smedley::game_state
{
    using EngineString = v2_304_layout::EngineString;

    class EngineStringArgument
    {
    public:
        explicit EngineStringArgument(std::string_view value)
        {
            if (value.size() > (std::numeric_limits<uint32_t>::max)()) return;
            value_ = {};
            value_.size = static_cast<uint32_t>(value.size());
            if (value.size() <= 0xf) {
                std::memcpy(value_.storage.inline_buffer, value.data(), value.size());
                value_.capacity = 0xf;
            } else {
                value_.storage.pointer = const_cast<char *>(value.data());
                value_.capacity = value_.size;
            }
            valid_ = true;
        }

        const EngineString *get() const noexcept { return &value_; }
        bool valid() const noexcept { return valid_; }

    private:
        EngineString value_{};
        bool valid_ = false;
    };

    inline void AssignTransferredEngineString(EngineString *target, const char *value)
    {
        if (target == nullptr || value == nullptr) throw std::bad_alloc();
        *target = {};
        const size_t size = std::strlen(value);
        if (size > (std::numeric_limits<uint32_t>::max)()) throw std::bad_alloc();
        if (size <= 0xf) {
            std::memcpy(target->storage.inline_buffer, value, size + 1);
            target->size = static_cast<uint32_t>(size);
            target->capacity = 0xf;
            return;
        }
        target->storage.pointer = static_cast<char *>(HeapAlloc(memory::Map::game_heap, 0, size + 1));
        if (target->storage.pointer == nullptr) throw std::bad_alloc();
        std::memcpy(target->storage.pointer, value, size + 1);
        target->size = static_cast<uint32_t>(size);
        target->capacity = static_cast<uint32_t>(size);
    }

    inline void ReleaseTransferredEngineString(EngineString *value) noexcept
    {
        if (value == nullptr) return;
        if (value->capacity > 0xf && value->storage.pointer != nullptr) {
            HeapFree(memory::Map::game_heap, 0, value->storage.pointer);
        }
        *value = {};
    }
}
