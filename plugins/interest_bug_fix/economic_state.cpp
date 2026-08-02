#include "economic_state.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace interest_bug_fix
{
    namespace
    {
        constexpr size_t country_minimum_size = 0xe9c;
        constexpr size_t country_tag_offset = 0x1c;
        constexpr size_t country_states_offset = 0xe44;
        constexpr size_t game_state_world_market_offset = 0xbcc;
        constexpr size_t country_treasury_offset = 0xe78;
        constexpr size_t country_bank_offset = 0xe88;
        constexpr size_t country_creditors_offset = 0xe8c;
        constexpr size_t state_size = 0x290;
        constexpr size_t state_id_offset = 0x0c;
        constexpr size_t state_provinces_offset = 0x48;
        constexpr size_t state_factories_offset = 0x60;
        constexpr size_t state_region_offset = 0x250;
        constexpr size_t region_key_offset = 0x18;
        constexpr size_t state_savings_offset = 0x258;
        constexpr size_t state_interest_offset = 0x260;
        constexpr size_t bank_interest_offset = 0x20;
        constexpr size_t province_pop_lists_offset = 0x194;
        constexpr size_t province_id_offset = 0x58;
        constexpr size_t pop_size_offset = 0x58;
        constexpr size_t pop_employed_offset = 0x60;
        constexpr size_t pop_province_offset = 0x64;
        constexpr size_t pop_type_offset = 0x68;
        constexpr size_t pop_type_id_offset = 0x28;
        constexpr size_t pop_consciousness_offset = 0x118;
        constexpr size_t pop_militancy_offset = 0x120;
        constexpr size_t pop_literacy_offset = 0x128;
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
        constexpr uint32_t max_creditor_destinations = max_sample_creditor_destinations;
        constexpr uint32_t max_destination_provinces = max_sample_destination_provinces;
        constexpr uint32_t max_pop_lists_per_province = 128;
        constexpr uint32_t max_pops = max_sample_pops;
        constexpr uint32_t max_factories_per_state = 64;
        constexpr size_t state_building_size = 0x220;
        constexpr size_t state_building_definition_offset = 0x18;
        constexpr size_t state_building_level_offset = 0x20;
        constexpr size_t state_building_stockpile_index_offset = 0x30;
        constexpr size_t state_building_stockpile_values_offset = 0x70;
        constexpr size_t state_building_output_offset = 0xd8;
        constexpr size_t state_building_employment_offset = 0xf0;
        constexpr size_t state_building_employees_offset = 0x128;
        constexpr size_t state_building_budget_offset = 0x150;
        constexpr size_t state_building_market_spending_offset = 0x158;
        constexpr size_t state_building_sales_income_offset = 0x160;
        constexpr size_t state_building_paychecks_offset = 0x168;
        constexpr size_t state_building_investment_offset = 0x170;
        constexpr size_t state_building_subsidized_offset = 0x180;
        constexpr size_t state_building_closed_offset = 0x188;
        constexpr size_t building_definition_key_offset = 0x20;
        constexpr size_t market_supply_offset = 0x08;
        constexpr size_t market_last_supply_offset = 0x60;
        constexpr size_t market_stock_offset = 0x120;
        constexpr size_t market_demand_offset = 0x178;
        constexpr size_t market_real_demand_offset = 0x1d0;
        constexpr size_t market_price_offset = 0x280;
        constexpr size_t market_last_price_offset = 0x2d8;
        constexpr size_t market_actual_sold_offset = 0x434;
        constexpr size_t market_actual_sold_world_offset = 0x4f4;
        constexpr size_t building_definition_production_type_offset = 0x12c;
        constexpr size_t production_type_output_good_offset = 0x80;
        constexpr size_t production_type_base_output_offset = 0x88;
        constexpr size_t goods_ordinal_offset = 0x08;
        constexpr size_t goods_key_offset = 0x0c;
        constexpr size_t pop_type_key_offset = 0x08;
        constexpr size_t pop_employment_size = 0x10;
        constexpr size_t pop_employment_pop_offset = 0x08;
        constexpr size_t pop_employment_count_offset = 0x0c;
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

        struct StateBuildingNode
        {
            std::array<uint8_t, state_building_size> data;
            const StateBuildingNode *previous;
            const StateBuildingNode *next;
            uint8_t deleted;
            uint8_t padding[3];
        };

        struct GameString
        {
            union
            {
                char inline_value[16];
                const char *pointer;
            } value;
            uint32_t size;
            uint32_t capacity;
            uint32_t allocator;
        };

        struct TraversalScratch
        {
            std::array<int32_t, max_destination_provinces> province_ids{};
            std::array<uintptr_t, max_pops> pop_pointers{};
            std::array<uintptr_t, max_pops> pop_identity_pointers{};
            std::array<int64_t, max_pops> pop_savings{};
            uint32_t province_attempts = 0;
            uint32_t province_id_count = 0;
            uint32_t pop_attempts = 0;
            uint32_t pop_pointer_count = 0;
        };

        // The event runs synchronously on the game thread. Static storage keeps
        // this bounded identity set off the thread's stack without allocating
        // on the hot path.
        TraversalScratch traversal_scratch;

        struct MemoryRegionCache
        {
            uintptr_t begin = 0;
            uintptr_t end = 0;
            DWORD protect = 0;
            DWORD state = 0;
        };

        MemoryRegionCache memory_region_cache;

        void ResetMemoryRegionCache()
        {
            memory_region_cache = {};
        }

        bool IsAccessible(const void *pointer, size_t size, bool require_writable)
        {
            if (pointer == nullptr || size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            uintptr_t cursor = begin;
            while (cursor < end) {
                if (cursor < memory_region_cache.begin || cursor >= memory_region_cache.end) {
                    MEMORY_BASIC_INFORMATION region{};
                    if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)) {
                        return false;
                    }
                    const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                    if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                    memory_region_cache.begin = region_begin;
                    memory_region_cache.end = region_begin + region.RegionSize;
                    memory_region_cache.protect = region.Protect;
                    memory_region_cache.state = region.State;
                }
                if (memory_region_cache.state != MEM_COMMIT
                    || (memory_region_cache.protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const DWORD allowed = require_writable
                    ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
                    : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if ((memory_region_cache.protect & allowed) == 0 || memory_region_cache.end <= cursor) return false;
                cursor = (std::min)(end, memory_region_cache.end);
            }
            return true;
        }

        bool IsReadable(const void *pointer, size_t size)
        {
            return IsAccessible(pointer, size, false);
        }

        bool IsWritable(const void *pointer, size_t size)
        {
            return IsAccessible(pointer, size, true);
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

        bool ReadBoundedString(const void *object, size_t key_offset, char *destination, size_t destination_size)
        {
            if (object == nullptr || destination == nullptr || destination_size == 0) return false;
            GameString key{};
            if (!ReadAt(object, key_offset, &key)
                || key.size == 0 || key.size >= destination_size || key.capacity < key.size) return false;
            const char *source = key.capacity <= 15 ? key.value.inline_value : key.value.pointer;
            if (source == nullptr || !CopyReadable(destination, source, key.size + 1)
                || destination[key.size] != '\0') return false;
            for (uint32_t index = 0; index < key.size; ++index) {
                const unsigned char value = static_cast<unsigned char>(destination[index]);
                if (value < 0x20 || value > 0x7e) return false;
            }
            return true;
        }

        bool ReadNormalizedKey(const void *object, size_t key_offset, char *destination, size_t destination_size)
        {
            if (!ReadBoundedString(object, key_offset, destination, destination_size)) return false;
            for (const char *value = destination; *value != '\0'; ++value) {
                if (!((*value >= 'a' && *value <= 'z') || (*value >= '0' && *value <= '9') || *value == '_')) return false;
            }
            return true;
        }

        bool ReadGoodsPool(const void *pool, std::array<int64_t, 64> *values,
                           std::array<bool, 64> *present)
        {
            if (pool == nullptr || values == nullptr || present == nullptr) return false;
            values->fill(0);
            present->fill(false);
            std::array<uint8_t, 64> indices{};
            PointerVector stored_values{};
            uint32_t stored_count = 0;
            if (!ReadAt(pool, 0x08, &indices)
                || !ReadAt(pool, 0x48, &stored_values)
                || !VectorCount(stored_values, sizeof(int64_t), 65, &stored_count)) return false;
            int64_t sentinel = 0;
            if (stored_count != 0 && (!ReadAt(stored_values.begin, 0, &sentinel) || sentinel != 0)) return false;
            std::array<bool, 65> seen{};
            for (uint32_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
                const uint8_t index = indices[ordinal];
                if (index == 0) continue;
                if (index >= stored_count || seen[index]) return false;
                seen[index] = true;
                if (!ReadAt(stored_values.begin, index * sizeof(int64_t), &(*values)[ordinal])) return false;
                (*present)[ordinal] = true;
            }
            return true;
        }

        bool ReadPopMoney(const void *pop, PopMoneySnapshot *snapshot)
        {
            if (snapshot == nullptr) return false;
            constexpr size_t span = pop_savings_offset + sizeof(int64_t) - pop_money_offset;
            std::array<uint8_t, span> bytes{};
            const auto *begin = reinterpret_cast<const uint8_t *>(pop) + pop_money_offset;
            if (!CopyReadable(bytes.data(), begin, bytes.size())) return false;
            std::memcpy(&snapshot->money_raw, bytes.data(), sizeof(snapshot->money_raw));
            std::memcpy(&snapshot->interest_cash_flow_raw,
                bytes.data() + pop_interest_cash_flow_offset - pop_money_offset,
                sizeof(snapshot->interest_cash_flow_raw));
            std::memcpy(&snapshot->total_cash_flow_raw,
                bytes.data() + pop_total_cash_flow_offset - pop_money_offset,
                sizeof(snapshot->total_cash_flow_raw));
            std::memcpy(&snapshot->savings_raw,
                bytes.data() + pop_savings_offset - pop_money_offset,
                sizeof(snapshot->savings_raw));
            return true;
        }

        void CollectPops(const PointerVector &provinces, ProvinceResolver resolver,
                          const void *resolver_context, TraversalScratch *scratch,
                          const void **immediate_pop, uint32_t province_limit,
                          uint32_t pop_limit, Sample *sample)
        {
            uint32_t province_count = 0;
            if (!VectorCount(provinces, sizeof(int32_t), max_provinces_per_state, &province_count)) {
                sample->flags |= SAMPLE_STATE_VECTOR_INVALID;
                return;
            }
            for (uint32_t province_index = 0; province_index < province_count; ++province_index) {
                if (scratch->province_attempts >= province_limit) {
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
                        if (scratch->pop_attempts >= pop_limit) {
                            sample->flags |= SAMPLE_POP_LIMIT;
                            return;
                        }
                        ++scratch->pop_attempts;
                        constexpr size_t pop_link_span = pop_next_offset + sizeof(void *) - pop_savings_offset;
                        std::array<uint8_t, pop_link_span> pop_fields{};
                        const auto *pop_field_begin = reinterpret_cast<const uint8_t *>(pop) + pop_savings_offset;
                        if (!CopyReadable(pop_fields.data(), pop_field_begin, pop_fields.size())) {
                            sample->flags |= SAMPLE_POP_UNREADABLE;
                            break;
                        }
                        int64_t savings = 0;
                        const void *next = nullptr;
                        std::memcpy(&savings, pop_fields.data(), sizeof(savings));
                        std::memcpy(&next, pop_fields.data() + pop_next_offset - pop_savings_offset, sizeof(next));
                        scratch->pop_pointers[scratch->pop_pointer_count] = reinterpret_cast<uintptr_t>(pop);
                        scratch->pop_identity_pointers[scratch->pop_pointer_count] = reinterpret_cast<uintptr_t>(pop);
                        scratch->pop_savings[scratch->pop_pointer_count] = savings;
                        ++scratch->pop_pointer_count;
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
                              const void *resolver_context, bool collect_states, bool collect_pops, TraversalScratch *scratch,
                              const void **immediate_pop, uint32_t province_limit, uint32_t pop_limit,
                              bool collect_creditors)
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
        ReadAt(country, country_tag_offset + sizeof(uint32_t), &sample.country_ordinal);
        ReadAt(country, country_treasury_offset, &sample.treasury_raw);

        if (collect_states) {
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
                                    scratch, immediate_pop, province_limit, pop_limit, &sample);
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
            if (sample.state_count_reported != 0 && tail == nullptr) {
                sample.flags |= SAMPLE_STATE_LIST_INVALID;
            }
        }

        const void *bank = nullptr;
        if (!ReadAt(country, country_bank_offset, &bank) || bank == nullptr
            || !ReadAt(bank, bank_interest_offset, &sample.bank_interest_raw)) {
            sample.flags |= SAMPLE_BANK_UNREADABLE;
        }

        if (!collect_creditors) return sample;

        PointerVector creditors{};
        if (!ReadAt(country, country_creditors_offset, &creditors)
            || !VectorCount(creditors, sizeof(void *), max_creditors, &sample.creditor_count)) {
            sample.flags |= SAMPLE_CREDITOR_VECTOR_INVALID;
            return sample;
        }
        if (sample.creditor_count == 0) return sample;
        if (country_resolver != nullptr && sample.creditor_count > max_creditor_destinations) {
            sample.flags |= SAMPLE_CREDITOR_DESTINATION_LIMIT;
        }

        const uint32_t creditor_limit = country_resolver == nullptr
            ? sample.creditor_count : (std::min)(sample.creditor_count, max_creditor_destinations);
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
            if (country_resolver == nullptr) {
                if (was_paid > 1) sample.flags |= SAMPLE_CREDITOR_TAG_INVALID;
                AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
                AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
                if (was_paid != 0) ++sample.creditors_was_paid;
                continue;
            }
            if (ordinal == 0 && was_paid <= 1) {
                AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
                AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
                if (was_paid != 0) ++sample.creditors_was_paid;
                continue;
            }
            if (!IsTagKey(key) || ordinal < 0 || was_paid > 1) {
                sample.flags |= SAMPLE_CREDITOR_TAG_INVALID;
                sample.invalid_creditor_key = key;
                sample.invalid_creditor_ordinal = ordinal;
                sample.invalid_creditor_was_paid = was_paid;
                continue;
            }
            AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
            AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
            if (was_paid != 0) ++sample.creditors_was_paid;

            bool duplicate = false;
            for (uint32_t prior = 0; prior < sample.creditor_destinations; ++prior) {
                if (sample.destination_ordinals[prior] == ordinal) {
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
                destination, date_raw, nullptr, province_resolver, resolver_context, collect_states, collect_pops, scratch,
                immediate_pop, province_limit, pop_limit, false);
            sample.destination_provinces_resolved += destination_sample.destination_provinces_resolved;
            sample.destination_pop_lists += destination_sample.destination_pop_lists;
            sample.destination_pops += destination_sample.destination_pops;
            if (destination_sample.flags != 0) {
                sample.flags |= destination_sample.flags | SAMPLE_CREDITOR_DESTINATION_INVALID;
                if ((destination_sample.flags & SAMPLE_POP_LIMIT) != 0) break;
                continue;
            }
            sample.destination_keys[sample.creditor_destinations] = key;
            sample.destination_ordinals[sample.creditor_destinations] = ordinal;
            sample.destination_bank_interests_raw[sample.creditor_destinations] = destination_sample.bank_interest_raw;
            ++sample.creditor_destinations;
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
        ResetMemoryRegionCache();
        if (immediate_pop != nullptr) *immediate_pop = nullptr;
        traversal_scratch.province_attempts = 0;
        traversal_scratch.province_id_count = 0;
        traversal_scratch.pop_attempts = 0;
        traversal_scratch.pop_pointer_count = 0;
        Sample sample = CollectSampleImpl(
            country, date_raw, country_resolver, province_resolver, resolver_context,
            true, province_resolver != nullptr, &traversal_scratch, immediate_pop,
            max_destination_provinces, max_pops, true);
        sample.destination_province_attempts = traversal_scratch.province_attempts;
        sample.destination_pop_attempts = traversal_scratch.pop_attempts;

        if ((sample.flags & SAMPLE_POP_LIMIT) == 0) {
            auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
            std::sort(traversal_scratch.province_ids.begin(), province_end);
            if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
                sample.flags |= SAMPLE_DUPLICATE_PROVINCE;
            }
            auto pop_end = traversal_scratch.pop_identity_pointers.begin() + traversal_scratch.pop_pointer_count;
            std::sort(traversal_scratch.pop_identity_pointers.begin(), pop_end);
            if (std::adjacent_find(traversal_scratch.pop_identity_pointers.begin(), pop_end) != pop_end) {
                sample.flags |= SAMPLE_DUPLICATE_POP;
            }
        }
        return sample;
    }

    Sample CollectInterestSample(const void *country, int32_t date_raw,
                                 CountryResolver country_resolver, const void *resolver_context)
    {
        ResetMemoryRegionCache();
        return CollectSampleImpl(country, date_raw, country_resolver, nullptr, resolver_context,
            false, false, &traversal_scratch, nullptr, 0, 0, true);
    }

    Sample CollectInterestAfter(const Sample &before, const void *country, int32_t date_raw,
                                CountryResolver country_resolver, const void *resolver_context)
    {
        ResetMemoryRegionCache();
        Sample after = CollectSampleImpl(country, date_raw, nullptr, nullptr, resolver_context,
            false, false, &traversal_scratch, nullptr, 0, 0, false);
        after.creditor_count = before.creditor_count;
        if (country_resolver == nullptr || before.creditor_destinations > max_creditor_destinations) {
            after.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
            return after;
        }
        for (uint32_t index = 0; index < before.creditor_destinations; ++index) {
            const int32_t ordinal = before.destination_ordinals[index];
            const uint32_t key = before.destination_keys[index];
            const void *destination = country_resolver(resolver_context, ordinal);
            uint32_t destination_key = 0;
            int32_t destination_ordinal = -1;
            const void *bank = nullptr;
            int64_t bank_interest = 0;
            if (!ReadAt(destination, country_tag_offset, &destination_key)
                || !ReadAt(destination, country_tag_offset + sizeof(destination_key), &destination_ordinal)
                || destination_key != key || destination_ordinal != ordinal
                || !ReadAt(destination, country_bank_offset, &bank) || bank == nullptr
                || !ReadAt(bank, bank_interest_offset, &bank_interest)) {
                after.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
                continue;
            }
            after.destination_keys[after.creditor_destinations] = key;
            after.destination_ordinals[after.creditor_destinations] = ordinal;
            after.destination_bank_interests_raw[after.creditor_destinations] = bank_interest;
            ++after.creditor_destinations;
            AddChecked(bank_interest, &after.destination_bank_interest_raw, &after.flags);
        }
        return after;
    }

    bool ComputeDestinationTransfers(const Sample &before, Sample *after)
    {
        if (after == nullptr || before.flags != 0 || after->flags != 0
            || before.creditor_destinations != after->creditor_destinations
            || before.creditor_destinations > max_sample_creditor_destinations) {
            if (after != nullptr) after->flags |= SAMPLE_DESTINATION_TRANSFER_INVALID;
            return false;
        }
        after->destination_transfers_raw.fill(0);
        after->destination_transfer_count = 0;
        after->destination_transfer_raw = 0;
        for (uint32_t index = 0; index < before.creditor_destinations; ++index) {
            if (before.destination_ordinals[index] != after->destination_ordinals[index]
                || before.destination_keys[index] != after->destination_keys[index]) {
                after->flags |= SAMPLE_DESTINATION_TRANSFER_INVALID;
                return false;
            }
            const int64_t before_value = before.destination_bank_interests_raw[index];
            const int64_t after_value = after->destination_bank_interests_raw[index];
            if (before_value < 0 || after_value < before_value) {
                after->flags |= SAMPLE_DESTINATION_TRANSFER_INVALID;
                return false;
            }
            const int64_t transfer = after_value - before_value;
            after->destination_transfers_raw[index] = transfer;
            if (transfer == 0) continue;
            AddChecked(transfer, &after->destination_transfer_raw, &after->flags);
            ++after->destination_transfer_count;
        }
        if (before.destination_bank_interest_raw < 0
            || after->destination_bank_interest_raw < before.destination_bank_interest_raw) {
            after->flags |= SAMPLE_DESTINATION_TRANSFER_INVALID;
            return false;
        }
        const int64_t aggregate_delta = after->destination_bank_interest_raw - before.destination_bank_interest_raw;
        if ((after->flags & SAMPLE_SUM_OVERFLOW) != 0 || aggregate_delta != after->destination_transfer_raw) {
            after->flags |= SAMPLE_DESTINATION_TRANSFER_INVALID;
            return false;
        }
        return true;
    }

    bool TreasuryLossCoversTransfer(int64_t before_treasury, int64_t after_treasury, int64_t transfer)
    {
        return transfer >= 0
            && before_treasury >= (std::numeric_limits<int64_t>::min)() + transfer
            && after_treasury <= before_treasury - transfer;
    }

    bool ComputeTreasuryResidual(int64_t before_treasury, int64_t after_treasury,
                                 int64_t transfer, int64_t *residual)
    {
        if (residual == nullptr || !TreasuryLossCoversTransfer(before_treasury, after_treasury, transfer)) {
            return false;
        }
        const int64_t after_transfer = before_treasury - transfer;
        if (after_treasury < 0
            && after_transfer > (std::numeric_limits<int64_t>::max)() + after_treasury) {
            return false;
        }
        *residual = after_transfer - after_treasury;
        return true;
    }

    bool CollectCountryPops(const void *country, int32_t date_raw,
                            ProvinceResolver province_resolver, const void *resolver_context,
                            PopCandidate *candidates, size_t candidate_capacity,
                            uint32_t province_attempt_capacity, uint32_t *candidate_count,
                            Sample *quality)
    {
        if (candidate_count == nullptr || quality == nullptr
            || (candidate_capacity != 0 && candidates == nullptr)
            || province_resolver == nullptr) {
            return false;
        }
        *candidate_count = 0;
        traversal_scratch.province_attempts = 0;
        traversal_scratch.province_id_count = 0;
        traversal_scratch.pop_attempts = 0;
        traversal_scratch.pop_pointer_count = 0;
        const uint32_t province_limit = (std::min)(province_attempt_capacity, max_destination_provinces);
        const uint32_t pop_limit = static_cast<uint32_t>((std::min)(candidate_capacity,
            static_cast<size_t>(max_pops)));
        Sample sample = CollectSampleImpl(country, date_raw, nullptr, province_resolver,
            resolver_context, true, true, &traversal_scratch, nullptr, province_limit, pop_limit, false);
        sample.destination_province_attempts = traversal_scratch.province_attempts;
        sample.destination_pop_attempts = traversal_scratch.pop_attempts;
        if ((sample.flags & SAMPLE_POP_LIMIT) == 0) {
            auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
            std::sort(traversal_scratch.province_ids.begin(), province_end);
            if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
                sample.flags |= SAMPLE_DUPLICATE_PROVINCE;
            }
            auto pop_end = traversal_scratch.pop_identity_pointers.begin() + traversal_scratch.pop_pointer_count;
            std::sort(traversal_scratch.pop_identity_pointers.begin(), pop_end);
            if (std::adjacent_find(traversal_scratch.pop_identity_pointers.begin(), pop_end) != pop_end) {
                sample.flags |= SAMPLE_DUPLICATE_POP;
            }
        }
        if (traversal_scratch.pop_pointer_count > candidate_capacity) sample.flags |= SAMPLE_POP_LIMIT;
        *quality = sample;
        if (sample.flags != 0) return false;
        for (uint32_t index = 0; index < traversal_scratch.pop_pointer_count; ++index) {
            candidates[index].address = reinterpret_cast<const void *>(traversal_scratch.pop_pointers[index]);
            candidates[index].savings_raw = traversal_scratch.pop_savings[index];
        }
        *candidate_count = traversal_scratch.pop_pointer_count;
        return true;
    }

    bool ReadPopMoneySnapshot(const void *pop, PopMoneySnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        PopMoneySnapshot value{};
        if (!ReadPopMoney(pop, &value)) return false;
        *snapshot = value;
        return true;
    }

    bool ReadPopDetailSnapshot(const void *pop, PopDetailSnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        PopDetailSnapshot value{};
        const void *province = nullptr;
        const void *pop_type = nullptr;
        if (!ReadAt(pop, pop_size_offset, &value.size_candidate)
            || !ReadAt(pop, pop_employed_offset, &value.employed_candidate)
            || !ReadAt(pop, pop_province_offset, &province)
            || !ReadAt(pop, pop_type_offset, &pop_type)
            || !ReadAt(province, province_id_offset, &value.province_id_candidate)
            || !ReadAt(pop_type, pop_type_id_offset, &value.pop_type_id_candidate)
            || !ReadAt(pop, pop_consciousness_offset, &value.consciousness_candidate_raw)
            || !ReadAt(pop, pop_militancy_offset, &value.militancy_candidate_raw)
            || !ReadAt(pop, pop_literacy_offset, &value.literacy_candidate_raw)
            || !ReadPopMoney(pop, &value.economy)) {
            return false;
        }

        if (value.province_id_candidate < 0 || value.pop_type_id_candidate < 0
            || value.pop_type_id_candidate > 127 || value.size_candidate < 0
            || value.employed_candidate < 0 || value.employed_candidate > value.size_candidate) {
            return false;
        }
        *snapshot = value;
        return true;
    }

    bool CollectCountryFactories(const void *country, FactorySnapshot *snapshots,
                                 size_t snapshot_capacity, uint32_t *snapshot_count,
                                 FactoryInputSnapshot *inputs, size_t input_capacity,
                                 uint32_t *input_count, uint32_t groups, uint32_t *flags)
    {
        if (snapshots == nullptr || snapshot_count == nullptr || inputs == nullptr
            || input_count == nullptr || flags == nullptr) return false;
        *snapshot_count = 0;
        *input_count = 0;
        *flags = 0;
        ResetMemoryRegionCache();
        if (!IsReadable(country, country_states_offset + 12)) {
            *flags = FACTORY_COUNTRY_UNREADABLE;
            return false;
        }

        const ListNode *state_node = nullptr;
        const ListNode *state_tail = nullptr;
        int32_t state_count = 0;
        if (!ReadAt(country, country_states_offset, &state_node)
            || !ReadAt(country, country_states_offset + 4, &state_tail)
            || !ReadAt(country, country_states_offset + 8, &state_count)
            || state_count < 0 || state_count > static_cast<int32_t>(max_states)
            || ((state_count == 0) != (state_node == nullptr && state_tail == nullptr))) {
            *flags = FACTORY_STATE_LIST_INVALID;
            return false;
        }

        const ListNode *previous_state_node = nullptr;
        uint32_t states_walked = 0;
        while (state_node != nullptr && states_walked < max_states) {
            ListNode current_state{};
            if (!CopyReadable(&current_state, state_node, sizeof(current_state))
                || current_state.previous != previous_state_node) {
                *flags |= FACTORY_STATE_LIST_INVALID;
                break;
            }
            if (current_state.deleted == 0 && current_state.data != nullptr) {
                if (!IsReadable(current_state.data, state_size)) {
                    *flags |= FACTORY_STATE_UNREADABLE;
                    break;
                }
                int32_t anchor_province_id = -1;
                int32_t state_id = -1;
                char state_region_key[64]{};
                if (!ReadAt(current_state.data, state_id_offset, &state_id)
                    || state_id <= 0) {
                    *flags |= FACTORY_STATE_UNREADABLE;
                    break;
                }
                if ((groups & FACTORY_IDENTITY) != 0) {
                    PointerVector provinces{};
                    uint32_t province_count = 0;
                    const void *region = nullptr;
                    if (!ReadAt(current_state.data, state_provinces_offset, &provinces)
                        || !VectorCount(provinces, sizeof(int32_t), max_provinces_per_state, &province_count)
                        || province_count == 0 || !ReadAt(provinces.begin, 0, &anchor_province_id)
                        || anchor_province_id < 0 || !ReadAt(current_state.data, state_region_offset, &region)
                        || !ReadBoundedString(region, region_key_offset,
                            state_region_key, sizeof(state_region_key))) {
                        *flags |= FACTORY_STATE_UNREADABLE;
                        break;
                    }
                }

                const StateBuildingNode *factory_node = nullptr;
                const StateBuildingNode *factory_tail = nullptr;
                int32_t factory_count = 0;
                if (!ReadAt(current_state.data, state_factories_offset, &factory_node)
                    || !ReadAt(current_state.data, state_factories_offset + 4, &factory_tail)
                    || !ReadAt(current_state.data, state_factories_offset + 8, &factory_count)
                    || factory_count < 0 || factory_count > static_cast<int32_t>(max_factories_per_state)
                    || ((factory_count == 0) != (factory_node == nullptr && factory_tail == nullptr))) {
                    *flags |= FACTORY_LIST_INVALID;
                    break;
                }

                const StateBuildingNode *previous_factory_node = nullptr;
                uint32_t factories_walked = 0;
                while (factory_node != nullptr && factories_walked < max_factories_per_state) {
                    StateBuildingNode current_factory{};
                    if (!CopyReadable(&current_factory, factory_node, sizeof(current_factory))
                        || current_factory.previous != previous_factory_node) {
                        *flags |= FACTORY_LIST_INVALID;
                        break;
                    }
                    if (current_factory.deleted == 0) {
                        if (*snapshot_count >= snapshot_capacity || *snapshot_count >= max_sample_factories) {
                            *flags |= FACTORY_LIMIT;
                            break;
                        }
                        FactorySnapshot snapshot{};
                        snapshot.state_index = states_walked;
                        snapshot.factory_index = factories_walked;
                        snapshot.state_id = state_id;
                        snapshot.anchor_province_id_candidate = anchor_province_id;
                        if ((groups & FACTORY_IDENTITY) != 0) {
                            std::memcpy(snapshot.state_region_key, state_region_key, sizeof(state_region_key));
                        }
                        const void *definition = nullptr;
                        std::memcpy(&definition, current_factory.data.data() + state_building_definition_offset, sizeof(definition));
                        if ((groups & FACTORY_IDENTITY) != 0) {
                            std::memcpy(&snapshot.level, current_factory.data.data() + state_building_level_offset, sizeof(snapshot.level));
                            const uint8_t subsidized = current_factory.data[state_building_subsidized_offset];
                            const uint8_t closed = current_factory.data[state_building_closed_offset];
                            if (subsidized > 1 || closed > 1 || snapshot.level < 0) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            snapshot.subsidized = subsidized != 0;
                            snapshot.closed = closed != 0;
                        }
                        if (!ReadNormalizedKey(definition, building_definition_key_offset,
                                snapshot.factory_type, sizeof(snapshot.factory_type))) {
                            *flags |= FACTORY_DEFINITION_INVALID;
                            break;
                        }

                        if ((groups & FACTORY_PRODUCTION) != 0) {
                            std::memcpy(&snapshot.output_raw, current_factory.data.data() + state_building_output_offset, sizeof(snapshot.output_raw));
                            const void *production_type = nullptr;
                            const void *output_good = nullptr;
                            if (!ReadAt(definition, building_definition_production_type_offset, &production_type)
                                || !ReadAt(production_type, production_type_output_good_offset, &output_good)
                                || !ReadAt(production_type, production_type_base_output_offset, &snapshot.base_output_raw)
                                || !ReadAt(output_good, goods_ordinal_offset, &snapshot.output_good_ordinal)
                                || !ReadNormalizedKey(output_good, goods_key_offset,
                                    snapshot.output_good, sizeof(snapshot.output_good))
                                || snapshot.output_raw < 0 || snapshot.output_good_ordinal < 0
                                || snapshot.base_output_raw < 0) {
                                *flags |= FACTORY_DEFINITION_INVALID;
                                break;
                            }
                        }

                        if ((groups & FACTORY_EMPLOYMENT) != 0) {
                            std::memcpy(&snapshot.employee_count, current_factory.data.data() + state_building_employees_offset, sizeof(snapshot.employee_count));
                            PointerVector employment{};
                            uint32_t employment_count = 0;
                            if (snapshot.employee_count < 0
                                || !ReadAt(current_factory.data.data(), state_building_employment_offset, &employment)
                                || !VectorCount(employment, pop_employment_size, 1024, &employment_count)) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            int64_t assigned_total = 0;
                            for (uint32_t index = 0; index < employment_count; ++index) {
                                std::array<uint8_t, pop_employment_size> record{};
                                const auto *record_address = static_cast<const uint8_t *>(employment.begin)
                                    + index * pop_employment_size;
                                const void *pop = nullptr;
                                int32_t assigned = 0;
                                const void *pop_type = nullptr;
                                char pop_type_key[64]{};
                                if (!CopyReadable(record.data(), record_address, record.size())) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                std::memcpy(&pop, record.data() + pop_employment_pop_offset, sizeof(pop));
                                std::memcpy(&assigned, record.data() + pop_employment_count_offset, sizeof(assigned));
                                if (assigned < 0 || !ReadAt(pop, pop_type_offset, &pop_type)
                                    || !ReadNormalizedKey(pop_type, pop_type_key_offset,
                                        pop_type_key, sizeof(pop_type_key))) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                assigned_total += assigned;
                                if (assigned_total > (std::numeric_limits<int32_t>::max)()) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                if (std::strcmp(pop_type_key, "craftsmen") == 0) snapshot.craftsmen_count += assigned;
                                else if (std::strcmp(pop_type_key, "clerks") == 0) snapshot.clerk_count += assigned;
                            }
                            if (*flags != 0) break;
                            if (assigned_total != snapshot.employee_count) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                        }

                        if ((groups & FACTORY_INPUTS) != 0) {
                            PointerVector stockpile_values{};
                            uint32_t stockpile_value_count = 0;
                            std::array<uint8_t, 64> stockpile_index{};
                            std::array<bool, 65> seen_value_indices{};
                            if (!CopyReadable(stockpile_index.data(),
                                    current_factory.data.data() + state_building_stockpile_index_offset,
                                    stockpile_index.size())
                                || !ReadAt(current_factory.data.data(), state_building_stockpile_values_offset,
                                    &stockpile_values)
                                || !VectorCount(stockpile_values, sizeof(int64_t), 65, &stockpile_value_count)) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            int64_t sentinel = 0;
                            if (stockpile_value_count != 0
                                && (!ReadAt(stockpile_values.begin, 0, &sentinel) || sentinel != 0)) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            for (uint32_t good_ordinal = 0; good_ordinal < stockpile_index.size(); ++good_ordinal) {
                                const uint8_t value_index = stockpile_index[good_ordinal];
                                if (value_index == 0) continue;
                                if (value_index >= stockpile_value_count || seen_value_indices[value_index]
                                    || *input_count >= input_capacity || *input_count >= max_sample_factory_inputs) {
                                    *flags |= value_index >= stockpile_value_count || seen_value_indices[value_index]
                                        ? FACTORY_UNREADABLE : FACTORY_LIMIT;
                                    break;
                                }
                                seen_value_indices[value_index] = true;
                                FactoryInputSnapshot input{};
                                input.factory_snapshot_index = *snapshot_count;
                                input.good_ordinal = static_cast<int32_t>(good_ordinal);
                                if (!ReadAt(stockpile_values.begin, value_index * sizeof(int64_t), &input.stockpile_raw)
                                    || input.stockpile_raw < 0) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                inputs[(*input_count)++] = input;
                            }
                            if (*flags != 0) break;
                        }

                        if ((groups & FACTORY_FINANCE) != 0) {
                            std::memcpy(&snapshot.budget_raw, current_factory.data.data() + state_building_budget_offset, sizeof(snapshot.budget_raw));
                            std::memcpy(&snapshot.market_spending_raw, current_factory.data.data() + state_building_market_spending_offset, sizeof(snapshot.market_spending_raw));
                            std::memcpy(&snapshot.sales_income_raw, current_factory.data.data() + state_building_sales_income_offset, sizeof(snapshot.sales_income_raw));
                            std::memcpy(&snapshot.paychecks_raw, current_factory.data.data() + state_building_paychecks_offset, sizeof(snapshot.paychecks_raw));
                            std::memcpy(&snapshot.investment_raw, current_factory.data.data() + state_building_investment_offset, sizeof(snapshot.investment_raw));
                            if (snapshot.budget_raw < 0 || snapshot.market_spending_raw < 0
                                || snapshot.sales_income_raw < 0 || snapshot.paychecks_raw < 0
                                || snapshot.investment_raw < 0) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                        }
                        snapshots[(*snapshot_count)++] = snapshot;
                    }
                    ++factories_walked;
                    previous_factory_node = factory_node;
                    if (current_factory.next == factory_node) {
                        *flags |= FACTORY_LIST_INVALID;
                        break;
                    }
                    factory_node = current_factory.next;
                }
                if (*flags != 0) break;
                if (factories_walked != static_cast<uint32_t>(factory_count)
                    || (factory_count != 0 && previous_factory_node != factory_tail)) {
                    *flags |= FACTORY_LIST_INVALID;
                    break;
                }
            }
            ++states_walked;
            previous_state_node = state_node;
            if (current_state.next == state_node) {
                *flags |= FACTORY_STATE_LIST_INVALID;
                break;
            }
            state_node = current_state.next;
        }
        if (states_walked != static_cast<uint32_t>(state_count)
            || (state_count != 0 && previous_state_node != state_tail)) {
            *flags |= FACTORY_STATE_LIST_INVALID;
        }
        return *flags == 0;
    }

    bool CollectWorldMarket(const void *game_state, WorldMarketSnapshot *snapshots,
                            size_t snapshot_capacity, uint32_t *snapshot_count)
    {
        if (game_state == nullptr || snapshots == nullptr || snapshot_count == nullptr) return false;
        *snapshot_count = 0;
        ResetMemoryRegionCache();
        const void *world_market = nullptr;
        if (!ReadAt(game_state, game_state_world_market_offset, &world_market)) return false;

        std::array<int64_t, 64> supply{}, last_supply{}, stock{}, demand{}, real_demand{};
        std::array<int64_t, 64> price{}, last_price{}, actual_sold{}, actual_sold_world{};
        std::array<bool, 64> supply_present{}, last_supply_present{}, stock_present{};
        std::array<bool, 64> demand_present{}, real_demand_present{}, price_present{};
        std::array<bool, 64> last_price_present{}, actual_sold_present{}, actual_sold_world_present{};
        if (!ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_supply_offset, &supply, &supply_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_last_supply_offset, &last_supply, &last_supply_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_stock_offset, &stock, &stock_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_demand_offset, &demand, &demand_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_real_demand_offset, &real_demand, &real_demand_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_price_offset, &price, &price_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_last_price_offset, &last_price, &last_price_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_actual_sold_offset, &actual_sold, &actual_sold_present)
            || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_actual_sold_world_offset,
                &actual_sold_world, &actual_sold_world_present)) return false;

        for (uint32_t ordinal = 0; ordinal < price_present.size(); ++ordinal) {
            if (!price_present[ordinal]) continue;
            if (!last_price_present[ordinal] || *snapshot_count >= snapshot_capacity
                || price[ordinal] < 0 || last_price[ordinal] < 0 || supply[ordinal] < 0
                || last_supply[ordinal] < 0 || stock[ordinal] < 0 || demand[ordinal] < 0
                || real_demand[ordinal] < 0 || actual_sold[ordinal] < 0
                || actual_sold_world[ordinal] < 0) return false;
            WorldMarketSnapshot snapshot{};
            snapshot.good_ordinal = static_cast<int32_t>(ordinal);
            snapshot.price_raw = price[ordinal];
            snapshot.last_price_raw = last_price[ordinal];
            snapshot.supply_raw = supply[ordinal];
            snapshot.last_supply_raw = last_supply[ordinal];
            snapshot.worldmarket_stock_raw = stock[ordinal];
            snapshot.demand_raw = demand[ordinal];
            snapshot.real_demand_raw = real_demand[ordinal];
            snapshot.actual_sold_raw = actual_sold[ordinal];
            snapshot.actual_sold_world_raw = actual_sold_world[ordinal];
            snapshots[(*snapshot_count)++] = snapshot;
        }
        return true;
    }

    bool CanWritePopMoney(const void *pop)
    {
        ResetMemoryRegionCache();
        if (pop == nullptr) return false;
        constexpr size_t span = pop_total_cash_flow_offset + sizeof(int64_t) - pop_money_offset;
        return IsWritable(reinterpret_cast<const uint8_t *>(pop) + pop_money_offset, span);
    }
}
