#include <smedley/game_state/runtime.hpp>

#include <smedley/events/bankinterest.hpp>
#include <smedley/events/dailyinterest.hpp>
#include <smedley/events/dailyupdate.hpp>
#include <smedley/events/console.hpp>
#include <smedley/event_services_runtime.hpp>
#include <smedley/eventregistry.hpp>
#include <smedley/executable_identity.hpp>
#include <smedley/memory.hpp>
#include <smedley/std/string.hpp>
#include <smedley/std/vector.hpp>
#include <smedley/v2/console.hpp>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

namespace smedley::game_state
{
    namespace
    {
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr std::array<uint8_t, 10> give_money_signature{
            0x55, 0x8b, 0xec, 0x83, 0xb8, 0x84, 0x01, 0x00, 0x00, 0x00,
        };
        constexpr uintptr_t current_game_state_rva = 0x00e588e8;
        constexpr uintptr_t return_country_to_ai_rva = 0x00287a70;
        constexpr std::array<uint8_t, 12> return_country_to_ai_signature{
            0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00,
        };
        constexpr uintptr_t native_tag_handler_rva = 0x0001f720;
        constexpr std::array<uint8_t, 5> native_tag_handler_signature{0x55, 0x8b, 0xec, 0x6a, 0xff};
        constexpr uintptr_t debug_command_handler_rva = 0x00020eb0;
        constexpr std::array<uint8_t, 8> debug_command_handler_signature{
            0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff,
        };
        constexpr uintptr_t fog_enabled_rva = 0x00b092fb;
        constexpr uintptr_t toggle_pause_rva = 0x0026a2c0;
        constexpr std::array<uint8_t, 9> toggle_pause_signature{
            0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00,
        };
        constexpr size_t game_state_idler_offset = 0x0b24;
        constexpr size_t idler_pause_state_offset = 0x1538;
        constexpr size_t idler_quit_requested_offset = 0x1d20;
        constexpr size_t idler_request_quit_slot_offset = 0x110;
        constexpr uintptr_t request_quit_rva = 0x0024edb0;
        constexpr std::array<uint8_t, 8> request_quit_signature{
            0xc6, 0x81, 0x20, 0x1d, 0x00, 0x00, 0x01, 0xc3,
        };
        constexpr uintptr_t speed_up_rva = 0x0032ee90;
        constexpr uintptr_t speed_down_rva = 0x0032efe0;
        constexpr size_t speed_handler_signature_offset = 6;
        constexpr std::array<uint8_t, 10> speed_up_signature{
            0x8b, 0x81, 0x28, 0x0b, 0x00, 0x00, 0x40, 0x83, 0xf8, 0x04,
        };
        constexpr std::array<uint8_t, 10> speed_down_signature{
            0x8b, 0x81, 0x28, 0x0b, 0x00, 0x00, 0x48, 0x83, 0xf8, 0x04,
        };
        constexpr char in_game_idler_type_name[] = ".?AVCInGameIdler@@";
        constexpr size_t pop_money_offset = 0x180;
        constexpr size_t pop_interest_cash_flow_offset = 0x210;
        constexpr size_t pop_total_cash_flow_offset = 0x218;
        constexpr size_t pop_savings_offset = 0x250;
        constexpr size_t bank_owner_offset = 0x08;
        constexpr size_t state_size = 0x290;
        constexpr size_t state_id_offset = 0x0c;
        constexpr size_t state_interest_offset = 0x260;
        constexpr uint32_t max_campaign_states = 4096;
        constexpr size_t pop_money_span = pop_total_cash_flow_offset + sizeof(int64_t) - pop_money_offset;
        constexpr size_t pop_snapshot_span = pop_savings_offset + sizeof(int64_t) - pop_money_offset;
        constexpr size_t pop_interest_identity_capacity = 131072;
        constexpr size_t game_state_country_ais_offset = 0x0a4;
        constexpr size_t game_state_provinces_offset = 0x0acc;
        constexpr size_t game_state_countries_offset = 0x0adc;
        constexpr size_t game_state_player_nations_offset = 0x0aec;
        constexpr size_t game_state_player_tag_offset = 0x0b5c;
        constexpr size_t game_state_date_offset = 0x0b0c;
        constexpr size_t game_state_speed_index_offset = 0x0b28;
        constexpr size_t game_state_wars_offset = 0x0b3c;
        constexpr size_t country_tag_offset = 0x01c;
        constexpr size_t country_ai_offset = 0x208;
        constexpr size_t country_owned_provinces_offset = 0x9d8;
        constexpr size_t country_mobilized_offset = 0x120;
        constexpr size_t country_plurality_offset = 0x1a8;
        constexpr size_t country_diplomatic_points_offset = 0x65c;
        constexpr size_t country_war_exhaustion_offset = 0x680;
        constexpr size_t country_units_offset = 0x7b4;
        constexpr size_t country_leadership_offset = 0x7d0;
        constexpr size_t country_substate_offset = 0xcf4;
        constexpr size_t country_vassal_offset = 0xcf5;
        constexpr size_t country_overlord_offset = 0xcf8;
        constexpr size_t country_vassals_offset = 0xd38;
        constexpr size_t country_allies_offset = 0xd58;
        constexpr size_t country_guaranteed_offset = 0xd78;
        constexpr size_t country_neighbors_offset = 0xd88;
        constexpr size_t country_research_points_offset = 0xe3c;
        constexpr size_t country_treasury_offset = 0xe78;
        constexpr size_t country_prestige_offset = 0xea0;
        constexpr size_t country_ranking_offset = 0x1404;
        constexpr size_t country_spherelings_offset = 0x1418;
        constexpr size_t country_sphere_leader_offset = 0x1428;
        constexpr size_t country_infamy_offset = 0x1430;
        constexpr size_t country_scheduled_mobilizations_offset = 0x15dc;
        constexpr size_t province_id_offset = 0x058;
        constexpr size_t province_constructions_offset = 0x0d8;
        constexpr size_t province_buildings_offset = 0x118;
        constexpr size_t province_owner_offset = 0x128;
        constexpr size_t province_controller_offset = 0x130;
        constexpr size_t province_colonial_level_offset = 0x190;
        constexpr size_t province_life_rating_offset = 0x1a4;
        constexpr size_t province_infrastructure_offset = 0x2b8;
        constexpr uint32_t max_game_provinces = 4096;
        constexpr uint32_t max_game_wars = 4096;
        constexpr uint32_t max_country_units = 100000;
        constexpr uint32_t max_country_relations = 512;
        constexpr uint32_t max_scheduled_mobilizations = 100000;
        constexpr size_t scheduled_mobilization_size = 0x60;
        constexpr uint32_t max_province_building_slots = 64;
        constexpr uint32_t max_province_constructions = 4096;
        constexpr uint32_t max_console_commands = 512;
        std::atomic<uintptr_t> observed_game_state{};
        std::atomic<uint64_t> game_session_epoch{};
        std::atomic<uintptr_t> captured_console{};
        std::atomic<uintptr_t> captured_native_tag_handler{};

        struct PopInterestIdentitySet
        {
            std::array<uintptr_t, pop_interest_identity_capacity> entries{};
            std::array<uint32_t, pop_interest_identity_capacity> generations{};
            uint32_t generation = 0;
        };

        PopInterestIdentitySet pop_interest_identities;
        std::mutex pop_interest_identity_mutex;

        class CampaignConsoleRef
        {
        public:
            explicit CampaignConsoleRef(const void *address = nullptr) noexcept
                : address_(reinterpret_cast<uintptr_t>(address))
            {
            }

            explicit operator bool() const noexcept { return address_ != 0; }
            uintptr_t address() const noexcept { return address_; }

        private:
            uintptr_t address_ = 0;
        };

        struct ForeignVector
        {
            const void *begin;
            const void *end;
            const void *capacity;
        };

        struct ForeignList
        {
            const void *head;
            const void *tail;
            int32_t count;
            uint32_t reserved;
        };

        struct RawObserverTag
        {
            char value[4];
            int32_t ordinal;
        };

        static_assert(sizeof(RawObserverTag) == 8);

