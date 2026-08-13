#include <smedley/campaign_runtime_api.h>
#include <smedley/campaign_automation_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/telemetry_game_api.h>
#include <smedley/telemetry_observation_api.h>

#include <smedley/events/bankinterest.hpp>
#include <smedley/event_abi_runtime.hpp>
#include <smedley/executable_identity.hpp>
#include <smedley/game_state/artisan_consumption_hook.hpp>
#include <smedley/game_state/factory_consumption_hook.hpp>
#include <smedley/game_state/factory_sales_hook.hpp>
#include <smedley/game_state/game_services_abi.hpp>
#include <smedley/game_state/pop_cash_flow_hook.hpp>
#include <smedley/game_state/runtime.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <mutex>

#include "service_support.hpp"

namespace smedley::game_state::services
{
    struct BankAuthoritySlot
    {
        BankAuthoritySlot *previous = nullptr;
        SmedleyBankInterestAuthority authority = 0;
        std::thread::id thread{};
        std::optional<BankInterestAccess> access;
        std::array<StateInterestCandidate, SMEDLEY_INTEREST_POOL_MAX_STATES> states{};
        std::array<PopCandidate, SMEDLEY_INTEREST_POOL_MAX_POPS> pops{};
        std::array<PopInterestBatchEntry, SMEDLEY_INTEREST_POOL_MAX_POPS> entries{};
        uint32_t state_count = 0;
        uint32_t pop_count = 0;
    };
    std::array<BankAuthoritySlot, 2> bank_authority_slots{};
    std::array<bool, bank_authority_slots.size()> bank_authority_slot_active{};
    std::mutex bank_authority_mutex;
    thread_local BankAuthoritySlot *bank_authority = nullptr;

    void ResetBankAuthoritySlot(BankAuthoritySlot *slot) noexcept
    {
        slot->previous = nullptr;
        slot->authority = 0;
        slot->thread = {};
        slot->access.reset();
        slot->state_count = 0;
        slot->pop_count = 0;
    }

    SmedleyInterestPoolResult InterestResult(PopInterestMutationStatus status, bool partial = false)
    {
        if (status == PopInterestMutationStatus::success) return SMEDLEY_INTEREST_POOL_SUCCESS;
        if (partial || status == PopInterestMutationStatus::postcondition_failed) return SMEDLEY_INTEREST_POOL_PARTIAL_MUTATION;
        if (status == PopInterestMutationStatus::invalid_context || status == PopInterestMutationStatus::invalid_thread
            || status == PopInterestMutationStatus::invalid_phase || status == PopInterestMutationStatus::state_changed) {
            return SMEDLEY_INTEREST_POOL_STALE_AUTHORITY;
        }
        if (status == PopInterestMutationStatus::unavailable || status == PopInterestMutationStatus::signature_mismatch) {
            return SMEDLEY_INTEREST_POOL_UNAVAILABLE;
        }
        return SMEDLEY_INTEREST_POOL_PRECONDITION_FAILED;
    }

    BankInterestAccess *Authority(SmedleyBankInterestAuthority authority)
    {
        return authority != 0 && bank_authority != nullptr && bank_authority->thread == std::this_thread::get_id()
            && bank_authority->authority == authority && bank_authority->access
            ? &*bank_authority->access : nullptr;
    }

    uint64_t InterestHandle(SmedleyBankInterestAuthority authority, uint32_t index) noexcept
    {
        return (authority << 20) | (static_cast<uint64_t>(index) + 1);
    }
    bool InterestHandleIndex(SmedleyBankInterestAuthority authority, uint64_t handle, uint32_t count, uint32_t *index)
    {
        if (index == nullptr || handle == 0 || (handle >> 20) != authority) return false;
        const uint64_t value = (handle & ((UINT64_C(1) << 20) - 1)) - 1;
        if (value >= count) return false;
        *index = static_cast<uint32_t>(value);
        return true;
    }

    SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL CollectInterestPools(
        SmedleyBankInterestAuthority authority, SmedleyInterestStateSnapshotV1 *states, uint32_t state_capacity,
        uint32_t *state_count, SmedleyInterestPopSnapshotV1 *pops, uint32_t pop_capacity,
        uint32_t *pop_count, uint32_t *flags)
    {
        auto *access = Authority(authority);
        if (access == nullptr) return SMEDLEY_INTEREST_POOL_STALE_AUTHORITY;
        if (state_count == nullptr || pop_count == nullptr || flags == nullptr || state_capacity > SMEDLEY_INTEREST_POOL_MAX_STATES
            || pop_capacity > SMEDLEY_INTEREST_POOL_MAX_POPS || (state_capacity != 0 && states == nullptr)
            || (pop_capacity != 0 && pops == nullptr)) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
        *state_count = 0;
        *pop_count = 0;
        *flags = 0;
        bank_authority->state_count = 0;
        bank_authority->pop_count = 0;
        CountryEconomySnapshot quality{};
        uint32_t internal_state_count = 0, internal_pop_count = 0;
        if (!CollectCountryStateInterest(access->country(), access->game_state(), 0, bank_authority->states.data(), state_capacity,
                &internal_state_count, bank_authority->pops.data(), pop_capacity, max_sample_destination_provinces,
                &internal_pop_count, &quality)) {
            *flags = quality.flags;
            return SMEDLEY_INTEREST_POOL_UNAVAILABLE;
        }
        for (uint32_t index = 0; index < internal_state_count; ++index) {
            auto &out = states[index];
            out = {};
            out.struct_size = sizeof(out); out.version = 1;
            out.state = InterestHandle(authority, index);
            out.state_id = bank_authority->states[index].state_id;
            out.first_pop = bank_authority->states[index].first_pop_index;
            out.pop_count = bank_authority->states[index].pop_count;
            out.province_count = bank_authority->states[index].province_count;
            out.savings_raw = bank_authority->states[index].savings_raw;
            out.interest_raw = bank_authority->states[index].interest_raw;
        }
        for (uint32_t index = 0; index < internal_pop_count; ++index) {
            auto &out = pops[index];
            out = {};
            out.struct_size = sizeof(out); out.version = 1;
            out.pop = InterestHandle(authority, index);
            out.savings_raw = bank_authority->pops[index].savings_raw;
        }
        *state_count = internal_state_count;
        *pop_count = internal_pop_count;
        bank_authority->state_count = internal_state_count;
        bank_authority->pop_count = internal_pop_count;
        return SMEDLEY_INTEREST_POOL_SUCCESS;
    }

    bool CopyStates(SmedleyBankInterestAuthority authority, const SmedleyInterestStateSnapshotV1 *states, uint32_t count,
        std::array<StateInterestCandidate, SMEDLEY_INTEREST_POOL_MAX_STATES> *out)
    {
        if (states == nullptr || count == 0 || count > out->size()) return false;
        for (uint32_t index = 0; index < count; ++index) {
            uint32_t source = 0;
            if (!ValidRecord(&states[index], 1) || !InterestHandleIndex(authority, states[index].state, bank_authority->state_count, &source)) return false;
            auto &value = (*out)[index];
            value.state = bank_authority->states[source].state;
            value = bank_authority->states[source];
        }
        return true;
    }
    SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL PrepareInterestPayouts(
        SmedleyBankInterestAuthority authority, const SmedleyInterestStateSnapshotV1 *states, uint32_t count)
    {
        auto *access = Authority(authority);
        if (access == nullptr) return SMEDLEY_INTEREST_POOL_STALE_AUTHORITY;
        std::array<StateInterestCandidate, SMEDLEY_INTEREST_POOL_MAX_STATES> internal{};
        if (!CopyStates(authority, states, count, &internal)) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
        for (uint32_t index = 0; index < count; ++index) {
            for (uint32_t prior = 0; prior < index; ++prior) {
                if (internal[index].state.address() == internal[prior].state.address()) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
            }
        }
        return InterestResult(PrepareCountryStateInterestPayouts(*access, internal.data(), count));
    }
    SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL ApplyInterestPayout(
        SmedleyBankInterestAuthority authority, const SmedleyInterestStateSnapshotV1 *state, const SmedleyInterestPayoutV1 *payouts,
        uint32_t count, SmedleyInterestPayoutResultV1 *result)
    {
        auto *access = Authority(authority);
        if (access == nullptr) return SMEDLEY_INTEREST_POOL_STALE_AUTHORITY;
        if (!ValidRecord(result, 1) || !ValidRecord(state, 1) || state->state == 0 || payouts == nullptr || count == 0 || count > SMEDLEY_INTEREST_POOL_MAX_POPS) {
            return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
        }
        StateInterestCandidate internal_state{};
        uint32_t state_index = 0;
        if (!InterestHandleIndex(authority, state->state, bank_authority->state_count, &state_index)) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
        internal_state = bank_authority->states[state_index];
        for (uint32_t index = 0; index < count; ++index) {
            if (!ValidRecord(&payouts[index], 1) || payouts[index].pop == 0 || payouts[index].amount_raw <= 0) {
                return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
            }
            uint32_t pop_index = 0;
            if (!InterestHandleIndex(authority, payouts[index].pop, bank_authority->pop_count, &pop_index)) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
            if (pop_index < internal_state.first_pop_index
                || pop_index >= internal_state.first_pop_index + internal_state.pop_count) {
                return SMEDLEY_INTEREST_POOL_PRECONDITION_FAILED;
            }
            for (uint32_t prior = 0; prior < index; ++prior) {
                if (bank_authority->entries[prior].pop.address() == bank_authority->pops[pop_index].address.address()) {
                    return SMEDLEY_INTEREST_POOL_PRECONDITION_FAILED;
                }
            }
            bank_authority->entries[index].pop = bank_authority->pops[pop_index].address;
            bank_authority->entries[index].amount = payouts[index].amount_raw;
        }
        PopInterestBatchResult internal_result{};
        const auto status = ApplyStateInterestPayout(*access, internal_state, bank_authority->entries.data(), count, &internal_result);
        result->failed_index = internal_result.failed_index;
        result->write_count = internal_result.write_count;
        result->verified_count = internal_result.verified_count;
        return InterestResult(status, internal_result.write_count != internal_result.verified_count);
    }
    SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL DiscardInterestPools(
        SmedleyBankInterestAuthority authority, SmedleyInterestInitializationV1 *result)
    {
        auto *access = Authority(authority);
        if (access == nullptr) return SMEDLEY_INTEREST_POOL_STALE_AUTHORITY;
        if (!ValidRecord(result, 1)) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
        StateInterestInitializationResult internal{};
        const auto status = DiscardStateInterestPools(*access, &internal);
        result->country_count = internal.country_count;
        result->state_count = internal.state_count;
        result->cleared_state_count = internal.cleared_state_count;
        result->flags = internal.flags;
        result->discarded_raw = internal.discarded_raw;
        return InterestResult(status, internal.cleared_state_count != 0 && status != PopInterestMutationStatus::success);
    }
}

