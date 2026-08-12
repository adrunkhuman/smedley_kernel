#include <smedley/campaign_runtime_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/telemetry_game_api.h>

#include <smedley/events/bankinterest.hpp>
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

namespace
{
    using namespace smedley::game_state;

    std::recursive_mutex metadata_mutex;
    std::thread::id service_owner_thread;

    bool IsServiceOwnerThread()
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (service_owner_thread == std::thread::id{}) service_owner_thread = std::this_thread::get_id();
        return service_owner_thread == std::this_thread::get_id();
    }

    template <typename Record>
    bool ValidRecord(const Record *record, uint32_t version)
    {
        if (record == nullptr || record->struct_size != sizeof(Record) || record->version != version) return false;
        for (const auto value : record->reserved) if (value != 0) return false;
        return true;
    }

    SmedleyCampaignRuntimeResult CampaignResult(CampaignOperationStatus status)
    {
        switch (status) {
        case CampaignOperationStatus::completed: return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
        case CampaignOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_RUNTIME_READBACK_FAILED;
        case CampaignOperationStatus::outside_campaign: return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        default: return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        }
    }

    SmedleyCampaignRuntimeResult FrontendResult(FrontendOperationStatus status)
    {
        switch (status) {
        case FrontendOperationStatus::completed: return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
        case FrontendOperationStatus::invalid_token: return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        case FrontendOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_RUNTIME_READBACK_FAILED;
        case FrontendOperationStatus::unavailable: return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        default: return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        }
    }

    SmedleyCampaignRuntimeResult ObserverResult(ObserverObservationStatus status)
    {
        return status == ObserverObservationStatus::completed ? SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS
            : status == ObserverObservationStatus::outside_campaign ? SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE
            : SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
    }

    struct CampaignSessionSlot { uint64_t handle = 0; uint64_t epoch = 0; std::thread::id thread{}; };
    std::array<CampaignSessionSlot, 8> campaign_sessions{};
    uint64_t next_campaign_session = 1;
    struct FrontendSlot {
        uint64_t handle = 0;
        SmedleyCampaignSession owner = 0;
        uint64_t epoch = 0;
        std::thread::id thread{};
        uint32_t kind = 0;
        FrontendControllerToken token{};
    };
    std::array<FrontendSlot, 4> frontend_slots{};
    uint64_t next_frontend_handle = 1;

    void RetireCampaignFrontendSlots(SmedleyCampaignSession session)
    {
        for (auto &frontend : frontend_slots) {
            if (frontend.owner == session) frontend = {};
        }
    }

    SmedleyCampaignRuntimeResult CampaignSessionStatus(SmedleyCampaignSession handle)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (handle == 0) return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        for (const auto &slot : campaign_sessions) {
            if (slot.handle == handle) {
                if (slot.thread != std::this_thread::get_id()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
                return CurrentGameSession().epoch == slot.epoch
                    ? SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS : SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
            }
        }
        return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
    }

    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL OpenCampaignSession(SmedleyCampaignSession *session)
    {
        if (session == nullptr) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        *session = 0;
        const auto current = CurrentGameSession();
        CampaignRuntimeSnapshot snapshot{};
        if (ReadCampaignRuntime(&snapshot) != CampaignRuntimeObservationStatus::completed || !current.game_state) {
            return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        }
        for (auto &slot : campaign_sessions) {
            if (slot.handle != 0 && slot.epoch != current.epoch) {
                RetireCampaignFrontendSlots(slot.handle);
                slot = {};
            }
            if (slot.handle == 0) {
                slot.epoch = current.epoch;
                slot.thread = std::this_thread::get_id();
                slot.handle = next_campaign_session++;
                if (slot.handle == 0) slot.handle = next_campaign_session++;
                *session = slot.handle;
                return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
            }
        }
        return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL CloseCampaignSession(SmedleyCampaignSession session)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (auto &slot : campaign_sessions) {
            if (slot.handle != session) continue;
            if (slot.thread != std::this_thread::get_id()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
            const bool current = CurrentGameSession().epoch == slot.epoch;
            for (auto &frontend : frontend_slots) {
                if (frontend.owner == session && current && frontend.epoch == slot.epoch
                    && frontend.thread == slot.thread) ReleaseFrontendController(frontend.token);
            }
            RetireCampaignFrontendSlots(session);
            slot = {};
            return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
        }
        return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
    }

    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReadCampaign(
        SmedleyCampaignSession session, SmedleyCampaignRuntimeSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        CampaignRuntimeSnapshot value{};
        const auto status = ReadCampaignRuntime(&value);
        if (status != CampaignRuntimeObservationStatus::completed) return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        snapshot->game_date_raw = value.date_raw;
        snapshot->speed_index = value.speed_index;
        snapshot->paused = value.paused ? 1 : 0;
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }

    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL SetPaused(SmedleyCampaignSession session, uint32_t paused)
    {
        if (paused > 1) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        return CampaignResult(SetCampaignPaused(paused != 0));
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL SetSpeed(SmedleyCampaignSession session, int32_t speed)
    {
        if (speed < 0 || speed > 4) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        return CampaignResult(SetCampaignSpeedIndex(speed));
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL RequestQuit(SmedleyCampaignSession session)
    {
        const auto status = CampaignSessionStatus(session);
        return status == SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS ? CampaignResult(RequestCampaignQuit()) : status;
    }

    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL AcquireFrontend(
        SmedleyCampaignSession session, uint32_t kind, SmedleyFrontendController *controller)
    {
        if (controller == nullptr || kind > 1) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        *controller = 0;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (const auto &slot : frontend_slots) {
            if (slot.owner == session && slot.kind == kind && slot.epoch == CurrentGameSession().epoch) {
                return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
            }
        }
        for (auto &slot : frontend_slots) {
            if (slot.handle != 0) continue;
            FrontendControllerToken token{};
            const auto status = AcquireFrontendController(static_cast<FrontendControllerKind>(kind), &token);
            if (status != FrontendOperationStatus::completed) return FrontendResult(status);
            slot.handle = next_frontend_handle++;
            if (slot.handle == 0) slot.handle = next_frontend_handle++;
            slot.token = token;
            slot.owner = session;
            slot.epoch = CurrentGameSession().epoch;
            slot.thread = std::this_thread::get_id();
            slot.kind = kind;
            *controller = slot.handle;
            return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
        }
        return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
    }

    FrontendSlot *FindFrontend(SmedleyFrontendController controller)
    {
        for (auto &slot : frontend_slots) if (slot.handle == controller) return &slot;
        return nullptr;
    }
    SmedleyCampaignRuntimeResult FrontendStatus(const FrontendSlot *slot)
    {
        if (slot == nullptr) return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        if (slot->thread != std::this_thread::get_id()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        const auto session = CampaignSessionStatus(slot->owner);
        if (session != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return session;
        return CurrentGameSession().epoch == slot->epoch ? SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS
                                                         : SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReleaseFrontend(SmedleyFrontendController controller)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (slot == nullptr) return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        const auto session_status = FrontendStatus(slot);
        if (session_status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) {
            *slot = {};
            return session_status;
        }
        const auto result = FrontendResult(ReleaseFrontendController(slot->token));
        *slot = {};
        return result;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL RequestSave(
        SmedleyFrontendController controller, const char *basename, uint32_t bytes)
    {
        if (basename == nullptr || bytes == 0 || bytes >= SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES
            || std::memchr(basename, '\0', bytes) != nullptr) {
            return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        }
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (const auto status = FrontendStatus(slot); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        char text[SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES]{};
        std::memcpy(text, basename, bytes);
        return FrontendResult(RequestFrontendSave(slot->token, text));
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReadSave(
        SmedleyFrontendController controller, SmedleyFrontendSaveSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (const auto status = FrontendStatus(slot); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        FrontendSaveSnapshot value{};
        const auto status = ObserveFrontendSave(slot->token, &value);
        if (status != FrontendOperationStatus::completed) return FrontendResult(status);
        snapshot->request_pending = value.request_pending ? 1 : 0;
        snapshot->completed = value.completed ? 1 : 0;
        std::memcpy(snapshot->selected_basename, value.selected_basename, sizeof(snapshot->selected_basename));
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL DispatchControl(
        SmedleyFrontendController controller, const char *name, uint32_t bytes)
    {
        if (name == nullptr || bytes == 0 || bytes >= SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES
            || std::memchr(name, '\0', bytes) != nullptr) {
            return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        }
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (const auto status = FrontendStatus(slot); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        char text[SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES]{};
        std::memcpy(text, name, bytes);
        return FrontendResult(DispatchFrontendControl(slot->token, text));
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL DispatchMainMenuSinglePlayer(SmedleyFrontendController controller)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (const auto status = FrontendStatus(slot); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        return FrontendResult(smedley::game_state::DispatchMainMenuSinglePlayer(slot->token));
    }

    void CopyObserverCountry(const ObserverCountrySnapshot &from, SmedleyObserverCountrySnapshotV1 *to)
    {
        std::memcpy(to->tag, from.tag.value, sizeof(to->tag));
        to->ordinal = from.tag.ordinal;
        to->exists = from.exists ? 1 : 0;
        to->human_controlled = from.human_controlled ? 1 : 0;
        to->has_ai = from.has_ai ? 1 : 0;
        to->ai_scheduled = from.ai_scheduled ? 1 : 0;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReadObserverState(
        SmedleyCampaignSession session, SmedleyObserverStateSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        if (!ValidRecord(&snapshot->view_country, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        ObserverStateSnapshot value{};
        const auto status = smedley::game_state::ReadObserverState(&value);
        if (status != ObserverObservationStatus::completed) return ObserverResult(status);
        CopyObserverCountry(value.view_country, &snapshot->view_country);
        snapshot->country_count = value.country_count;
        snapshot->country_ai_count = value.country_ai_count;
        snapshot->human_control_present = value.human_control_present ? 1 : 0;
        snapshot->full_map_visibility_enabled = value.full_map_visibility_enabled ? 1 : 0;
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReadObserverCountry(
        SmedleyCampaignSession session, int32_t ordinal, SmedleyObserverCountrySnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1) || ordinal < 0) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        ObserverCountrySnapshot value{};
        const auto status = smedley::game_state::ReadObserverCountry(ordinal, &value);
        if (status != ObserverObservationStatus::completed) return ObserverResult(status);
        CopyObserverCountry(value, snapshot);
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL FindObserverCountry(
        SmedleyCampaignSession session, int32_t excluded, SmedleyObserverCountrySnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        ObserverCountrySnapshot value{};
        const auto status = FindHealthyObserverCountry(excluded, &value);
        if (status != ObserverObservationStatus::completed) return ObserverResult(status);
        CopyObserverCountry(value, snapshot);
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL SetObserverViewCountry(
        SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after)
    {
        if (!ValidRecord(country, 1) || !ValidRecord(after, 1) || !ValidRecord(&after->view_country, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        ObserverCountrySnapshot internal{};
        const auto read = smedley::game_state::ReadObserverCountry(country->ordinal, &internal);
        if (read != ObserverObservationStatus::completed || std::memcmp(country->tag, internal.tag.value, 4) != 0) {
            return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        }
        ObserverStateSnapshot state{};
        const auto status = smedley::game_state::SetObserverViewCountry(internal, &state);
        if (status != ObserverOperationStatus::completed) return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        CopyObserverCountry(state.view_country, &after->view_country);
        after->country_count = state.country_count;
        after->country_ai_count = state.country_ai_count;
        after->human_control_present = state.human_control_present ? 1 : 0;
        after->full_map_visibility_enabled = state.full_map_visibility_enabled ? 1 : 0;
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReturnObserverCountry(
        SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after)
    {
        if (!ValidRecord(country, 1) || !ValidRecord(after, 1) || !ValidRecord(&after->view_country, 1)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        ObserverCountrySnapshot internal{};
        const auto read = smedley::game_state::ReadObserverCountry(country->ordinal, &internal);
        if (read != ObserverObservationStatus::completed || std::memcmp(country->tag, internal.tag.value, 4) != 0) {
            return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        }
        ObserverStateSnapshot state{};
        if (smedley::game_state::ReturnObserverCountryToAI(internal, &state) != ObserverOperationStatus::completed) {
            return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
        }
        CopyObserverCountry(state.view_country, &after->view_country);
        after->country_count = state.country_count; after->country_ai_count = state.country_ai_count;
        after->human_control_present = state.human_control_present ? 1 : 0;
        after->full_map_visibility_enabled = state.full_map_visibility_enabled ? 1 : 0;
        return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL EnableObserverFow(SmedleyCampaignSession session)
    {
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        return smedley::game_state::EnableObserverFullMapVisibility() == ObserverOperationStatus::completed
            ? SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS : SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
    }

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

    struct TelemetrySessionSlot { uint64_t handle = 0; uint64_t epoch = 0; std::thread::id thread{}; };
    std::array<TelemetrySessionSlot, 8> telemetry_sessions{};
    uint64_t next_telemetry_session = 1;
    SmedleyTelemetryGameResult TelemetrySessionStatus(SmedleyTelemetrySession handle)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (const auto &slot : telemetry_sessions) {
            if (slot.handle == handle && handle != 0) {
                if (slot.thread != std::this_thread::get_id()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
                return CurrentGameSession().epoch == slot.epoch ? SMEDLEY_TELEMETRY_GAME_SUCCESS
                                                               : SMEDLEY_TELEMETRY_GAME_STALE_HANDLE;
            }
        }
        return SMEDLEY_TELEMETRY_GAME_STALE_HANDLE;
    }
    void RetireTelemetrySubscriptions(SmedleyTelemetrySession session, uint64_t epoch, bool current);
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL OpenTelemetrySession(SmedleyTelemetrySession *session)
    {
        if (session == nullptr) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        if (!IsServiceOwnerThread()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        *session = 0;
        const auto current = CurrentGameSession();
        if (!current.game_state) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        for (auto &slot : telemetry_sessions) {
            if (slot.handle != 0 && slot.epoch != current.epoch) {
                RetireTelemetrySubscriptions(slot.handle, slot.epoch, false);
                slot = {};
            }
            if (slot.handle != 0) continue;
            slot.epoch = current.epoch;
            slot.thread = std::this_thread::get_id();
            slot.handle = next_telemetry_session++;
            if (slot.handle == 0) slot.handle = next_telemetry_session++;
            *session = slot.handle;
            return SMEDLEY_TELEMETRY_GAME_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_GAME_CAPACITY;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL CloseTelemetrySession(SmedleyTelemetrySession session)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (auto &slot : telemetry_sessions) {
            if (slot.handle != session) continue;
            if (slot.thread != std::this_thread::get_id()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
            const bool current = CurrentGameSession().epoch == slot.epoch;
            RetireTelemetrySubscriptions(session, slot.epoch, current);
            slot = {};
            return SMEDLEY_TELEMETRY_GAME_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_GAME_STALE_HANDLE;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadWorld(
        SmedleyTelemetrySession session, SmedleyTelemetryWorldSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        TelemetryCurrentState state{};
        if (!ReadTelemetryCurrentState(&state)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        snapshot->date_raw = state.date_raw;
        snapshot->country_count = state.country_count();
        snapshot->country_ai_count = state.country_ai_count();
        snapshot->human_control_present = state.has_human_controlled_country() ? 1 : 0;
        snapshot->province_count = static_cast<uint32_t>(state.province_count());
        snapshot->availability_flags = (state.world_daily_available() ? 1u : 0u)
            | (state.military_available() ? 2u : 0u) | (state.province_count_available_value ? 4u : 0u);
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadMarket(
        SmedleyTelemetrySession session, SmedleyTelemetryMarketSnapshotV1 *market, uint32_t capacity, uint32_t *count)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        if (count == nullptr || (capacity != 0 && market == nullptr) || capacity > 64) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        *count = 0;
        static std::array<WorldMarketSnapshot, 64> internal{};
        uint32_t internal_count = 0;
        if (!CollectWorldMarket(CurrentGameSession().game_state, internal.data(), capacity, &internal_count)) {
            return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        }
        for (uint32_t index = 0; index < internal_count; ++index) {
            auto &out = market[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.good_ordinal = internal[index].good_ordinal; out.price_raw = internal[index].price_raw;
            out.last_price_raw = internal[index].last_price_raw; out.supply_raw = internal[index].supply_raw;
            out.demand_raw = internal[index].demand_raw; out.stock_raw = internal[index].worldmarket_stock_raw;
        }
        *count = internal_count;
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadCountry(
        SmedleyTelemetrySession session, int32_t ordinal, SmedleyTelemetryCountrySnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1) || ordinal < 0) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        const auto country = ResolveCountry(CurrentGameSession().game_state, ordinal);
        TelemetryCountrySnapshot value{};
        if (!country || !ReadTelemetryCountry(country, &value)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        snapshot->ordinal = ordinal; std::memcpy(snapshot->tag, value.tag().value, sizeof(snapshot->tag));
        snapshot->treasury_raw = value.treasury_raw();
        snapshot->unit_count = value.unit_count_candidate_value;
        snapshot->availability_flags = (value.daily_available() ? 1u : 0u) | (value.power_available() ? 2u : 0u)
            | (value.politics_available() ? 4u : 0u) | (value.military_available() ? 8u : 0u)
            | (value.diplomacy_status_available() ? 16u : 0u) | (value.diplomacy_relations_available() ? 32u : 0u);
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadProvince(
        SmedleyTelemetrySession session, int32_t id, SmedleyTelemetryProvinceSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1) || id < 0) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        TelemetryProvinceSnapshot value{};
        const auto province = ResolveProvince(CurrentGameSession().game_state, id);
        if (!province || !ReadTelemetryProvince(province, &value)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        snapshot->id = value.id_candidate(); std::memcpy(snapshot->owner_tag, value.owner_candidate().value, 4);
        std::memcpy(snapshot->controller_tag, value.controller_candidate().value, 4);
        snapshot->infrastructure_raw = value.infrastructure_candidate(); snapshot->colonial_level = value.colonial_level_candidate();
        snapshot->life_rating = value.life_rating_candidate(); snapshot->building_slots = static_cast<uint32_t>(value.building_slot_count_value);
        snapshot->constructions = value.construction_count_value;
        snapshot->availability_flags = (value.owner_available() ? 1u : 0u) | (value.daily_available() ? 2u : 0u)
            | (value.production_available() ? 4u : 0u);
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadPops(
        SmedleyTelemetrySession session, int32_t country_ordinal, SmedleyTelemetryPopSnapshotV1 *pops,
        uint32_t capacity, uint32_t *count, uint32_t *flags)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        if (count == nullptr || flags == nullptr || (capacity != 0 && pops == nullptr) || capacity > max_sample_pops) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        *count = 0; *flags = 0;
        static std::array<PopCandidate, max_sample_pops> candidates{};
        uint32_t candidate_count = 0; CountryEconomySnapshot quality{};
        const auto country = ResolveCountry(CurrentGameSession().game_state, country_ordinal);
        if (!country || !CollectCountryPops(country, CurrentGameSession().game_state, 0, candidates.data(), capacity,
                max_sample_destination_provinces, &candidate_count, &quality)) { *flags = quality.flags; return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE; }
        for (uint32_t index = 0; index < candidate_count; ++index) {
            PopDetailSnapshot detail{};
            if (!ReadPopDetailSnapshot(candidates[index].address, &detail)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
            auto &out = pops[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.pop_id = detail.pop_id; out.province_id = detail.province_id_candidate; out.pop_type_id = detail.pop_type_id_candidate;
            out.size = detail.size_candidate; out.employed = detail.employed_candidate; out.money_raw = detail.economy.money_raw; out.savings_raw = detail.economy.savings_raw;
        }
        *count = candidate_count; return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL ReadFactories(
        SmedleyTelemetrySession session, int32_t country_ordinal, SmedleyTelemetryFactorySnapshotV1 *factories,
        uint32_t capacity, uint32_t *count, uint32_t *flags)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        if (count == nullptr || flags == nullptr || (capacity != 0 && factories == nullptr) || capacity > max_sample_factories) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        *count = 0; *flags = 0;
        static std::array<FactorySnapshot, max_sample_factories> internal{};
        static std::array<FactoryInputSnapshot, max_sample_factory_inputs> inputs{};
        const auto country = ResolveCountry(CurrentGameSession().game_state, country_ordinal);
        uint32_t input_count = 0;
        if (!country || !CollectCountryFactories(country, internal.data(), capacity, count, inputs.data(), inputs.size(), &input_count,
                FACTORY_IDENTITY | FACTORY_EMPLOYMENT | FACTORY_PRODUCTION | FACTORY_FINANCE, flags)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        for (uint32_t index = 0; index < *count; ++index) {
            auto &out = factories[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.state_id = internal[index].state_id; out.anchor_province_id = internal[index].anchor_province_id_candidate;
            out.level = internal[index].level; out.employee_count = internal[index].employee_count;
            out.output_good_ordinal = internal[index].output_good_ordinal; out.budget_raw = internal[index].budget_raw;
            out.sales_income_raw = internal[index].sales_income_raw;
        }
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }

    struct HookSubscriptionSlot
    {
        struct Identity {
            uintptr_t address = 0;
            uint32_t id = 0;
        };
        static constexpr uint32_t identity_capacity = 131072;
        uint64_t handle = 0;
        SmedleyTelemetrySession session = 0;
        uint64_t epoch = 0;
        uint32_t hooks = 0;
        uint32_t next_identity = 1;
        std::thread::id thread{};
        std::array<Identity, identity_capacity> identities{};
    };
    std::array<HookSubscriptionSlot, 2> hook_subscriptions{};
    uint64_t next_hook_subscription = 1;
    HookSubscriptionSlot *FindSubscription(SmedleyTelemetryHookSubscription subscription)
    {
        for (auto &slot : hook_subscriptions) if (slot.handle == subscription) return &slot;
        return nullptr;
    }
    SmedleyTelemetryGameResult SubscriptionStatus(const HookSubscriptionSlot *slot)
    {
        if (slot == nullptr) return SMEDLEY_TELEMETRY_GAME_STALE_HANDLE;
        if (slot->thread != std::this_thread::get_id()) return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        const auto session = TelemetrySessionStatus(slot->session);
        if (session != SMEDLEY_TELEMETRY_GAME_SUCCESS) return session;
        return CurrentGameSession().epoch == slot->epoch ? SMEDLEY_TELEMETRY_GAME_SUCCESS
                                                         : SMEDLEY_TELEMETRY_GAME_STALE_HANDLE;
    }
    uint64_t OpaqueHookEntityId(HookSubscriptionSlot *slot, uintptr_t address) noexcept
    {
        const uint32_t mask = HookSubscriptionSlot::identity_capacity - 1;
        uint32_t index = static_cast<uint32_t>((address >> 4) ^ (address >> 17)) & mask;
        for (uint32_t attempt = 0; attempt <= mask; ++attempt, index = (index + 1) & mask) {
            auto &identity = slot->identities[index];
            if (identity.address == address) return (slot->handle << 32) | identity.id;
            if (identity.address != 0) continue;
            identity.address = address;
            identity.id = slot->next_identity++;
            if (identity.id == 0) identity.id = slot->next_identity++;
            return (slot->handle << 32) | identity.id;
        }
        return 0;
    }
    bool InstallHook(uint32_t hook, const uint32_t *artisan_country_keys, uint32_t artisan_country_count)
    {
        std::string error;
        if (hook == SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW) return InstallPopCashFlowHook(&error);
        if (hook == SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION) return InstallFactoryConsumptionHook(&error);
        if (hook == SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES) return InstallFactorySalesHook(&error);
        return InstallArtisanConsumptionHook(artisan_country_keys, artisan_country_count, &error);
    }
    bool UninstallHook(uint32_t hook)
    {
        std::string error;
        if (hook == SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW) return UninstallPopCashFlowHook(&error);
        if (hook == SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION) return UninstallFactoryConsumptionHook(&error);
        if (hook == SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES) return UninstallFactorySalesHook(&error);
        return UninstallArtisanConsumptionHook(&error);
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL SubscribeHooks(
        SmedleyTelemetrySession session, const SmedleyTelemetryHookOptionsV1 *options,
        SmedleyTelemetryHookSubscription *subscription)
    {
        constexpr uint32_t all_hooks = SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW | SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION
            | SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES | SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION;
        if (const auto status = TelemetrySessionStatus(session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        if (!ValidRecord(options, 1) || subscription == nullptr || options->hooks == 0 || (options->hooks & ~all_hooks) != 0
            || options->artisan_country_count > SMEDLEY_TELEMETRY_MAX_ARTISAN_COUNTRY_KEYS
            || ((options->hooks & SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION) == 0 && options->artisan_country_count != 0)) {
            return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        }
        *subscription = 0;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (auto &slot : hook_subscriptions) {
            if (slot.handle != 0) continue;
            uint32_t installed = 0;
            for (uint32_t hook = 1; hook <= SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION; hook <<= 1) {
                if ((options->hooks & hook) == 0) continue;
                if (!InstallHook(hook, options->artisan_country_keys, options->artisan_country_count)) {
                    for (uint32_t rollback = 1; rollback <= SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION; rollback <<= 1) {
                        if ((installed & rollback) != 0) UninstallHook(rollback);
                    }
                    return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
                }
                installed |= hook;
            }
            slot.handle = next_hook_subscription++;
            if (slot.handle == 0) slot.handle = next_hook_subscription++;
            slot.hooks = options->hooks;
            slot.session = session;
            slot.epoch = CurrentGameSession().epoch;
            slot.thread = std::this_thread::get_id();
            *subscription = slot.handle;
            return SMEDLEY_TELEMETRY_GAME_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_GAME_CAPACITY;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL DrainHooks(
        SmedleyTelemetryHookSubscription subscription, SmedleyTelemetryHookRecordV1 *records,
        uint32_t capacity, uint32_t *count, uint64_t *dropped)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindSubscription(subscription);
        if (const auto status = SubscriptionStatus(slot); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        if (count == nullptr || dropped == nullptr || (capacity != 0 && records == nullptr)) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        *count = 0; *dropped = 0;
        auto append = [&](uint32_t kind, uint64_t entity_id, const int64_t *values, uint32_t values_count,
                          uint32_t pool, uint32_t calls, uint32_t auxiliary_count) {
            if (*count >= capacity) return false;
            auto &out = records[(*count)++]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.kind = kind; out.entity_id = entity_id; out.pool = pool; out.value_count = values_count;
            out.call_count = calls; out.auxiliary_count = auxiliary_count;
            std::memcpy(out.values, values, values_count * sizeof(*values));
            return true;
        };
        if ((slot->hooks & SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW) != 0) {
            static std::array<PopCashFlowHookRecord, max_pop_cash_flow_records> source{};
            uint32_t source_count = 0; PopCashFlowHookStats stats{};
            if (!DrainPopCashFlowHook(source.data(), source.size(), &source_count, &stats)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
            *dropped += stats.invalid_index + stats.table_full + stats.overflow + stats.output_overflow;
            for (uint32_t index = 0; index < source_count; ++index) {
                int64_t values[16]{};
                std::memcpy(values, source[index].posted_raw.data(), sizeof(source[index].posted_raw));
                std::memcpy(values + 8, source[index].money_delta_raw.data(), sizeof(source[index].money_delta_raw));
                const auto entity = OpaqueHookEntityId(slot, source[index].pop.address());
                if (entity == 0 || !append(SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW, entity, values, 16, 0,
                        source[index].call_count, source[index].clamped_call_count)) ++*dropped;
            }
        }
        if ((slot->hooks & SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION) != 0) {
            static std::array<FactorySettlementHookRecord, max_factory_flow_records> source{};
            uint32_t source_count = 0; uint64_t source_dropped = 0;
            if (!DrainFactoryConsumptionHook(source.data(), source.size(), &source_count, &source_dropped)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
            *dropped += source_dropped;
            for (uint32_t index = 0; index < source_count; ++index) {
                const auto entity = OpaqueHookEntityId(slot, source[index].factory.address());
                if (entity == 0 || !append(SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION,
                        entity, source[index].quantity_raw.data(), 64, source[index].pool, 0, 0)) ++*dropped;
            }
        }
        if ((slot->hooks & SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES) != 0) {
            static std::array<FactorySalesHookRecord, max_factory_sales_records> source{};
            uint32_t source_count = 0; uint64_t source_dropped = 0;
            if (!DrainFactorySalesHook(source.data(), source.size(), &source_count, &source_dropped)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
            *dropped += source_dropped;
            for (uint32_t index = 0; index < source_count; ++index) {
                const int64_t values[] = {source[index].proceeds_raw, source[index].produced_raw, source[index].opening_inventory_raw, source[index].closing_inventory_raw};
                const auto entity = OpaqueHookEntityId(slot, source[index].factory.address());
                if (entity == 0 || !append(SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES, entity, values, 4, 0, 0, 0)) ++*dropped;
            }
        }
        if ((slot->hooks & SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION) != 0) {
            static std::array<ArtisanSettlementHookRecord, max_artisan_flow_records> source{};
            uint32_t source_count = 0; uint64_t source_dropped = 0;
            if (!DrainArtisanConsumptionHook(source.data(), source.size(), &source_count, &source_dropped)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
            *dropped += source_dropped;
            for (uint32_t index = 0; index < source_count; ++index) {
                const auto entity = OpaqueHookEntityId(slot, source[index].pop.address());
                if (entity == 0 || !append(SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION,
                        entity, source[index].quantity_raw.data(), 64, source[index].pool, 0, 0)) ++*dropped;
            }
        }
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL UnsubscribeHooks(SmedleyTelemetryHookSubscription subscription)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindSubscription(subscription);
        if (const auto status = SubscriptionStatus(slot); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) return status;
        for (uint32_t hook = 1; hook <= SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION; hook <<= 1) {
            if ((slot->hooks & hook) != 0 && !UninstallHook(hook)) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        }
        *slot = {};
        return SMEDLEY_TELEMETRY_GAME_SUCCESS;
    }
    void RetireTelemetrySubscriptions(SmedleyTelemetrySession session, uint64_t epoch, bool current)
    {
        for (auto &subscription : hook_subscriptions) {
            if (subscription.session != session) continue;
            if (current && subscription.epoch == epoch && subscription.thread == std::this_thread::get_id()) {
                for (uint32_t hook = 1; hook <= SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION; hook <<= 1) {
                    if ((subscription.hooks & hook) != 0) UninstallHook(hook);
                }
            }
            subscription = {};
        }
    }
}

namespace smedley::game_state
{
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
        *binding = {};
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
            *bank_authority = {};
            bank_authority_slot_active[index] = false;
            bank_authority = previous;
        }
    }
}

SMEDLEY_CAMPAIGN_RUNTIME_EXPORT SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL
SmedleyGetCampaignRuntimeApiV1(SmedleyCampaignRuntimeApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
    api->open_session = &OpenCampaignSession;
    api->close_session = &CloseCampaignSession;
    api->read_campaign = &ReadCampaign;
    api->set_paused = &SetPaused;
    api->set_speed_index = &SetSpeed;
    api->request_quit = &RequestQuit;
    api->acquire_frontend = &AcquireFrontend;
    api->release_frontend = &ReleaseFrontend;
    api->request_save = &RequestSave;
    api->read_save = &ReadSave;
    api->dispatch_frontend_control = &DispatchControl;
    api->dispatch_main_menu_single_player = &DispatchMainMenuSinglePlayer;
    api->read_observer_state = &ReadObserverState;
    api->read_observer_country = &ReadObserverCountry;
    api->find_observer_country = &FindObserverCountry;
    api->set_observer_view_country = &SetObserverViewCountry;
    api->return_observer_country = &ReturnObserverCountry;
    api->enable_observer_fow = &EnableObserverFow;
    return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
}

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

SMEDLEY_TELEMETRY_GAME_EXPORT SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL
SmedleyGetTelemetryGameApiV1(SmedleyTelemetryGameApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_TELEMETRY_GAME_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
    api->open_session = &OpenTelemetrySession;
    api->close_session = &CloseTelemetrySession;
    api->read_world = &ReadWorld;
    api->read_market = &ReadMarket;
    api->read_country = &ReadCountry;
    api->read_province = &ReadProvince;
    api->read_pops = &ReadPops;
    api->read_factories = &ReadFactories;
    api->subscribe_hooks = &SubscribeHooks;
    api->drain_hooks = &DrainHooks;
    api->unsubscribe_hooks = &UnsubscribeHooks;
    return SMEDLEY_TELEMETRY_GAME_SUCCESS;
}

static_assert(sizeof(SmedleyCampaignRuntimeSnapshotV1) == 32, "campaign runtime snapshot ABI v1 layout changed");
static_assert(sizeof(SmedleyFrontendSaveSnapshotV1) == 288, "frontend save ABI v1 layout changed");
static_assert(sizeof(SmedleyObserverCountrySnapshotV1) == 44, "observer country ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestStateSnapshotV1) == 64, "interest state ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPopSnapshotV1) == 40, "interest pop ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPayoutV1) == 40, "interest payout ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPayoutResultV1) == 32, "interest payout result ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestInitializationV1) == 48, "interest initialization ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryHookOptionsV1) == 92, "telemetry hook options ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryHookRecordV1) == 560, "telemetry hook ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignRuntimeApiV1) == 88, "campaign runtime API v1 layout changed");
static_assert(sizeof(SmedleyInterestPoolApiV1) == 32, "interest pool API v1 layout changed");
static_assert(sizeof(SmedleyTelemetryGameApiV1) == 60, "telemetry game API v1 layout changed");
static_assert(sizeof(SmedleyTelemetryWorldSnapshotV1) == 44, "telemetry world ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryMarketSnapshotV1) == 72, "telemetry market ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryCountrySnapshotV1) == 48, "telemetry country ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryProvinceSnapshotV1) == 64, "telemetry province ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryPopSnapshotV1) == 64, "telemetry POP ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryFactorySnapshotV1) == 64, "telemetry factory ABI v1 layout changed");