        bool IsAccessible(const void *pointer, size_t size, bool writable)
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
                const DWORD allowed = writable
                    ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
                    : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                    || (region.Protect & allowed) == 0 || region_end <= cursor) return false;
                cursor = (std::min)(end, region_end);
            }
            return true;
        }

        struct MemoryRegionCache
        {
            uintptr_t begin = 0;
            uintptr_t end = 0;
            DWORD protect = 0;
            DWORD state = 0;
        };

        bool IsAccessibleCached(const void *pointer, size_t size, bool writable, MemoryRegionCache *cache)
        {
            if (pointer == nullptr || size == 0 || cache == nullptr) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            for (uintptr_t cursor = begin; cursor < end;) {
                if (cursor < cache->begin || cursor >= cache->end) {
                    MEMORY_BASIC_INFORMATION region{};
                    if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)) {
                        return false;
                    }
                    const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                    if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                    cache->begin = region_begin;
                    cache->end = region_begin + region.RegionSize;
                    cache->protect = region.Protect;
                    cache->state = region.State;
                }
                const DWORD allowed = writable
                    ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
                    : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if (cache->state != MEM_COMMIT || (cache->protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                    || (cache->protect & allowed) == 0 || cache->end <= cursor) return false;
                cursor = (std::min)(end, cache->end);
            }
            return true;
        }

        bool CopyPopMoneyFields(PopRef pop, PopMoneySnapshot *snapshot)
        {
            if (!pop || snapshot == nullptr) return false;
            const auto *address = reinterpret_cast<const uint8_t *>(pop.address());
            __try {
                std::memcpy(&snapshot->money_raw, address + pop_money_offset, sizeof(snapshot->money_raw));
                std::memcpy(&snapshot->interest_cash_flow_raw,
                    address + pop_interest_cash_flow_offset, sizeof(snapshot->interest_cash_flow_raw));
                std::memcpy(&snapshot->total_cash_flow_raw,
                    address + pop_total_cash_flow_offset, sizeof(snapshot->total_cash_flow_raw));
                std::memcpy(&snapshot->savings_raw, address + pop_savings_offset, sizeof(snapshot->savings_raw));
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void BeginPopInterestIdentitySet()
        {
            ++pop_interest_identities.generation;
            if (pop_interest_identities.generation == 0) {
                pop_interest_identities.generations.fill(0);
                pop_interest_identities.generation = 1;
            }
        }

        bool InsertPopInterestIdentity(uintptr_t address, size_t capacity)
        {
            const size_t mask = capacity - 1;
            size_t slot = static_cast<size_t>((address >> 4) * uintptr_t{2654435761u}) & mask;
            for (size_t attempt = 0; attempt < capacity; ++attempt) {
                if (pop_interest_identities.generations[slot] != pop_interest_identities.generation) {
                    pop_interest_identities.generations[slot] = pop_interest_identities.generation;
                    pop_interest_identities.entries[slot] = address;
                    return true;
                }
                if (pop_interest_identities.entries[slot] == address) return false;
                slot = (slot + 1) & mask;
            }
            return false;
        }

        bool CopyReadable(void *destination, const void *source, size_t size)
        {
            if (!IsAccessible(source, size, false)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool CopyWritable(void *destination, const void *source, size_t size)
        {
            if (!IsAccessible(destination, size, true)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        template <typename T>
        bool ReadValue(uintptr_t address, T *value)
        {
            return value != nullptr && CopyReadable(value, reinterpret_cast<const void *>(address), sizeof(T));
        }

        bool AddOffset(uintptr_t address, size_t offset, uintptr_t *result)
        {
            if (result == nullptr || address > (std::numeric_limits<uintptr_t>::max)() - offset) return false;
            *result = address + offset;
            return true;
        }

        template <typename T>
        bool ReadField(const void *object, size_t offset, T *value)
        {
            uintptr_t address = 0;
            return object != nullptr && AddOffset(reinterpret_cast<uintptr_t>(object), offset, &address)
                && ReadValue(address, value);
        }

        bool ReadTag(const void *object, size_t offset, TelemetryTag *tag)
        {
            char value[4]{};
            if (tag == nullptr || !ReadField(object, offset, &value) || value[3] != '\0') return false;
            if (!((value[0] == '-' && value[1] == '-' && value[2] == '-')
                    || ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= '0' && value[0] <= '9'))
                    && ((value[1] >= 'A' && value[1] <= 'Z') || (value[1] >= '0' && value[1] <= '9'))
                    && ((value[2] >= 'A' && value[2] <= 'Z') || (value[2] >= '0' && value[2] <= '9')))) return false;
            std::memcpy(tag->value, value, sizeof(value));
            return true;
        }

        bool ReadVectorCount(const void *object, size_t offset, size_t element_size, uint32_t limit, uint32_t *count)
        {
            ForeignVector vector{};
            if (count == nullptr || element_size == 0 || !ReadField(object, offset, &vector)) return false;
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
            if (elements > limit || (elements != 0 && !IsAccessible(vector.begin, static_cast<size_t>(end - begin), false))) {
                return false;
            }
            *count = static_cast<uint32_t>(elements);
            return true;
        }

        bool ReadVector(const void *object, size_t offset, size_t element_size, uint32_t limit, ForeignVector *vector,
                        uint32_t *count)
        {
            if (vector == nullptr || count == nullptr || !ReadField(object, offset, vector)) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(vector->begin);
            const uintptr_t end = reinterpret_cast<uintptr_t>(vector->end);
            const uintptr_t capacity = reinterpret_cast<uintptr_t>(vector->capacity);
            if (begin == 0 && end == 0 && capacity == 0) {
                *count = 0;
                return true;
            }
            if (element_size == 0 || begin == 0 || begin > end || end > capacity || begin % alignof(void *) != 0
                || end % alignof(void *) != 0 || capacity % alignof(void *) != 0
                || (end - begin) % element_size != 0 || (capacity - begin) % element_size != 0) return false;
            const uintptr_t elements = (end - begin) / element_size;
            if (elements > limit || (elements != 0 && !IsAccessible(vector->begin, static_cast<size_t>(end - begin), false))) {
                return false;
            }
            *count = static_cast<uint32_t>(elements);
            return true;
        }

        bool ReadExpectedCString(const char *address, const char *expected)
        {
            if (address == nullptr || expected == nullptr) return false;
            const size_t size = std::strlen(expected) + 1;
            if (size > 128) return false;
            std::array<char, 128> copy{};
            return CopyReadable(copy.data(), address, size) && std::memcmp(copy.data(), expected, size) == 0;
        }

        bool ReadRawObserverTag(const void *object, size_t offset, ObserverTag *tag)
        {
            RawObserverTag raw{};
            if (tag == nullptr || !ReadField(object, offset, &raw) || raw.ordinal < 0 || raw.ordinal >= max_game_countries
                || raw.value[3] != '\0') return false;
            for (const char value : {raw.value[0], raw.value[1], raw.value[2]}) {
                if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) return false;
            }
            std::memcpy(tag->value, raw.value, sizeof(tag->value));
            tag->ordinal = raw.ordinal;
            return true;
        }

        bool ObserverTagMatches(const ObserverTag &left, const ObserverTag &right)
        {
            return left.ordinal == right.ordinal && std::memcmp(left.value, right.value, sizeof(left.value)) == 0;
        }

        bool NativeSignatureMatches(uintptr_t rva, const uint8_t *signature, size_t size)
        {
            const uintptr_t module = smedley::memory::Map::base_addr;
            uintptr_t address = 0;
            return smedley::IsCurrentExecutableSupported() && module != 0 && signature != nullptr
                && AddOffset(module, rva, &address)
                && smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, signature, size);
        }

        bool ReadConsoleCommand(CampaignConsoleRef console, const char *name,
                                smedley::v2::CConsoleCmd::SCommandData *command, uintptr_t *address)
        {
            ForeignVector commands{};
            uint32_t count = 0;
            if (!console || name == nullptr || command == nullptr || address == nullptr
                || !ReadVector(reinterpret_cast<const void *>(console.address()), 0,
                    sizeof(smedley::v2::CConsoleCmd::SCommandData *), max_console_commands, &commands, &count)) {
                return false;
            }
            for (uint32_t index = 0; index < count; ++index) {
                uintptr_t candidate = 0;
                if (!ReadValue(reinterpret_cast<uintptr_t>(commands.begin)
                        + index * sizeof(smedley::v2::CConsoleCmd::SCommandData *), &candidate)
                    || candidate == 0 || !ReadValue(candidate, command)) continue;
                if (ReadExpectedCString(command->name, name)) {
                    *address = candidate;
                    return true;
                }
            }
            return false;
        }

        class InlineConsoleString final : public smedley::sstd::string
        {
        public:
            bool Assign(const char *value)
            {
                if (value == nullptr) return false;
                const size_t size = std::strlen(value);
                if (size > default_capacity) return false;
                std::fill(std::begin(_impl.buf), std::end(_impl.buf), '\0');
                std::memcpy(_impl.buf, value, size);
                _size = size;
                _capacity = default_capacity;
                return true;
            }
        };

        class SingleConsoleArgument final : public smedley::sstd::vector<smedley::sstd::string>
        {
        public:
            explicit SingleConsoleArgument(const char *value)
            {
                if (!argument_.Assign(value)) return;
                _first = &argument_;
                _last = _first + 1;
                _end = _last;
                valid_ = true;
            }

            bool valid() const noexcept { return valid_; }

        private:
            InlineConsoleString argument_;
            bool valid_ = false;
        };

        bool CopyConsoleResult(const smedley::v2::CConsoleCmd::SResult &native, CampaignConsoleCommandResult *result)
        {
            if (result == nullptr) return native.success;
            result->success = native.success;
            struct RawEngineString
            {
                union {
                    char buffer[16];
                    const char *pointer;
                } storage;
                uint32_t size;
                uint32_t capacity;
                uint32_t allocator;
            } message{};
            static_assert(sizeof(message) == sizeof(native.message));
            if (!CopyReadable(&message, &native.message, sizeof(message)) || message.size >= sizeof(result->message)
                || message.capacity < message.size || message.capacity < 0xf) return native.success;
            const char *source = message.capacity > 0xf ? message.storage.pointer : message.storage.buffer;
            if (!CopyReadable(result->message, source, message.size)) return native.success;
            result->message[message.size] = '\0';
            result->message_available = true;
            return native.success;
        }

        bool InvokeConsoleHandler(uintptr_t handler, const char *argument, CampaignConsoleCommandResult *result)
        {
            if (handler == 0 || argument == nullptr) return false;
            SingleConsoleArgument arguments(argument);
            if (!arguments.valid()) return false;
            using Handler = smedley::v2::CConsoleCmd::SCommandData::Handler;
            __try {
                const auto native = reinterpret_cast<Handler>(handler)(arguments);
                return CopyConsoleResult(native, result);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool ReadListCount(const void *object, size_t offset, uint32_t limit, uint32_t *count)
        {
            ForeignList list{};
            if (count == nullptr || !ReadField(object, offset, &list) || list.count < 0
                || static_cast<uint32_t>(list.count) > limit
                || (list.count == 0 && (list.head != nullptr || list.tail != nullptr))
                || (list.count != 0 && (list.head == nullptr || list.tail == nullptr))) return false;
            if (list.count != 0 && (!IsAccessible(list.head, sizeof(uintptr_t), false)
                    || !IsAccessible(list.tail, sizeof(uintptr_t), false))) return false;
            *count = static_cast<uint32_t>(list.count);
            return true;
        }

        GameStateRef ReadCurrentGameStateRef()
        {
            const uintptr_t module = smedley::memory::Map::base_addr;
            uintptr_t address = 0;
            const void *game_state = nullptr;
            if (module == 0 || !AddOffset(module, current_game_state_rva, &address)
                || !ReadValue(address, &game_state) || game_state == nullptr) return {};
            return GameStateRef{static_cast<const void *>(game_state)};
        }

        bool ReadCurrentIdler(void **idler)
        {
            if (idler == nullptr) return false;
            *idler = nullptr;
            const uintptr_t module = smedley::memory::Map::base_addr;
            uintptr_t game_state_address = 0;
            if (module == 0 || !AddOffset(module, current_game_state_rva, &game_state_address)) return false;
            void *game_state = nullptr;
            if (!ReadValue(game_state_address, &game_state) || game_state == nullptr) return false;
            uintptr_t idler_address = 0;
            return AddOffset(reinterpret_cast<uintptr_t>(game_state), game_state_idler_offset, &idler_address)
                && ReadValue(idler_address, idler) && *idler != nullptr;
        }

        bool IsInGameIdler(const void *object)
        {
            uintptr_t vtable = 0;
            uintptr_t locator = 0;
            uintptr_t type_descriptor = 0;
            std::array<char, sizeof(in_game_idler_type_name)> type_name{};
            const uintptr_t object_address = reinterpret_cast<uintptr_t>(object);
            uintptr_t locator_address = 0;
            uintptr_t type_name_address = 0;
            if (!ReadValue(object_address, &vtable) || vtable < sizeof(uintptr_t)
                || !ReadValue(vtable - sizeof(uintptr_t), &locator)
                || !AddOffset(locator, 0x0c, &locator_address)
                || !ReadValue(locator_address, &type_descriptor)
                || !AddOffset(type_descriptor, 0x08, &type_name_address)
                || !CopyReadable(type_name.data(), reinterpret_cast<const void *>(type_name_address), type_name.size())) {
                return false;
            }
            return std::memcmp(type_name.data(), in_game_idler_type_name, sizeof(in_game_idler_type_name)) == 0;
        }

        bool PauseSignatureMatches()
        {
            if (!smedley::IsCurrentExecutableSupported()) return false;
            const uintptr_t module = smedley::memory::Map::base_addr;
            uintptr_t address = 0;
            std::array<uint8_t, toggle_pause_signature.size()> bytes{};
            return module != 0 && AddOffset(module, toggle_pause_rva, &address)
                && CopyReadable(bytes.data(), reinterpret_cast<const void *>(address), bytes.size())
                && std::memcmp(bytes.data(), toggle_pause_signature.data(), bytes.size()) == 0;
        }

        bool SpeedSignaturesMatch()
        {
            if (!smedley::IsCurrentExecutableSupported()) return false;
            const uintptr_t module = smedley::memory::Map::base_addr;
            uintptr_t up = 0;
            uintptr_t down = 0;
            std::array<uint8_t, speed_up_signature.size()> up_bytes{};
            std::array<uint8_t, speed_down_signature.size()> down_bytes{};
            return module != 0
                && AddOffset(module, speed_up_rva + speed_handler_signature_offset, &up)
                && AddOffset(module, speed_down_rva + speed_handler_signature_offset, &down)
                && CopyReadable(up_bytes.data(), reinterpret_cast<const void *>(up), up_bytes.size())
                && CopyReadable(down_bytes.data(), reinterpret_cast<const void *>(down), down_bytes.size())
                && std::memcmp(up_bytes.data(), speed_up_signature.data(), up_bytes.size()) == 0
                && std::memcmp(down_bytes.data(), speed_down_signature.data(), down_bytes.size()) == 0;
        }

        bool ReadPauseState(const void *idler, uint8_t *state)
        {
            uintptr_t address = 0;
            return AddOffset(reinterpret_cast<uintptr_t>(idler), idler_pause_state_offset, &address)
                && ReadValue(address, state);
        }

        bool TogglePauseVerified(void *idler)
        {
            using TogglePauseFn = void (__thiscall *)(void *);
            uintptr_t address = 0;
            if (!AddOffset(smedley::memory::Map::base_addr, toggle_pause_rva, &address)) return false;
            const auto function = reinterpret_cast<TogglePauseFn>(address);
            function(idler);
            return true;
        }

        bool SetSpeedVerified(bool increase)
        {
            using SetSpeedFn = void (__cdecl *)();
            uintptr_t address = 0;
            if (!AddOffset(smedley::memory::Map::base_addr, increase ? speed_up_rva : speed_down_rva, &address)) {
                return false;
            }
            __try {
                reinterpret_cast<SetSpeedFn>(address)();
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool RequestQuitVerified(void *idler)
        {
            using RequestQuitFn = void (__thiscall *)(void *);
            uintptr_t address = 0;
            if (!AddOffset(smedley::memory::Map::base_addr, request_quit_rva, &address)) return false;
            __try {
                reinterpret_cast<RequestQuitFn>(address)(idler);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool CanAdd(int64_t value, int64_t amount)
        {
            return amount > 0 && value <= (std::numeric_limits<int64_t>::max)() - amount;
        }

        PopInterestMutationStatus VerifySignature(PopInterestMutationStatus *status)
        {
            if (!smedley::IsCurrentExecutableSupported()) {
                return *status = PopInterestMutationStatus::unavailable;
            }
            const uintptr_t module = smedley::memory::Map::base_addr;
            if (module == 0 || module > (std::numeric_limits<uintptr_t>::max)() - give_money_rva) {
                return *status = PopInterestMutationStatus::unavailable;
            }
            std::array<uint8_t, give_money_signature.size()> bytes{};
            if (!CopyReadable(bytes.data(), reinterpret_cast<const void *>(module + give_money_rva), bytes.size())) {
                *status = PopInterestMutationStatus::unavailable;
            } else if (!smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                           module + give_money_rva, give_money_signature.data(), give_money_signature.size())) {
                *status = PopInterestMutationStatus::signature_mismatch;
            } else {
                *status = PopInterestMutationStatus::success;
            }
            return *status;
        }

        void GiveMoneyVerified(PopRef pop, int64_t amount)
        {
            const uintptr_t function = smedley::memory::Map::base_addr + give_money_rva;
            const uint32_t amount_low = static_cast<uint32_t>(amount);
            const uint32_t amount_high = static_cast<uint32_t>(static_cast<uint64_t>(amount) >> 32);
            const void *pop_address = reinterpret_cast<const void *>(pop.address());
            __asm {
                push esi
                mov eax, pop_address
                mov esi, 7
                push amount_high
                push amount_low
                call function
                pop esi
            }
        }

        bool ReadObserverCountryInternal(const GameStateRef game_state, int32_t ordinal,
                                         ObserverCountrySnapshot *snapshot)
        {
            if (!game_state || snapshot == nullptr || ordinal < 0 || ordinal >= max_game_countries) return false;
            const void *state = reinterpret_cast<const void *>(game_state.address());
            ForeignVector countries{};
            ForeignVector controls{};
            ForeignVector scheduled_ais{};
            uint32_t country_count = 0;
            uint32_t control_count = 0;
            uint32_t ai_count = 0;
            if (!ReadVector(state, game_state_countries_offset, sizeof(void *), max_game_countries, &countries, &country_count)
                || !ReadVector(state, game_state_player_nations_offset, sizeof(int32_t), max_game_countries, &controls, &control_count)
                || !ReadVector(state, game_state_country_ais_offset, sizeof(void *), max_game_countries, &scheduled_ais, &ai_count)
                || country_count != control_count || ordinal >= static_cast<int32_t>(country_count)) return false;
            uintptr_t country = 0;
            int32_t control = 0;
            if (!ReadValue(reinterpret_cast<uintptr_t>(countries.begin) + ordinal * sizeof(country), &country)
                || !ReadValue(reinterpret_cast<uintptr_t>(controls.begin) + ordinal * sizeof(control), &control)
                || country == 0) return false;
            ObserverCountrySnapshot value{};
            if (!ReadRawObserverTag(reinterpret_cast<const void *>(country), country_tag_offset, &value.tag)
                || value.tag.ordinal != ordinal
                || !ReadVectorCount(reinterpret_cast<const void *>(country), country_owned_provinces_offset,
                    sizeof(int32_t), max_game_provinces, &country_count)) return false;
            value.exists = country_count != 0;
            value.human_controlled = control != 0;
            uintptr_t ai = 0;
            if (!ReadField(reinterpret_cast<const void *>(country), country_ai_offset, &ai)) return false;
            value.has_ai = ai != 0;
            for (uint32_t index = 0; index < ai_count && ai != 0; ++index) {
                uintptr_t candidate = 0;
                if (!ReadValue(reinterpret_cast<uintptr_t>(scheduled_ais.begin) + index * sizeof(candidate), &candidate)) return false;
                value.ai_scheduled = candidate == ai;
                if (value.ai_scheduled) break;
            }
            *snapshot = value;
            return true;
        }

        bool ReadObserverStateInternal(const GameStateRef game_state, ObserverStateSnapshot *snapshot)
        {
            if (!game_state || snapshot == nullptr) return false;
            const void *state = reinterpret_cast<const void *>(game_state.address());
            ForeignVector countries{};
            ForeignVector controls{};
            ForeignVector scheduled_ais{};
            uint32_t country_count = 0;
            uint32_t control_count = 0;
            uint32_t ai_count = 0;
            ObserverTag view_tag{};
            uint8_t fog_enabled = 0;
            uintptr_t fog_address = 0;
            if (!ReadVector(state, game_state_countries_offset, sizeof(void *), max_game_countries, &countries, &country_count)
                || !ReadVector(state, game_state_player_nations_offset, sizeof(int32_t), max_game_countries, &controls, &control_count)
                || !ReadVector(state, game_state_country_ais_offset, sizeof(void *), max_game_countries, &scheduled_ais, &ai_count)
                || country_count != control_count || !ReadRawObserverTag(state, game_state_player_tag_offset, &view_tag)
                || view_tag.ordinal >= static_cast<int32_t>(country_count)
                || !AddOffset(smedley::memory::Map::base_addr, fog_enabled_rva, &fog_address)
                || !ReadValue(fog_address, &fog_enabled)) return false;
            ObserverStateSnapshot value{};
            value.country_count = country_count;
            value.country_ai_count = ai_count;
            value.full_map_visibility_enabled = fog_enabled == 0;
            for (uint32_t index = 0; index < control_count; ++index) {
                int32_t control = 0;
                if (!ReadValue(reinterpret_cast<uintptr_t>(controls.begin) + index * sizeof(control), &control)) return false;
                value.human_control_present = value.human_control_present || control != 0;
            }
            if (!ReadObserverCountryInternal(game_state, view_tag.ordinal, &value.view_country)
                || !ObserverTagMatches(value.view_country.tag, view_tag)) return false;
            *snapshot = value;
            return true;
        }

        constexpr uintptr_t frontend_constructor_rva = 0x36a2f0;
        constexpr uintptr_t main_menu_constructor_rva = 0x354a00;
        constexpr uintptr_t frontend_destructor_rva = 0x36b030;
        constexpr uintptr_t main_menu_destructor_rva = 0x354df0;
        constexpr uintptr_t signal_press_rva = 0x5ee510;
        constexpr uintptr_t signal_release_rva = 0x5ee550;
        constexpr uintptr_t load_save_rva = 0x27f1d0;
        constexpr size_t frontend_gui_offset = 0x278;
        constexpr size_t main_menu_gui_offset = 0x704;
        constexpr size_t selected_save_offset = 0x590;
        constexpr size_t save_request_offset = 0x5bc;
        constexpr size_t save_complete_offset = 0x5bd;
        constexpr size_t control_signal_offset = 0x54;
        constexpr uintptr_t frontend_vtable_rva = 0xa14ed0;
        constexpr uintptr_t main_menu_vtable_rva = 0xa13dbc;
        constexpr size_t maximum_save_basename = 259;
        static_assert(save_complete_offset == save_request_offset + 1);

        struct CapturedFrontendController
        {
            std::atomic<uintptr_t> address{};
            std::atomic<DWORD> thread_id{};
            std::atomic<uint64_t> generation{1};
        };

        CapturedFrontendController captured_frontend{};
        CapturedFrontendController captured_main_menu{};
        std::atomic<bool> frontend_automation_active{};
        std::atomic<bool> legacy_frontend_automation_active{};
        std::atomic<bool> campaign_automation_frontend_active{};
        std::atomic<bool> frontend_hooks_installed{};
        std::atomic<bool> frontend_hooks_poisoned{};
        std::atomic<FrontendControllerCaptureCallback> frontend_capture_callback{};
        std::atomic<FrontendControllerCaptureCallback> campaign_automation_frontend_capture_callback{};
        std::vector<uintptr_t> frontend_installed_hooks;
        constexpr std::array<uint8_t, 5> load_save_signature{0x55, 0x8b, 0xec, 0x6a, 0xff};
        void *frontend_constructor_original = nullptr;
        void *main_menu_constructor_original = nullptr;
        void *frontend_destructor_original = nullptr;
        void *main_menu_destructor_original = nullptr;

        void RefreshFrontendAutomationActive() noexcept
        {
            frontend_automation_active.store(
                legacy_frontend_automation_active.load(std::memory_order_acquire)
                    || campaign_automation_frontend_active.load(std::memory_order_acquire),
                std::memory_order_release);
        }

        CapturedFrontendController &CapturedController(FrontendControllerKind kind)
        {
            return kind == FrontendControllerKind::frontend ? captured_frontend : captured_main_menu;
        }

        void CaptureFrontendController(FrontendControllerKind kind, void *controller) noexcept
        {
            if (!frontend_automation_active.load(std::memory_order_acquire) || controller == nullptr) return;
            auto &capture = CapturedController(kind);
            capture.address.store(reinterpret_cast<uintptr_t>(controller), std::memory_order_release);
            capture.thread_id.store(GetCurrentThreadId(), std::memory_order_release);
            capture.generation.fetch_add(1, std::memory_order_acq_rel);
            const auto callback = frontend_capture_callback.load(std::memory_order_acquire);
            if (callback != nullptr) callback(kind);
            const auto automation_callback = campaign_automation_frontend_capture_callback.load(std::memory_order_acquire);
            if (automation_callback != nullptr) automation_callback(kind);
        }

        void ReleaseCapturedFrontendController(FrontendControllerKind kind, void *controller) noexcept
        {
            auto &capture = CapturedController(kind);
            uintptr_t expected = reinterpret_cast<uintptr_t>(controller);
            if (expected != 0 && capture.address.compare_exchange_strong(
                    expected, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
                capture.thread_id.store(0, std::memory_order_release);
                capture.generation.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        void __stdcall OnFrontendConstructed(void *controller) noexcept
        {
            CaptureFrontendController(FrontendControllerKind::frontend, controller);
        }

        void __stdcall OnMainMenuConstructed(void *controller) noexcept
        {
            CaptureFrontendController(FrontendControllerKind::main_menu, controller);
        }

        void __stdcall OnFrontendDestroyed(void *controller) noexcept
        {
            ReleaseCapturedFrontendController(FrontendControllerKind::frontend, controller);
        }

        void __stdcall OnMainMenuDestroyed(void *controller) noexcept
        {
            ReleaseCapturedFrontendController(FrontendControllerKind::main_menu, controller);
        }

        __declspec(naked) void FrontendConstructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call OnFrontendConstructed
                popad
                popfd
                jmp dword ptr [frontend_constructor_original]
            }
        }

        __declspec(naked) void MainMenuConstructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call OnMainMenuConstructed
                popad
                popfd
                jmp dword ptr [main_menu_constructor_original]
            }
        }

        __declspec(naked) void FrontendDestructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                push ecx
                call OnFrontendDestroyed
                popad
                popfd
                jmp dword ptr [frontend_destructor_original]
            }
        }

        __declspec(naked) void MainMenuDestructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                push ecx
                call OnMainMenuDestroyed
                popad
                popfd
                jmp dword ptr [main_menu_destructor_original]
            }
        }

        bool ReadVirtualTarget(const void *object, size_t slot_offset, uintptr_t *target)
        {
            uintptr_t vtable = 0;
            uintptr_t slot = 0;
            MODULEINFO module{};
            if (!ReadValue(reinterpret_cast<uintptr_t>(object), &vtable) || vtable == 0
                || !AddOffset(vtable, slot_offset, &slot) || !ReadValue(slot, target) || *target == 0
                || !GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &module, sizeof(module))) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(module.lpBaseOfDll);
            const uintptr_t end = begin + module.SizeOfImage;
            MEMORY_BASIC_INFORMATION region{};
            return *target >= begin && *target < end
                && VirtualQuery(reinterpret_cast<const void *>(*target), &region, sizeof(region)) == sizeof(region)
                && region.State == MEM_COMMIT
                && (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0
                && (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
        }

        bool ControllerVtableMatches(const void *controller, uintptr_t expected_rva)
        {
            uintptr_t vtable = 0;
            return ReadValue(reinterpret_cast<uintptr_t>(controller), &vtable)
                && vtable == smedley::memory::Map::base_addr + expected_rva;
        }

        class EngineStringArgument final : public smedley::sstd::string
        {
        public:
            explicit EngineStringArgument(std::string_view value)
            {
                _size = value.size();
                if (value.size() <= default_capacity) {
                    std::fill(std::begin(_impl.buf), std::end(_impl.buf), '\0');
                    std::memcpy(_impl.buf, value.data(), value.size());
                    _capacity = default_capacity;
                } else {
                    _impl.ptr = const_cast<char *>(value.data());
                    _capacity = value.size();
                }
            }
        };

        struct RawFrontendString
        {
            union { char buffer[16]; const char *pointer; } storage;
            uint32_t size;
            uint32_t capacity;
            uint32_t allocator;
        };
        static_assert(sizeof(RawFrontendString) == sizeof(smedley::sstd::string));

        bool ReadFrontendString(const void *address, std::string *value, RawFrontendString *metadata = nullptr)
        {
            RawFrontendString snapshot{};
            if (value == nullptr || !CopyReadable(&snapshot, address, sizeof(snapshot))
                || snapshot.size > maximum_save_basename || snapshot.capacity < snapshot.size || snapshot.capacity < 0xf) return false;
            const char *source = snapshot.capacity > 0xf ? snapshot.storage.pointer : snapshot.storage.buffer;
            std::string copy(snapshot.size, '\0');
            char terminator = 0;
            if ((snapshot.size != 0 && !CopyReadable(copy.data(), source, snapshot.size))
                || !ReadValue(reinterpret_cast<uintptr_t>(source) + snapshot.size, &terminator) || terminator != '\0') return false;
            *value = std::move(copy);
            if (metadata != nullptr) *metadata = snapshot;
            return true;
        }

        void *InvokeFindControl(void *object, uintptr_t target, const smedley::sstd::string *name)
        {
            using FindControl = void *(__thiscall *)(void *, const smedley::sstd::string *);
            __try {
                return reinterpret_cast<FindControl>(target)(object, name);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
        }

        bool DispatchNativeSignal(void *signal)
        {
            uintptr_t press = 0;
            uintptr_t release = 0;
            if (!IsAccessible(signal, sizeof(uintptr_t), true)
                || !AddOffset(smedley::memory::Map::base_addr, signal_press_rva, &press)
                || !AddOffset(smedley::memory::Map::base_addr, signal_release_rva, &release)) return false;
            __try {
                __asm mov eax, signal
                __asm call press
                __asm mov eax, signal
                __asm call release
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        FrontendOperationStatus ResolveController(uint64_t token_generation, FrontendControllerKind expected,
                                                  void **controller)
        {
            if (controller == nullptr || token_generation == 0) return FrontendOperationStatus::invalid_token;
            if (!frontend_automation_active.load(std::memory_order_acquire)) return FrontendOperationStatus::unavailable;
            auto &capture = CapturedController(expected);
            const uint64_t generation = capture.generation.load(std::memory_order_acquire);
            const uintptr_t address = capture.address.load(std::memory_order_acquire);
            const DWORD thread_id = capture.thread_id.load(std::memory_order_acquire);
            if (generation != token_generation || address == 0 || thread_id == 0) return FrontendOperationStatus::invalid_token;
            if (thread_id != GetCurrentThreadId()) return FrontendOperationStatus::invalid_thread;
            if (capture.generation.load(std::memory_order_acquire) != generation
                || capture.address.load(std::memory_order_acquire) != address) return FrontendOperationStatus::invalid_token;
            *controller = reinterpret_cast<void *>(address);
            return FrontendOperationStatus::completed;
        }
    }

    FrontendOperationStatus InstallFrontendAutomationHooks()
    {
        constexpr std::array<uint8_t, 5> constructor_signature{0x55, 0x8b, 0xec, 0x6a, 0xff};
        constexpr std::array<uint8_t, 6> destructor_signature{0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1};
        constexpr std::array<uint8_t, 10> signal_signature{0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
        if (frontend_hooks_poisoned.load(std::memory_order_acquire)) return FrontendOperationStatus::readback_failed;
        if (frontend_hooks_installed.load(std::memory_order_acquire)) {
            legacy_frontend_automation_active.store(true, std::memory_order_release);
            RefreshFrontendAutomationActive();
            return FrontendOperationStatus::completed;
        }
        if (!smedley::IsCurrentExecutableSupported()) return FrontendOperationStatus::signature_mismatch;
        uintptr_t frontend_constructor = 0, main_menu_constructor = 0, frontend_destructor = 0, main_menu_destructor = 0;
        uintptr_t press = 0, release = 0, load_save = 0;
        if (!AddOffset(smedley::memory::Map::base_addr, frontend_constructor_rva, &frontend_constructor)
            || !AddOffset(smedley::memory::Map::base_addr, main_menu_constructor_rva, &main_menu_constructor)
            || !AddOffset(smedley::memory::Map::base_addr, frontend_destructor_rva, &frontend_destructor)
            || !AddOffset(smedley::memory::Map::base_addr, main_menu_destructor_rva, &main_menu_destructor)
            || !AddOffset(smedley::memory::Map::base_addr, signal_press_rva, &press)
            || !AddOffset(smedley::memory::Map::base_addr, signal_release_rva, &release)
            || !AddOffset(smedley::memory::Map::base_addr, load_save_rva, &load_save)
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                frontend_constructor, constructor_signature.data(), constructor_signature.size())
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                main_menu_constructor, constructor_signature.data(), constructor_signature.size())
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                frontend_destructor, destructor_signature.data(), destructor_signature.size())
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                main_menu_destructor, destructor_signature.data(), destructor_signature.size())
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(press, signal_signature.data(), signal_signature.size())
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(release, signal_signature.data(), signal_signature.size())) {
            return FrontendOperationStatus::signature_mismatch;
        }
        if (!smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                load_save, load_save_signature.data(), load_save_signature.size())) {
            return FrontendOperationStatus::signature_mismatch;
        }
        std::vector<uintptr_t> installed;
        const auto install = [&installed](uintptr_t address, void *detour, void **original) {
            if (!smedley::memory::InstallDetour(address, detour, original)) {
                throw std::runtime_error("MinHook could not install frontend detour");
            }
            installed.push_back(address);
        };
        try {
            installed.reserve(4);
            install(frontend_constructor, reinterpret_cast<void *>(&FrontendConstructorTrampoline), &frontend_constructor_original);
            install(main_menu_constructor, reinterpret_cast<void *>(&MainMenuConstructorTrampoline), &main_menu_constructor_original);
            install(frontend_destructor, reinterpret_cast<void *>(&FrontendDestructorTrampoline), &frontend_destructor_original);
            install(main_menu_destructor, reinterpret_cast<void *>(&MainMenuDestructorTrampoline), &main_menu_destructor_original);
        } catch (...) {
            if (!installed.empty()) {
                frontend_installed_hooks = std::move(installed);
                frontend_hooks_poisoned.store(true, std::memory_order_release);
                return FrontendOperationStatus::readback_failed;
            }
            return FrontendOperationStatus::unavailable;
        }
        frontend_installed_hooks = std::move(installed);
        frontend_hooks_installed.store(true, std::memory_order_release);
        legacy_frontend_automation_active.store(true, std::memory_order_release);
        RefreshFrontendAutomationActive();
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus RollbackFrontendAutomationHooks()
    {
        legacy_frontend_automation_active.store(false, std::memory_order_release);
        frontend_capture_callback.store(nullptr, std::memory_order_release);
        RefreshFrontendAutomationActive();
        if (frontend_hooks_poisoned.load(std::memory_order_acquire)) return FrontendOperationStatus::readback_failed;
        // Published MinHook trampolines remain process-lifetime so an in-flight
        // capture cannot tail-jump through freed executable memory.
        return FrontendOperationStatus::completed;
    }

    void DeactivateFrontendAutomation() noexcept
    {
        legacy_frontend_automation_active.store(false, std::memory_order_release);
        frontend_capture_callback.store(nullptr, std::memory_order_release);
        RefreshFrontendAutomationActive();
        if (!campaign_automation_frontend_active.load(std::memory_order_acquire)) {
            ReleaseCapturedFrontendController(FrontendControllerKind::frontend,
                reinterpret_cast<void *>(captured_frontend.address.load(std::memory_order_acquire)));
            ReleaseCapturedFrontendController(FrontendControllerKind::main_menu,
                reinterpret_cast<void *>(captured_main_menu.address.load(std::memory_order_acquire)));
        }
    }

    FrontendOperationStatus SetFrontendControllerCaptureCallback(FrontendControllerCaptureCallback callback)
    {
        if (callback == nullptr || !frontend_automation_active.load(std::memory_order_acquire)) {
            return FrontendOperationStatus::unavailable;
        }
        frontend_capture_callback.store(callback, std::memory_order_release);
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus SetCampaignAutomationFrontendCaptureCallback(FrontendControllerCaptureCallback callback)
    {
        if (!frontend_automation_active.load(std::memory_order_acquire)) return FrontendOperationStatus::unavailable;
        campaign_automation_frontend_capture_callback.store(callback, std::memory_order_release);
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus ActivateCampaignAutomationFrontend()
    {
        const bool legacy_was_active = legacy_frontend_automation_active.load(std::memory_order_acquire);
        const auto status = InstallFrontendAutomationHooks();
        if (status != FrontendOperationStatus::completed) return status;
        if (!legacy_was_active) legacy_frontend_automation_active.store(false, std::memory_order_release);
        campaign_automation_frontend_active.store(true, std::memory_order_release);
        RefreshFrontendAutomationActive();
        return FrontendOperationStatus::completed;
    }

    void DeactivateCampaignAutomationFrontend() noexcept
    {
        campaign_automation_frontend_capture_callback.store(nullptr, std::memory_order_release);
        campaign_automation_frontend_active.store(false, std::memory_order_release);
        RefreshFrontendAutomationActive();
        if (!legacy_frontend_automation_active.load(std::memory_order_acquire)) {
            ReleaseCapturedFrontendController(FrontendControllerKind::frontend,
                reinterpret_cast<void *>(captured_frontend.address.load(std::memory_order_acquire)));
            ReleaseCapturedFrontendController(FrontendControllerKind::main_menu,
                reinterpret_cast<void *>(captured_main_menu.address.load(std::memory_order_acquire)));
        }
    }

    FrontendOperationStatus AcquireFrontendController(FrontendControllerKind kind, FrontendControllerToken *token)
    {
        if (token == nullptr || !frontend_automation_active.load(std::memory_order_acquire)) {
            return FrontendOperationStatus::unavailable;
        }
        auto &capture = CapturedController(kind);
        const uint64_t generation = capture.generation.load(std::memory_order_acquire);
        const uintptr_t address = capture.address.load(std::memory_order_acquire);
        const DWORD thread_id = capture.thread_id.load(std::memory_order_acquire);
        if (address == 0 || thread_id == 0 || thread_id != GetCurrentThreadId()
            || capture.generation.load(std::memory_order_acquire) != generation) return FrontendOperationStatus::unavailable;
        *token = FrontendControllerToken(generation, kind);
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus ReleaseFrontendController(FrontendControllerToken token)
    {
        if (!token) return FrontendOperationStatus::invalid_token;
        auto &capture = CapturedController(token.kind_);
        if (capture.generation.load(std::memory_order_acquire) != token.generation_) return FrontendOperationStatus::invalid_token;
        ReleaseCapturedFrontendController(token.kind_, reinterpret_cast<void *>(capture.address.load(std::memory_order_acquire)));
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus DispatchMainMenuSinglePlayer(FrontendControllerToken token)
    {
        void *controller = nullptr;
        if (token.kind_ != FrontendControllerKind::main_menu) return FrontendOperationStatus::invalid_token;
        if (const auto status = ResolveController(token.generation_, token.kind_, &controller);
            status != FrontendOperationStatus::completed) return status;
        if (!ControllerVtableMatches(controller, main_menu_vtable_rva)) return FrontendOperationStatus::invalid_controller;
        void *gui = nullptr;
        if (!ReadField(controller, main_menu_gui_offset, &gui) || gui == nullptr) return FrontendOperationStatus::invalid_controller;
        EngineStringArgument panel_name("mainmenu_panel");
        EngineStringArgument button_name("single_player_button");
        uintptr_t find_panel = 0, find_button = 0;
        void *panel = nullptr;
        void *button = nullptr;
        if (!ReadVirtualTarget(gui, 0x6c, &find_panel) || (panel = InvokeFindControl(gui, find_panel, &panel_name)) == nullptr
            || !ReadVirtualTarget(panel, 0x34, &find_button) || (button = InvokeFindControl(panel, find_button, &button_name)) == nullptr) {
            return FrontendOperationStatus::unavailable;
        }
        if (!DispatchNativeSignal(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(button) + control_signal_offset))) {
            return FrontendOperationStatus::readback_failed;
        }
        return ReleaseFrontendController(token);
    }

    FrontendOperationStatus ObserveFrontendSave(FrontendControllerToken token, FrontendSaveSnapshot *snapshot)
    {
        void *controller = nullptr;
        if (snapshot == nullptr || token.kind_ != FrontendControllerKind::frontend) return FrontendOperationStatus::invalid_token;
        *snapshot = {};
        if (const auto status = ResolveController(token.generation_, token.kind_, &controller);
            status != FrontendOperationStatus::completed) return status;
        if (!ControllerVtableMatches(controller, frontend_vtable_rva)) return FrontendOperationStatus::invalid_controller;
        std::string selected;
        unsigned char flags[2]{};
        if (!ReadFrontendString(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(controller) + selected_save_offset), &selected)
            || !CopyReadable(flags, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(controller) + save_request_offset), sizeof(flags))
            || flags[0] > 1 || flags[1] > 1) return FrontendOperationStatus::invalid_controller;
        std::memcpy(snapshot->selected_basename, selected.c_str(), selected.size());
        snapshot->request_pending = flags[0] != 0;
        snapshot->completed = flags[1] != 0;
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus RequestFrontendSave(FrontendControllerToken token, const char *basename)
    {
        if (basename == nullptr || token.kind_ != FrontendControllerKind::frontend) return FrontendOperationStatus::precondition_failed;
        const size_t size = std::strlen(basename);
        if (size == 0 || size > maximum_save_basename || std::strpbrk(basename, "\\/:") != nullptr) {
            return FrontendOperationStatus::precondition_failed;
        }
        void *controller = nullptr;
        if (const auto status = ResolveController(token.generation_, token.kind_, &controller);
            status != FrontendOperationStatus::completed) return status;
        if (!ControllerVtableMatches(controller, frontend_vtable_rva)) return FrontendOperationStatus::invalid_controller;
        void *gui = nullptr;
        RawFrontendString metadata{};
        std::string existing;
        unsigned char flags[2]{};
        auto *selected = reinterpret_cast<smedley::sstd::string *>(reinterpret_cast<uintptr_t>(controller) + selected_save_offset);
        uintptr_t lookup = 0;
        if (!ReadField(controller, frontend_gui_offset, &gui) || gui == nullptr || !ReadVirtualTarget(gui, 0x34, &lookup)
            || !ReadFrontendString(selected, &existing, &metadata)
            || !CopyReadable(flags, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(controller) + save_request_offset), sizeof(flags))
            || flags[0] != 0 || flags[1] != 0) return FrontendOperationStatus::precondition_failed;
        if (!existing.empty() && CompareStringOrdinal(std::wstring(existing.begin(), existing.end()).c_str(), static_cast<int>(existing.size()),
                std::wstring(basename, basename + size).c_str(), static_cast<int>(size), TRUE) != CSTR_EQUAL) {
            return FrontendOperationStatus::precondition_failed;
        }
        if (existing.empty()) {
            if (metadata.capacity != 0xf || metadata.storage.buffer[0] != '\0' || !IsAccessible(selected, sizeof(*selected), true)) {
                return FrontendOperationStatus::precondition_failed;
            }
            try {
                const smedley::sstd::string prepared(basename);
                new (selected) smedley::sstd::string(prepared);
            } catch (const std::bad_alloc &) {
                return FrontendOperationStatus::unavailable;
            }
            std::string readback;
            if (!ReadFrontendString(selected, &readback) || readback != basename) return FrontendOperationStatus::readback_failed;
        }
        constexpr unsigned char requested[] = {1, 0};
        if (!CopyWritable(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(controller) + save_request_offset), requested, sizeof(requested))) {
            return FrontendOperationStatus::precondition_failed;
        }
        unsigned char readback[2]{};
        if (!CopyReadable(readback, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(controller) + save_request_offset), sizeof(readback))
            || readback[0] != 1 || readback[1] != 0) return FrontendOperationStatus::readback_failed;
        return FrontendOperationStatus::completed;
    }

    FrontendOperationStatus DispatchFrontendControl(FrontendControllerToken token, const char *name)
    {
        void *controller = nullptr;
        if (name == nullptr || name[0] == '\0' || token.kind_ != FrontendControllerKind::frontend) {
            return FrontendOperationStatus::precondition_failed;
        }
        if (const auto status = ResolveController(token.generation_, token.kind_, &controller);
            status != FrontendOperationStatus::completed) return status;
        if (!ControllerVtableMatches(controller, frontend_vtable_rva)) return FrontendOperationStatus::invalid_controller;
        void *gui = nullptr;
        uintptr_t find_control = 0;
        if (!ReadField(controller, frontend_gui_offset, &gui) || gui == nullptr || !ReadVirtualTarget(gui, 0x34, &find_control)) {
            return FrontendOperationStatus::invalid_controller;
        }
        EngineStringArgument control_name(name);
        void *control = InvokeFindControl(gui, find_control, &control_name);
        uintptr_t vtable = 0;
        if (control == nullptr || !ReadValue(reinterpret_cast<uintptr_t>(control), &vtable)) return FrontendOperationStatus::unavailable;
        return DispatchNativeSignal(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(control) + control_signal_offset))
            ? FrontendOperationStatus::completed : FrontendOperationStatus::readback_failed;
    }

    GameSession CurrentGameSession()
    {
        const GameStateRef game_state = ReadCurrentGameStateRef();
        const uintptr_t address = game_state.address();
        uintptr_t observed = observed_game_state.load(std::memory_order_acquire);
        while (observed != address) {
            if (observed_game_state.compare_exchange_weak(
                    observed, address, std::memory_order_acq_rel, std::memory_order_acquire)) {
                game_session_epoch.fetch_add(1, std::memory_order_acq_rel);
                break;
            }
        }
        return {game_state, game_session_epoch.load(std::memory_order_acquire)};
    }

    bool ReadTelemetryCurrentState(TelemetryCurrentState *state)
    {
        if (state == nullptr) return false;
        const GameStateRef game_state = ReadCurrentGameStateRef();
        if (!game_state) return false;
        TelemetryCurrentState value{};
        value.game_state = game_state;
        if (!ReadField(reinterpret_cast<const void *>(game_state.address()), game_state_date_offset, &value.date_raw)) return false;

        uint32_t country_count = 0;
        uint32_t country_ai_count = 0;
        uint32_t player_count = 0;
        if (ReadCountryCount(game_state, &country_count)
            && ReadVectorCount(reinterpret_cast<const void *>(game_state.address()), game_state_country_ais_offset,
                sizeof(void *), max_game_countries, &country_ai_count)
            && ReadVectorCount(reinterpret_cast<const void *>(game_state.address()), game_state_player_nations_offset,
                sizeof(int32_t), max_game_countries, &player_count)) {
            ForeignVector players{};
            bool player_values_available = ReadField(
                reinterpret_cast<const void *>(game_state.address()), game_state_player_nations_offset, &players);
            bool human_control_present = false;
            for (uint32_t ordinal = 0; ordinal < player_count; ++ordinal) {
                int32_t control = 0;
                if (!player_values_available || !ReadValue(
                        reinterpret_cast<uintptr_t>(players.begin) + ordinal * sizeof(control), &control)) {
                    player_values_available = false;
                    break;
                }
                human_control_present = human_control_present || control != 0;
            }
            if (player_values_available) {
                value.country_count_value = country_count;
                value.country_ai_count_value = country_ai_count;
                value.human_control_present_value = human_control_present;
                value.world_daily_available_value = true;
            }
        }
        uint32_t provinces = 0;
        if (ReadVectorCount(reinterpret_cast<const void *>(game_state.address()), game_state_provinces_offset,
                sizeof(void *), max_game_provinces, &provinces)) {
            value.province_count_value = provinces;
            value.province_count_available_value = true;
        }
        uint32_t wars = 0;
        if (ReadListCount(reinterpret_cast<const void *>(game_state.address()), game_state_wars_offset, max_game_wars, &wars)) {
            value.ongoing_war_count_value = wars;
            value.military_available_value = true;
        }
        *state = value;
        return true;
    }

    CountryRef DailyUpdateCountry(events::DailyUpdateEvent &event)
    {
        return CountryRef{static_cast<const void *>(event.GetCountry())};
    }

    bool ReadDailyUpdateSnapshot(CountryRef country_ref, DailyUpdateSnapshot *snapshot)
    {
        if (snapshot == nullptr) return false;
        TelemetryCurrentState current{};
        TelemetryCountrySnapshot country{};
        if (!ReadTelemetryCurrentState(&current) || !current.world_daily_available()
            || !ReadTelemetryCountry(country_ref, &country) || !country.daily_available()) return false;

        uint32_t owned_province_count = 0;
        if (!ReadVectorCount(reinterpret_cast<const void *>(country_ref.address()), country_owned_provinces_offset,
                sizeof(void *), max_game_provinces, &owned_province_count)) return false;

        DailyUpdateSnapshot value{};
        value.date_raw = current.date_raw;
        value.treasury_raw = country.treasury_raw();
        value.country_slot_count = current.country_count_value;
        value.ai_scheduler_entry_count = current.country_ai_count_value;
        value.country_tag = country.tag();
        value.country_exists = owned_province_count != 0;
        value.human_control_present = current.human_control_present_value;
        *snapshot = value;
        return true;
    }

    bool ReadDailyUpdateSnapshot(events::DailyUpdateEvent &event, DailyUpdateSnapshot *snapshot)
    {
        return ReadDailyUpdateSnapshot(DailyUpdateCountry(event), snapshot);
    }

    bool TelemetryCurrentState::ongoing_war_count_candidate(int *count) const noexcept
    {
        if (count == nullptr || !military_available_value) return false;
        *count = static_cast<int>(ongoing_war_count_value);
        return true;
    }

    bool TelemetryCurrentState::province_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !province_count_available_value) return false;
        *count = province_count_value;
        return true;
    }

    bool TelemetryCountrySnapshot::unit_count_candidate(int *count) const noexcept
    {
        if (count == nullptr || !military_available_value) return false;
        *count = static_cast<int>(unit_count_candidate_value);
        return true;
    }

    bool TelemetryCountrySnapshot::scheduled_mobilization_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !military_available_value) return false;
        *count = scheduled_mobilization_count_candidate_value;
        return true;
    }

    bool TelemetryCountrySnapshot::sphereling_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !diplomacy_relations_available_value) return false;
        *count = sphereling_count_candidate_value;
        return true;
    }

    bool TelemetryCountrySnapshot::vassal_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !diplomacy_relations_available_value) return false;
        *count = vassal_count_candidate_value;
        return true;
    }

    bool TelemetryCountrySnapshot::ally_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !diplomacy_relations_available_value) return false;
        *count = ally_count_candidate_value;
        return true;
    }

    bool TelemetryCountrySnapshot::guaranteed_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !diplomacy_relations_available_value) return false;
        *count = guaranteed_count_candidate_value;
        return true;
    }

    bool TelemetryCountrySnapshot::neighbor_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !diplomacy_relations_available_value) return false;
        *count = neighbor_count_candidate_value;
        return true;
    }

    bool TelemetryProvinceSnapshot::building_slot_count_candidate(size_t *count) const noexcept
    {
        if (count == nullptr || !production_available_value) return false;
        *count = building_slot_count_value;
        return true;
    }

    bool TelemetryProvinceSnapshot::construction_count_candidate(int *count) const noexcept
    {
        if (count == nullptr || !production_available_value) return false;
        *count = construction_count_value;
        return true;
    }

    bool ReadTelemetryCountry(CountryRef country, TelemetryCountrySnapshot *snapshot)
    {
        if (!country || snapshot == nullptr) return false;
        const void *value = reinterpret_cast<const void *>(country.address());
        TelemetryCountrySnapshot result{};
        result.country = country;
        if (!ReadTag(value, country_tag_offset, &result.tag_value)) return false;
        result.daily_available_value = ReadField(value, country_treasury_offset, &result.treasury_raw_value);
        result.power_available_value = ReadField(value, country_prestige_offset, &result.prestige_candidate_raw_value)
            && ReadField(value, country_infamy_offset, &result.infamy_candidate_raw_value)
            && ReadField(value, country_ranking_offset, &result.ranking_candidate_value)
            && ReadField(value, country_ranking_offset + sizeof(int32_t), &result.military_ranking_candidate_value)
            && ReadField(value, country_ranking_offset + 2 * sizeof(int32_t), &result.industrial_ranking_candidate_value)
            && ReadField(value, country_ranking_offset + 3 * sizeof(int32_t), &result.prestige_ranking_candidate_value);
        result.politics_available_value = ReadField(value, country_plurality_offset, &result.plurality_candidate_raw_value)
            && ReadField(value, country_war_exhaustion_offset, &result.war_exhaustion_candidate_raw_value)
            && ReadField(value, country_diplomatic_points_offset, &result.diplomatic_points_candidate_raw_value)
            && ReadField(value, country_research_points_offset, &result.research_points_candidate_raw_value)
            && ReadField(value, country_leadership_offset, &result.leadership_candidate_raw_value);
        uint32_t units = 0;
        uint32_t mobilizations = 0;
        result.military_available_value = ReadListCount(value, country_units_offset, max_country_units, &units)
            && ReadVectorCount(value, country_scheduled_mobilizations_offset, scheduled_mobilization_size,
                max_scheduled_mobilizations, &mobilizations)
            && ReadField(value, country_mobilized_offset, &result.mobilized_candidate_value)
            && ReadField(value, country_leadership_offset, &result.leadership_candidate_raw_value)
            && ReadField(value, country_ranking_offset + sizeof(int32_t), &result.military_ranking_candidate_value);
        result.unit_count_candidate_value = units;
        result.scheduled_mobilization_count_candidate_value = mobilizations;
        result.diplomacy_status_available_value = ReadField(value, country_substate_offset, &result.substate_candidate_value)
            && ReadField(value, country_vassal_offset, &result.vassal_candidate_value)
            && ReadTag(value, country_overlord_offset, &result.overlord_tag_value)
            && ReadTag(value, country_sphere_leader_offset, &result.sphere_leader_tag_value);
        uint32_t spherelings = 0, vassals = 0, allies = 0, guaranteed = 0, neighbors = 0;
        result.diplomacy_relations_available_value = ReadVectorCount(value, country_spherelings_offset, 8, max_country_relations, &spherelings)
            && ReadVectorCount(value, country_vassals_offset, 8, max_country_relations, &vassals)
            && ReadVectorCount(value, country_allies_offset, 8, max_country_relations, &allies)
            && ReadVectorCount(value, country_guaranteed_offset, 8, max_country_relations, &guaranteed)
            && ReadVectorCount(value, country_neighbors_offset, 8, max_country_relations, &neighbors);
        result.sphereling_count_candidate_value = spherelings;
        result.vassal_count_candidate_value = vassals;
        result.ally_count_candidate_value = allies;
        result.guaranteed_count_candidate_value = guaranteed;
        result.neighbor_count_candidate_value = neighbors;
        *snapshot = result;
        return true;
    }

    bool ReadTelemetryProvince(ProvinceRef province, TelemetryProvinceSnapshot *snapshot)
    {
        if (!province || snapshot == nullptr) return false;
        const void *value = reinterpret_cast<const void *>(province.address());
        TelemetryProvinceSnapshot result{};
        result.province = province;
        if (!ReadField(value, province_id_offset, &result.id_value)
            || result.id_value < 0 || result.id_value >= static_cast<int32_t>(max_game_provinces)) return false;
        result.owner_available_value = ReadTag(value, province_owner_offset, &result.owner_tag_value);
        const bool controller_available = ReadTag(value, province_controller_offset, &result.controller_tag_value);
        result.daily_available_value = result.owner_available_value && controller_available
            && ReadField(value, province_colonial_level_offset, &result.colonial_level_value)
            && ReadField(value, province_life_rating_offset, &result.life_rating_value)
            && ReadField(value, province_infrastructure_offset, &result.infrastructure_value);
        uint32_t building_slots = 0;
        uint32_t constructions = 0;
        result.production_available_value = ReadVectorCount(value, province_buildings_offset, sizeof(void *),
                max_province_building_slots, &building_slots)
            && ReadListCount(value, province_constructions_offset, max_province_constructions, &constructions);
        result.building_slot_count_value = building_slots;
        result.construction_count_value = static_cast<int32_t>(constructions);
        *snapshot = result;
        return true;
    }

    bool IsPauseOperationAvailable()
    {
        return PauseSignatureMatches();
    }

    CampaignRuntimeObservationStatus ReadCampaignRuntime(CampaignRuntimeSnapshot *snapshot)
    {
        if (snapshot == nullptr) return CampaignRuntimeObservationStatus::invalid_state;
        *snapshot = {};
        if (!smedley::IsCurrentExecutableSupported()) return CampaignRuntimeObservationStatus::signature_mismatch;
        void *idler = nullptr;
        const GameStateRef game_state = ReadCurrentGameStateRef();
        uint8_t pause_state = 0;
        CampaignRuntimeSnapshot value{};
        if (!game_state || !ReadCurrentIdler(&idler) || !IsInGameIdler(idler)) {
            return CampaignRuntimeObservationStatus::outside_campaign;
        }
        if (!ReadField(reinterpret_cast<const void *>(game_state.address()), game_state_date_offset, &value.date_raw)
            || !ReadField(reinterpret_cast<const void *>(game_state.address()), game_state_speed_index_offset, &value.speed_index)
            || !ReadPauseState(idler, &pause_state)
            || value.speed_index < 0 || value.speed_index > 4 || (pause_state != 0 && pause_state != 1)) {
            return CampaignRuntimeObservationStatus::invalid_state;
        }
        value.paused = pause_state == 1;
        *snapshot = value;
        return CampaignRuntimeObservationStatus::completed;
    }

    CampaignOperationStatus SetCampaignPaused(bool paused)
    {
        if (!smedley::IsCurrentExecutableSupported()) return CampaignOperationStatus::signature_mismatch;
        void *idler = nullptr;
        if (!ReadCurrentIdler(&idler) || !IsInGameIdler(idler)) return CampaignOperationStatus::outside_campaign;
        uint8_t previous = 0;
        if (!ReadPauseState(idler, &previous) || (previous != 0 && previous != 1)) {
            return CampaignOperationStatus::invalid_state;
        }
        if (!PauseSignatureMatches()) return CampaignOperationStatus::signature_mismatch;
        if ((previous == 1) != paused && !TogglePauseVerified(idler)) return CampaignOperationStatus::signature_mismatch;
        uint8_t readback = 0;
        if (!ReadPauseState(idler, &readback) || (readback == 1) != paused) {
            return CampaignOperationStatus::readback_failed;
        }
        return CampaignOperationStatus::completed;
    }

    CampaignOperationStatus SetCampaignSpeedIndex(int32_t speed_index)
    {
        if (speed_index < 0 || speed_index > 4) return CampaignOperationStatus::invalid_state;
        CampaignRuntimeSnapshot snapshot{};
        const auto observation = ReadCampaignRuntime(&snapshot);
        if (observation == CampaignRuntimeObservationStatus::outside_campaign) return CampaignOperationStatus::outside_campaign;
        if (observation == CampaignRuntimeObservationStatus::invalid_state) return CampaignOperationStatus::invalid_state;
        if (observation == CampaignRuntimeObservationStatus::signature_mismatch) return CampaignOperationStatus::signature_mismatch;
        if (!SpeedSignaturesMatch()) return CampaignOperationStatus::signature_mismatch;
        while (snapshot.speed_index != speed_index) {
            const bool increase = snapshot.speed_index < speed_index;
            const int32_t expected = snapshot.speed_index + (increase ? 1 : -1);
            if (!SetSpeedVerified(increase)) return CampaignOperationStatus::signature_mismatch;
            const auto readback = ReadCampaignRuntime(&snapshot);
            if (readback != CampaignRuntimeObservationStatus::completed
                || snapshot.speed_index != expected) {
                return CampaignOperationStatus::readback_failed;
            }
        }
        return CampaignOperationStatus::completed;
    }

    CampaignOperationStatus RequestCampaignQuit()
    {
        if (!smedley::IsCurrentExecutableSupported()) return CampaignOperationStatus::signature_mismatch;
        void *idler = nullptr;
        uintptr_t vtable = 0;
        uintptr_t request_quit_slot = 0;
        uintptr_t expected_quit = 0;
        uintptr_t request_quit = 0;
        uint8_t requested = 0;
        if (!ReadCurrentIdler(&idler) || !IsInGameIdler(idler)) return CampaignOperationStatus::outside_campaign;
        if (!ReadValue(reinterpret_cast<uintptr_t>(idler), &vtable)
            || !AddOffset(vtable, idler_request_quit_slot_offset, &request_quit_slot)
            || !ReadValue(request_quit_slot, &request_quit)
            || !AddOffset(smedley::memory::Map::base_addr, request_quit_rva, &expected_quit)
            || request_quit != expected_quit
            || !smedley::memory::MatchesOriginalOrRegisteredCodePatch(
                request_quit, request_quit_signature.data(), request_quit_signature.size())) {
            return CampaignOperationStatus::signature_mismatch;
        }
        if (!RequestQuitVerified(idler)) return CampaignOperationStatus::signature_mismatch;
        if (!ReadField(idler, idler_quit_requested_offset, &requested) || requested != 1) {
            return CampaignOperationStatus::readback_failed;
        }
        return CampaignOperationStatus::completed;
    }

    ProcessMetricsSnapshot SampleProcessMetrics()
    {
        ProcessMetricsSnapshot snapshot{};
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            ULARGE_INTEGER kernel_ticks{}, user_ticks{};
            kernel_ticks.LowPart = kernel.dwLowDateTime;
            kernel_ticks.HighPart = kernel.dwHighDateTime;
            user_ticks.LowPart = user.dwLowDateTime;
            user_ticks.HighPart = user.dwHighDateTime;
            if (user_ticks.QuadPart <= (std::numeric_limits<uint64_t>::max)() - kernel_ticks.QuadPart) {
                const uint64_t total_us = (kernel_ticks.QuadPart + user_ticks.QuadPart) / 10;
                if (total_us <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                    snapshot.process_cpu_us = static_cast<int64_t>(total_us);
                }
            }
        }
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
            snapshot.working_set_bytes = static_cast<int64_t>(counters.WorkingSetSize);
            snapshot.private_bytes = static_cast<int64_t>(counters.PrivateUsage);
            snapshot.process_peak_working_set_bytes = static_cast<int64_t>(counters.PeakWorkingSetSize);
        }
        return snapshot;
    }

    bool ObserverTag::normalized_candidate() const noexcept
    {
        if (ordinal < 0 || value[3] != '\0') return false;
        return std::all_of(std::begin(value), std::begin(value) + 3, [](char character) {
            return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        });
    }

    CampaignConsoleResponse::CampaignConsoleResponse(const char *text, bool result) noexcept
        : success(result)
    {
        if (text != nullptr) {
            std::strncpy(message, text, sizeof(message) - 1);
        }
    }

    ObserverObservationStatus ReadObserverState(ObserverStateSnapshot *snapshot)
    {
        if (snapshot == nullptr) return ObserverObservationStatus::invalid_state;
        *snapshot = {};
        if (!smedley::IsCurrentExecutableSupported()) return ObserverObservationStatus::signature_mismatch;
        CampaignRuntimeSnapshot runtime{};
        if (ReadCampaignRuntime(&runtime) != CampaignRuntimeObservationStatus::completed) {
            return ObserverObservationStatus::outside_campaign;
        }
        if (!ReadObserverStateInternal(ReadCurrentGameStateRef(), snapshot)) {
            return ObserverObservationStatus::invalid_state;
        }
        return ObserverObservationStatus::completed;
    }

    ObserverObservationStatus ReadObserverCountry(int32_t ordinal, ObserverCountrySnapshot *snapshot)
    {
        if (snapshot == nullptr || ordinal < 0 || ordinal >= max_game_countries) {
            return ObserverObservationStatus::invalid_state;
        }
        *snapshot = {};
        if (!smedley::IsCurrentExecutableSupported()) return ObserverObservationStatus::signature_mismatch;
        CampaignRuntimeSnapshot runtime{};
        if (ReadCampaignRuntime(&runtime) != CampaignRuntimeObservationStatus::completed) {
            return ObserverObservationStatus::outside_campaign;
        }
        if (!ReadObserverCountryInternal(ReadCurrentGameStateRef(), ordinal, snapshot)) {
            return ObserverObservationStatus::not_found;
        }
        return ObserverObservationStatus::completed;
    }

    ObserverObservationStatus ResolveObserverCountry(const char tag[4], ObserverCountrySnapshot *snapshot)
    {
        if (snapshot == nullptr || tag == nullptr || tag[3] != '\0') return ObserverObservationStatus::invalid_state;
        *snapshot = {};
        for (const char value : {tag[0], tag[1], tag[2]}) {
            if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) {
                return ObserverObservationStatus::invalid_state;
            }
        }
        if (!smedley::IsCurrentExecutableSupported()) return ObserverObservationStatus::signature_mismatch;
        CampaignRuntimeSnapshot runtime{};
        if (ReadCampaignRuntime(&runtime) != CampaignRuntimeObservationStatus::completed) {
            return ObserverObservationStatus::outside_campaign;
        }
        const GameStateRef game_state = ReadCurrentGameStateRef();
        ForeignVector countries{};
        uint32_t count = 0;
        if (!game_state || !ReadVector(reinterpret_cast<const void *>(game_state.address()), game_state_countries_offset,
                sizeof(void *), max_game_countries, &countries, &count)) {
            return ObserverObservationStatus::invalid_state;
        }
        for (uint32_t ordinal = 1; ordinal < count; ++ordinal) {
            ObserverCountrySnapshot candidate{};
            if (!ReadObserverCountryInternal(game_state, static_cast<int32_t>(ordinal), &candidate)) {
                return ObserverObservationStatus::invalid_state;
            }
            if (std::memcmp(candidate.tag.value, tag, sizeof(candidate.tag.value)) == 0) {
                *snapshot = candidate;
                return ObserverObservationStatus::completed;
            }
        }
        return ObserverObservationStatus::not_found;
    }

    ObserverObservationStatus FindHealthyObserverCountry(int32_t excluded_ordinal, ObserverCountrySnapshot *snapshot)
    {
        if (snapshot == nullptr) return ObserverObservationStatus::invalid_state;
        *snapshot = {};
        if (!smedley::IsCurrentExecutableSupported()) return ObserverObservationStatus::signature_mismatch;
        CampaignRuntimeSnapshot runtime{};
        if (ReadCampaignRuntime(&runtime) != CampaignRuntimeObservationStatus::completed) {
            return ObserverObservationStatus::outside_campaign;
        }
        const GameStateRef game_state = ReadCurrentGameStateRef();
        ForeignVector countries{};
        uint32_t count = 0;
        if (!game_state || !ReadVector(reinterpret_cast<const void *>(game_state.address()), game_state_countries_offset,
                sizeof(void *), max_game_countries, &countries, &count)) {
            return ObserverObservationStatus::invalid_state;
        }
        for (uint32_t ordinal = 1; ordinal < count; ++ordinal) {
            if (static_cast<int32_t>(ordinal) == excluded_ordinal) continue;
            ObserverCountrySnapshot candidate{};
            if (!ReadObserverCountryInternal(game_state, static_cast<int32_t>(ordinal), &candidate)) {
                return ObserverObservationStatus::invalid_state;
            }
            if (candidate.healthy_ai()) {
                *snapshot = candidate;
                return ObserverObservationStatus::completed;
            }
        }
        return ObserverObservationStatus::not_found;
    }

    ObserverOperationStatus ReturnObserverCountryToAI(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after)
    {
        if (after != nullptr) *after = {};
        if (!country.tag.normalized_candidate()) return ObserverOperationStatus::invalid_state;
        if (!smedley::IsCurrentExecutableSupported()) return ObserverOperationStatus::signature_mismatch;
        CampaignRuntimeSnapshot runtime{};
        if (ReadCampaignRuntime(&runtime) != CampaignRuntimeObservationStatus::completed) {
            return ObserverOperationStatus::outside_campaign;
        }
        const GameSession session = CurrentGameSession();
        const GameStateRef game_state = session.game_state;
        ObserverCountrySnapshot before{};
        if (!ReadObserverCountryInternal(game_state, country.tag.ordinal, &before)
            || !ObserverTagMatches(before.tag, country.tag) || !before.exists || !before.human_controlled || before.has_ai
            || !NativeSignatureMatches(return_country_to_ai_rva, return_country_to_ai_signature.data(),
                return_country_to_ai_signature.size())) {
            return ObserverOperationStatus::precondition_failed;
        }
        using ReturnCountryToAIFn = void (__thiscall *)(void *, uint32_t, int);
        uintptr_t address = 0;
        if (!AddOffset(smedley::memory::Map::base_addr, return_country_to_ai_rva, &address)) {
            return ObserverOperationStatus::signature_mismatch;
        }
        uint32_t tag_key = 0;
        std::memcpy(&tag_key, before.tag.value, sizeof(tag_key));
        __try {
            reinterpret_cast<ReturnCountryToAIFn>(address)(reinterpret_cast<void *>(game_state.address()),
                tag_key, before.tag.ordinal);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return ObserverOperationStatus::command_failed;
        }
        ObserverStateSnapshot observed{};
        ObserverCountrySnapshot restored{};
        const GameSession current_session = CurrentGameSession();
        if (current_session.epoch != session.epoch
            || current_session.game_state.address() != session.game_state.address()
            || ReadObserverState(&observed) != ObserverObservationStatus::completed
            || ReadObserverCountry(country.tag.ordinal, &restored) != ObserverObservationStatus::completed
            || !ObserverTagMatches(restored.tag, before.tag) || !restored.healthy_ai()) {
            return ObserverOperationStatus::readback_failed;
        }
        if (after != nullptr) *after = observed;
        return ObserverOperationStatus::completed;
    }

    ObserverOperationStatus SetObserverViewCountry(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after)
    {
        if (after != nullptr) *after = {};
        if (!country.tag.normalized_candidate()) return ObserverOperationStatus::invalid_state;
        ObserverStateSnapshot before{};
        const auto observation = ReadObserverState(&before);
        if (observation != ObserverObservationStatus::completed) {
            return observation == ObserverObservationStatus::signature_mismatch
                ? ObserverOperationStatus::signature_mismatch : ObserverOperationStatus::outside_campaign;
        }
        ObserverCountrySnapshot target{};
        if (ReadObserverCountry(country.tag.ordinal, &target) != ObserverObservationStatus::completed
            || !ObserverTagMatches(target.tag, country.tag) || !target.healthy_ai()) {
            return ObserverOperationStatus::precondition_failed;
        }
        const GameSession session = CurrentGameSession();
        const GameStateRef game_state = session.game_state;
        RawObserverTag raw{};
        std::memcpy(raw.value, target.tag.value, sizeof(raw.value));
        raw.ordinal = target.tag.ordinal;
        uintptr_t address = 0;
        if (!game_state || !AddOffset(game_state.address(), game_state_player_tag_offset, &address)
            || !CopyWritable(reinterpret_cast<void *>(address), &raw, sizeof(raw))) {
            return ObserverOperationStatus::invalid_state;
        }
        ObserverStateSnapshot observed{};
        const GameSession current_session = CurrentGameSession();
        if (current_session.epoch != session.epoch
            || current_session.game_state.address() != session.game_state.address()
            || ReadObserverState(&observed) != ObserverObservationStatus::completed
            || !ObserverTagMatches(observed.view_country.tag, target.tag)
            || observed.human_control_present != before.human_control_present
            || observed.country_ai_count != before.country_ai_count
            || !observed.view_country.healthy_ai()) {
            return ObserverOperationStatus::readback_failed;
        }
        if (after != nullptr) *after = observed;
        return ObserverOperationStatus::completed;
    }

    ObserverOperationStatus BlockNativeObserverTagCommand(CampaignConsoleRef console, uintptr_t replacement_handler)
    {
        if (!console || replacement_handler == 0) return ObserverOperationStatus::invalid_state;
        smedley::v2::CConsoleCmd::SCommandData command{};
        uintptr_t command_address = 0;
        uintptr_t expected = 0;
        if (!NativeSignatureMatches(native_tag_handler_rva, native_tag_handler_signature.data(), native_tag_handler_signature.size())) {
            return ObserverOperationStatus::signature_mismatch;
        }
        if (!AddOffset(smedley::memory::Map::base_addr, native_tag_handler_rva, &expected)
            || !ReadConsoleCommand(console, "tag", &command, &command_address) || command.handler == nullptr
            || reinterpret_cast<uintptr_t>(command.handler) != expected
            || !CopyWritable(reinterpret_cast<void *>(command_address
                + offsetof(smedley::v2::CConsoleCmd::SCommandData, handler)), &replacement_handler,
                sizeof(replacement_handler))
            || !ReadValue(command_address, &command)
            || reinterpret_cast<uintptr_t>(command.handler) != replacement_handler) {
            return ObserverOperationStatus::unavailable;
        }
        captured_console.store(console.address(), std::memory_order_release);
        captured_native_tag_handler.store(expected, std::memory_order_release);
        return ObserverOperationStatus::completed;
    }

    ObserverOperationStatus RestoreNativeObserverTagCommand(CampaignConsoleRef console, uintptr_t replacement_handler)
    {
        const uintptr_t handler = captured_native_tag_handler.load(std::memory_order_acquire);
        if (!console || replacement_handler == 0 || captured_console.load(std::memory_order_acquire) != console.address()
            || handler == 0) return ObserverOperationStatus::unavailable;
        smedley::v2::CConsoleCmd::SCommandData command{};
        uintptr_t command_address = 0;
        if (!ReadConsoleCommand(console, "tag", &command, &command_address)) return ObserverOperationStatus::unavailable;
        if (reinterpret_cast<uintptr_t>(command.handler) == replacement_handler) {
            if (!CopyWritable(reinterpret_cast<void *>(command_address
                    + offsetof(smedley::v2::CConsoleCmd::SCommandData, handler)), &handler, sizeof(handler))
                || !ReadValue(command_address, &command)
                || reinterpret_cast<uintptr_t>(command.handler) != handler) {
                return ObserverOperationStatus::readback_failed;
            }
        }
        return ObserverOperationStatus::completed;
    }

    ObserverOperationStatus StartNativeObserverTagSwitch(
        CampaignConsoleRef console, const ObserverTag &tag, CampaignConsoleCommandResult *result)
    {
        if (result != nullptr) *result = {};
        const uintptr_t handler = captured_native_tag_handler.load(std::memory_order_acquire);
        if (!console || !tag.normalized_candidate() || captured_console.load(std::memory_order_acquire) != console.address()
            || handler == 0 || !NativeSignatureMatches(native_tag_handler_rva, native_tag_handler_signature.data(),
                native_tag_handler_signature.size())) {
            return ObserverOperationStatus::unavailable;
        }
        if (!InvokeConsoleHandler(handler, tag.value, result)) return ObserverOperationStatus::command_failed;
        return ObserverOperationStatus::completed;
    }

    ObserverOperationStatus EnableObserverFullMapVisibility(CampaignConsoleRef console)
    {
        if (!console) return ObserverOperationStatus::invalid_state;
        if (!smedley::IsCurrentExecutableSupported()) return ObserverOperationStatus::signature_mismatch;
        uintptr_t fog_address = 0;
        uint8_t fog_enabled = 0;
        smedley::v2::CConsoleCmd::SCommandData command{};
        uintptr_t command_address = 0;
        uintptr_t expected = 0;
        if (!AddOffset(smedley::memory::Map::base_addr, fog_enabled_rva, &fog_address)
            || !ReadValue(fog_address, &fog_enabled)) return ObserverOperationStatus::invalid_state;
        if (fog_enabled == 0) return ObserverOperationStatus::completed;
        if (!NativeSignatureMatches(debug_command_handler_rva, debug_command_handler_signature.data(),
                debug_command_handler_signature.size())
            || !AddOffset(smedley::memory::Map::base_addr, debug_command_handler_rva, &expected)
            || !ReadConsoleCommand(console, "debug", &command, &command_address)
            || reinterpret_cast<uintptr_t>(command.handler) != expected) {
            return ObserverOperationStatus::unavailable;
        }
        CampaignConsoleCommandResult result{};
        if (!InvokeConsoleHandler(expected, "fow", &result) || !result.success) {
            return ObserverOperationStatus::command_failed;
        }
        if (!ReadValue(fog_address, &fog_enabled) || fog_enabled != 0) {
            return ObserverOperationStatus::readback_failed;
        }
        return ObserverOperationStatus::completed;
    }

    namespace
    {
        std::atomic<bool> campaign_automation_observer_enabled{};
        std::atomic<bool> campaign_console_ready{};
        std::atomic<CampaignAnnexationCallback> campaign_annexation_callback{};
        std::atomic<CampaignAnnexationCallback> campaign_automation_annexation_callback{};
        std::atomic<CampaignConsoleCaptureCallback> campaign_console_capture_callback{};
        std::atomic<CampaignConsoleCaptureCallback> campaign_automation_console_capture_callback{};
        std::atomic<CampaignConsoleCallback> campaign_console_callback{};
        std::atomic<bool> campaign_hooks_poisoned{};
        std::atomic<bool> campaign_hooks_installed{};
        smedley::v2::CConsoleCmdManager *campaign_console_manager = nullptr;
        int campaign_automation_console_owner{};
        std::atomic<bool> campaign_automation_console_capture_registered{};
        smedley::v2::CConsoleCmd::SCommandData *campaign_switch_command = nullptr;
        GameSession campaign_console_session{};
        volatile bool suppress_campaign_message_popups = false;
        std::atomic<bool> campaign_automation_message_popups_suppressed{};
        volatile long suppressed_campaign_message_count = 0;

        bool CampaignObserverEnabled() noexcept
        {
            return campaign_automation_observer_enabled.load(std::memory_order_acquire);
        }

        void RefreshCampaignPopupSuppression() noexcept
        {
            suppress_campaign_message_popups = campaign_automation_message_popups_suppressed.load(std::memory_order_acquire);
        }

        CampaignConsoleArguments CopyCampaignConsoleArguments(
            const smedley::sstd::vector<smedley::sstd::string> &raw)
        {
            CampaignConsoleArguments copied{};
            ForeignVector arguments{};
            if (!CopyReadable(&arguments, &raw, sizeof(arguments))) return copied;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(arguments.begin);
            const uintptr_t end = reinterpret_cast<uintptr_t>(arguments.end);
            const uintptr_t capacity = reinterpret_cast<uintptr_t>(arguments.capacity);
            if (begin == 0 && end == 0 && capacity == 0) {
                copied.valid = true;
                return copied;
            }
            if (begin == 0 || begin > end || end > capacity || (end - begin) % sizeof(smedley::sstd::string) != 0
                || (capacity - begin) % sizeof(smedley::sstd::string) != 0) return copied;
            const uintptr_t count = (end - begin) / sizeof(smedley::sstd::string);
            if (count > 8 || !IsAccessible(arguments.begin, static_cast<size_t>(end - begin), false)) return copied;
            copied.count = static_cast<uint32_t>(count);
            copied.valid = true;
            if (count == 0) return copied;

            struct RawEngineString
            {
                union { char buffer[16]; const char *pointer; } storage;
                uint32_t size;
                uint32_t capacity;
                uint32_t allocator;
            } argument{};
            if (!ReadValue(begin, &argument) || argument.size >= sizeof(copied.first)
                || argument.capacity < argument.size || argument.capacity < 0xf) {
                copied.valid = false;
                return copied;
            }
            const char *source = argument.capacity > 0xf ? argument.storage.pointer : argument.storage.buffer;
            if ((argument.size != 0 && !CopyReadable(copied.first, source, argument.size))) {
                copied.valid = false;
                return copied;
            }
            copied.first[argument.size] = '\0';
            return copied;
        }

        smedley::v2::CConsoleCmd::SResult DispatchCampaignConsoleCommand(
            CampaignConsoleCommand command, const smedley::sstd::vector<smedley::sstd::string> &raw)
        {
            if (!IsCampaignObserverConsoleReady()) {
                return smedley::v2::CConsoleCmd::SResult("observer unavailable", false);
            }
            const CampaignConsoleArguments arguments = CopyCampaignConsoleArguments(raw);
            SmedleyCampaignConsoleInputV1 input{};
            input.struct_size = sizeof(input);
            input.version = SMEDLEY_CAMPAIGN_CONSOLE_INPUT_VERSION_V1;
            input.command = command == CampaignConsoleCommand::native_tag
                ? SMEDLEY_CAMPAIGN_CONSOLE_NATIVE_TAG : SMEDLEY_CAMPAIGN_CONSOLE_OBSERVER_SWITCH;
            input.argument_count = arguments.count;
            input.arguments_valid = arguments.valid ? 1 : 0;
            std::memcpy(input.first_argument, arguments.first, sizeof(input.first_argument));
            SmedleyCampaignConsoleResultV1 result{};
            if (smedley::DispatchCampaignConsoleEventServices(input, &result)) {
                char message[SMEDLEY_CAMPAIGN_CONSOLE_MAX_RESULT_BYTES + 1]{};
                std::memcpy(message, result.message, result.message_bytes);
                return smedley::v2::CConsoleCmd::SResult(message, result.success != 0);
            }
            const auto callback = campaign_console_callback.load(std::memory_order_acquire);
            if (callback == nullptr) return smedley::v2::CConsoleCmd::SResult("observer unavailable", false);
            const CampaignConsoleResponse response = callback(command, arguments);
            return smedley::v2::CConsoleCmd::SResult(response.message, response.success);
        }

        smedley::v2::CConsoleCmd::SResult CampaignNativeTagHandler(
            const smedley::sstd::vector<smedley::sstd::string> &raw)
        {
            return DispatchCampaignConsoleCommand(CampaignConsoleCommand::native_tag, raw);
        }

        smedley::v2::CConsoleCmd::SResult CampaignSwitchHandler(
            const smedley::sstd::vector<smedley::sstd::string> &raw)
        {
            return DispatchCampaignConsoleCommand(CampaignConsoleCommand::observer_switch, raw);
        }

        bool CampaignConsoleSessionIsCurrent()
        {
            if (!campaign_console_session.game_state) return false;
            const GameSession current = CurrentGameSession();
            return current.game_state.address() == campaign_console_session.game_state.address()
                && current.epoch == campaign_console_session.epoch;
        }

        void ForgetCampaignConsole(bool restore) noexcept
        {
            const bool manager_is_current = campaign_console_manager != nullptr && CampaignConsoleSessionIsCurrent();
            bool removed_switch = false;
            if (manager_is_current && restore) {
                RestoreNativeObserverTagCommand(
                    CampaignConsoleRef{campaign_console_manager}, reinterpret_cast<uintptr_t>(&CampaignNativeTagHandler));
                removed_switch = campaign_switch_command != nullptr
                    && campaign_console_manager->commands().erase_value(campaign_switch_command);
            }
            // Without a manager destructor boundary, stale engine storage may still
            // reference this record. A bounded leak is safer than a use-after-free.
            if (campaign_switch_command != nullptr && removed_switch) {
                delete campaign_switch_command;
            }
            campaign_switch_command = nullptr;
            campaign_console_manager = nullptr;
            campaign_console_session = {};
            campaign_console_ready.store(false, std::memory_order_release);
        }

        CampaignConsoleCaptureStatus CaptureCampaignConsoleCommandManager(smedley::v2::CConsoleCmdManager *raw)
        {
            if (raw == nullptr) return CampaignConsoleCaptureStatus::native_tag_unavailable;
            if (!CampaignObserverEnabled()) {
                return CampaignConsoleCaptureStatus::observer_disabled;
            }
            if (campaign_console_ready.load(std::memory_order_acquire) && campaign_console_manager == raw
                && CampaignConsoleSessionIsCurrent()) {
                return CampaignConsoleCaptureStatus::already_configured;
            }
            if (campaign_console_manager != nullptr) ForgetCampaignConsole(false);

            const GameSession session = CurrentGameSession();
            if (!session.game_state) return CampaignConsoleCaptureStatus::native_tag_unavailable;
            campaign_console_manager = raw;
            campaign_console_session = session;
            if (raw->FindCommand("switch") != nullptr) return CampaignConsoleCaptureStatus::command_conflict;
            if (raw->FindCommand("tag") == nullptr
                || BlockNativeObserverTagCommand(CampaignConsoleRef{raw}, reinterpret_cast<uintptr_t>(&CampaignNativeTagHandler))
                    != ObserverOperationStatus::completed) {
                return CampaignConsoleCaptureStatus::native_tag_unavailable;
            }
            campaign_switch_command = new smedley::v2::CConsoleCmd::SCommandData{};
            campaign_switch_command->is_allowed = true;
            campaign_switch_command->name = "switch";
            campaign_switch_command->description = "Change observer view while preserving country AI.";
            campaign_switch_command->handler = &CampaignSwitchHandler;
            campaign_switch_command->num_args = 1;
            campaign_switch_command->args[0] = "TAG";
            raw->commands().push_back(campaign_switch_command);
            campaign_console_ready.store(true, std::memory_order_release);
            return CampaignConsoleCaptureStatus::completed;
        }

        void OnCampaignConsoleCaptured(smedley::events::ConsoleCmdManagerInitEvent &event)
        {
            const CampaignConsoleCaptureStatus status = CaptureCampaignConsoleCommandManager(event.cmd_mgr());
            const auto callback = campaign_console_capture_callback.load(std::memory_order_acquire);
            if (callback != nullptr) callback(status);
            const auto automation_callback = campaign_automation_console_capture_callback.load(std::memory_order_acquire);
            if (automation_callback != nullptr) automation_callback(status);
        }
    }

    void DeactivateCampaignAutomation() noexcept
    {
        campaign_annexation_callback.store(nullptr, std::memory_order_release);
        campaign_console_capture_callback.store(nullptr, std::memory_order_release);
        campaign_console_callback.store(nullptr, std::memory_order_release);
        RefreshCampaignPopupSuppression();
        if (!campaign_automation_console_capture_registered.load(std::memory_order_acquire)) ForgetCampaignConsole(true);
    }

    void SetCampaignAutomationObserverMode(bool enabled) noexcept
    {
        campaign_automation_observer_enabled.store(enabled, std::memory_order_release);
        campaign_automation_message_popups_suppressed.store(false, std::memory_order_release);
        RefreshCampaignPopupSuppression();
        InterlockedExchange(&suppressed_campaign_message_count, 0);
    }

    bool RegisterCampaignAutomationConsoleCapture()
    {
        if (campaign_automation_console_capture_registered.load(std::memory_order_acquire)) return true;
        try {
            EventRegistry<events::ConsoleCmdManagerInitEvent>::Register(
                &campaign_automation_console_owner, "campaign_automation_console", &OnCampaignConsoleCaptured);
            campaign_automation_console_capture_registered.store(true, std::memory_order_release);
            return true;
        } catch (...) {
            return false;
        }
    }

    void UnregisterCampaignAutomationConsoleCapture() noexcept
    {
        if (!campaign_automation_console_capture_registered.exchange(false, std::memory_order_acq_rel)) return;
        try {
            EventRegistry<events::ConsoleCmdManagerInitEvent>::Unregister(
                &campaign_automation_console_owner, "campaign_automation_console");
            campaign_automation_message_popups_suppressed.store(false, std::memory_order_release);
            campaign_automation_observer_enabled.store(false, std::memory_order_release);
            RefreshCampaignPopupSuppression();
            ForgetCampaignConsole(true);
        } catch (...) {
        }
    }

    void SetCampaignAutomationAnnexationCallback(CampaignAnnexationCallback callback) noexcept
    {
        campaign_automation_annexation_callback.store(callback, std::memory_order_release);
    }

    void SetCampaignAutomationConsoleCaptureCallback(CampaignConsoleCaptureCallback callback) noexcept
    {
        campaign_automation_console_capture_callback.store(callback, std::memory_order_release);
    }

    CampaignOperationStatus ActivateCampaignAutomationHooks()
    {
        const auto status = InstallCampaignAutomationHooks({});
        if (status != CampaignOperationStatus::completed) return status;
        return CampaignOperationStatus::completed;
    }

    void DeactivateCampaignAutomationHooks() noexcept
    {
        SetCampaignAutomationAnnexationCallback(nullptr);
        SetCampaignAutomationConsoleCaptureCallback(nullptr);
        SetCampaignAutomationObserverMode(false);
        SetCampaignAutomationMessagePopupSuppression(false);
        if (!campaign_automation_console_capture_registered.load(std::memory_order_acquire)) ForgetCampaignConsole(true);
    }

    bool IsCampaignObserverConsoleReady() noexcept
    {
        if (campaign_console_ready.load(std::memory_order_acquire) && !CampaignConsoleSessionIsCurrent()) {
            ForgetCampaignConsole(false);
        }
        return campaign_console_ready.load(std::memory_order_acquire);
    }

    ObserverOperationStatus StartNativeObserverTagSwitch(
        const ObserverTag &tag, CampaignConsoleCommandResult *result)
    {
        if (!IsCampaignObserverConsoleReady()) return ObserverOperationStatus::invalid_state;
        return StartNativeObserverTagSwitch(CampaignConsoleRef{campaign_console_manager}, tag, result);
    }

    ObserverOperationStatus EnableObserverFullMapVisibility()
    {
        if (!IsCampaignObserverConsoleReady()) return ObserverOperationStatus::invalid_state;
        return EnableObserverFullMapVisibility(CampaignConsoleRef{campaign_console_manager});
    }

    void SetCampaignAutomationMessagePopupSuppression(bool enabled) noexcept
    {
        campaign_automation_message_popups_suppressed.store(enabled, std::memory_order_release);
        RefreshCampaignPopupSuppression();
    }

    bool IsCampaignMessagePopupSuppressionEnabled() noexcept
    {
        return suppress_campaign_message_popups;
    }

    int32_t CampaignSuppressedMessageCount() noexcept
    {
        return InterlockedCompareExchange(&suppressed_campaign_message_count, 0, 0);
    }

    namespace
    {
        void *country_annex_original = nullptr;
        uintptr_t message_dispatch_return_addresses[9]{};
        uintptr_t message_dispatch_popup_addresses[9]{};
        uintptr_t message_dispatch_suppressed_addresses[9]{};

        void __stdcall NotifyCampaignAnnexation(int32_t annexed_ordinal) noexcept
        {
            const auto callback = campaign_annexation_callback.load(std::memory_order_acquire);
            if (callback != nullptr) callback(annexed_ordinal);
            const auto automation_callback = campaign_automation_annexation_callback.load(std::memory_order_acquire);
            if (automation_callback != nullptr) automation_callback(annexed_ordinal);
        }

        __declspec(naked) void CountryAnnexTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x34]
                push eax
                call NotifyCampaignAnnexation
                popad
                popfd
                jmp dword ptr [country_annex_original]
            }
        }

