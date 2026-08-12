#include <smedley/game_state/readers.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace smedley::game_state
{
    namespace
    {
        constexpr size_t country_minimum_size = 0xe9c;
        constexpr size_t country_tag_offset = 0x1c;
        constexpr size_t country_states_offset = 0xe44;
        constexpr size_t game_state_provinces_offset = 0xacc;
        constexpr size_t game_state_countries_offset = 0xadc;
        constexpr size_t game_state_current_date_offset = 0xb0c;
        constexpr size_t game_state_world_market_offset = 0xbcc;
        constexpr uintptr_t state_employment_registry_rva = 0x00e58728;
        constexpr uintptr_t loaded_goods_count_rva = 0x00e587f4;
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
        constexpr size_t province_rgo_capacity_offset = 0x1ac;
        constexpr size_t province_state_offset = 0x188;
        constexpr size_t state_rgo_capacity_offset = 0xc8;
        constexpr size_t state_population_by_type_offset = 0x118;
        constexpr size_t pop_size_offset = 0x58;
        constexpr size_t pop_employed_offset = 0x60;
        constexpr size_t pop_province_offset = 0x64;
        constexpr size_t pop_type_offset = 0x68;
        constexpr size_t pop_culture_offset = 0x6c;
        constexpr size_t pop_religion_offset = 0x70;
        constexpr size_t culture_key_offset = 0x18;
        constexpr size_t religion_key_offset = 0x10;
        constexpr size_t pop_type_id_offset = 0x28;
        constexpr size_t pop_consciousness_offset = 0x118;
        constexpr size_t pop_militancy_offset = 0x120;
        constexpr size_t pop_literacy_offset = 0x128;
        constexpr size_t pop_life_needs_satisfaction_offset = 0x130;
        constexpr size_t pop_everyday_needs_satisfaction_offset = 0x138;
        constexpr size_t pop_luxury_needs_satisfaction_offset = 0x140;
        constexpr size_t pop_money_offset = 0x180;
        constexpr size_t pop_interest_cash_flow_offset = 0x210;
        constexpr size_t pop_total_cash_flow_offset = 0x218;
        constexpr size_t pop_savings_offset = 0x250;
        constexpr size_t pop_next_offset = 0x27c;
        constexpr size_t pop_id_offset = 0x0c;
        constexpr size_t pop_economy_offset = 0x1d4;
        constexpr size_t artisan_need_pool_offset = 0x58;
        constexpr size_t artisan_production_type_offset = 0xb0;
        constexpr size_t artisan_last_spending_offset = 0xb8;
        constexpr size_t artisan_current_producing_offset = 0xc0;
        constexpr size_t artisan_percent_afforded_offset = 0xc8;
        constexpr size_t artisan_percent_sold_domestic_offset = 0xd0;
        constexpr size_t artisan_percent_sold_export_offset = 0xd8;
        constexpr size_t artisan_leftover_offset = 0xe0;
        constexpr size_t artisan_throttle_offset = 0xe8;
        constexpr size_t artisan_needs_cost_offset = 0xf0;
        constexpr size_t artisan_production_income_offset = 0xf8;
        constexpr size_t creditor_tag_offset = 0x08;
        constexpr size_t creditor_interest_offset = 0x10;
        constexpr size_t creditor_debt_offset = 0x18;
        constexpr size_t creditor_was_paid_offset = 0x20;
        constexpr uint32_t max_states = 512;
        constexpr uint32_t max_game_provinces = 4096;
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
        constexpr size_t state_building_requested_input_index_offset = 0x88;
        constexpr size_t state_building_requested_input_values_offset = 0xc8;
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
        constexpr size_t production_type_owner_modifier_offset = 0xf0;
        constexpr size_t owner_modifier_pop_type_ordinal_offset = 0x28;
        constexpr size_t goods_ordinal_offset = 0x08;
        constexpr size_t goods_key_offset = 0x0c;
        constexpr size_t pop_type_key_offset = 0x08;
        constexpr size_t pop_employment_size = 0x10;
        constexpr size_t pop_employment_pop_offset = 0x08;
        constexpr size_t pop_employment_count_offset = 0x0c;
        constexpr size_t state_employment_record_size = 0xb0;
        constexpr size_t state_employment_production_type_offset = 0x08;
        constexpr size_t state_employment_output_good_offset = 0x0c;
        constexpr size_t state_employment_province_offset = 0x1c;
        constexpr size_t state_employment_output_efficiency_offset = 0x38;
        constexpr size_t state_employment_throughput_offset = 0x40;
        constexpr size_t state_employment_employed_offset = 0x58;
        constexpr size_t state_employment_income_offset = 0x80;
        constexpr size_t state_employment_percent_sold_domestic_offset = 0x90;
        constexpr size_t state_employment_percent_sold_export_offset = 0x98;
        constexpr size_t state_employment_leftover_offset = 0xa0;
        constexpr size_t state_employment_base_size_offset = 0x88;
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

        constexpr size_t pop_identity_table_capacity = 262144;

        struct TraversalScratch
        {
            std::array<int32_t, max_destination_provinces> province_ids{};
            std::array<uintptr_t, max_pops> pop_pointers{};
            std::array<int64_t, max_pops> pop_savings{};
            // Generation tags retain duplicate detection without sorting every
            // candidate set or clearing a large table for each country.
            std::array<uintptr_t, pop_identity_table_capacity> pop_identity_entries{};
            std::array<uint32_t, pop_identity_table_capacity> pop_identity_generations{};
            uint32_t province_attempts = 0;
            uint32_t province_id_count = 0;
            uint32_t pop_attempts = 0;
            uint32_t pop_pointer_count = 0;
            uint32_t pop_identity_generation = 0;
            bool duplicate_pop = false;
        };

        // The event runs synchronously on the game thread. Static storage keeps
        // this bounded identity set off the thread's stack without allocating
        // on the hot path.
        TraversalScratch traversal_scratch;

        void BeginPopIdentitySet(TraversalScratch *scratch)
        {
            scratch->duplicate_pop = false;
            ++scratch->pop_identity_generation;
            if (scratch->pop_identity_generation == 0) {
                scratch->pop_identity_generations.fill(0);
                scratch->pop_identity_generation = 1;
            }
        }

        bool InsertPopIdentity(TraversalScratch *scratch, uintptr_t address)
        {
            constexpr size_t mask = pop_identity_table_capacity - 1;
            static_assert((pop_identity_table_capacity & mask) == 0);
            size_t slot = static_cast<size_t>((address >> 4) * uintptr_t{2654435761u}) & mask;
            for (size_t attempt = 0; attempt < pop_identity_table_capacity; ++attempt) {
                if (scratch->pop_identity_generations[slot] != scratch->pop_identity_generation) {
                    scratch->pop_identity_generations[slot] = scratch->pop_identity_generation;
                    scratch->pop_identity_entries[slot] = address;
                    return true;
                }
                if (scratch->pop_identity_entries[slot] == address) return false;
                slot = (slot + 1) & mask;
            }
            return false;
        }

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
            if (base == nullptr || value == nullptr) return false;
            const uintptr_t address = reinterpret_cast<uintptr_t>(base);
            if (address > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
            return CopyReadable(value, reinterpret_cast<const void *>(address + offset), sizeof(T));
        }

        bool VectorCount(const PointerVector &vector, size_t element_size, uint32_t limit, uint32_t *count)
        {
            if (count == nullptr || element_size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(vector.begin);
            const uintptr_t end = reinterpret_cast<uintptr_t>(vector.end);
            const uintptr_t capacity = reinterpret_cast<uintptr_t>(vector.capacity);
            if (begin == 0 && end == 0 && capacity == 0) {
                *count = 0;
                return true;
            }
            if (begin == 0 || begin > end || end > capacity
                || begin % alignof(void *) != 0 || end % alignof(void *) != 0 || capacity % alignof(void *) != 0
                || (end - begin) % element_size != 0 || (capacity - begin) % element_size != 0) return false;
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

        bool MultiplyFixed15(int64_t left, int64_t right, int64_t *result)
        {
            if (result == nullptr || left < 0 || right < 0
                || (left != 0 && right > (std::numeric_limits<int64_t>::max)() / left)) return false;
            *result = left * right >> 15;
            return true;
        }

        bool LoadedGoodsCount(uint32_t *loaded_goods_count)
        {
            if (loaded_goods_count == nullptr) return false;
            const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            int32_t count = 0;
            if (module != 0 && module <= (std::numeric_limits<uintptr_t>::max)() - loaded_goods_count_rva
                && ReadAt(reinterpret_cast<const void *>(module + loaded_goods_count_rva), 0, &count)
                && count > 0 && count <= 64) {
                *loaded_goods_count = static_cast<uint32_t>(count);
                return true;
            }
            return false;
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
                           PopRef *immediate_pop, uint32_t province_limit,
                           uint32_t pop_limit, bool traverse_pops, bool collect_savings_aggregates,
                           CountryEconomySnapshot *sample)
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
                const void *province = detail::RawPointer(resolver(resolver_context, province_id));
                PointerVector pop_lists{};
                uint32_t pop_list_count = 0;
                if (!ReadAt(province, province_pop_lists_offset, &pop_lists)
                    || !VectorCount(pop_lists, sizeof(PopList), max_pop_lists_per_province, &pop_list_count)) {
                    sample->flags |= SAMPLE_POP_VECTOR_INVALID;
                    continue;
                }
                ++sample->destination_provinces_resolved;
                sample->destination_pop_lists += pop_list_count;
                if (!traverse_pops) continue;
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
                        if (!InsertPopIdentity(scratch, reinterpret_cast<uintptr_t>(pop))) {
                            scratch->duplicate_pop = true;
                        }
                        scratch->pop_savings[scratch->pop_pointer_count] = savings;
                        ++scratch->pop_pointer_count;
                        if (collect_savings_aggregates) {
                            AddChecked(savings, &sample->destination_pop_savings_raw, &sample->flags);
                            AddChecked(savings / pop_savings_state_scale,
                                &sample->destination_pop_savings_state_scale_raw, &sample->flags);
                        }
                        if (immediate_pop != nullptr && !*immediate_pop) {
                            PopMoneySnapshot snapshot{};
                            if (!ReadPopMoney(pop, &snapshot)) sample->flags |= SAMPLE_POP_UNREADABLE;
                            else *immediate_pop = PopRef{static_cast<const void *>(pop)};
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

    bool ReadCurrentDate(GameStateRef game_state, int32_t *date_raw)
    {
        if (date_raw == nullptr) return false;
        int32_t value = 0;
        if (!ReadAt(detail::RawPointer(game_state), game_state_current_date_offset, &value)) return false;
        *date_raw = value;
        return true;
    }

    bool ReadCountryCount(GameStateRef game_state, uint32_t *count)
    {
        if (count == nullptr) return false;
        PointerVector countries{};
        uint32_t value = 0;
        if (!ReadAt(detail::RawPointer(game_state), game_state_countries_offset, &countries)
            || !VectorCount(countries, sizeof(void *), max_game_countries, &value)) return false;
        *count = value;
        return true;
    }

    CountryRef ResolveCountry(GameStateRef game_state, int32_t ordinal)
    {
        if (ordinal < 0) return {};
        PointerVector countries{};
        uint32_t count = 0;
        const void *country = nullptr;
        if (!ReadAt(detail::RawPointer(game_state), game_state_countries_offset, &countries)
            || !VectorCount(countries, sizeof(void *), max_game_countries, &count)
            || static_cast<uint32_t>(ordinal) >= count
            || !ReadAt(countries.begin, static_cast<size_t>(ordinal) * sizeof(country), &country)
            || country == nullptr) return {};
        return CountryRef{static_cast<const void *>(country)};
    }

    ProvinceRef ResolveProvince(GameStateRef game_state, int32_t id)
    {
        if (id < 0) return {};
        PointerVector provinces{};
        uint32_t count = 0;
        const void *province = nullptr;
        if (!ReadAt(detail::RawPointer(game_state), game_state_provinces_offset, &provinces)
            || !VectorCount(provinces, sizeof(void *), max_game_provinces, &count)
            || static_cast<uint32_t>(id) >= count
            || !ReadAt(provinces.begin, static_cast<size_t>(id) * sizeof(province), &province)
            || province == nullptr) return {};
        return ProvinceRef{static_cast<const void *>(province)};
    }

    CountryEconomySnapshot ReadCountryEconomyImpl(CountryRef country_ref, int32_t date_raw,
                                                    CountryResolver country_resolver, ProvinceResolver province_resolver,
                                                     const void *resolver_context, bool collect_states, bool collect_pops,
                                                     TraversalScratch *scratch, PopRef *immediate_pop,
                                                     uint32_t province_limit, uint32_t pop_limit, bool collect_creditors,
                                                     bool collect_economy_values)
    {
        const void *country = detail::RawPointer(country_ref);
        CountryEconomySnapshot sample{};
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
        if (collect_economy_values) ReadAt(country, country_treasury_offset, &sample.treasury_raw);

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
                                    scratch, immediate_pop, province_limit, pop_limit,
                                    true, collect_economy_values, &sample);
                            }
                            if (collect_economy_values) {
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

        if (collect_economy_values) {
            const void *bank = nullptr;
            if (!ReadAt(country, country_bank_offset, &bank) || bank == nullptr
                || !ReadAt(bank, bank_interest_offset, &sample.bank_interest_raw)) {
                sample.flags |= SAMPLE_BANK_UNREADABLE;
            }
        }

        if (!collect_creditors) return sample;

        PointerVector creditors{};
        if (!ReadAt(country, country_creditors_offset, &creditors)
            || !VectorCount(creditors, sizeof(void *), max_creditors, &sample.creditor_count)) {
            sample.flags |= SAMPLE_CREDITOR_VECTOR_INVALID;
            return sample;
        }
        if (sample.creditor_count == 0) return sample;
        std::array<int32_t, max_creditor_destinations * 2> destination_slots{};
        destination_slots.fill(-1);
        for (uint32_t index = 0; index < sample.creditor_count; ++index) {
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
                || !ReadAt(creditor, creditor_was_paid_offset, &was_paid)
                || (collect_economy_values
                    && (!ReadAt(creditor, creditor_interest_offset, &interest)
                        || !ReadAt(creditor, creditor_debt_offset, &debt)))) {
                sample.flags |= SAMPLE_CREDITOR_UNREADABLE;
                continue;
            }
            if (country_resolver == nullptr) {
                if (was_paid > 1) sample.flags |= SAMPLE_CREDITOR_TAG_INVALID;
                if (collect_economy_values) {
                    AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
                    AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
                }
                if (was_paid != 0) ++sample.creditors_was_paid;
                continue;
            }
            if (ordinal == 0 && was_paid <= 1) {
                if (collect_economy_values) {
                    AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
                    AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
                }
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
            if (collect_economy_values) {
                AddChecked(interest, &sample.creditor_interest_raw, &sample.flags);
                AddChecked(debt, &sample.creditor_debt_raw, &sample.flags);
            }
            if (was_paid != 0) ++sample.creditors_was_paid;

            const uint32_t slot_mask = static_cast<uint32_t>(destination_slots.size() - 1);
            uint32_t slot = static_cast<uint32_t>(ordinal) & slot_mask;
            while (destination_slots[slot] >= 0 && destination_slots[slot] != ordinal) {
                slot = (slot + 1) & slot_mask;
            }
            if (destination_slots[slot] == ordinal) {
                sample.flags |= SAMPLE_CREDITOR_DUPLICATE_DESTINATION;
                continue;
            }
            if (sample.creditor_destinations == max_creditor_destinations) {
                sample.flags |= SAMPLE_CREDITOR_DESTINATION_LIMIT;
                break;
            }
            const void *destination = detail::RawPointer(country_resolver(resolver_context, ordinal));
            uint32_t destination_key = 0;
            int32_t destination_ordinal = -1;
            if (!ReadAt(destination, country_tag_offset, &destination_key)
                || !ReadAt(destination, country_tag_offset + sizeof(destination_key), &destination_ordinal)
                || destination_key != key || destination_ordinal != ordinal) {
                sample.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
                continue;
            }
            destination_slots[slot] = ordinal;
            if (!collect_states && !collect_pops) {
                const void *destination_bank = nullptr;
                int64_t destination_bank_interest = 0;
                if (!ReadAt(destination, country_bank_offset, &destination_bank) || destination_bank == nullptr
                    || !ReadAt(destination_bank, bank_interest_offset, &destination_bank_interest)) {
                    sample.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
                    continue;
                }
                sample.destination_keys[sample.creditor_destinations] = key;
                sample.destination_ordinals[sample.creditor_destinations] = ordinal;
                sample.destination_bank_interests_raw[sample.creditor_destinations] = destination_bank_interest;
                ++sample.creditor_destinations;
                AddChecked(destination_bank_interest, &sample.destination_bank_interest_raw, &sample.flags);
                continue;
            }
            const CountryEconomySnapshot destination_sample = ReadCountryEconomyImpl(
                CountryRef{static_cast<const void *>(destination)}, date_raw, nullptr, province_resolver, resolver_context, collect_states, collect_pops, scratch,
                immediate_pop, province_limit, pop_limit, false, collect_economy_values);
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

    CountryEconomySnapshot ReadCountryEconomy(CountryRef country, int32_t date_raw,
                                               CountryResolver country_resolver, ProvinceResolver province_resolver,
                                               const void *resolver_context, PopRef *immediate_pop)
    {
        ResetMemoryRegionCache();
        if (immediate_pop != nullptr) *immediate_pop = {};
        traversal_scratch.province_attempts = 0;
        traversal_scratch.province_id_count = 0;
        traversal_scratch.pop_attempts = 0;
        traversal_scratch.pop_pointer_count = 0;
        BeginPopIdentitySet(&traversal_scratch);
        CountryEconomySnapshot sample = ReadCountryEconomyImpl(
            country, date_raw, country_resolver, province_resolver, resolver_context,
            true, province_resolver != nullptr, &traversal_scratch, immediate_pop,
            max_destination_provinces, max_pops, true, true);
        sample.destination_province_attempts = traversal_scratch.province_attempts;
        sample.destination_pop_attempts = traversal_scratch.pop_attempts;

        if ((sample.flags & SAMPLE_POP_LIMIT) == 0) {
            auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
            std::sort(traversal_scratch.province_ids.begin(), province_end);
            if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
                sample.flags |= SAMPLE_DUPLICATE_PROVINCE;
            }
            if (traversal_scratch.duplicate_pop) sample.flags |= SAMPLE_DUPLICATE_POP;
        }
        return sample;
    }

    CountryEconomySnapshot ReadCountryCreditors(CountryRef country, int32_t date_raw,
                                                CountryResolver country_resolver, const void *resolver_context)
    {
        ResetMemoryRegionCache();
        return ReadCountryEconomyImpl(country, date_raw, country_resolver, nullptr, resolver_context,
            false, false, &traversal_scratch, nullptr, 0, 0, true, false);
    }

    CountryEconomySnapshot ReadCountryCreditorBalances(const CountryEconomySnapshot &before,
                                                         CountryRef country, int32_t date_raw,
                                                        CountryResolver country_resolver, const void *resolver_context)
    {
        ResetMemoryRegionCache();
        CountryEconomySnapshot after = ReadCountryEconomyImpl(country, date_raw, nullptr, nullptr, resolver_context,
            false, false, &traversal_scratch, nullptr, 0, 0, false, false);
        after.creditor_count = before.creditor_count;
        if (country_resolver == nullptr || before.creditor_destinations > max_creditor_destinations) {
            after.flags |= SAMPLE_CREDITOR_DESTINATION_INVALID;
            return after;
        }
        for (uint32_t index = 0; index < before.creditor_destinations; ++index) {
            const int32_t ordinal = before.destination_ordinals[index];
            const uint32_t key = before.destination_keys[index];
            const void *destination = detail::RawPointer(country_resolver(resolver_context, ordinal));
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

    bool CollectCountryPops(CountryRef country, int32_t date_raw,
                            ProvinceResolver province_resolver, const void *resolver_context,
                            PopCandidate *candidates, size_t candidate_capacity,
                            uint32_t province_attempt_capacity, uint32_t *candidate_count,
                             CountryEconomySnapshot *quality)
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
        BeginPopIdentitySet(&traversal_scratch);
        const uint32_t province_limit = (std::min)(province_attempt_capacity, max_destination_provinces);
        const uint32_t pop_limit = static_cast<uint32_t>((std::min)(candidate_capacity,
            static_cast<size_t>(max_pops)));
        CountryEconomySnapshot sample = ReadCountryEconomyImpl(country, date_raw, nullptr, province_resolver,
            resolver_context, true, true, &traversal_scratch, nullptr, province_limit, pop_limit, false, false);
        sample.destination_province_attempts = traversal_scratch.province_attempts;
        sample.destination_pop_attempts = traversal_scratch.pop_attempts;
        if ((sample.flags & SAMPLE_POP_LIMIT) == 0) {
            auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
            std::sort(traversal_scratch.province_ids.begin(), province_end);
            if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
                sample.flags |= SAMPLE_DUPLICATE_PROVINCE;
            }
            if (traversal_scratch.duplicate_pop) sample.flags |= SAMPLE_DUPLICATE_POP;
        }
        if (traversal_scratch.pop_pointer_count > candidate_capacity) sample.flags |= SAMPLE_POP_LIMIT;
        *quality = sample;
        for (uint32_t index = 0; index < traversal_scratch.pop_pointer_count; ++index) {
            candidates[index].address = PopRef{reinterpret_cast<const void *>(traversal_scratch.pop_pointers[index])};
            candidates[index].savings_raw = traversal_scratch.pop_savings[index];
        }
        *candidate_count = traversal_scratch.pop_pointer_count;
        return (sample.flags & ~SAMPLE_POP_LIMIT) == 0;
    }

    namespace
    {
        ProvinceRef ResolveProvinceFromGameState(const void *context, int32_t id)
        {
            return ResolveProvince(*static_cast<const GameStateRef *>(context), id);
        }
    }

    bool CollectCountryPops(CountryRef country, GameStateRef game_state, int32_t date_raw,
                            PopCandidate *candidates, size_t candidate_capacity,
                            uint32_t province_attempt_capacity, uint32_t *candidate_count,
                            CountryEconomySnapshot *quality)
    {
        return CollectCountryPops(country, date_raw, ResolveProvinceFromGameState, &game_state,
            candidates, candidate_capacity, province_attempt_capacity, candidate_count, quality);
    }

    bool CollectCountryStateInterest(CountryRef country_ref, GameStateRef game_state, int32_t date_raw,
                                     StateInterestCandidate *states, size_t state_capacity,
                                     uint32_t *state_count, PopCandidate *pops, size_t pop_capacity,
                                     uint32_t province_attempt_capacity, uint32_t *pop_count,
                                     CountryEconomySnapshot *quality)
    {
        if (states == nullptr || state_count == nullptr || pop_count == nullptr || quality == nullptr
            || state_capacity > max_states || pop_capacity > max_pops
            || (pop_capacity != 0 && pops == nullptr) || !game_state) {
            return false;
        }
        *state_count = 0;
        *pop_count = 0;
        *quality = {};
        quality->date_raw = date_raw;
        const void *country = detail::RawPointer(country_ref);
        if (!IsReadable(country, country_minimum_size)) {
            quality->flags = SAMPLE_COUNTRY_UNREADABLE;
            return false;
        }
        ReadAt(country, country_tag_offset, &quality->country_tag);
        quality->country_tag[3] = '\0';
        ReadAt(country, country_tag_offset + sizeof(uint32_t), &quality->country_ordinal);

        const ListNode *node = nullptr;
        const ListNode *tail = nullptr;
        if (!ReadAt(country, country_states_offset, &node)
            || !ReadAt(country, country_states_offset + 4, &tail)
            || !ReadAt(country, country_states_offset + 8, &quality->state_count_reported)
            || quality->state_count_reported < 0
            || quality->state_count_reported > static_cast<int32_t>(max_states)) {
            quality->flags |= SAMPLE_STATE_LIST_INVALID;
            return false;
        }

        traversal_scratch.province_attempts = 0;
        traversal_scratch.province_id_count = 0;
        traversal_scratch.pop_attempts = 0;
        traversal_scratch.pop_pointer_count = 0;
        BeginPopIdentitySet(&traversal_scratch);
        const uint32_t province_limit = (std::min)(province_attempt_capacity, max_destination_provinces);
        const uint32_t pop_limit = static_cast<uint32_t>((std::min)(pop_capacity, static_cast<size_t>(max_pops)));
        while (node != nullptr && quality->states_walked < max_states) {
            ListNode current{};
            if (!CopyReadable(&current, node, sizeof(current))) {
                quality->flags |= SAMPLE_STATE_LIST_INVALID;
                break;
            }
            if (current.deleted == 0 && current.data != nullptr) {
                if (*state_count >= state_capacity || !IsReadable(current.data, state_size)) {
                    quality->flags |= *state_count >= state_capacity ? SAMPLE_STATE_LIMIT : SAMPLE_STATE_UNREADABLE;
                } else {
                    bool duplicate_state = false;
                    for (uint32_t index = 0; index < *state_count; ++index) {
                        if (states[index].state.address() == reinterpret_cast<uintptr_t>(current.data)) {
                            duplicate_state = true;
                            break;
                        }
                    }
                    if (duplicate_state) {
                        quality->flags |= SAMPLE_STATE_LIST_INVALID;
                    } else {
                        StateInterestCandidate &candidate = states[*state_count];
                        candidate = {};
                        candidate.state = StateRef{static_cast<const void *>(current.data)};
                        candidate.first_pop_index = traversal_scratch.pop_pointer_count;
                        PointerVector provinces{};
                        if (!ReadAt(current.data, state_id_offset, &candidate.state_id)
                            || !ReadAt(current.data, state_savings_offset, &candidate.savings_raw)
                            || !ReadAt(current.data, state_interest_offset, &candidate.interest_raw)
                            || !ReadAt(current.data, state_provinces_offset, &provinces)
                            || !VectorCount(provinces, sizeof(int32_t), max_provinces_per_state,
                                &candidate.province_count)) {
                            quality->flags |= SAMPLE_STATE_UNREADABLE;
                        } else {
                            if (candidate.province_count > (std::numeric_limits<uint32_t>::max)()
                                - quality->province_element_candidates) {
                                quality->flags |= SAMPLE_SUM_OVERFLOW;
                            } else {
                                quality->province_element_candidates += candidate.province_count;
                            }
                            if (candidate.savings_raw != 0) ++quality->states_with_savings;
                            if (candidate.interest_raw != 0) ++quality->states_with_interest;
                            AddChecked(candidate.savings_raw, &quality->state_savings_raw, &quality->flags);
                            AddChecked(candidate.interest_raw, &quality->state_interest_raw, &quality->flags);
                            if (pop_capacity != 0) {
                                CollectPops(provinces, ResolveProvinceFromGameState, &game_state,
                                    &traversal_scratch, nullptr, province_limit, pop_limit,
                                    candidate.interest_raw > 0, false, quality);
                                candidate.pop_count = traversal_scratch.pop_pointer_count - candidate.first_pop_index;
                            }
                            ++*state_count;
                        }
                    }
                }
            }
            ++quality->states_walked;
            if (current.next == node) {
                quality->flags |= SAMPLE_STATE_LIST_INVALID;
                break;
            }
            node = current.next;
        }
        if (node != nullptr) quality->flags |= SAMPLE_STATE_LIMIT;
        if (quality->states_walked != static_cast<uint32_t>(quality->state_count_reported)) {
            quality->flags |= SAMPLE_STATE_COUNT_MISMATCH;
        }
        if ((quality->state_count_reported == 0 && tail != nullptr)
            || (quality->state_count_reported != 0 && tail == nullptr)) {
            quality->flags |= SAMPLE_STATE_LIST_INVALID;
        }
        quality->destination_province_attempts = traversal_scratch.province_attempts;
        quality->destination_pop_attempts = traversal_scratch.pop_attempts;
        if (traversal_scratch.duplicate_pop) quality->flags |= SAMPLE_DUPLICATE_POP;
        auto province_end = traversal_scratch.province_ids.begin() + traversal_scratch.province_id_count;
        std::sort(traversal_scratch.province_ids.begin(), province_end);
        if (std::adjacent_find(traversal_scratch.province_ids.begin(), province_end) != province_end) {
            quality->flags |= SAMPLE_DUPLICATE_PROVINCE;
        }
        if (quality->flags != 0) return false;

        for (uint32_t index = 0; index < traversal_scratch.pop_pointer_count; ++index) {
            pops[index].address = PopRef{reinterpret_cast<const void *>(traversal_scratch.pop_pointers[index])};
            pops[index].savings_raw = traversal_scratch.pop_savings[index];
        }
        *pop_count = traversal_scratch.pop_pointer_count;
        return true;
    }

    bool ReadPopMoneySnapshot(PopRef pop, PopMoneySnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        PopMoneySnapshot value{};
        if (!ReadPopMoney(detail::RawPointer(pop), &value)) return false;
        *snapshot = value;
        return true;
    }

    bool ReadPopDetailSnapshot(PopRef pop_ref, PopDetailSnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        const void *pop = detail::RawPointer(pop_ref);
        PopDetailSnapshot value{};
        const void *province = nullptr;
        const void *pop_type = nullptr;
        if (!ReadAt(pop, pop_id_offset, &value.pop_id)
            || !ReadAt(pop, pop_size_offset, &value.size_candidate)
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

        if (value.pop_id < 0 || value.province_id_candidate < 0 || value.pop_type_id_candidate < 0
            || value.pop_type_id_candidate > 127 || value.size_candidate < 0
            || value.employed_candidate < 0 || value.employed_candidate > value.size_candidate) {
            return false;
        }
        *snapshot = value;
        return true;
    }

    bool ReadPopNeedsSnapshot(PopRef pop_ref, PopNeedsSnapshot *snapshot)
    {
        const void *pop = detail::RawPointer(pop_ref);
        if (pop == nullptr || snapshot == nullptr) return false;
        PopNeedsSnapshot value{};
        if (!ReadAt(pop, pop_life_needs_satisfaction_offset, &value.life_satisfaction_candidate_raw)
            || !ReadAt(pop, pop_everyday_needs_satisfaction_offset, &value.everyday_satisfaction_candidate_raw)
            || !ReadAt(pop, pop_luxury_needs_satisfaction_offset, &value.luxury_satisfaction_candidate_raw)) {
            return false;
        }
        if (value.life_satisfaction_candidate_raw < 0 || value.life_satisfaction_candidate_raw > 32768
            || value.everyday_satisfaction_candidate_raw < 0 || value.everyday_satisfaction_candidate_raw > 32768
            || value.luxury_satisfaction_candidate_raw < 0 || value.luxury_satisfaction_candidate_raw > 32768) {
            return false;
        }
        *snapshot = value;
        return true;
    }

    bool ReadPopIdentityDimensions(PopRef pop_ref, PopIdentityDimensions *identity)
    {
        const void *pop = detail::RawPointer(pop_ref);
        if (pop == nullptr || identity == nullptr) return false;
        const void *culture = nullptr;
        const void *religion = nullptr;
        const void *pop_type = nullptr;
        PopIdentityDimensions value{};
        if (!ReadAt(pop, pop_type_offset, &pop_type)
            || !ReadAt(pop, pop_culture_offset, &culture)
            || !ReadAt(pop, pop_religion_offset, &religion)
            || !ReadNormalizedKey(pop_type, pop_type_key_offset,
                value.pop_type_tag_candidate, sizeof(value.pop_type_tag_candidate))
            || !ReadNormalizedKey(culture, culture_key_offset,
                value.culture_tag_candidate, sizeof(value.culture_tag_candidate))
            || !ReadNormalizedKey(religion, religion_key_offset,
                value.religion_tag_candidate, sizeof(value.religion_tag_candidate))) {
            return false;
        }
        *identity = value;
        return true;
    }

    bool ReadArtisanSnapshot(PopRef pop_ref, ArtisanSnapshot *snapshot,
                              ArtisanInputSnapshot *inputs, size_t input_capacity, uint32_t *input_count,
                              uint32_t groups, ArtisanReadFailure *failure)
    {
        const void *pop = detail::RawPointer(pop_ref);
        if (failure != nullptr) *failure = {};
        const auto fail = [&](ArtisanReadFailureReason reason, int64_t raw = 0) {
            if (failure != nullptr) {
                failure->reason = reason;
                failure->offending_raw = raw;
            }
            return false;
        };
        if (pop == nullptr || snapshot == nullptr || inputs == nullptr || input_count == nullptr) {
            return fail(ArtisanReadFailureReason::InvalidArgument);
        }
        *input_count = 0;
        const void *pop_type = nullptr;
        const void *economy = nullptr;
        const void *production_type = nullptr;
        char pop_type_key[64]{};
        ArtisanSnapshot value{};
        value.address = pop_ref;
        if (!ReadAt(pop, pop_id_offset, &value.pop_id) || value.pop_id < 0) {
            return fail(ArtisanReadFailureReason::PopHeader, value.pop_id);
        }
        if (failure != nullptr) failure->pop_id = value.pop_id;
        if (!ReadAt(pop, pop_type_offset, &pop_type)
            || !ReadNormalizedKey(pop_type, pop_type_key_offset, pop_type_key, sizeof(pop_type_key))
            || std::strcmp(pop_type_key, "artisans") != 0
            || !ReadAt(pop, pop_economy_offset, &economy) || economy == nullptr) {
            return fail(ArtisanReadFailureReason::PopHeader);
        }

        const uint32_t recipe_groups = ARTISAN_IDENTITY | ARTISAN_PRODUCTION | ARTISAN_INPUTS;
        if ((groups & recipe_groups) != 0
            && (!ReadAt(economy, artisan_production_type_offset, &production_type)
                || production_type == nullptr)) return fail(ArtisanReadFailureReason::ProductionTypeMissing);
        if ((groups & ARTISAN_IDENTITY) != 0) {
            const void *output_good = nullptr;
            if (!ReadNormalizedKey(production_type, 0x08, value.production_type, sizeof(value.production_type))
                || !ReadAt(production_type, production_type_output_good_offset, &output_good) || output_good == nullptr
                || !ReadAt(output_good, goods_ordinal_offset, &value.output_good_ordinal)
                || value.output_good_ordinal < 0 || value.output_good_ordinal >= 64
                || !ReadNormalizedKey(output_good, goods_key_offset, value.output_good, sizeof(value.output_good))) {
                return fail(ArtisanReadFailureReason::Identity);
            }
        }
        if ((groups & ARTISAN_PRODUCTION) != 0) {
            if (!ReadAt(production_type, production_type_base_output_offset, &value.base_output_raw)
                || !ReadAt(economy, artisan_current_producing_offset, &value.current_producing_raw)
                || !MultiplyFixed15(value.current_producing_raw, value.base_output_raw, &value.gross_output_raw)) {
                return fail(ArtisanReadFailureReason::ProductionRead);
            }
            if (value.base_output_raw < 0) return fail(ArtisanReadFailureReason::ProductionValue, value.base_output_raw);
            if (value.current_producing_raw < 0) return fail(ArtisanReadFailureReason::ProductionValue, value.current_producing_raw);
            if (value.gross_output_raw < 0) return fail(ArtisanReadFailureReason::ProductionValue, value.gross_output_raw);
        }
        if ((groups & ARTISAN_FINANCE) != 0) {
            if (!ReadAt(economy, artisan_last_spending_offset, &value.last_spending_raw)
                || !ReadAt(economy, artisan_percent_afforded_offset, &value.percent_afforded_raw)
                || !ReadAt(economy, artisan_percent_sold_domestic_offset, &value.percent_sold_domestic_raw)
                || !ReadAt(economy, artisan_percent_sold_export_offset, &value.percent_sold_export_raw)
                || !ReadAt(economy, artisan_leftover_offset, &value.leftover_raw)
                || !ReadAt(economy, artisan_throttle_offset, &value.throttle_raw)
                || !ReadAt(economy, artisan_needs_cost_offset, &value.needs_cost_raw)
                || !ReadAt(economy, artisan_production_income_offset, &value.production_income_raw)) {
                return fail(ArtisanReadFailureReason::FinanceRead);
            }
            if (value.last_spending_raw < 0) return fail(ArtisanReadFailureReason::LastSpending, value.last_spending_raw);
            if (value.percent_afforded_raw < 0 || value.percent_afforded_raw > 32768) {
                return fail(ArtisanReadFailureReason::PercentAfforded, value.percent_afforded_raw);
            }
            if (value.percent_sold_domestic_raw < 0 || value.percent_sold_domestic_raw > 32768) {
                return fail(ArtisanReadFailureReason::PercentSoldDomestic, value.percent_sold_domestic_raw);
            }
            if (value.percent_sold_export_raw < 0) {
                return fail(ArtisanReadFailureReason::PercentSoldExport, value.percent_sold_export_raw);
            }
            if (value.leftover_raw < 0) return fail(ArtisanReadFailureReason::Leftover, value.leftover_raw);
            if (value.throttle_raw < 0 || value.throttle_raw > 32768) {
                return fail(ArtisanReadFailureReason::Throttle, value.throttle_raw);
            }
            if (value.needs_cost_raw < 0) return fail(ArtisanReadFailureReason::NeedsCost, value.needs_cost_raw);
            if (value.production_income_raw < 0) {
                return fail(ArtisanReadFailureReason::ProductionIncome, value.production_income_raw);
            }
        }

        if ((groups & ARTISAN_INPUTS) != 0) {
            std::array<int64_t, 64> stockpile{}, need{};
            std::array<bool, 64> stockpile_present{}, need_present{};
            if (!ReadGoodsPool(economy, &stockpile, &stockpile_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(economy) + artisan_need_pool_offset,
                    &need, &need_present)) return fail(ArtisanReadFailureReason::Inputs);
            for (size_t ordinal = 0; ordinal < stockpile.size(); ++ordinal) {
                if (!stockpile_present[ordinal] && !need_present[ordinal]) continue;
                if (*input_count >= input_capacity || stockpile[ordinal] < 0 || need[ordinal] < 0) {
                    return fail(ArtisanReadFailureReason::Inputs);
                }
                inputs[*input_count] = {
                    static_cast<int32_t>(ordinal), stockpile[ordinal], need[ordinal]};
                ++*input_count;
            }
        }
        *snapshot = value;
        return true;
    }

    const char *ArtisanReadFailureName(ArtisanReadFailureReason reason)
    {
        switch (reason) {
        case ArtisanReadFailureReason::None: return "none";
        case ArtisanReadFailureReason::InvalidArgument: return "invalid_argument";
        case ArtisanReadFailureReason::PopHeader: return "pop_header";
        case ArtisanReadFailureReason::ProductionTypeMissing: return "production_type_missing";
        case ArtisanReadFailureReason::Identity: return "identity";
        case ArtisanReadFailureReason::ProductionRead: return "production_read";
        case ArtisanReadFailureReason::ProductionValue: return "production_value";
        case ArtisanReadFailureReason::FinanceRead: return "finance_read";
        case ArtisanReadFailureReason::LastSpending: return "last_spending";
        case ArtisanReadFailureReason::PercentAfforded: return "percent_afforded";
        case ArtisanReadFailureReason::PercentSoldDomestic: return "percent_sold_domestic";
        case ArtisanReadFailureReason::PercentSoldExport: return "percent_sold_export";
        case ArtisanReadFailureReason::Leftover: return "leftover";
        case ArtisanReadFailureReason::Throttle: return "throttle";
        case ArtisanReadFailureReason::NeedsCost: return "needs_cost";
        case ArtisanReadFailureReason::ProductionIncome: return "production_income";
        case ArtisanReadFailureReason::Inputs: return "inputs";
        }
        return "unknown";
    }

    bool ReadInactiveArtisan(PopRef pop_ref, int32_t *pop_id)
    {
        const void *pop = detail::RawPointer(pop_ref);
        if (pop == nullptr || pop_id == nullptr) return false;
        const void *pop_type = nullptr;
        const void *economy = nullptr;
        const void *production_type = nullptr;
        char pop_type_key[64]{};
        int32_t id = -1;
        if (!ReadAt(pop, pop_id_offset, &id) || id < 0
            || !ReadAt(pop, pop_type_offset, &pop_type)
            || !ReadNormalizedKey(pop_type, pop_type_key_offset, pop_type_key, sizeof(pop_type_key))
            || std::strcmp(pop_type_key, "artisans") != 0
            || !ReadAt(pop, pop_economy_offset, &economy) || economy == nullptr
            || !ReadAt(economy, artisan_production_type_offset, &production_type)
            || production_type != nullptr) return false;
        *pop_id = id;
        return true;
    }

    bool CollectCountryFactories(CountryRef country_ref, FactorySnapshot *snapshots,
                                 size_t snapshot_capacity, uint32_t *snapshot_count,
                                 FactoryInputSnapshot *inputs, size_t input_capacity,
                                 uint32_t *input_count, uint32_t groups, uint32_t *flags,
                                 uint32_t loaded_goods_count_override)
    {
        const void *country = detail::RawPointer(country_ref);
        if (snapshots == nullptr || snapshot_count == nullptr || inputs == nullptr
            || input_count == nullptr || flags == nullptr) return false;
        *snapshot_count = 0;
        *input_count = 0;
        *flags = 0;
        ResetMemoryRegionCache();
        uint32_t loaded_goods_count = 0;
        if ((groups & FACTORY_INPUTS) != 0) {
            if (loaded_goods_count_override > 64
                || (loaded_goods_count_override == 0 && !LoadedGoodsCount(&loaded_goods_count))) {
                *flags = FACTORY_GOODS_REGISTRY_INVALID;
                return false;
            }
            if (loaded_goods_count_override != 0) loaded_goods_count = loaded_goods_count_override;
        }
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
                                return true;
                        }
                        FactorySnapshot snapshot{};
                        snapshot.address = FactoryRef{static_cast<const void *>(factory_node)};
                        snapshot.state_index = states_walked;
                        snapshot.factory_index = factories_walked;
                        snapshot.state_id = state_id;
                        snapshot.anchor_province_id_candidate = anchor_province_id;
                        if ((groups & FACTORY_IDENTITY) != 0) {
                            std::memcpy(snapshot.state_region_key, state_region_key, sizeof(state_region_key));
                        }
                        const void *definition = nullptr;
                        if ((groups & (FACTORY_IDENTITY | FACTORY_PRODUCTION)) != 0) {
                            std::memcpy(&definition, current_factory.data.data() + state_building_definition_offset, sizeof(definition));
                        }
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
                        if ((groups & FACTORY_IDENTITY) != 0
                            && !ReadNormalizedKey(definition, building_definition_key_offset,
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
                            if ((*flags & ~FACTORY_LIMIT) != 0) break;
                            if (assigned_total != snapshot.employee_count) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                        }

                        if ((groups & FACTORY_INPUTS) != 0) {
                            const uint32_t input_start = *input_count;
                            PointerVector stockpile_values{};
                            PointerVector requested_values{};
                            uint32_t stockpile_value_count = 0;
                            uint32_t requested_value_count = 0;
                            std::array<uint8_t, 64> stockpile_index{};
                            std::array<uint8_t, 64> requested_index{};
                            std::array<bool, 65> seen_stockpile_indices{};
                            std::array<bool, 65> seen_requested_indices{};
                            if (!CopyReadable(stockpile_index.data(),
                                    current_factory.data.data() + state_building_stockpile_index_offset,
                                    stockpile_index.size())
                                || !ReadAt(current_factory.data.data(), state_building_stockpile_values_offset,
                                    &stockpile_values)
                                || !VectorCount(stockpile_values, sizeof(int64_t), 65, &stockpile_value_count)) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            if (!CopyReadable(requested_index.data(),
                                    current_factory.data.data() + state_building_requested_input_index_offset,
                                    requested_index.size())
                                || !ReadAt(current_factory.data.data(), state_building_requested_input_values_offset,
                                    &requested_values)
                                || !VectorCount(requested_values, sizeof(int64_t), 65, &requested_value_count)) {
                                *flags |= FACTORY_REQUESTED_INPUT_METADATA_INVALID;
                                break;
                            }
                            int64_t stockpile_sentinel = 0;
                            int64_t requested_sentinel = 0;
                            if (stockpile_value_count != 0
                                && (!ReadAt(stockpile_values.begin, 0, &stockpile_sentinel) || stockpile_sentinel != 0)) {
                                *flags |= FACTORY_UNREADABLE;
                                break;
                            }
                            if (requested_value_count != 0
                                && (!ReadAt(requested_values.begin, 0, &requested_sentinel) || requested_sentinel != 0)) {
                                *flags |= FACTORY_REQUESTED_INPUT_SENTINEL_INVALID;
                                break;
                            }
                            for (uint32_t good_ordinal = 0; good_ordinal < loaded_goods_count; ++good_ordinal) {
                                const uint8_t stockpile_value_index = stockpile_index[good_ordinal];
                                const uint8_t requested_value_index = requested_index[good_ordinal];
                                const bool has_requested_value = requested_value_index != 0;
                                if (stockpile_value_index == 0 && !has_requested_value) continue;
                                if (stockpile_value_index != 0 && (stockpile_value_index >= stockpile_value_count
                                        || seen_stockpile_indices[stockpile_value_index])) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                if (has_requested_value && (requested_value_index >= requested_value_count
                                        || seen_requested_indices[requested_value_index])) {
                                    *flags |= FACTORY_REQUESTED_INPUT_INDEX_INVALID;
                                    break;
                                }
                                if (*input_count >= input_capacity || *input_count >= max_sample_factory_inputs) {
                                    *flags |= FACTORY_LIMIT;
                                    *input_count = input_start;
                                    break;
                                }
                                if (stockpile_value_index != 0) seen_stockpile_indices[stockpile_value_index] = true;
                                if (has_requested_value) seen_requested_indices[requested_value_index] = true;
                                FactoryInputSnapshot input{};
                                input.factory_snapshot_index = *snapshot_count;
                                input.good_ordinal = static_cast<int32_t>(good_ordinal);
                                if ((stockpile_value_index != 0 && !ReadAt(stockpile_values.begin,
                                        stockpile_value_index * sizeof(int64_t), &input.stockpile_raw))
                                    || input.stockpile_raw < 0) {
                                    *flags |= FACTORY_UNREADABLE;
                                    break;
                                }
                                if ((has_requested_value && !ReadAt(requested_values.begin,
                                        requested_value_index * sizeof(int64_t), &input.requested_raw))
                                    || input.requested_raw < 0) {
                                    *flags |= FACTORY_REQUESTED_INPUT_VALUE_INVALID;
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
                if ((*flags & FACTORY_LIMIT) != 0) return true;
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
        return (*flags & ~FACTORY_LIMIT) == 0;
    }

    bool CollectWorldMarket(GameStateRef game_state_ref, WorldMarketSnapshot *snapshots,
                            size_t snapshot_capacity, uint32_t *snapshot_count)
    {
        return CollectWorldMarketGroups(
            game_state_ref, snapshots, snapshot_capacity, snapshot_count, MARKET_ALL);
    }

    bool CollectWorldMarketGroups(GameStateRef game_state_ref, WorldMarketSnapshot *snapshots,
                                  size_t snapshot_capacity, uint32_t *snapshot_count, uint32_t groups)
    {
        const void *game_state = detail::RawPointer(game_state_ref);
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
        if (groups == 0 || (groups & ~MARKET_ALL) != 0) return false;
        if (((groups & MARKET_SUPPLY) != 0 && (!ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_supply_offset, &supply, &supply_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_last_supply_offset, &last_supply, &last_supply_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_stock_offset, &stock, &stock_present)))
            || ((groups & MARKET_DEMAND) != 0 && (!ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_demand_offset, &demand, &demand_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_real_demand_offset, &real_demand, &real_demand_present)))
            || ((groups & MARKET_PRICE) != 0 && (!ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_price_offset, &price, &price_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_last_price_offset, &last_price, &last_price_present)))
            || ((groups & MARKET_SALES) != 0 && (!ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_actual_sold_offset, &actual_sold, &actual_sold_present)
                || !ReadGoodsPool(static_cast<const uint8_t *>(world_market) + market_actual_sold_world_offset,
                    &actual_sold_world, &actual_sold_world_present)))) return false;

        for (uint32_t ordinal = 0; ordinal < price_present.size(); ++ordinal) {
            const bool requested_value_present = ((groups & MARKET_PRICE) != 0 && price_present[ordinal])
                || ((groups & MARKET_SUPPLY) != 0 && supply_present[ordinal])
                || ((groups & MARKET_DEMAND) != 0 && demand_present[ordinal])
                || ((groups & MARKET_SALES) != 0 && actual_sold_present[ordinal]);
            if (!requested_value_present) continue;
            if (*snapshot_count >= snapshot_capacity
                || ((groups & MARKET_PRICE) != 0 && (!price_present[ordinal] || !last_price_present[ordinal]
                    || price[ordinal] < 0 || last_price[ordinal] < 0))
                || ((groups & MARKET_SUPPLY) != 0 && (!supply_present[ordinal] || !last_supply_present[ordinal]
                    || !stock_present[ordinal] || supply[ordinal] < 0 || last_supply[ordinal] < 0 || stock[ordinal] < 0))
                || ((groups & MARKET_DEMAND) != 0 && (!demand_present[ordinal] || !real_demand_present[ordinal]
                    || demand[ordinal] < 0 || real_demand[ordinal] < 0))
                || ((groups & MARKET_SALES) != 0 && (!actual_sold_present[ordinal] || !actual_sold_world_present[ordinal]
                    || actual_sold[ordinal] < 0 || actual_sold_world[ordinal] < 0))) return false;
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

    EmploymentRegistryRef ResolveStateEmploymentRegistry()
    {
        ResetMemoryRegionCache();
        const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (module == 0 || module > (std::numeric_limits<uintptr_t>::max)() - state_employment_registry_rva) {
            return {};
        }
        const void *registry = nullptr;
        return ReadAt(reinterpret_cast<const void *>(module + state_employment_registry_rva), 0, &registry)
            ? EmploymentRegistryRef{static_cast<const void *>(registry)} : EmploymentRegistryRef{};
    }

    bool ReadProvinceRgo(EmploymentRegistryRef registry_ref, ProvinceRef province_ref, int32_t province_id,
                         size_t province_count, uint32_t groups, RgoSnapshot *snapshot)
    {
        const void *registry = detail::RawPointer(registry_ref);
        const void *province = detail::RawPointer(province_ref);
        if (registry == nullptr || province == nullptr || snapshot == nullptr || province_id < 0
            || province_count > max_sample_destination_provinces) return false;
        ResetMemoryRegionCache();
        PointerVector records{};
        uint32_t record_count = 0;
        if (!ReadAt(registry, 0, &records)
            || !VectorCount(records, state_employment_record_size, max_sample_destination_provinces, &record_count)
            || record_count != province_count || static_cast<uint32_t>(province_id) >= record_count) return false;

        const auto *record = static_cast<const uint8_t *>(records.begin)
            + static_cast<size_t>(province_id) * state_employment_record_size;
        const void *record_province = nullptr;
        RgoSnapshot value{};
        value.province_id = province_id;
        if (!ReadAt(record, state_employment_province_offset, &record_province) || record_province != province) return false;

        if ((groups & RGO_IDENTITY) != 0 || (groups & (RGO_PRODUCTION | RGO_MODIFIERS)) != 0) {
            const void *production_type = nullptr;
            const void *output_good = nullptr;
            const void *definition_output_good = nullptr;
            if (!ReadAt(record, state_employment_production_type_offset, &production_type)
                || !ReadAt(record, state_employment_output_good_offset, &output_good)
                || !ReadAt(production_type, production_type_output_good_offset, &definition_output_good)
                || definition_output_good != output_good
                || !ReadAt(output_good, goods_ordinal_offset, &value.output_good_ordinal)
                || value.output_good_ordinal < 0 || value.output_good_ordinal >= 64) return false;
            if ((groups & RGO_IDENTITY) != 0
                && (!ReadNormalizedKey(production_type, 0x08, value.production_type, sizeof(value.production_type))
                    || !ReadNormalizedKey(output_good, goods_key_offset, value.output_good, sizeof(value.output_good)))) {
                return false;
            }
            if ((groups & RGO_PRODUCTION) != 0) {
                if (!ReadAt(production_type, production_type_base_output_offset, &value.base_output_per_size_raw)
                    || !ReadAt(record, state_employment_base_size_offset, &value.base_size_raw)
                    || !ReadAt(record, state_employment_output_efficiency_offset, &value.output_efficiency_raw)
                    || !ReadAt(record, state_employment_throughput_offset, &value.throughput_raw)
                    || value.base_output_per_size_raw < 0 || value.base_size_raw < 0
                    || value.output_efficiency_raw < 0 || value.throughput_raw < 0) return false;
                int64_t output_modifier_raw = 0;
                int64_t output_per_size_raw = 0;
                if (!MultiplyFixed15(value.output_efficiency_raw, value.throughput_raw, &output_modifier_raw)
                    || !MultiplyFixed15(output_modifier_raw, value.base_output_per_size_raw, &output_per_size_raw)
                    || !MultiplyFixed15(output_per_size_raw, value.base_size_raw, &value.gross_output_raw)) return false;
            }
            if ((groups & RGO_MODIFIERS) != 0) {
                const void *state = nullptr;
                const void *owner_modifier = nullptr;
                const void *population_by_type = nullptr;
                int32_t owner_pop_type_ordinal = -1;
                if (!ReadAt(province, province_state_offset, &state)
                    || !ReadAt(production_type, production_type_owner_modifier_offset, &owner_modifier)
                    || !ReadAt(owner_modifier, owner_modifier_pop_type_ordinal_offset, &owner_pop_type_ordinal)
                    || owner_pop_type_ordinal < 0 || owner_pop_type_ordinal >= 128
                    || !ReadAt(state, state_population_by_type_offset, &population_by_type)
                    || !ReadAt(population_by_type, static_cast<size_t>(owner_pop_type_ordinal) * sizeof(int32_t),
                        &value.owner_population)
                    || !ReadAt(state, state_rgo_capacity_offset, &value.state_rgo_employment_capacity)
                    || value.owner_population < 0 || value.state_rgo_employment_capacity < 0) return false;
                if (value.state_rgo_employment_capacity != 0) {
                    value.owner_output_modifier_raw = static_cast<int64_t>(value.owner_population) * 32768
                        / value.state_rgo_employment_capacity;
                }
            }
        }
        if ((groups & RGO_EMPLOYMENT) != 0
            && (!ReadAt(province, province_rgo_capacity_offset, &value.employment_capacity)
                || !ReadAt(record, state_employment_employed_offset, &value.employed)
                || value.employment_capacity < 0 || value.employed < 0
                || value.employed > value.employment_capacity)) return false;
        if ((groups & (RGO_FINANCE | RGO_SALES)) != 0
            && (!ReadAt(record, state_employment_income_offset, &value.income_raw) || value.income_raw < 0)) return false;
        if ((groups & RGO_SALES) != 0
            && (!ReadAt(record, state_employment_percent_sold_domestic_offset, &value.percent_sold_domestic_raw)
                || !ReadAt(record, state_employment_percent_sold_export_offset, &value.percent_sold_export_raw)
                || !ReadAt(record, state_employment_leftover_offset, &value.leftover_raw)
                || value.percent_sold_domestic_raw < 0 || value.percent_sold_domestic_raw > 32768
                || value.percent_sold_export_raw < 0
                || value.leftover_raw < 0)) return false;
        *snapshot = value;
        return true;
    }

}
