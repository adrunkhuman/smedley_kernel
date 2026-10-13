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
    std::recursive_mutex metadata_mutex;
    std::thread::id service_owner_thread;

    bool IsServiceOwnerThread()
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (service_owner_thread == std::thread::id{}) service_owner_thread = std::this_thread::get_id();
        return service_owner_thread == std::this_thread::get_id();
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

    struct CampaignSessionSlot {
        uint64_t handle = 0;
        uint64_t epoch = 0;
        std::thread::id thread{};
        std::thread::id engine_thread{};
    };
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
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (handle == 0) return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        for (auto &slot : campaign_sessions) {
            if (slot.handle == handle) {
                const auto current_thread = std::this_thread::get_id();
                if (slot.thread != current_thread && slot.engine_thread != current_thread) {
                    return SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD;
                }
                const auto current = CurrentGameSession();
                if (current.epoch != slot.epoch) {
                    RetireCampaignFrontendSlots(handle);
                    slot.epoch = current.epoch;
                    RefreshCampaignAutomationEpoch(handle, current.epoch);
                }
                return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
            }
        }
        return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
    }

    SmedleyCampaignRuntimeResult FrontendSessionStatus(SmedleyCampaignSession handle)
    {
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (handle == 0) return SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE;
        for (auto &slot : campaign_sessions) {
            if (slot.handle != handle) continue;
            slot.engine_thread = std::this_thread::get_id();
            const auto current = CurrentGameSession();
            if (current.epoch != slot.epoch) {
                RetireCampaignFrontendSlots(handle);
                slot.epoch = current.epoch;
                RefreshCampaignAutomationEpoch(handle, current.epoch);
            }
            return SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS;
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
        if (!smedley::IsCurrentExecutableSupported()) return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        for (auto &slot : campaign_sessions) {
            if (slot.handle != 0 && slot.epoch != current.epoch) {
                RetireCampaignFrontendSlots(slot.handle);
                RetireCampaignAutomations(slot.handle);
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
            RetireCampaignAutomations(session);
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

    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL ReadCampaignV2(
        SmedleyCampaignSession session, SmedleyCampaignRuntimeSnapshotV2 *snapshot)
    {
        if (!ValidRecord(snapshot, 2)) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        CampaignRuntimeSnapshot value{};
        const auto status = ReadCampaignRuntimeWithGameOver(&value);
        if (status != CampaignRuntimeObservationStatus::completed) return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        snapshot->game_date_raw = value.date_raw;
        snapshot->speed_index = value.speed_index;
        snapshot->paused = value.paused ? 1 : 0;
        snapshot->game_over = value.game_over ? 1 : 0;
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
        FrontendControllerToken token{};
        const auto controller_status = AcquireFrontendController(static_cast<FrontendControllerKind>(kind), &token);
        if (controller_status != FrontendOperationStatus::completed) return FrontendResult(controller_status);
        if (const auto status = FrontendSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (const auto &slot : frontend_slots) {
            if (slot.owner == session && slot.kind == kind && slot.epoch == CurrentGameSession().epoch) {
                return SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED;
            }
        }
        for (auto &slot : frontend_slots) {
            if (slot.handle != 0) continue;
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
        const auto session = FrontendSessionStatus(slot->owner);
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
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        auto *slot = FindFrontend(controller);
        if (const auto status = FrontendStatus(slot); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) return status;
        char text[SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES]{};
        std::memcpy(text, name, bytes);
        return FrontendResult(DispatchFrontendControl(slot->token, text));
    }
    SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL DispatchMainMenuSinglePlayer(SmedleyFrontendController controller)
    {
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
}

using namespace smedley::game_state::services;

SMEDLEY_CAMPAIGN_RUNTIME_EXPORT SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL
SmedleyGetCampaignRuntimeApiV2(SmedleyCampaignRuntimeApiV2 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V2
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT;
    api->open_session = &OpenCampaignSession;
    api->close_session = &CloseCampaignSession;
    api->read_campaign = &ReadCampaignV2;
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

static_assert(sizeof(SmedleyCampaignRuntimeSnapshotV2) == 32, "campaign runtime snapshot ABI v2 layout changed");
static_assert(sizeof(SmedleyFrontendSaveSnapshotV1) == 288, "frontend save ABI v1 layout changed");
static_assert(sizeof(SmedleyObserverCountrySnapshotV1) == 44, "observer country ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignRuntimeApiV2) == 88, "campaign runtime API v2 layout changed");