#define SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(name, source, index) \
        __declspec(naked) void name() { \
            __asm cmp byte ptr [suppress_campaign_message_popups], 0 \
            __asm jne name##_suppressed \
            __asm cmp byte ptr [source + 0x0e], 0 \
            __asm jne name##_popup \
            __asm jmp dword ptr [message_dispatch_return_addresses + index * 4] \
            __asm name##_suppressed: \
            __asm cmp byte ptr [source + 0x0e], 0 \
            __asm jne name##_count \
            __asm cmp byte ptr [source + 0x10], 0 \
            __asm je name##_bypass \
            __asm name##_count: \
            __asm lock inc dword ptr [suppressed_campaign_message_count] \
            __asm name##_bypass: \
            __asm jmp dword ptr [message_dispatch_suppressed_addresses + index * 4] \
            __asm name##_popup: \
            __asm jmp dword ptr [message_dispatch_popup_addresses + index * 4] \
        }

        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatchTrampoline, edi, 0)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch2Trampoline, ebx, 1)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch3Trampoline, edi, 2)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch4Trampoline, edi, 3)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch5Trampoline, ebx, 4)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch6Trampoline, edi, 5)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch7Trampoline, edi, 6)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch8Trampoline, edi, 7)
        SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE(MessageDispatch9Trampoline, edi, 8)
