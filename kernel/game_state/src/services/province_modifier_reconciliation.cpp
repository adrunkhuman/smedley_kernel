#include <smedley/province_modifier_reconciliation_api.h>
#include <smedley/event_services_api.h>

#include <smedley/executable_identity.hpp>
#include <smedley/game_state/game_services_abi.hpp>
#include <smedley/game_state/province_modifier_list.hpp>
#include <smedley/game_state/runtime.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

#include "../foreign_memory.hpp"
#include "../v2_304_layout.hpp"
#include "service_support.hpp"

namespace smedley::game_state::services
{
    namespace
    {
        using foreign_memory::MemoryRegionCache;
        using foreign_memory::ReadValue;

        constexpr uintptr_t country_database_rva = 0x00e587e4;
        constexpr uintptr_t modifier_registry_rva = 0x00e5a69c;
        constexpr uintptr_t modifier_lookup_rva = 0x001661b0;
        constexpr uintptr_t issue_option_registry_rva = 0x00e58a48;
        constexpr uintptr_t issue_option_lookup_rva = 0x00575ed0;
        constexpr uintptr_t modifier_remover_rva = 0x000b39a0;
        constexpr uint32_t handle_index_bits = 13;
        constexpr uint64_t handle_index_mask = (UINT64_C(1) << handle_index_bits) - 1;
        constexpr uint32_t handle_payload_bits = handle_index_bits + 1;
        constexpr uint64_t handle_country_bit = UINT64_C(1) << handle_index_bits;

        enum class HandleDomain : uint64_t
        {
            province = 0,
            country = handle_country_bit,
        };

        constexpr std::array<uint8_t, 15> lookup_signature{
            0x55, 0x8b, 0xec, 0x51, 0x8b, 0x4d, 0x08, 0x53, 0x56, 0x8b, 0xf0, 0x8b, 0x41, 0x14, 0x57,
        };
        constexpr std::array<uint8_t, 16> remover_signature{
            0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08, 0x57,
            0x8b, 0xbe, 0xc8, 0x00, 0x00, 0x00, 0x85, 0xff,
        };

        struct ForeignVector
        {
            const void **begin = nullptr;
            const void **end = nullptr;
            const void **capacity = nullptr;
        };

        struct EngineString
        {
            union Storage { char inline_buffer[16]; const char *pointer; } storage{};
            uint32_t size = 0;
            uint32_t capacity = 0;
            uint32_t allocator = 0;
        };

        struct CachedProvince
        {
            uintptr_t address = 0;
            uintptr_t owner = 0;
            uint32_t owner_index = 0;
            uint32_t modifier_mask = 0;
        };

        struct CachedCountry
        {
            uintptr_t address = 0;
            uint32_t selected = 0;
        };

        struct ReconciliationSlot
        {
            ReconciliationSlot *previous = nullptr;
            SmedleyProvinceModifierReconciliationAuthority authority = 0;
            std::thread::id thread{};
            BankInterestAccess *access = nullptr;
            std::array<uintptr_t, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_KEYS> definitions{};
            std::array<CachedProvince, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> provinces{};
            std::array<CachedCountry, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES> countries{};
            std::array<uint32_t, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> matched_provinces{};
            std::array<uint32_t, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES> matched_countries{};
            uint32_t matched_province_count = 0;
            uint32_t matched_country_count = 0;
            uint32_t definition_count = 0;
            uintptr_t owner_option = 0;
            uint32_t owner_group_index = 0;
            bool collected = false;
        };

        struct CallbackSlot
        {
            SmedleyProvinceModifierReconciliationCallbackV1Fn callback = nullptr;
            void *context = nullptr;
            SmedleyEventServicesRegistration event_registration = 0;
            bool reserved = false;
        };

        std::array<ReconciliationSlot, 2> slots{};
        std::array<bool, slots.size()> slot_active{};
        std::mutex slot_mutex;
        thread_local ReconciliationSlot *active_slot = nullptr;
        std::array<CallbackSlot, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_REGISTRATIONS> callback_slots{};
        std::mutex callback_mutex;
        std::atomic<uint64_t> next_authority{1};