namespace smedley::game_state
{
    using namespace services;

    bool BindBankInterestGameServices(SmedleyBankInterestAuthority authority, events::BankInterestEvent &event) noexcept
    {
        std::lock_guard<std::mutex> lock(bank_authority_mutex);
        BankAuthoritySlot *binding = nullptr;
        for (size_t index = 0; index < bank_authority_slots.size(); ++index) {
            if (bank_authority_slot_active[index]) continue;
            bank_authority_slot_active[index] = true;
            binding = &bank_authority_slots[index];
            break;
        }
        if (binding == nullptr) return false;
        ResetBankAuthoritySlot(binding);
        binding->previous = bank_authority;
        binding->authority = authority;
        binding->thread = std::this_thread::get_id();
        binding->access.emplace(BankInterestAccess::FromEvent(event));
        bank_authority = binding;
        return true;
    }
    void UnbindBankInterestGameServices(SmedleyBankInterestAuthority authority) noexcept
    {
        if (bank_authority != nullptr && bank_authority->authority == authority) {
            std::lock_guard<std::mutex> lock(bank_authority_mutex);
            auto *previous = bank_authority->previous;
            const auto index = static_cast<size_t>(bank_authority - bank_authority_slots.data());
            ResetBankAuthoritySlot(bank_authority);
            bank_authority_slot_active[index] = false;
            bank_authority = previous;
        }
    }
}

using namespace smedley::game_state::services;

SMEDLEY_INTEREST_POOL_EXPORT SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL
SmedleyGetInterestPoolApiV1(SmedleyInterestPoolApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_INTEREST_POOL_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT;
    api->collect = &CollectInterestPools;
    api->prepare = &PrepareInterestPayouts;
    api->apply = &ApplyInterestPayout;
    api->discard = &DiscardInterestPools;
    return SMEDLEY_INTEREST_POOL_SUCCESS;
}

static_assert(sizeof(SmedleyInterestStateSnapshotV1) == 64, "interest state ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPopSnapshotV1) == 40, "interest pop ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPayoutV1) == 40, "interest payout ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPayoutResultV1) == 32, "interest payout result ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestInitializationV1) == 48, "interest initialization ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPoolApiV1) == 32, "interest pool API v1 layout changed");
