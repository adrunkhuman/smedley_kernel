#include "probe_core.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
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
        constexpr size_t province_pop_lists_offset = 0x194;
        constexpr size_t pop_money_offset = 0x180;
        constexpr size_t pop_interest_cash_flow_offset = 0x210;
        constexpr size_t pop_total_cash_flow_offset = 0x218;
        constexpr size_t pop_savings_offset = 0x250;
        constexpr size_t pop_next_offset = 0x27c;
        constexpr size_t creditor_tag_offset = 0x08;
        constexpr size_t creditor_interest_offset = 0x10;
        constexpr size_t creditor_debt_offset = 0x18;
        constexpr size_t creditor_was_paid_offset = 0x20;
        constexpr uint32_t max_states = 512;
        constexpr uint32_t max_provinces_per_state = 1024;
        constexpr uint32_t max_creditors = 4096;
        constexpr uint32_t max_creditor_destinations = 64;
        constexpr uint32_t max_destination_provinces = 4096;
        constexpr uint32_t max_pop_lists_per_province = 128;
        constexpr uint32_t max_pops = 100000;
        constexpr int64_t pop_savings_state_scale = 1000;

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

        struct PopList
        {
            const void *first;
            const void *last;
            int32_t count;
            uint32_t unknown;
        };

        struct TraversalScratch
        {
            std::array<int32_t, max_destination_provinces> province_ids{};
            std::array<uintptr_t, max_pops> pop_pointers{};
            uint32_t province_attempts = 0;
            uint32_t province_id_count = 0;
            uint32_t pop_attempts = 0;
            uint32_t pop_pointer_count = 0;
        };

        // The event is synchronous on the game thread; static storage keeps the
        // bounded identity set off that thread's stack without hot-path allocation.
        TraversalScratch traversal_scratch;

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

        bool IsTagKey(uint32_t key)
        {
            const auto *bytes = reinterpret_cast<const uint8_t *>(&key);
            if (bytes[3] != 0) return false;
            for (size_t index = 0; index < 3; ++index) {
                const uint8_t value = bytes[index];
                if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) return false;
            }
            return true;
        }

        bool ReadPopMoney(const void *pop, PopMoneySnapshot *snapshot)
        {
            return snapshot != nullptr
                && ReadAt(pop, pop_money_offset, &snapshot->money_raw)
                && ReadAt(pop, pop_interest_cash_flow_offset, &snapshot->interest_cash_flow_raw)
                && ReadAt(pop, pop_total_cash_flow_offset, &snapshot->total_cash_flow_raw)
                && ReadAt(pop, pop_savings_offset, &snapshot->savings_raw);
        }

        void CollectPops(const PointerVector &provinces, ProvinceResolver resolver,
                         const void *resolver_context, TraversalScratch *scratch,
                         const void **immediate_pop, Sample *sample)
        {
            uint32_t province_count = 0;
            if (!VectorCount(provinces, sizeof(int32_t), max_provinces_per_state, &province_count)) {
                sample->flags |= SAMPLE_STATE_VECTOR_INVALID;
                return;
            }
            for (uint32_t province_index = 0; province_index < province_count; ++province_index) {
                if (scratch->province_attempts >= max_destination_provinces) {
                    sample->flags |= SAMPLE_POP_LIMIT;
                    return;
                }
                ++scratch->province_attempts;
                int32_t province_id = -1;
                if (!ReadAt(provinces.begin, province_index * sizeof(province_id), &province_id)) {
                    sample->flags |= SAMPLE_PROVINCE_INVALID;
                    continue;
                }
                scratch->province_ids[scratch->province_id_count++] = province_id;
                const void *province = resolver(resolver_context, province_id);
                PointerVector pop_lists{};
                uint32_t pop_list_count = 0;
                if (!ReadAt(province, province_pop_lists_offset, &pop_lists)
                    || !VectorCount(pop_lists, sizeof(PopList), max_pop_lists_per_province, &pop_list_count)) {
                    sample->flags |= SAMPLE_POP_VECTOR_INVALID;
                    continue;
                }
                ++sample->destination_provinces_resolved;
                sample->destination_pop_lists += pop_list_count;
                for (uint32_t list_index = 0; list_index < pop_list_count; ++list_index) {
                    PopList list{};
                    if (!ReadAt(pop_lists.begin, list_index * sizeof(PopList), &list) || list.count < 0) {
                        sample->flags |= SAMPLE_POP_LIST_INVALID;
                        continue;
                    }
                    if (static_cast<uint32_t>(list.count) > max_pops) {
                        sample->flags |= SAMPLE_POP_LIMIT;
                        return;
                    }
                    if (list.count == 0) {
                        if (list.first != nullptr || list.last != nullptr) sample->flags |= SAMPLE_POP_LIST_INVALID;
                        continue;
                    }
                    if (list.first == nullptr || list.last == nullptr) {
                        sample->flags |= SAMPLE_POP_LIST_INVALID;
                        continue;
                    }
                    const void *pop = list.first;
                    const void *last = nullptr;
                    uint32_t walked = 0;
                    while (pop != nullptr && walked < static_cast<uint32_t>(list.count)) {
                        if (scratch->pop_attempts >= max_pops) {
                            sample->flags |= SAMPLE_POP_LIMIT;
                            return;
                        }
                        ++scratch->pop_attempts;
                        scratch->pop_pointers[scratch->pop_pointer_count++] = reinterpret_cast<uintptr_t>(pop);
                        int64_t savings = 0;
                        const void *next = nullptr;
                        if (!ReadAt(pop, pop_savings_offset, &savings) || !ReadAt(pop, pop_next_offset, &next)) {
                            sample->flags |= SAMPLE_POP_UNREADABLE;
                            break;
                        }
                        AddChecked(savings, &sample->destination_pop_savings_raw, &sample->flags);
                        AddChecked(savings / pop_savings_state_scale,
                            &sample->destination_pop_savings_state_scale_raw, &sample->flags);
                        if (immediate_pop != nullptr && *immediate_pop == nullptr) {
                            PopMoneySnapshot snapshot{};
                            if (!ReadPopMoney(pop, &snapshot)) sample->flags |= SAMPLE_POP_UNREADABLE;
                            else *immediate_pop = pop;
                        }
                        ++sample->destination_pops;
                        ++walked;
                        last = pop;
                        if (next == pop) {
                            sample->flags |= SAMPLE_POP_LIST_INVALID;
                            break;
                        }
                        pop = next;
                    }
                    if (walked != static_cast<uint32_t>(list.count) || pop != nullptr || last != list.last) {
                        sample->flags |= SAMPLE_POP_LIST_INVALID;
                    }
                }
            }
        }
    }

    Sample CollectSampleImpl(const void *country, int32_t date_raw,
                             CountryResolver country_resolver, ProvinceResolver province_resolver,
                             const void *resolver_context, bool collect_pops, TraversalScratch *scratch,
                             const void **immediate_pop)
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
                        if (collect_pops && province_resolver != nullptr) {
                            CollectPops(provinces, province_resolver, resolver_context,
                                scratch, immediate_pop, &sample);
                        }
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
            return sample;
        }
        if (country_resolver == nullptr || sample.creditor_count == 0) return sample;
        if (sample.creditor_count > max_creditor_destinations) sample.flags |= SAMPLE_CREDITOR_DESTINATION_LIMIT;

        std::array<int32_t, max_creditor_destinations> destination_ordinals{};
        const uint32_t creditor_limit = (std::min)(sample.creditor_count, max_creditor_destinations);
        for (uint32_t index = 0; index < creditor_limit; ++index) {
            const void *creditor = nullptr;
            if (!ReadAt(creditors.begin, index * sizeof(void *), &creditor) || creditor == nullptr) {
                sample.flags |= SAMPLE_CREDITOR_UNREADABLE;
                continue;
            }
            uint32_t key = 0;
            int32_t ordinal = -1;
            int64_t interest = 0;
            int64_t debt = 0;
            uint8_t was_paid = 0;
            if (!ReadAt(creditor, creditor_tag_offset, &key)
                || !ReadAt(creditor, creditor_tag_offset + sizeof(key), &ordinal)
                || !ReadAt(creditor, creditor_interest_offset, &interest)
                || !ReadAt(creditor, creditor_debt_offset, &debt)
                || !ReadAt(creditor, creditor_was_paid_offset, &was_paid)) {
                sample.flags |= SAMPLE_CREDITOR_UNREADABLE;
                continue;
            }
            if (!IsTagKey(key) || ordinal < 0 || was_paid > 1) {
                sample.flags |= SAMPLE_CREDITOR_TAG_INVALID;
                continue;
            }
            AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
            AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
            if (was_paid != 0) ++sample.creditors_was_paid;

            bool duplicate = false;
            for (uint32_t prior = 0; prior < sample.creditor_destinations; ++prior) {
                if (destination_ordinals[prior] == ordinal) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                sample.flags |= SAMPLE_CREDITOR_DUPLICATE_DESTINATION;
                continue;
            }
            const void *destination = country_resolver(resolver_context, ordinal);
            uint32_t destination_key = 0;
            int32_t destination_ordinal = -1;
            if (!ReadAt(destination, country_tag_offset, &destination_key)
                || !ReadAt(destination, country_tag_offset + sizeof(destination_key), &destination_ordinal)
                || destination_key != key || destination_ordinal != ordinal) {
                sample.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
                continue;
            }
            const Sample destination_sample = CollectSampleImpl(
                destination, date_raw, nullptr, province_resolver, resolver_context, true, scratch, immediate_pop);
            sample.destination_provinces_resolved += destination_sample.destination_provinces_resolved;
            sample.destination_pop_lists += destination_sample.destination_pop_lists;
            sample.destination_pops += destination_sample.destination_pops;
            if (destination_sample.flags != 0) {
                sample.flags |= destination_sample.flags | SAMPLE_CREDITOR_DESTINATION_INVALID;
                if ((destination_sample.flags & SAMPLE_POP_LIMIT) != 0) break;
                continue;
            }
            destination_ordinals[sample.creditor_destinations++] = ordinal;
            AddChecked(destination_sample.bank_interest_raw, &sample.destination_bank_interest_raw, &sample.flags);
            AddChecked(destination_sample.state_savings_raw, &sample.destination_state_savings_raw, &sample.flags);
            AddChecked(destination_sample.state_interest_raw, &sample.destination_state_interest_raw, &sample.flags);
            AddChecked(destination_sample.destination_pop_savings_raw,
                &sample.destination_pop_savings_raw, &sample.flags);
            AddChecked(destination_sample.destination_pop_savings_state_scale_raw,
                &sample.destination_pop_savings_state_scale_raw, &sample.flags);
        }
        return sample;
    }

    Sample CollectSample(const void *country, int32_t date_raw,
                         CountryResolver country_resolver, ProvinceResolver province_resolver,
                         const void *resolver_context, const void **immediate_pop)
    {
        if (immediate_pop != nullptr) *immediate_pop = nullptr;
        traversal_scratch.province_attempts = 0;
        traversal_scratch.province_id_count = 0;
        traversal_scratch.pop_attempts = 0;
        traversal_scratch.pop_pointer_count = 0;
        Sample sample = CollectSampleImpl(
            country, date_raw, country_resolver, province_resolver, resolver_context,
            false, &traversal_scratch, immediate_pop);
        sample.destination_province_attempts = traversal_scratch.province_attempts;
        sample.destination_pop_attempts = traversal_scratch.pop_attempts;

        if ((sample.flags & SAMPLE_POP_LIMIT) == 0) {
            auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
            std::sort(traversal_scratch.province_ids.begin(), province_end);
            if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
                sample.flags |= SAMPLE_DUPLICATE_PROVINCE;
            }
            auto pop_end = traversal_scratch.pop_pointers.begin() + traversal_scratch.pop_pointer_count;
            std::sort(traversal_scratch.pop_pointers.begin(), pop_end);
            if (std::adjacent_find(traversal_scratch.pop_pointers.begin(), pop_end) != pop_end) {
                sample.flags |= SAMPLE_DUPLICATE_POP;
            }
        }
        return sample;
    }

    bool ReadPopMoneySnapshot(const void *pop, PopMoneySnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        PopMoneySnapshot value{};
        if (!ReadPopMoney(pop, &value)) return false;
        *snapshot = value;
        return true;
    }
}