        void ResetQuery(ReconciliationSlot *slot) noexcept
        {
            for (uint32_t index = 0; index < slot->matched_province_count; ++index) {
                slot->provinces[slot->matched_provinces[index]] = {};
            }
            for (uint32_t index = 0; index < slot->matched_country_count; ++index) {
                slot->countries[slot->matched_countries[index]] = {};
            }
            slot->definitions = {};
            slot->matched_province_count = 0;
            slot->matched_country_count = 0;
            slot->definition_count = 0;
            slot->owner_option = 0;
            slot->owner_group_index = 0;
            slot->collected = false;
        }

        void ResetSlot(ReconciliationSlot *slot) noexcept
        {
            ResetQuery(slot);
            slot->previous = nullptr;
            slot->authority = 0;
            slot->thread = {};
            slot->access = nullptr;
        }

        ReconciliationSlot *Active(SmedleyProvinceModifierReconciliationAuthority authority) noexcept
        {
            return authority != 0 && active_slot != nullptr && active_slot->authority == authority
                && active_slot->thread == std::this_thread::get_id()
                ? active_slot : nullptr;
        }

        uint64_t Handle(SmedleyProvinceModifierReconciliationAuthority authority,
                        uint32_t index, HandleDomain domain) noexcept
        {
            if (authority == 0 || authority > (UINT64_MAX >> handle_payload_bits)
                || index >= handle_index_mask) return 0;
            return (authority << handle_payload_bits) | static_cast<uint64_t>(domain)
                | (static_cast<uint64_t>(index) + 1);
        }

        bool HandleIndex(SmedleyProvinceModifierReconciliationAuthority authority, uint64_t handle, uint32_t limit,
                         HandleDomain domain, uint32_t *index) noexcept
        {
            if (index == nullptr || handle == 0 || (handle >> handle_payload_bits) != authority
                || (handle & handle_country_bit) != static_cast<uint64_t>(domain)) return false;
            const uint64_t encoded = handle & handle_index_mask;
            if (encoded == 0 || encoded - 1 >= limit) return false;
            *index = static_cast<uint32_t>(encoded - 1);
            return true;
        }

        bool ValidKey(const SmedleyReconciliationKeyV1 *key) noexcept
        {
            return ValidRecord(key, 1) && key->bytes != 0
                && key->bytes < SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_KEY_BYTES
                && std::memchr(key->value, '\0', key->bytes) == nullptr && key->value[key->bytes] == '\0';
        }

        EngineString BorrowedString(const SmedleyReconciliationKeyV1 &key) noexcept
        {
            EngineString result{};
            result.size = key.bytes;
            result.capacity = key.bytes;
            if (key.bytes < sizeof(result.storage.inline_buffer)) {
                std::memcpy(result.storage.inline_buffer, key.value, key.bytes);
                result.capacity = sizeof(result.storage.inline_buffer) - 1;
            } else {
                result.storage.pointer = key.value;
            }
            return result;
        }

        __declspec(noinline) void *CallLookup(uintptr_t function, void *registry, const EngineString *key) noexcept
        {
            void *result = nullptr;
            __asm {
                mov eax, registry
                push key
                call function
                mov result, eax
            }
            return result;
        }

        __declspec(noinline) void CallRemove(uintptr_t function, void *province, void *definition) noexcept
        {
            __asm {
                push esi
                mov esi, province
                push definition
                call function
                pop esi
            }
        }

        bool NativeSignature(uintptr_t address, const uint8_t *signature, size_t size) noexcept
        {
            return foreign_memory::IsAccessible(reinterpret_cast<const void *>(address), size, false)
                && std::memcmp(reinterpret_cast<const void *>(address), signature, size) == 0;
        }

        bool NativeBoundary(uintptr_t *base) noexcept
        {
            if (base == nullptr || !smedley::IsCurrentExecutableSupported()) return false;
            *base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            return *base != 0
                && NativeSignature(*base + modifier_lookup_rva, lookup_signature.data(), lookup_signature.size())
                && NativeSignature(*base + issue_option_lookup_rva, lookup_signature.data(), lookup_signature.size())
                && NativeSignature(*base + modifier_remover_rva, remover_signature.data(), remover_signature.size());
        }