#undef SMEDLEY_MESSAGE_DISPATCH_TRAMPOLINE
    }

    CampaignOperationStatus InstallCampaignAutomationHooks(CampaignAutomationCallbacks callbacks)
    {
        constexpr std::array<uintptr_t, 9> dispatch_rvas{
            0x2bc68, 0x934f8, 0xe9678, 0x149c68, 0x1abaa8, 0x32dfb8, 0x507038, 0x509168, 0x53d818,
        };
        constexpr std::array<uintptr_t, 9> popup_rvas{
            0x2bc91, 0x93521, 0xe96a1, 0x149c91, 0x1abad1, 0x32dfe1, 0x507061, 0x509191, 0x53d841,
        };
        constexpr std::array<uintptr_t, 9> suppressed_rvas{
            0x2be42, 0x93663, 0xe9852, 0x149e42, 0x1abc13, 0x32e192, 0x507212, 0x509342, 0x53d9f2,
        };
        constexpr std::array<uint8_t, 6> country_annex_signature{0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
        constexpr std::array<uint8_t, 6> dispatch_edi_signature{0x80, 0x7f, 0x0e, 0x00, 0x75, 0x23};
        constexpr std::array<uint8_t, 6> dispatch_ebx_signature{0x80, 0x7b, 0x0e, 0x00, 0x75, 0x23};
        constexpr std::array<uint8_t, 5> suppressed_edi_signature{0x8b, 0x5d, 0x10, 0xeb, 0x89};
        constexpr std::array<uint8_t, 6> suppressed_ebx_signature{0x80, 0x7b, 0x11, 0x00, 0x0f, 0x84};
        constexpr std::array<bool, 9> ebx_dispatch{false, true, false, false, true, false, false, false, false};
        if (campaign_hooks_poisoned.load(std::memory_order_acquire)) {
            return CampaignOperationStatus::readback_failed;
        }
        if (campaign_hooks_installed.load(std::memory_order_acquire)) {
            if (callbacks.annexation != nullptr) campaign_annexation_callback.store(callbacks.annexation, std::memory_order_release);
            if (callbacks.console_capture != nullptr) campaign_console_capture_callback.store(callbacks.console_capture, std::memory_order_release);
            if (callbacks.console != nullptr) campaign_console_callback.store(callbacks.console, std::memory_order_release);
            return CampaignOperationStatus::completed;
        }
        const uintptr_t base = smedley::memory::Map::base_addr;
        uintptr_t country_annex = 0;
        if (!NativeSignatureMatches(0x118620, country_annex_signature.data(), country_annex_signature.size())
            || !AddOffset(base, 0x118620, &country_annex)) {
            return CampaignOperationStatus::signature_mismatch;
        }
        std::array<uintptr_t, 9> dispatch_addresses{};
        for (size_t index = 0; index < dispatch_rvas.size(); ++index) {
            const bool signatures_match = ebx_dispatch[index]
                ? NativeSignatureMatches(dispatch_rvas[index], dispatch_ebx_signature.data(), dispatch_ebx_signature.size())
                    && NativeSignatureMatches(suppressed_rvas[index], suppressed_ebx_signature.data(), suppressed_ebx_signature.size())
                : NativeSignatureMatches(dispatch_rvas[index], dispatch_edi_signature.data(), dispatch_edi_signature.size())
                    && NativeSignatureMatches(suppressed_rvas[index], suppressed_edi_signature.data(), suppressed_edi_signature.size());
            if (!signatures_match || !AddOffset(base, dispatch_rvas[index], &dispatch_addresses[index])) {
                return CampaignOperationStatus::signature_mismatch;
            }
        }
        for (size_t index = 0; index < dispatch_rvas.size(); ++index) {
            if (!AddOffset(dispatch_addresses[index], dispatch_edi_signature.size(), &message_dispatch_return_addresses[index])
                || !AddOffset(base, popup_rvas[index], &message_dispatch_popup_addresses[index])
                || !AddOffset(base, suppressed_rvas[index], &message_dispatch_suppressed_addresses[index])) {
                return CampaignOperationStatus::signature_mismatch;
            }
        }
        void *const trampolines[] = {
            reinterpret_cast<void *>(&MessageDispatchTrampoline), reinterpret_cast<void *>(&MessageDispatch2Trampoline),
            reinterpret_cast<void *>(&MessageDispatch3Trampoline), reinterpret_cast<void *>(&MessageDispatch4Trampoline),
            reinterpret_cast<void *>(&MessageDispatch5Trampoline), reinterpret_cast<void *>(&MessageDispatch6Trampoline),
            reinterpret_cast<void *>(&MessageDispatch7Trampoline), reinterpret_cast<void *>(&MessageDispatch8Trampoline),
            reinterpret_cast<void *>(&MessageDispatch9Trampoline),
        };
        std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> installed;
        const auto install = [&installed](uintptr_t address, void *trampoline) {
            std::vector<uint8_t> original;
            if (!smedley::memory::Hook(address, trampoline, 6, &original)) {
                throw std::runtime_error("campaign automation hook is too short");
            }
            installed.emplace_back(address, std::move(original));
        };
        try {
            installed.reserve(9);
            if (!smedley::memory::InstallDetour(
                    country_annex, reinterpret_cast<void *>(&CountryAnnexTrampoline), &country_annex_original)) {
                throw std::runtime_error("MinHook could not install campaign annexation detour");
            }
            for (size_t index = 0; index < dispatch_rvas.size(); ++index) install(dispatch_addresses[index], trampolines[index]);
        } catch (...) {
            bool restored = true;
            for (auto hook = installed.rbegin(); hook != installed.rend(); ++hook) {
                restored = smedley::memory::RestoreHook(hook->first, hook->second) && restored;
            }
            if (country_annex_original != nullptr) {
                // A published MinHook trampoline may still be reached by a
                // detour already executing on another thread.
                campaign_hooks_poisoned.store(true, std::memory_order_release);
                return CampaignOperationStatus::readback_failed;
            }
            return restored ? CampaignOperationStatus::invalid_state : CampaignOperationStatus::readback_failed;
        }
        campaign_annexation_callback.store(callbacks.annexation, std::memory_order_release);
        campaign_console_capture_callback.store(callbacks.console_capture, std::memory_order_release);
        campaign_console_callback.store(callbacks.console, std::memory_order_release);
        campaign_hooks_installed.store(true, std::memory_order_release);
        return CampaignOperationStatus::completed;
    }

    PauseOperationStatus PauseGame()
    {
        return SetCampaignPaused(true);
    }

    DailyInterestAccess::DailyInterestAccess(
        GameSession session, CountryRef country, bool after, uint64_t generation) noexcept
        : game_state_(session.game_state), country_(country), thread_(std::this_thread::get_id()), generation_(generation),
          session_epoch_(session.epoch), after_(after)
    {
    }

    DailyInterestAccess DailyInterestAccess::FromEvent(events::DailyInterestEvent &event)
    {
        const uint64_t generation = events::DailyInterestEvent::ActiveDispatchGeneration();
        const uint64_t trusted_generation = events::DailyInterestEvent::IsCurrentDispatch(event, generation)
            ? generation : 0;
        return DailyInterestAccess(CurrentGameSession(), CountryRef{static_cast<const void *>(event.GetCountry())},
            event.GetPhase() == events::DailyInterestPhase::AFTER, trusted_generation);
    }

    PopInterestMutationStatus DailyInterestAccess::CheckMutationAccess() const
    {
        if (!country_) return PopInterestMutationStatus::invalid_context;
        if (thread_ != std::this_thread::get_id()) return PopInterestMutationStatus::invalid_thread;
        if (!after_) return PopInterestMutationStatus::invalid_phase;
        if (!game_state_) return PopInterestMutationStatus::invalid_context;
        if (!events::DailyInterestEvent::IsDispatchActive(generation_)) return PopInterestMutationStatus::invalid_context;
        const GameSession current_session = CurrentGameSession();
        if (current_session.epoch != session_epoch_
            || current_session.game_state.address() != game_state_.address()) return PopInterestMutationStatus::state_changed;
        return PopInterestMutationStatus::success;
    }

    PopInterestMutationStatus DailyInterestAccess::CheckSignature(bool recheck)
    {
        if (!signature_checked_ || recheck) {
            signature_checked_ = true;
            VerifySignature(&signature_status_);
        }
        return signature_status_;
    }

    bool IsPopInterestWritable(PopRef pop)
    {
        const uintptr_t address = pop.address();
        if (address == 0 || address > (std::numeric_limits<uintptr_t>::max)() - pop_money_offset) return false;
        return IsAccessible(reinterpret_cast<const void *>(address + pop_money_offset), pop_money_span, true);
    }

    template <typename AccessCheck, typename SignatureCheck>
    PopInterestMutationStatus ApplyPopInterestBatchImpl(
        PopInterestBatchEntry *entries, uint32_t entry_count, PopInterestBatchResult *result,
        AccessCheck check_access, SignatureCheck check_signature)
    {
        if (result == nullptr) return PopInterestMutationStatus::invalid_context;
        *result = {};
        auto fail = [result](PopInterestMutationStatus status, uint32_t index) {
            result->status = status;
            result->failed_index = index;
            return status;
        };
        if (entry_count != 0 && entries == nullptr) {
            return fail(PopInterestMutationStatus::invalid_context, 0);
        }
        if (entry_count > max_sample_pops) return fail(PopInterestMutationStatus::invalid_context, 0);
        for (uint32_t index = 0; index < entry_count; ++index) {
            entries[index].before = {};
            entries[index].status = PopInterestMutationStatus::invalid_context;
            if (entries[index].amount <= 0) {
                entries[index].status = PopInterestMutationStatus::invalid_amount;
                return fail(entries[index].status, index);
            }
            if (!entries[index].pop) {
                entries[index].status = PopInterestMutationStatus::invalid_context;
                return fail(entries[index].status, index);
            }
        }
        if (entry_count <= 64) {
            for (uint32_t index = 0; index < entry_count; ++index) {
                for (uint32_t prior = 0; prior < index; ++prior) {
                    if (entries[prior].pop.address() == entries[index].pop.address()) {
                        entries[index].status = PopInterestMutationStatus::state_changed;
                        return fail(entries[index].status, index);
                    }
                }
            }
        } else {
            const std::lock_guard lock(pop_interest_identity_mutex);
            BeginPopInterestIdentitySet();
            size_t identity_capacity = 1;
            while (identity_capacity < entry_count * 2u
                && identity_capacity < pop_interest_identity_capacity) identity_capacity *= 2;
            for (uint32_t index = 0; index < entry_count; ++index) {
                if (!InsertPopInterestIdentity(entries[index].pop.address(), identity_capacity)) {
                    entries[index].status = PopInterestMutationStatus::state_changed;
                    return fail(entries[index].status, index);
                }
            }
        }
        if (entry_count == 0) return fail(PopInterestMutationStatus::success, 0);
        if (const auto status = check_access(); status != PopInterestMutationStatus::success) {
            return fail(status, 0);
        }
        if (const auto status = check_signature(false); status != PopInterestMutationStatus::success) {
            return fail(status, 0);
        }

        MemoryRegionCache access_cache{};
        for (uint32_t index = 0; index < entry_count; ++index) {
            PopInterestBatchEntry &entry = entries[index];
            const uintptr_t address = entry.pop.address();
            if (address > (std::numeric_limits<uintptr_t>::max)() - pop_money_offset
                || !IsAccessibleCached(reinterpret_cast<const void *>(address + pop_money_offset),
                    pop_snapshot_span, false, &access_cache)
                || !CopyPopMoneyFields(entry.pop, &entry.before)) {
                entry.status = PopInterestMutationStatus::balance_unreadable;
            } else if (!CanAdd(entry.before.money_raw, entry.amount)
                || !CanAdd(entry.before.interest_cash_flow_raw, entry.amount)
                || !CanAdd(entry.before.total_cash_flow_raw, entry.amount)) {
                entry.status = PopInterestMutationStatus::balance_overflow;
            } else {
                entry.status = address <= (std::numeric_limits<uintptr_t>::max)() - pop_money_offset
                    && IsAccessibleCached(reinterpret_cast<const void *>(address + pop_money_offset),
                        pop_money_span, true, &access_cache)
                    ? PopInterestMutationStatus::success : PopInterestMutationStatus::not_writable;
            }
            if (entry.status != PopInterestMutationStatus::success) return fail(entry.status, index);
        }

        if (const auto status = check_access(); status != PopInterestMutationStatus::success) {
            return fail(status, 0);
        }
        if (const auto status = check_signature(true); status != PopInterestMutationStatus::success) {
            return fail(status, 0);
        }

        for (uint32_t index = 0; index < entry_count; ++index) {
            PopInterestBatchEntry &entry = entries[index];
            GiveMoneyVerified(entry.pop, entry.amount);
            ++result->write_count;

            PopMoneySnapshot after{};
            if (!CopyPopMoneyFields(entry.pop, &after)
                || after.money_raw != entry.before.money_raw + entry.amount
                || after.interest_cash_flow_raw != entry.before.interest_cash_flow_raw + entry.amount
                || after.total_cash_flow_raw != entry.before.total_cash_flow_raw + entry.amount
                || after.savings_raw != entry.before.savings_raw) {
                entry.status = PopInterestMutationStatus::postcondition_failed;
                return fail(entry.status, index);
            }
            ++result->verified_count;
        }
        result->status = PopInterestMutationStatus::success;
        result->failed_index = entry_count;
        return result->status;
    }

    PopInterestMutationStatus ApplyPopInterestBatch(
        DailyInterestAccess &access, PopInterestBatchEntry *entries, uint32_t entry_count,
        PopInterestBatchResult *result)
    {
        return ApplyPopInterestBatchImpl(entries, entry_count, result,
            [&access] { return access.CheckMutationAccess(); },
            [&access](bool recheck) { return access.CheckSignature(recheck); });
    }

    BankInterestAccess::BankInterestAccess(GameSession session, CountryRef country, const void *bank,
                                           bool after, bool first_country, uint64_t generation) noexcept
        : game_state_(session.game_state), country_(country), bank_(bank), thread_(std::this_thread::get_id()),
          generation_(generation), session_epoch_(session.epoch), after_(after), first_country_(first_country)
    {
    }

    BankInterestAccess BankInterestAccess::FromEvent(events::BankInterestEvent &event)
    {
        const uint64_t generation = events::BankInterestEvent::ActiveDispatchGeneration();
        const uint64_t trusted_generation = events::BankInterestEvent::IsCurrentDispatch(event, generation)
            ? generation : 0;
        const void *bank = static_cast<const void *>(event.GetBank());
        const void *country = nullptr;
        ReadField(bank, bank_owner_offset, &country);
        const GameSession session = CurrentGameSession();
        return BankInterestAccess(session, CountryRef{country}, bank,
            event.GetPhase() == events::BankInterestPhase::AFTER,
            event.GetCountryIndex() == 0, trusted_generation);
    }

    PopInterestMutationStatus BankInterestAccess::CheckMutationAccess(bool require_after) const
    {
        if (!bank_ || !country_ || !game_state_) return PopInterestMutationStatus::invalid_context;
        if (thread_ != std::this_thread::get_id()) return PopInterestMutationStatus::invalid_thread;
        if (after_ != require_after) return PopInterestMutationStatus::invalid_phase;
        if (!events::BankInterestEvent::IsDispatchActive(generation_)) {
            return PopInterestMutationStatus::invalid_context;
        }
        const GameSession current_session = CurrentGameSession();
        if (current_session.epoch != session_epoch_
            || current_session.game_state.address() != game_state_.address()) {
            return PopInterestMutationStatus::state_changed;
        }
        const void *owner = nullptr;
        if (!ReadField(bank_, bank_owner_offset, &owner) || owner != detail::RawPointer(country_)) {
            return PopInterestMutationStatus::state_changed;
        }
        return PopInterestMutationStatus::success;
    }

    PopInterestMutationStatus BankInterestAccess::CheckPreparedMutationAccess() const
    {
        if (!bank_ || !country_ || !game_state_ || prepared_state_count_ == 0) {
            return PopInterestMutationStatus::invalid_context;
        }
        if (thread_ != std::this_thread::get_id()) return PopInterestMutationStatus::invalid_thread;
        if (!after_) return PopInterestMutationStatus::invalid_phase;
        return events::BankInterestEvent::IsDispatchActive(generation_)
            ? PopInterestMutationStatus::success : PopInterestMutationStatus::invalid_context;
    }

    PopInterestMutationStatus BankInterestAccess::CheckSignature(bool recheck)
    {
        if (!signature_checked_ || recheck) {
            signature_checked_ = true;
            VerifySignature(&signature_status_);
        }
        return signature_status_;
    }

    bool BankInterestAccess::ContainsPreparedState(const StateInterestCandidate &state) const
    {
        for (uint32_t index = 0; index < prepared_state_count_; ++index) {
            if (prepared_state_addresses_[index] == state.state.address()) return true;
        }
        return false;
    }

    namespace
    {
        bool ValidateStatePool(const StateInterestCandidate &state, bool writable)
        {
            if (!state.state || state.state_id < 0 || state.interest_raw < 0) return false;
            const uintptr_t address = state.state.address();
            if (address > (std::numeric_limits<uintptr_t>::max)() - state_size) return false;
            MemoryRegionCache cache{};
            if (!IsAccessibleCached(reinterpret_cast<const void *>(address + state_id_offset),
                    sizeof(int32_t), false, &cache)
                || !IsAccessibleCached(reinterpret_cast<const void *>(address + state_interest_offset),
                    sizeof(int64_t), writable, &cache)) {
                return false;
            }
            int32_t state_id = -1;
            int64_t interest = 0;
            __try {
                std::memcpy(&state_id, reinterpret_cast<const void *>(address + state_id_offset), sizeof(state_id));
                std::memcpy(&interest, reinterpret_cast<const void *>(address + state_interest_offset), sizeof(interest));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return state_id == state.state_id && interest == state.interest_raw;
        }

        bool ClearStatePool(const StateInterestCandidate &state)
        {
            if (!ValidateStatePool(state, true)) return false;
            const int64_t zero = 0;
            int64_t after = -1;
            const uintptr_t address = state.state.address() + state_interest_offset;
            __try {
                std::memcpy(reinterpret_cast<void *>(address), &zero, sizeof(zero));
                std::memcpy(&after, reinterpret_cast<const void *>(address), sizeof(after));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return after == 0;
        }

    }

    PopInterestMutationStatus DiscardStateInterestPools(
        BankInterestAccess &access, StateInterestInitializationResult *result)
    {
        if (result == nullptr) return PopInterestMutationStatus::invalid_context;
        *result = {};
        if (const auto status = access.CheckMutationAccess(false); status != PopInterestMutationStatus::success) {
            return result->status = status;
        }
        uint32_t country_count = 0;
        if (!ReadCountryCount(access.game_state_, &country_count)) {
            return result->status = PopInterestMutationStatus::unavailable;
        }
        result->country_count = country_count > 0 ? country_count - 1 : 0;
        static thread_local std::array<StateInterestCandidate, max_campaign_states> campaign_states{};
        static thread_local std::array<StateInterestCandidate, 512> country_states{};
        uint32_t campaign_state_count = 0;
        for (uint32_t ordinal = 1; ordinal < country_count; ++ordinal) {
            uint32_t state_count = 0;
            uint32_t pop_count = 0;
            CountryEconomySnapshot quality{};
            const CountryRef country = ResolveCountry(access.game_state_, static_cast<int32_t>(ordinal));
            if (!CollectCountryStateInterest(country, access.game_state_, 0,
                    country_states.data(), country_states.size(), &state_count,
                    nullptr, 0, 0, &pop_count, &quality)) {
                result->flags |= quality.flags;
                return result->status = PopInterestMutationStatus::unavailable;
            }
            if (state_count > max_campaign_states - campaign_state_count) {
                return result->status = PopInterestMutationStatus::unavailable;
            }
            for (uint32_t state_index = 0; state_index < state_count; ++state_index) {
                const StateInterestCandidate &candidate = country_states[state_index];
                for (uint32_t prior = 0; prior < campaign_state_count; ++prior) {
                    if (campaign_states[prior].state.address() == candidate.state.address()) {
                        return result->status = PopInterestMutationStatus::state_changed;
                    }
                }
                campaign_states[campaign_state_count++] = candidate;
            }
        }
        result->state_count = campaign_state_count;
        for (uint32_t index = 0; index < campaign_state_count; ++index) {
            const StateInterestCandidate &state = campaign_states[index];
            if (!ValidateStatePool(state, true)) {
                return result->status = PopInterestMutationStatus::not_writable;
            }
            if (state.interest_raw > 0
                && result->discarded_raw > (std::numeric_limits<int64_t>::max)() - state.interest_raw) {
                return result->status = PopInterestMutationStatus::balance_overflow;
            }
            result->discarded_raw += state.interest_raw;
        }
        if (const auto status = access.CheckMutationAccess(false); status != PopInterestMutationStatus::success) {
            return result->status = status;
        }
        for (uint32_t index = 0; index < campaign_state_count; ++index) {
            if (campaign_states[index].interest_raw == 0) continue;
            if (!ClearStatePool(campaign_states[index])) {
                return result->status = PopInterestMutationStatus::postcondition_failed;
            }
            ++result->cleared_state_count;
        }
        return result->status = PopInterestMutationStatus::success;
    }

    PopInterestMutationStatus PrepareCountryStateInterestPayouts(
        BankInterestAccess &access, const StateInterestCandidate *states, uint32_t state_count)
    {
        access.prepared_state_count_ = 0;
        if (states == nullptr || state_count == 0 || state_count > access.prepared_state_addresses_.size()) {
            return PopInterestMutationStatus::invalid_context;
        }
        if (const auto status = access.CheckMutationAccess(true); status != PopInterestMutationStatus::success) {
            return status;
        }
        static thread_local std::array<StateInterestCandidate, 512> current_states{};
        uint32_t current_state_count = 0;
        uint32_t pop_count = 0;
        CountryEconomySnapshot quality{};
        if (!CollectCountryStateInterest(access.country_, access.game_state_, 0,
                current_states.data(), current_states.size(), &current_state_count,
                nullptr, 0, 0, &pop_count, &quality)
            || current_state_count != state_count) {
            return PopInterestMutationStatus::state_changed;
        }
        for (uint32_t index = 0; index < state_count; ++index) {
            if (current_states[index].state.address() != states[index].state.address()
                || current_states[index].state_id != states[index].state_id
                || current_states[index].interest_raw != states[index].interest_raw) {
                return PopInterestMutationStatus::state_changed;
            }
        }
        for (uint32_t index = 0; index < state_count; ++index) {
            access.prepared_state_addresses_[index] = current_states[index].state.address();
        }
        access.prepared_state_count_ = state_count;
        return PopInterestMutationStatus::success;
    }

    PopInterestMutationStatus ApplyStateInterestPayout(
        BankInterestAccess &access, const StateInterestCandidate &state,
        PopInterestBatchEntry *entries, uint32_t entry_count,
        PopInterestBatchResult *result)
    {
        if (result == nullptr) return PopInterestMutationStatus::invalid_context;
        if (const auto status = access.CheckPreparedMutationAccess(); status != PopInterestMutationStatus::success) {
            *result = {};
            result->status = status;
            return status;
        }
        if (state.interest_raw <= 0 || !ValidateStatePool(state, true)) {
            *result = {};
            result->status = state.interest_raw <= 0
                ? PopInterestMutationStatus::invalid_amount : PopInterestMutationStatus::state_changed;
            return result->status;
        }
        if (!access.ContainsPreparedState(state)) {
            *result = {};
            return result->status = PopInterestMutationStatus::state_changed;
        }

        const PopInterestMutationStatus status = ApplyPopInterestBatchImpl(entries, entry_count, result,
            [] { return PopInterestMutationStatus::success; },
            [&access](bool) { return access.CheckSignature(false); });
        if (status != PopInterestMutationStatus::success) return status;
        // GiveMoney is synchronous and its verified field effects cannot alter country state membership.
        if (access.CheckMutationAccess(true) != PopInterestMutationStatus::success || !ClearStatePool(state)) {
            return result->status = PopInterestMutationStatus::postcondition_failed;
        }
        return result->status;
    }

}
