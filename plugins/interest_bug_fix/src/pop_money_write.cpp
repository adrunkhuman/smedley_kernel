#include "pop_money_write.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace interest_bug_fix
{
    namespace
    {
        constexpr size_t pop_money_offset = 0x180;
        constexpr size_t pop_total_cash_flow_offset = 0x218;

        bool IsWritable(const void *pointer, size_t size)
        {
            if (pointer == nullptr || size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            for (uintptr_t cursor = begin; cursor < end;) {
                MEMORY_BASIC_INFORMATION region{};
                if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)) {
                    return false;
                }
                const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                const uintptr_t region_end = region_begin + region.RegionSize;
                const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                    || (region.Protect & writable) == 0 || region_end <= cursor) return false;
                cursor = (std::min)(end, region_end);
            }
            return true;
        }
    }

    bool CanWritePopMoney(const void *pop)
    {
        if (pop == nullptr) return false;
        const uintptr_t address = reinterpret_cast<uintptr_t>(pop);
        if (address > (std::numeric_limits<uintptr_t>::max)() - pop_money_offset) return false;
        constexpr size_t span = pop_total_cash_flow_offset + sizeof(int64_t) - pop_money_offset;
        return IsWritable(reinterpret_cast<const void *>(address + pop_money_offset), span);
    }
}