        SmedleyProvinceModifierReconciliationResult CheckAuthority(
            SmedleyProvinceModifierReconciliationAuthority authority, ReconciliationSlot **slot) noexcept
        {
            if (slot == nullptr) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            *slot = Active(authority);
            if (*slot == nullptr) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_STALE_AUTHORITY;
            if ((*slot)->access == nullptr || !(*slot)->access->first_country()
                || (*slot)->access->CheckSynchronousAccess(false) != PopInterestMutationStatus::success) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_STALE_AUTHORITY;
            }
            return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
        }

        SmedleyProvinceModifierReconciliationAuthority NextAuthority() noexcept
        {
            constexpr uint64_t max_authority = UINT64_MAX >> handle_payload_bits;
            for (;;) {
                const auto authority = next_authority.fetch_add(1, std::memory_order_relaxed) & max_authority;
                if (authority != 0) return authority;
            }
        }

        bool BindAuthority(SmedleyProvinceModifierReconciliationAuthority authority,
                           BankInterestAccess *access) noexcept
        {
            std::lock_guard<std::mutex> lock(slot_mutex);
            for (size_t index = 0; index < slots.size(); ++index) {
                if (slot_active[index]) continue;
                slot_active[index] = true;
                auto &slot = slots[index];
                ResetSlot(&slot);
                slot.previous = active_slot;
                slot.authority = authority;
                slot.thread = std::this_thread::get_id();
                slot.access = access;
                active_slot = &slot;
                return true;
            }
            return false;
        }

        void UnbindAuthority(SmedleyProvinceModifierReconciliationAuthority authority) noexcept
        {
            if (active_slot == nullptr || active_slot->authority != authority) return;
            std::lock_guard<std::mutex> lock(slot_mutex);
            auto *slot = active_slot;
            active_slot = slot->previous;
            const auto index = static_cast<size_t>(slot - slots.data());
            ResetSlot(slot);
            slot_active[index] = false;
        }

        SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL DispatchReconciliation(
            void *context, const SmedleyBankInterestEventV1 *event)
        {
            if (context == nullptr || event == nullptr || event->struct_size != sizeof(*event)
                || event->version != SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1) {
                return SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
            }
            if (event->phase != SMEDLEY_BANK_INTEREST_BEFORE || event->country_index != 0) {
                return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
            }
            auto *slot = static_cast<CallbackSlot *>(context);
            const auto authority = NextAuthority();
            if (!BindAuthority(authority, FindBankInterestGameServices(event->authority))) {
                return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
            }
            SmedleyProvinceModifierReconciliationEventV1 reconciliation_event{};
            reconciliation_event.struct_size = sizeof(reconciliation_event);
            reconciliation_event.version = SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EVENT_VERSION_V1;
            reconciliation_event.authority = authority;
            SmedleyProvinceModifierReconciliationCallbackResult result
                = SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE;
            try {
                result = slot->callback(slot->context, &reconciliation_event);
            } catch (...) {
            }
            UnbindAuthority(authority);
            return result == SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_CONTINUE
                ? SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE : SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
        }

        bool EventServices(SmedleyEventServicesApiV1 *api) noexcept
        {
            if (api == nullptr) return false;
            *api = {};
            api->struct_size = sizeof(*api);
            api->version = SMEDLEY_EVENT_SERVICES_API_VERSION_V1;
            return SmedleyGetEventServicesApiV1(api) == SMEDLEY_EVENT_SERVICES_SUCCESS
                && api->register_bank_interest != nullptr && api->unregister != nullptr;
        }

