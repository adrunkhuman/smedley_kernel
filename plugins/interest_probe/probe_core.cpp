#include "probe_core.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace interest_probe
{
    namespace
    {
        constexpr size_t country_minimum_size = 0xe9c;
        constexpr size_t country_tag_offset = 0x1c;
        constexpr size_t country_states_offset = 0xe44;
        constexpr size_t country_treasury_offset = 0xe78;
        constexpr size_t country_bank_offset = 0xe88;
        constexpr size_t country_creditors_offset = 0xe8c;
        constexpr size_t state_size = 0x290;
        constexpr size_t state_provinces_offset = 0x48;
        constexpr size_t state_savings_offset = 0x258;
        constexpr size_t state_interest_offset = 0x260;
        constexpr size_t bank_interest_offset = 0x20;
        constexpr uint32_t max_states = 512;
        constexpr uint32_t max_provinces_per_state = 1024;
        constexpr uint32_t max_creditors = 4096;

        struct ListNode
        {
            const void *data;
            const ListNode *previous;
            const ListNode *next;
            uint8_t deleted;
            uint8_t padding[3];
        };

        struct PointerVector
        {
            const void *begin;
            const void *end;
            const void *capacity;
        };

        bool IsReadable(const void *pointer, size_t size)
        {
            if (pointer == nullptr || size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            uintptr_t cursor = begin;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION region{};
                if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)) return false;
                if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
                const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                    | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if ((region.Protect & readable) == 0) return false;
                const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                const uintptr_t region_end = region_begin + region.RegionSize;
                if (region_end <= cursor) return false;
                cursor = (std::min)(end, region_end);
            }
            return true;
        }

        bool CopyReadable(void *destination, const void *source, size_t size)
        {
            if (!IsReadable(source, size)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        template <typename T>
        bool ReadAt(const void *base, size_t offset, T *value)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(base);
            if (address > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
            return CopyReadable(value, reinterpret_cast<const void *>(address + offset), sizeof(T));
        }

        bool VectorCount(const PointerVector &vector, size_t element_size, uint32_t limit, uint32_t *count)
        {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(vector.begin);
            const uintptr_t end = reinterpret_cast<uintptr_t>(vector.end);
            const uintptr_t capacity = reinterpret_cast<uintptr_t>(vector.capacity);
            if (begin == 0 && end == 0 && capacity == 0) {
                *count = 0;
                return true;
            }
            if (begin == 0 || begin > end || end > capacity || (end - begin) % element_size != 0) return false;
            const uintptr_t elements = (end - begin) / element_size;
            if (elements > limit || (elements != 0 && !IsReadable(vector.begin, static_cast<size_t>(end - begin)))) return false;
            *count = static_cast<uint32_t>(elements);
            return true;
        }

        void AddChecked(int64_t value, int64_t *sum, uint32_t *flags)
        {
            if ((value > 0 && *sum > (std::numeric_limits<int64_t>::max)() - value)
                || (value < 0 && *sum < (std::numeric_limits<int64_t>::min)() - value)) {
                *flags |= SAMPLE_SUM_OVERFLOW;
                return;
            }
            *sum += value;
        }
    }

    Sample CollectSample(const void *country, int32_t date_raw)
    {
        Sample sample{};
        sample.date_raw = date_raw;
        if (!IsReadable(country, country_minimum_size)) {
            sample.flags |= SAMPLE_COUNTRY_UNREADABLE;
            return sample;
        }

        char tag[4]{};
        ReadAt(country, country_tag_offset, &tag);
        std::memcpy(sample.country_tag, tag, sizeof(tag));
        sample.country_tag[3] = '\0';
        ReadAt(country, country_treasury_offset, &sample.treasury_raw);

        const ListNode *node = nullptr;
        const ListNode *tail = nullptr;
        if (!ReadAt(country, country_states_offset, &node)
            || !ReadAt(country, country_states_offset + 4, &tail)
            || !ReadAt(country, country_states_offset + 8, &sample.state_count_reported)
            || sample.state_count_reported < 0 || sample.state_count_reported > static_cast<int32_t>(max_states)) {
            sample.flags |= SAMPLE_STATE_LIST_INVALID;
            return sample;
        }

        while (node != nullptr && sample.states_walked < max_states) {
            ListNode current{};
            if (!CopyReadable(&current, node, sizeof(current))) {
                sample.flags |= SAMPLE_STATE_LIST_INVALID;
                break;
            }
            if (current.deleted == 0 && current.data != nullptr) {
                if (!IsReadable(current.data, state_size)) {
                    sample.flags |= SAMPLE_STATE_UNREADABLE;
                } else {
                    PointerVector provinces{};
                    uint32_t province_count = 0;
                    if (!ReadAt(current.data, state_provinces_offset, &provinces)
                        || !VectorCount(provinces, sizeof(int32_t), max_provinces_per_state, &province_count)
                        || province_count > (std::numeric_limits<uint32_t>::max)() - sample.province_element_candidates) {
                        sample.flags |= SAMPLE_STATE_VECTOR_INVALID;
                    } else {
                        sample.province_element_candidates += province_count;
                    }
                    int64_t savings = 0;
                    int64_t interest = 0;
                    if (!ReadAt(current.data, state_savings_offset, &savings)
                        || !ReadAt(current.data, state_interest_offset, &interest)) {
                        sample.flags |= SAMPLE_STATE_UNREADABLE;
                    } else {
                        if (savings != 0) ++sample.states_with_savings;
                        if (interest != 0) ++sample.states_with_interest;
                        AddChecked(savings, &sample.state_savings_raw, &sample.flags);
                        AddChecked(interest, &sample.state_interest_raw, &sample.flags);
                    }
                }
            }
            ++sample.states_walked;
            if (current.next == node) {
                sample.flags |= SAMPLE_STATE_LIST_INVALID;
                break;
            }
            node = current.next;
        }
        if (node != nullptr) sample.flags |= SAMPLE_STATE_LIMIT;
        if (sample.states_walked != static_cast<uint32_t>(sample.state_count_reported)) {
            sample.flags |= SAMPLE_STATE_COUNT_MISMATCH;
        }
        if (sample.state_count_reported != 0 && tail == nullptr) sample.flags |= SAMPLE_STATE_LIST_INVALID;

        const void *bank = nullptr;
        if (!ReadAt(country, country_bank_offset, &bank) || bank == nullptr
            || !ReadAt(bank, bank_interest_offset, &sample.bank_interest_raw)) {
            sample.flags |= SAMPLE_BANK_UNREADABLE;
        }

        PointerVector creditors{};
        if (!ReadAt(country, country_creditors_offset, &creditors)
            || !VectorCount(creditors, sizeof(void *), max_creditors, &sample.creditor_count)) {
            sample.flags |= SAMPLE_CREDITOR_VECTOR_INVALID;
        }
        return sample;
    }
}
