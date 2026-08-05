#include <smedley/game_state/runtime.hpp>

#include <smedley/events/dailyinterest.hpp>
#include <smedley/executable_identity.hpp>
#include <smedley/memory.hpp>
#include <smedley/v2/gamestate.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>

namespace smedley::game_state
{
    namespace
    {
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr std::array<uint8_t, 10> give_money_signature{
            0x55, 0x8b, 0xec, 0x83, 0xb8, 0x84, 0x01, 0x00, 0x00, 0x00,
        };
        constexpr size_t pop_money_offset = 0x180;
        constexpr size_t pop_interest_cash_flow_offset = 0x210;
        constexpr size_t pop_total_cash_flow_offset = 0x218;
        constexpr size_t pop_money_span = pop_total_cash_flow_offset + sizeof(int64_t) - pop_money_offset;
        std::atomic<uintptr_t> observed_game_state{};
        std::atomic<uint64_t> game_session_epoch{};

        GameStateRef ReadCurrentGameStateRef()
        {
            if (smedley::memory::Map::base_addr == 0) return {};
            return GameStateRef{smedley::v2::CCurrentGameState::instance()};
        }

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

        bool CanAdd(int64_t value, int64_t amount)
        {
            return amount > 0 && value <= (std::numeric_limits<int64_t>::max)() - amount;
        }

        bool SameSnapshot(const PopMoneySnapshot &left, const PopMoneySnapshot &right)
        {
            return left.money_raw == right.money_raw
                && left.interest_cash_flow_raw == right.interest_cash_flow_raw
                && left.total_cash_flow_raw == right.total_cash_flow_raw
                && left.savings_raw == right.savings_raw;
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
        return DailyInterestAccess(CurrentGameSession(), CountryRef{event.GetCountry()},
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

    PopInterestMutationStatus PreparePopInterest(
        DailyInterestAccess &access, PopRef pop, int64_t amount, PopInterestPreflight *preflight)
    {
        if (preflight == nullptr) return PopInterestMutationStatus::invalid_context;
        *preflight = {};
        preflight->pop = pop;
        preflight->amount = amount;
        if (amount <= 0) return preflight->status = PopInterestMutationStatus::invalid_amount;
        if (const auto status = access.CheckMutationAccess(); status != PopInterestMutationStatus::success) {
            return preflight->status = status;
        }
        if (!pop) return preflight->status = PopInterestMutationStatus::invalid_context;
        if (const auto status = access.CheckSignature(); status != PopInterestMutationStatus::success) {
            return preflight->status = status;
        }
        if (!ReadPopMoneySnapshot(pop, &preflight->before)) {
            return preflight->status = PopInterestMutationStatus::balance_unreadable;
        }
        if (!CanAdd(preflight->before.money_raw, amount)
            || !CanAdd(preflight->before.interest_cash_flow_raw, amount)
            || !CanAdd(preflight->before.total_cash_flow_raw, amount)) {
            return preflight->status = PopInterestMutationStatus::balance_overflow;
        }
        if (!IsPopInterestWritable(pop)) return preflight->status = PopInterestMutationStatus::not_writable;
        return preflight->status = PopInterestMutationStatus::success;
    }

    PopInterestMutationStatus ApplyPopInterest(
        DailyInterestAccess &access, PopRef pop, int64_t amount, const PopInterestPreflight &preflight,
        PopMoneySnapshot *after)
    {
        if (after != nullptr) *after = {};
        if (amount <= 0) return PopInterestMutationStatus::invalid_amount;
        if (const auto status = access.CheckMutationAccess(); status != PopInterestMutationStatus::success) return status;
        if (!pop || preflight.status != PopInterestMutationStatus::success) return PopInterestMutationStatus::invalid_context;
        if (preflight.pop.address() != pop.address() || preflight.amount != amount) {
            return PopInterestMutationStatus::state_changed;
        }
        if (const auto status = access.CheckSignature(true); status != PopInterestMutationStatus::success) return status;
        PopMoneySnapshot before{};
        if (!ReadPopMoneySnapshot(pop, &before)) return PopInterestMutationStatus::balance_unreadable;
        if (!SameSnapshot(before, preflight.before)) return PopInterestMutationStatus::state_changed;
        if (!CanAdd(before.money_raw, amount)
            || !CanAdd(before.interest_cash_flow_raw, amount)
            || !CanAdd(before.total_cash_flow_raw, amount)) {
            return PopInterestMutationStatus::balance_overflow;
        }
        if (!IsPopInterestWritable(pop)) return PopInterestMutationStatus::not_writable;

        GiveMoneyVerified(pop, amount);

        const int64_t expected_money = before.money_raw + amount;
        const int64_t expected_interest_cash_flow = before.interest_cash_flow_raw + amount;
        const int64_t expected_total_cash_flow = before.total_cash_flow_raw + amount;
        PopMoneySnapshot value{};
        if (!ReadPopMoneySnapshot(pop, &value)
            || value.money_raw != expected_money
            || value.interest_cash_flow_raw != expected_interest_cash_flow
            || value.total_cash_flow_raw != expected_total_cash_flow
            || value.savings_raw != before.savings_raw) {
            return PopInterestMutationStatus::postcondition_failed;
        }
        if (after != nullptr) *after = value;
        return PopInterestMutationStatus::success;
    }
}