        SmedleyProvinceModifierReconciliationResult SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL
        RegisterCallback(SmedleyProvinceModifierReconciliationCallbackV1Fn callback, void *context,
                         SmedleyProvinceModifierReconciliationRegistration *registration)
        {
            if (callback == nullptr || registration == nullptr) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            }
            *registration = 0;
            SmedleyEventServicesApiV1 events{};
            if (!EventServices(&events)) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            CallbackSlot *slot = nullptr;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                for (auto &candidate : callback_slots) {
                    if (candidate.reserved) continue;
                    candidate.reserved = true;
                    candidate.callback = callback;
                    candidate.context = context;
                    slot = &candidate;
                    break;
                }
            }
            if (slot == nullptr) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CAPACITY;
            SmedleyEventServicesRegistration event_registration = 0;
            const auto result = events.register_bank_interest(
                &DispatchReconciliation, slot, &event_registration);
            if (result == SMEDLEY_EVENT_SERVICES_SUCCESS) {
                std::lock_guard<std::mutex> lock(callback_mutex);
                slot->event_registration = event_registration;
                *registration = event_registration;
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
            }
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                *slot = {};
            }
            return result == SMEDLEY_EVENT_SERVICES_CAPACITY
                ? SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CAPACITY
                : SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
        }

        SmedleyProvinceModifierReconciliationResult SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL
        UnregisterCallback(SmedleyProvinceModifierReconciliationRegistration registration)
        {
            if (registration == 0) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            SmedleyEventServicesApiV1 events{};
            if (!EventServices(&events)) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            CallbackSlot *slot = nullptr;
            SmedleyEventServicesRegistration event_registration = 0;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                for (auto &candidate : callback_slots) {
                    if (candidate.reserved && candidate.event_registration == registration) {
                        slot = &candidate;
                        event_registration = candidate.event_registration;
                        break;
                    }
                }
            }
            if (slot == nullptr) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_NOT_FOUND;
            const auto result = events.unregister(event_registration);
            if (result == SMEDLEY_EVENT_SERVICES_BUSY) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_BUSY;
            }
            if (result != SMEDLEY_EVENT_SERVICES_SUCCESS) {
                return result == SMEDLEY_EVENT_SERVICES_NOT_FOUND
                    ? SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_NOT_FOUND
                    : SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            }
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (slot->event_registration == event_registration) *slot = {};
            return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
        }

        bool ResolveOwner(uintptr_t country_database, const ForeignVector &countries, uint32_t country_count,
                          uintptr_t province, uint32_t *owner_index, uintptr_t *owner) noexcept
        {
            uintptr_t database_countries = 0;
            uintptr_t vector_owner = 0;
            *owner_index = 0;
            *owner = 0;
            if (!ReadValue(province + 0x12c, owner_index)) return false;
            if (*owner_index == 0) return true;
            return country_database != 0 && *owner_index < country_count
                && ReadValue(country_database + 0x4, &database_countries) && database_countries != 0
                && ReadValue(database_countries + *owner_index * sizeof(uintptr_t), owner)
                && ReadValue(reinterpret_cast<uintptr_t>(countries.begin) + *owner_index * sizeof(uintptr_t), &vector_owner)
                && *owner != 0 && *owner == vector_owner;
        }

        bool OwnerOptionSelected(uintptr_t owner, uint32_t group_index, uintptr_t option,
                                 uint32_t *selected) noexcept
        {
            uintptr_t options = 0;
            uintptr_t current = 0;
            if (selected == nullptr || owner == 0 || group_index >= 64
                || !ReadValue(owner + 0x860, &options) || options == 0
                || !ReadValue(options + group_index * sizeof(uintptr_t), &current)) return false;
            *selected = current == option ? 1u : 0u;
            return true;
        }

        bool ModifierMask(uintptr_t province, const ReconciliationSlot &slot, uint32_t *mask,
                           uint32_t *iteration_guard) noexcept
        {
            return detail::ReadProvinceModifierMask(province, slot.definitions.data(), slot.definition_count,
                [](uintptr_t address, void *output, size_t size) {
                    return foreign_memory::CopyReadable(output, reinterpret_cast<const void *>(address), size);
                }, mask, iteration_guard);
        }

        bool ResolveQuery(uintptr_t base, const SmedleyReconciliationKeyV1 *modifier_keys,
                          uint32_t modifier_count, const SmedleyReconciliationKeyV1 &owner_option,
                          ReconciliationSlot *slot) noexcept
        {
            void *modifier_registry = nullptr;
            void *issue_registry = nullptr;
            if (!ReadValue(base + modifier_registry_rva, &modifier_registry) || modifier_registry == nullptr
                || !ReadValue(base + issue_option_registry_rva, &issue_registry) || issue_registry == nullptr) return false;
            slot->definition_count = modifier_count;
            __try {
                for (uint32_t index = 0; index < modifier_count; ++index) {
                    const EngineString key = BorrowedString(modifier_keys[index]);
                    slot->definitions[index] = reinterpret_cast<uintptr_t>(
                        CallLookup(base + modifier_lookup_rva, modifier_registry, &key));
                    if (slot->definitions[index] == 0) return false;
                    for (uint32_t prior = 0; prior < index; ++prior) {
                        if (slot->definitions[prior] == slot->definitions[index]) return false;
                    }
                }
                const EngineString key = BorrowedString(owner_option);
                slot->owner_option = reinterpret_cast<uintptr_t>(
                    CallLookup(base + issue_option_lookup_rva, issue_registry, &key));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            uintptr_t group = 0;
            uint32_t category = 0;
            return slot->owner_option != 0 && ReadValue(slot->owner_option + 0x54, &group) && group != 0
                && ReadValue(group + 0x50, &slot->owner_group_index)
                && ReadValue(group + 0x5c, &category) && slot->owner_group_index < 64 && category == 2;
        }

        SmedleyProvinceModifierReconciliationResult SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL Collect(
            SmedleyProvinceModifierReconciliationAuthority authority,
            const SmedleyReconciliationKeyV1 *modifier_keys,
            uint32_t modifier_count, const SmedleyReconciliationKeyV1 *owner_option_key,
            SmedleyReconciliationProvinceSnapshotV1 *provinces, uint32_t province_capacity,
            uint32_t *province_count, SmedleyReconciliationCountrySnapshotV1 *countries,
            uint32_t country_capacity, uint32_t *country_count)
        {
            ReconciliationSlot *slot = nullptr;
            const auto authority_status = CheckAuthority(authority, &slot);
            if (authority_status != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS) return authority_status;
            if (modifier_keys == nullptr || modifier_count == 0
                || modifier_count > SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_KEYS
                || !ValidKey(owner_option_key) || province_count == nullptr || country_count == nullptr
                || province_capacity > SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES
                || country_capacity > SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES
                || (province_capacity != 0 && provinces == nullptr)
                || (country_capacity != 0 && countries == nullptr)) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            }
            for (uint32_t index = 0; index < modifier_count; ++index) {
                if (!ValidKey(&modifier_keys[index])) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            }
            *province_count = 0;
            *country_count = 0;
            if (!smedley::IsCurrentExecutableSupported()) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNSUPPORTED;
            }
            ResetQuery(slot);
            uintptr_t base = 0;
            if (!NativeBoundary(&base) || !ResolveQuery(base, modifier_keys, modifier_count, *owner_option_key, slot)) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            }
            uintptr_t country_database = 0;
            ForeignVector province_vector{};
            ForeignVector country_vector{};
            uint32_t province_slots = 0;
            uint32_t country_slots = 0;
            MemoryRegionCache cache{};
            if (!ReadValue(base + country_database_rva, &country_database, &cache) || country_database == 0
                || !foreign_memory::ReadVector(reinterpret_cast<const void *>(slot->access->game_state().address()),
                    v2_304_layout::game_state_provinces_offset,
                    sizeof(uintptr_t), SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES,
                    &province_vector, &province_slots, &cache)
                || !foreign_memory::ReadVector(reinterpret_cast<const void *>(slot->access->game_state().address()),
                    v2_304_layout::game_state_countries_offset,
                    sizeof(uintptr_t), SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES,
                    &country_vector, &country_slots, &cache)) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            }

            uint32_t matched_count = 0;
            uint32_t matched_country_count = 0;
            for (uint32_t id = 0; id < province_slots; ++id) {
                uintptr_t province = 0;
                if (!ReadValue(reinterpret_cast<uintptr_t>(province_vector.begin) + id * sizeof(uintptr_t), &province)
                    || province == 0) continue;
                uint32_t mask = 0;
                uint32_t guard = 0;
                if (!ModifierMask(province, *slot, &mask, &guard)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
                if (mask == 0) continue;
                uint32_t owner_index = 0;
                uintptr_t owner = 0;
                if (!ResolveOwner(country_database, country_vector, country_slots, province, &owner_index, &owner)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
                slot->provinces[id] = {province, owner, owner_index, mask};
                slot->matched_provinces[matched_count++] = id;
                slot->matched_province_count = matched_count;
                if (owner_index == 0) continue;
                auto &country = slot->countries[owner_index];
                if (country.address != 0 && country.address != owner) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
                if (country.address == 0) {
                    if (!OwnerOptionSelected(owner, slot->owner_group_index, slot->owner_option, &country.selected)) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                    }
                    country.address = owner;
                    slot->matched_countries[matched_country_count++] = owner_index;
                    slot->matched_country_count = matched_country_count;
                }
            }
            *province_count = matched_count;
            *country_count = matched_country_count;
            if (matched_count > province_capacity || matched_country_count > country_capacity) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CAPACITY;
            }
            for (uint32_t index = 0; index < matched_country_count; ++index) {
                const uint32_t owner_index = slot->matched_countries[index];
                auto &out = countries[index];
                out = {};
                out.struct_size = sizeof(out);
                out.version = 1;
                out.country = Handle(authority, owner_index, HandleDomain::country);
                out.owner_option_selected = slot->countries[owner_index].selected;
                if (out.country == 0) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            }
            for (uint32_t index = 0; index < matched_count; ++index) {
                const uint32_t id = slot->matched_provinces[index];
                const auto &cached = slot->provinces[id];
                auto &out = provinces[index];
                out = {};
                out.struct_size = sizeof(out);
                out.version = 1;
                out.province = Handle(authority, id, HandleDomain::province);
                out.owner = cached.owner_index == 0 ? 0 : Handle(authority, cached.owner_index, HandleDomain::country);
                out.present_modifier_mask = cached.modifier_mask;
                if (out.province == 0 || (cached.owner_index != 0 && out.owner == 0)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
            }
            slot->collected = true;
            return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
        }

        SmedleyProvinceModifierReconciliationResult SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL Apply(
            SmedleyProvinceModifierReconciliationAuthority authority,
            const SmedleyReconciliationRemovalV1 *removals,
            uint32_t removal_count, SmedleyReconciliationBatchResultV1 *result)
        {
            ReconciliationSlot *slot = nullptr;
            const auto authority_status = CheckAuthority(authority, &slot);
            if (authority_status != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS) return authority_status;
            if (!ValidRecord(result, 1) || removals == nullptr || removal_count == 0
                || removal_count > SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES
                || slot == nullptr || !slot->collected) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
            }
            result->failed_index = 0;
            result->write_count = 0;
            result->verified_count = 0;
            result->deferred_count = 0;
            if (!smedley::IsCurrentExecutableSupported()) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNSUPPORTED;
            }
            uintptr_t base = 0;
            uintptr_t country_database = 0;
            ForeignVector country_vector{};
            uint32_t country_slots = 0;
            if (!NativeBoundary(&base) || !ReadValue(base + country_database_rva, &country_database)
                || country_database == 0
                || !foreign_memory::ReadVector(reinterpret_cast<const void *>(slot->access->game_state().address()),
                    v2_304_layout::game_state_countries_offset,
                    sizeof(uintptr_t), SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES,
                    &country_vector, &country_slots)) {
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            }
            const uint32_t all_mask = (1u << slot->definition_count) - 1;
            for (uint32_t index = 0; index < removal_count; ++index) {
                const auto &request = removals[index];
                result->failed_index = index;
                if (!ValidRecord(&request, 1) || request.remove_modifier_mask == 0
                    || (request.remove_modifier_mask & ~all_mask) != 0
                    || (request.expected_present_modifier_mask & ~all_mask) != 0
                    || request.expected_owner_option_selected > 1) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
                }
                uint32_t id = 0;
                if (!HandleIndex(authority, request.province, slot->provinces.size(), HandleDomain::province, &id)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
                }
                const auto &cached = slot->provinces[id];
                if (cached.address == 0 || request.expected_present_modifier_mask != cached.modifier_mask
                    || request.expected_owner != (cached.owner_index == 0 ? 0
                        : Handle(authority, cached.owner_index, HandleDomain::country))) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                }
                for (uint32_t prior = 0; prior < index; ++prior) {
                    if (removals[prior].province == request.province) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
                    }
                }
                uint32_t current_mask = 0;
                uint32_t guard = 0;
                if (!ModifierMask(cached.address, *slot, &current_mask, &guard)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
                if (guard != 0) {
                    result->deferred_count = 1;
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_DEFERRED;
                }
                if (current_mask != cached.modifier_mask
                    || (current_mask & request.remove_modifier_mask) == 0) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                }
                uint32_t owner_index = 0;
                uintptr_t owner = 0;
                if (!ResolveOwner(country_database, country_vector, country_slots, cached.address, &owner_index, &owner)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                }
                if (owner_index != cached.owner_index || owner != cached.owner) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                }
                if (cached.owner_index == 0) {
                    if (request.expected_owner_option_selected != 0) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                    }
                } else {
                    uint32_t selected = 0;
                    if (!OwnerOptionSelected(cached.owner, slot->owner_group_index, slot->owner_option, &selected)) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                    }
                    if (selected != request.expected_owner_option_selected
                        || selected != slot->countries[cached.owner_index].selected) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                    }
                }
            }

            if (!NativeBoundary(&base)) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
            for (uint32_t index = 0; index < removal_count; ++index) {
                result->failed_index = index;
                uint32_t id = 0;
                HandleIndex(authority, removals[index].province, slot->provinces.size(), HandleDomain::province, &id);
                const auto &cached = slot->provinces[id];
                const uint32_t present_removals = cached.modifier_mask & removals[index].remove_modifier_mask;
                uint32_t expected_mask = cached.modifier_mask;
                __try {
                    for (uint32_t modifier = 0; modifier < slot->definition_count; ++modifier) {
                        if ((present_removals & (1u << modifier)) != 0) {
                            uint32_t current_mask = 0;
                            uint32_t guard = 0;
                            if (!ModifierMask(cached.address, *slot, &current_mask, &guard)
                                || guard != 0 || current_mask != expected_mask) {
                                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                            }
                            CallRemove(base + modifier_remover_rva, reinterpret_cast<void *>(cached.address),
                                reinterpret_cast<void *>(slot->definitions[modifier]));
                            ++result->write_count;
                            expected_mask &= ~(1u << modifier);
                            if (!ModifierMask(cached.address, *slot, &current_mask, &guard)
                                || guard != 0 || current_mask != expected_mask) {
                                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                            }
                            ++result->verified_count;
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                }
                uint32_t current_mask = 0;
                uint32_t guard = 0;
                if (!ModifierMask(cached.address, *slot, &current_mask, &guard)
                    || guard != 0
                    || current_mask != (cached.modifier_mask & ~removals[index].remove_modifier_mask)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                }
                uint32_t owner_index = 0;
                uintptr_t owner = 0;
                uint32_t selected = 0;
                if (!ResolveOwner(country_database, country_vector, country_slots, cached.address, &owner_index, &owner)
                    || owner_index != cached.owner_index || owner != cached.owner
                    || (owner_index != 0 && (!OwnerOptionSelected(owner, slot->owner_group_index,
                            slot->owner_option, &selected)
                        || selected != removals[index].expected_owner_option_selected))) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                }
            }
            result->failed_index = removal_count;
            return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
        }
    }
}

