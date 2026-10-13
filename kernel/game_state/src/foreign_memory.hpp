#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace smedley::game_state::foreign_memory
{
    struct MemoryRegionCache
    {
        uintptr_t begin = 0;
        uintptr_t end = 0;
        DWORD protect = 0;
        DWORD state = 0;
    };

    bool AddOffset(uintptr_t address, size_t offset, uintptr_t *result) noexcept;
    bool IsAccessible(const void *pointer, size_t size, bool writable, MemoryRegionCache *cache = nullptr) noexcept;
    bool CopyReadable(void *destination, const void *source, size_t size,
                      MemoryRegionCache *cache = nullptr) noexcept;
    bool CopyWritable(void *destination, const void *source, size_t size,
                      MemoryRegionCache *cache = nullptr) noexcept;

    template <typename T>
    bool ReadValue(uintptr_t address, T *value, MemoryRegionCache *cache = nullptr)
    {
        if (value == nullptr) return false;
        std::array<std::byte, sizeof(T)> copy{};
        if (!CopyReadable(copy.data(), reinterpret_cast<const void *>(address), copy.size(), cache)) return false;
        std::memcpy(value, copy.data(), copy.size());
        return true;
    }

    template <typename T>
    bool ReadField(const void *object, size_t offset, T *value, MemoryRegionCache *cache = nullptr)
    {
        uintptr_t address = 0;
        return object != nullptr && AddOffset(reinterpret_cast<uintptr_t>(object), offset, &address)
            && ReadValue(address, value, cache);
    }

    template <typename Vector>
    bool VectorCount(const Vector &vector, size_t element_size, uint32_t limit, uint32_t *count,
                     MemoryRegionCache *cache = nullptr)
    {
        if (count == nullptr || element_size == 0) return false;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(vector.begin);
        const uintptr_t end = reinterpret_cast<uintptr_t>(vector.end);
        const uintptr_t capacity = reinterpret_cast<uintptr_t>(vector.capacity);
        if (begin == 0 && end == 0 && capacity == 0) {
            *count = 0;
            return true;
        }
        if (begin == 0 || begin > end || end > capacity || begin % alignof(void *) != 0
            || end % alignof(void *) != 0 || capacity % alignof(void *) != 0
            || (end - begin) % element_size != 0 || (capacity - begin) % element_size != 0) return false;
        const uintptr_t elements = (end - begin) / element_size;
        if (elements > limit || (elements != 0
                && !IsAccessible(vector.begin, static_cast<size_t>(end - begin), false, cache))) {
            return false;
        }
        *count = static_cast<uint32_t>(elements);
        return true;
    }

    template <typename Vector>
    bool ReadVector(const void *object, size_t offset, size_t element_size, uint32_t limit,
                    Vector *vector, uint32_t *count, MemoryRegionCache *cache = nullptr)
    {
        if (vector == nullptr || count == nullptr) return false;
        Vector copy{};
        uint32_t value = 0;
        if (!ReadField(object, offset, &copy, cache)
            || !VectorCount(copy, element_size, limit, &value, cache)) return false;
        *vector = copy;
        *count = value;
        return true;
    }
}
