#include "foreign_memory.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace smedley::game_state::foreign_memory
{
    bool AddOffset(uintptr_t address, size_t offset, uintptr_t *result) noexcept
    {
        if (result == nullptr || address > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
        *result = address + offset;
        return true;
    }

    bool IsAccessible(const void *pointer, size_t size, bool writable, MemoryRegionCache *cache) noexcept
    {
        if (pointer == nullptr || size == 0) return false;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
        uintptr_t end = 0;
        if (!AddOffset(begin, size, &end)) return false;
        for (uintptr_t cursor = begin; cursor < end;) {
            MemoryRegionCache region{};
            MemoryRegionCache *current = cache == nullptr ? &region : cache;
            if (cursor < current->begin || cursor >= current->end) {
                MEMORY_BASIC_INFORMATION info{};
                if (VirtualQuery(reinterpret_cast<const void *>(cursor), &info, sizeof(info)) != sizeof(info)) {
                    return false;
                }
                const uintptr_t region_begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
                if (!AddOffset(region_begin, info.RegionSize, &current->end)) return false;
                current->begin = region_begin;
                current->protect = info.Protect;
                current->state = info.State;
            }
            const DWORD allowed = writable
                ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
                : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                    | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (current->state != MEM_COMMIT || (current->protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || (current->protect & allowed) == 0 || current->end <= cursor) return false;
            cursor = (std::min)(end, current->end);
        }
        return true;
    }

    bool CopyReadable(void *destination, const void *source, size_t size, MemoryRegionCache *cache) noexcept
    {
        if (destination == nullptr || !IsAccessible(source, size, false, cache)) return false;
        __try {
            std::memcpy(destination, source, size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool CopyWritable(void *destination, const void *source, size_t size, MemoryRegionCache *cache) noexcept
    {
        if (source == nullptr || !IsAccessible(destination, size, true, cache)) return false;
        __try {
            std::memcpy(destination, source, size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}