using namespace smedley::game_state::services;

SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EXPORT SmedleyProvinceModifierReconciliationResult
SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL
SmedleyGetProvinceModifierReconciliationApiV1(SmedleyProvinceModifierReconciliationApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api)
        || api->version != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) {
        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT;
    }
    api->register_callback = &RegisterCallback;
    api->unregister_callback = &UnregisterCallback;
    api->collect = &Collect;
    api->apply = &Apply;
    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
}

static_assert(sizeof(void *) == 4, "province modifier reconciliation requires the supported x86 executable");
static_assert(sizeof(SmedleyProvinceModifierReconciliationEventV1) == 32,
    "reconciliation event ABI v1 layout changed");
static_assert(sizeof(SmedleyReconciliationKeyV1) == 88, "reconciliation key ABI v1 layout changed");
static_assert(sizeof(SmedleyReconciliationProvinceSnapshotV1) == 40, "reconciliation province ABI v1 layout changed");
static_assert(sizeof(SmedleyReconciliationCountrySnapshotV1) == 32, "reconciliation country ABI v1 layout changed");
static_assert(sizeof(SmedleyReconciliationRemovalV1) == 48, "reconciliation removal ABI v1 layout changed");
static_assert(sizeof(SmedleyReconciliationBatchResultV1) == 36, "reconciliation result ABI v1 layout changed");
static_assert(sizeof(SmedleyProvinceModifierReconciliationApiV1) == 32, "reconciliation API v1 layout changed");
