#include <smedley/campaign_runtime_api.h>
#include <smedley/campaign_automation_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/telemetry_game_api.h>
#include <smedley/telemetry_observation_api.h>

#include <smedley/events/bankinterest.hpp>
#include <smedley/event_abi_runtime.hpp>
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

    struct CampaignAutomationSlot {
        std::atomic<uint32_t> control{0};
        std::atomic<uint64_t> handle{0};
        std::atomic<SmedleyCampaignSession> session{0};
        std::atomic<uint64_t> epoch{0};
        std::thread::id thread{};
        std::atomic<uint64_t> context{0};
        std::atomic<SmedleyCampaignFrontendCaptureCallbackV1Fn> frontend_capture{nullptr};
        std::atomic<SmedleyCampaignAnnexationCallbackV1Fn> annexation{nullptr};
        std::atomic<SmedleyCampaignConsoleCaptureCallbackV1Fn> console_capture{nullptr};
    };
    std::array<CampaignAutomationSlot, SMEDLEY_CAMPAIGN_AUTOMATION_MAX_REGISTRATIONS> campaign_automation_slots{};
    uint64_t next_campaign_automation = 1;
    thread_local SmedleyCampaignAutomation callback_automation = 0;
    constexpr uint32_t automation_active = UINT32_C(1) << 31;
    constexpr uint32_t automation_removing = UINT32_C(1) << 30;
    constexpr uint32_t automation_references = ~(automation_active | automation_removing);
    void RetireCampaignAutomations(SmedleyCampaignSession session);

    bool AcquireCampaignAutomationCallback(CampaignAutomationSlot *slot) noexcept
    {
        auto control = slot->control.load(std::memory_order_acquire);
        for (;;) {
            if ((control & automation_active) == 0 || (control & automation_references) == automation_references) return false;
            if (slot->control.compare_exchange_weak(control, control + 1, std::memory_order_acq_rel)) return true;
        }
    }

    void ReleaseCampaignAutomationCallback(CampaignAutomationSlot *slot) noexcept
    {
        slot->control.fetch_sub(1, std::memory_order_release);
    }

    SmedleyCampaignAutomationResult AutomationResult(CampaignOperationStatus status)
    {
        switch (status) {
        case CampaignOperationStatus::completed: return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
        case CampaignOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_AUTOMATION_READBACK_FAILED;
        case CampaignOperationStatus::outside_campaign: return SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE;
        default: return SMEDLEY_CAMPAIGN_AUTOMATION_PRECONDITION_FAILED;
        }
    }

    SmedleyCampaignAutomationResult AutomationResult(FrontendOperationStatus status)
    {
        switch (status) {
        case FrontendOperationStatus::completed: return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
        case FrontendOperationStatus::invalid_token: return SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        case FrontendOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_AUTOMATION_READBACK_FAILED;
        case FrontendOperationStatus::unavailable: return SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE;
        default: return SMEDLEY_CAMPAIGN_AUTOMATION_PRECONDITION_FAILED;
        }
    }

    SmedleyCampaignAutomationResult AutomationResult(ObserverOperationStatus status)
    {
        switch (status) {
        case ObserverOperationStatus::completed: return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
        case ObserverOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_AUTOMATION_READBACK_FAILED;
        case ObserverOperationStatus::outside_campaign: return SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE;
        default: return SMEDLEY_CAMPAIGN_AUTOMATION_PRECONDITION_FAILED;
        }
    }

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
                    for (auto &automation : campaign_automation_slots) {
                        if (automation.session.load(std::memory_order_acquire) == handle) {
                            automation.epoch.store(current.epoch, std::memory_order_release);
                        }
                    }
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
                for (auto &automation : campaign_automation_slots) {
                    if (automation.session.load(std::memory_order_acquire) == handle) {
                        automation.epoch.store(current.epoch, std::memory_order_release);
                    }
                }
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
        CampaignRuntimeSnapshot snapshot{};
        if (ReadCampaignRuntime(&snapshot) != CampaignRuntimeObservationStatus::completed || !current.game_state) {
            return SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE;
        }
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

    CampaignAutomationSlot *FindCampaignAutomation(SmedleyCampaignAutomation automation)
    {
        for (auto &slot : campaign_automation_slots) {
            if (slot.handle.load(std::memory_order_acquire) == automation) return &slot;
        }
        return nullptr;
    }
    SmedleyCampaignAutomationResult CampaignAutomationStatus(const CampaignAutomationSlot *slot)
    {
        if (slot == nullptr) return SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        const auto session = CampaignSessionStatus(slot->session.load(std::memory_order_acquire));
        if (session != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) {
            return session == SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD ? SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD
                : SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        }
        return CurrentGameSession().epoch == slot->epoch.load(std::memory_order_acquire) ? SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS
                                                         : SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
    }
    void DisableCampaignAutomation(CampaignAutomationSlot *slot)
    {
        if (slot == nullptr) return;
        auto control = slot->control.load(std::memory_order_acquire);
        while ((control & automation_active) != 0
            && !slot->control.compare_exchange_weak(control, automation_removing | (control & automation_references), std::memory_order_acq_rel)) {}
        while ((slot->control.load(std::memory_order_acquire) & automation_references) != 0) std::this_thread::yield();
        SetCampaignAutomationFrontendCaptureCallback(nullptr);
        SetCampaignAutomationAnnexationCallback(nullptr);
        SetCampaignAutomationConsoleCaptureCallback(nullptr);
        SetCampaignAutomationMessagePopupSuppression(false);
        UnregisterCampaignAutomationConsoleCapture();
        DeactivateCampaignAutomationHooks();
        DeactivateCampaignAutomationFrontend();
        slot->frontend_capture.store(nullptr, std::memory_order_release);
        slot->annexation.store(nullptr, std::memory_order_release);
        slot->console_capture.store(nullptr, std::memory_order_release);
        slot->context.store(0, std::memory_order_release);
        slot->epoch.store(0, std::memory_order_release);
        slot->session.store(0, std::memory_order_release);
        slot->handle.store(0, std::memory_order_release);
        slot->control.store(0, std::memory_order_release);
    }
    void RetireCampaignAutomations(SmedleyCampaignSession session)
    {
        for (auto &slot : campaign_automation_slots) {
            if (slot.session.load(std::memory_order_acquire) == session) DisableCampaignAutomation(&slot);
        }
    }
    void __stdcall OnCampaignAutomationFrontendCapture(FrontendControllerKind kind) noexcept
    {
        auto *slot = &campaign_automation_slots[0];
        if (!AcquireCampaignAutomationCallback(slot)) return;
        const auto callback = slot->frontend_capture.load(std::memory_order_acquire);
        const auto context = slot->context.load(std::memory_order_acquire);
        const auto epoch = CurrentGameSession().epoch;
        slot->epoch.store(epoch, std::memory_order_release);
        if (callback == nullptr) {
            ReleaseCampaignAutomationCallback(slot); return;
        }
        SmedleyCampaignFrontendCaptureV1 event{};
        event.struct_size = sizeof(event); event.version = 1; event.controller_kind = static_cast<uint32_t>(kind);
        event.epoch = epoch;
        SmedleyCampaignAutomationCallbackResult result = SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_DISABLE;
        const auto previous = callback_automation; callback_automation = slot->handle.load(std::memory_order_acquire);
        try { result = callback(context, &event); } catch (...) {}
        callback_automation = previous;
        if (result != SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE) slot->frontend_capture.store(nullptr, std::memory_order_release);
        ReleaseCampaignAutomationCallback(slot);
    }
    void __stdcall OnCampaignAutomationAnnexation(int32_t annexed_ordinal) noexcept
    {
        auto *slot = &campaign_automation_slots[0];
        if (!AcquireCampaignAutomationCallback(slot)) return;
        const auto callback = slot->annexation.load(std::memory_order_acquire);
        const auto context = slot->context.load(std::memory_order_acquire);
        const auto epoch = CurrentGameSession().epoch;
        slot->epoch.store(epoch, std::memory_order_release);
        if (callback == nullptr) {
            ReleaseCampaignAutomationCallback(slot); return;
        }
        SmedleyCampaignAnnexationV1 event{};
        event.struct_size = sizeof(event); event.version = 1; event.annexed_ordinal = annexed_ordinal; event.epoch = epoch;
        SmedleyCampaignAutomationCallbackResult result = SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_DISABLE;
        const auto previous = callback_automation; callback_automation = slot->handle.load(std::memory_order_acquire);
        try { result = callback(context, &event); } catch (...) {}
        callback_automation = previous;
        if (result != SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE) slot->annexation.store(nullptr, std::memory_order_release);
        ReleaseCampaignAutomationCallback(slot);
    }
    void __stdcall OnCampaignAutomationConsoleCapture(CampaignConsoleCaptureStatus status) noexcept
    {
        auto *slot = &campaign_automation_slots[0];
        if (!AcquireCampaignAutomationCallback(slot)) return;
        const auto callback = slot->console_capture.load(std::memory_order_acquire);
        const auto context = slot->context.load(std::memory_order_acquire);
        const auto epoch = CurrentGameSession().epoch;
        slot->epoch.store(epoch, std::memory_order_release);
        if (callback == nullptr) {
            ReleaseCampaignAutomationCallback(slot); return;
        }
        SmedleyCampaignConsoleCaptureV1 event{};
        event.struct_size = sizeof(event); event.version = 1; event.status = static_cast<uint32_t>(status);
        event.ready = IsCampaignObserverConsoleReady() ? 1 : 0; event.epoch = epoch;
        SmedleyCampaignAutomationCallbackResult result = SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_DISABLE;
        const auto previous = callback_automation; callback_automation = slot->handle.load(std::memory_order_acquire);
        try { result = callback(context, &event); } catch (...) {}
        callback_automation = previous;
        if (result != SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE) slot->console_capture.store(nullptr, std::memory_order_release);
        ReleaseCampaignAutomationCallback(slot);
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL InstallCampaignAutomation(
        SmedleyCampaignSession session, const SmedleyCampaignAutomationOptionsV1 *options, SmedleyCampaignAutomation *automation)
    {
        if (!ValidRecord(options, 1) || automation == nullptr) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) {
            return status == SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD ? SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD
                : SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        }
        *automation = 0;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (const auto &slot : campaign_automation_slots) {
            if (slot.handle.load(std::memory_order_acquire) != 0
                && slot.epoch.load(std::memory_order_acquire) == CurrentGameSession().epoch) return SMEDLEY_CAMPAIGN_AUTOMATION_CAPACITY;
        }
        const auto frontend = ActivateCampaignAutomationFrontend();
        if (frontend != FrontendOperationStatus::completed) return AutomationResult(frontend);
        const auto campaign = ActivateCampaignAutomationHooks();
        if (campaign != CampaignOperationStatus::completed) {
            DeactivateCampaignAutomationFrontend();
            return AutomationResult(campaign);
        }
        if (!RegisterCampaignAutomationConsoleCapture()) {
            DeactivateCampaignAutomationHooks();
            DeactivateCampaignAutomationFrontend();
            return SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE;
        }
        auto &slot = campaign_automation_slots[0];
        auto handle = next_campaign_automation++;
        if (handle == 0) handle = next_campaign_automation++;
        slot.thread = std::this_thread::get_id();
        slot.context.store(options->context, std::memory_order_relaxed);
        slot.frontend_capture.store(options->frontend_capture, std::memory_order_relaxed);
        slot.annexation.store(options->annexation, std::memory_order_relaxed);
        slot.console_capture.store(options->console_capture, std::memory_order_relaxed);
        slot.session.store(session, std::memory_order_relaxed);
        slot.epoch.store(CurrentGameSession().epoch, std::memory_order_relaxed);
        slot.handle.store(handle, std::memory_order_relaxed);
        slot.control.store(automation_active, std::memory_order_release);
        SetCampaignAutomationFrontendCaptureCallback(options->frontend_capture == nullptr ? nullptr : &OnCampaignAutomationFrontendCapture);
        SetCampaignAutomationAnnexationCallback(options->annexation == nullptr ? nullptr : &OnCampaignAutomationAnnexation);
        SetCampaignAutomationConsoleCaptureCallback(options->console_capture == nullptr ? nullptr : &OnCampaignAutomationConsoleCapture);
        *automation = handle;
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL DeactivateCampaignAutomation(
        SmedleyCampaignAutomation automation)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD;
        auto *slot = FindCampaignAutomation(automation);
        if (callback_automation == automation) return SMEDLEY_CAMPAIGN_AUTOMATION_BUSY;
        if (slot == nullptr) return SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        if (slot->thread != std::this_thread::get_id()) return SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        const auto status = CampaignAutomationStatus(slot);
        DisableCampaignAutomation(slot);
        return status;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL SetAutomationObserverMode(
        SmedleyCampaignAutomation automation, uint32_t enabled)
    {
        if (enabled > 1) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        auto *slot = FindCampaignAutomation(automation);
        if (const auto status = CampaignAutomationStatus(slot); status != SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS) return status;
        SetCampaignAutomationObserverMode(enabled != 0);
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL ReadAutomationConsoleState(
        SmedleyCampaignAutomation automation, SmedleyCampaignConsoleStateV1 *state)
    {
        if (!ValidRecord(state, 1)) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        auto *slot = FindCampaignAutomation(automation);
        if (const auto status = CampaignAutomationStatus(slot); status != SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS) return status;
        state->ready = IsCampaignObserverConsoleReady() ? 1 : 0;
        state->epoch = slot->epoch.load(std::memory_order_acquire);
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL ReadObserverCountryByTag(
        SmedleyCampaignSession session, const SmedleyCampaignTagV1 *tag, SmedleyObserverCountrySnapshotV1 *country)
    {
        if (!ValidRecord(tag, 1) || !ValidRecord(country, 1) || tag->tag[3] != '\0') return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) {
            return status == SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD ? SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD
                : SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        }
        ObserverCountrySnapshot internal{};
        if (ResolveObserverCountry(tag->tag, &internal) != ObserverObservationStatus::completed) return SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE;
        CopyObserverCountry(internal, country);
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL StartAutomationObserverTagSwitch(
        SmedleyCampaignAutomation automation, const SmedleyCampaignTagV1 *tag, SmedleyCampaignConsoleCommandResultV1 *result)
    {
        if (!ValidRecord(tag, 1) || !ValidRecord(result, 1) || tag->tag[3] != '\0') return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        auto *slot = FindCampaignAutomation(automation);
        if (const auto status = CampaignAutomationStatus(slot); status != SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS) return status;
        ObserverTag internal{}; std::memcpy(internal.value, tag->tag, sizeof(internal.value)); internal.ordinal = tag->ordinal;
        CampaignConsoleCommandResult native{};
        const auto status = StartNativeObserverTagSwitch(internal, &native);
        result->success = 0; result->message_available = 0; result->message_bytes = 0;
        std::memset(result->message, 0, sizeof(result->message));
        result->success = native.success ? 1 : 0; result->message_available = native.message_available ? 1 : 0;
        while (result->message_bytes < sizeof(native.message) && native.message[result->message_bytes] != '\0') {
            ++result->message_bytes;
        }
        std::memcpy(result->message, native.message, result->message_bytes);
        return AutomationResult(status);
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL SetAutomationPopupSuppression(
        SmedleyCampaignAutomation automation, uint32_t enabled)
    {
        if (enabled > 1) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        auto *slot = FindCampaignAutomation(automation);
        if (const auto status = CampaignAutomationStatus(slot); status != SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS) return status;
        SetCampaignAutomationMessagePopupSuppression(enabled != 0);
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL ReadAutomationPopupState(
        SmedleyCampaignAutomation automation, SmedleyCampaignPopupSnapshotV1 *snapshot)
    {
        if (!ValidRecord(snapshot, 1)) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        auto *slot = FindCampaignAutomation(automation);
        if (const auto status = CampaignAutomationStatus(slot); status != SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS) return status;
        snapshot->suppression_enabled = IsCampaignMessagePopupSuppressionEnabled() ? 1 : 0;
        snapshot->suppressed_count = static_cast<uint64_t>(CampaignSuppressedMessageCount());
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
    }
    SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL ReadAutomationProcessMetrics(
        SmedleyCampaignSession session, SmedleyCampaignProcessMetricsV1 *metrics)
    {
        if (!ValidRecord(metrics, 1)) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
        if (const auto status = CampaignSessionStatus(session); status != SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS) {
            return status == SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD ? SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD
                : SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE;
        }
        const auto source = SampleProcessMetrics();
        metrics->availability_flags = 0;
        metrics->process_cpu_us = 0; metrics->working_set_bytes = 0;
        metrics->private_bytes = 0; metrics->peak_working_set_bytes = 0;
        if (source.process_cpu_us) { metrics->availability_flags |= 1; metrics->process_cpu_us = *source.process_cpu_us; }
        if (source.working_set_bytes) { metrics->availability_flags |= 2; metrics->working_set_bytes = *source.working_set_bytes; }
        if (source.private_bytes) { metrics->availability_flags |= 4; metrics->private_bytes = *source.private_bytes; }
        if (source.process_peak_working_set_bytes) { metrics->availability_flags |= 8; metrics->peak_working_set_bytes = *source.process_peak_working_set_bytes; }
        return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
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

    struct TelemetrySessionSlot {
        static constexpr uint32_t identity_capacity = max_sample_pops + max_sample_factories;
        uint64_t handle = 0;
        uint64_t epoch = 0;
        std::thread::id thread{};
        smedley::game_state::TelemetryEntityIndex<identity_capacity> identities{};
    };
    std::array<TelemetrySessionSlot, 8> telemetry_sessions{};
    uint64_t next_telemetry_session = 1;
    void ResetTelemetrySession(TelemetrySessionSlot *slot)
    {
        if (slot == nullptr) return;
        slot->handle = 0;
        slot->epoch = 0;
        slot->thread = {};
        slot->identities.reset();
    }
    SmedleyTelemetryGameResult TelemetrySessionStatus(SmedleyTelemetrySession handle)
    {
        if (!smedley::IsDailyEventApiDispatchThread() && !IsServiceOwnerThread()) {
            return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        }
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
    TelemetrySessionSlot *FindTelemetrySession(SmedleyTelemetrySession handle)
    {
        for (auto &slot : telemetry_sessions) if (slot.handle == handle && handle != 0) return &slot;
        return nullptr;
    }
    uint64_t OpaqueTelemetryEntityId(TelemetrySessionSlot *slot, uintptr_t address) noexcept
    {
        if (slot == nullptr || address == 0) return 0;
        return smedley::game_state::TelemetryOpaqueEntityHandle(
            static_cast<uint32_t>(slot->handle), slot->identities.find_or_insert(address));
    }
    uintptr_t OpaqueTelemetryEntityAddress(const TelemetrySessionSlot *slot, uint64_t handle) noexcept
    {
        if (slot == nullptr || handle == 0 || (handle >> 32) != static_cast<uint32_t>(slot->handle)) return 0;
        return slot->identities.find(static_cast<uint32_t>(handle));
    }
    void RetireTelemetrySubscriptions(SmedleyTelemetrySession session, uint64_t epoch, bool current);
    void RetireTelemetryObservations(SmedleyTelemetrySession session);
    SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL OpenTelemetrySession(SmedleyTelemetrySession *session)
    {
        if (session == nullptr) return SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT;
        if (!smedley::IsDailyEventApiDispatchThread() && !IsServiceOwnerThread()) {
            return SMEDLEY_TELEMETRY_GAME_WRONG_THREAD;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        *session = 0;
        const auto current = CurrentGameSession();
        if (!current.game_state) return SMEDLEY_TELEMETRY_GAME_UNAVAILABLE;
        for (auto &slot : telemetry_sessions) {
            if (slot.handle != 0 && slot.epoch != current.epoch) {
                RetireTelemetrySubscriptions(slot.handle, slot.epoch, false);
                RetireTelemetryObservations(slot.handle);
                ResetTelemetrySession(&slot);
            }
            if (slot.handle != 0) continue;
            if (next_telemetry_session > UINT32_MAX) return SMEDLEY_TELEMETRY_GAME_CAPACITY;
            slot.epoch = current.epoch;
            slot.thread = std::this_thread::get_id();
            slot.handle = next_telemetry_session++;
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
            const bool current = CurrentGameSession().epoch == slot.epoch;
            RetireTelemetrySubscriptions(session, slot.epoch, current);
            RetireTelemetryObservations(session);
            ResetTelemetrySession(&slot);
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

    struct TelemetryObservationSessionSlot
    {
        uint64_t handle = 0;
        SmedleyTelemetrySession parent = 0;
        uint64_t epoch = 0;
        std::thread::id thread{};
    };
    std::array<TelemetryObservationSessionSlot, 8> telemetry_observation_sessions{};
    uint64_t next_telemetry_observation_session = 1;

    SmedleyTelemetryObservationResult ObservationSessionStatus(SmedleyTelemetryObservationSession session,
                                                                 TelemetryObservationSessionSlot **result = nullptr)
    {
        if (!smedley::IsDailyEventApiDispatchThread() && !IsServiceOwnerThread()) {
            return SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (auto &slot : telemetry_observation_sessions) {
            if (session == 0 || slot.handle != session) continue;
            if (slot.thread != std::this_thread::get_id()) return SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD;
            const auto parent = TelemetrySessionStatus(slot.parent);
            if (parent == SMEDLEY_TELEMETRY_GAME_WRONG_THREAD) return SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD;
            if (parent != SMEDLEY_TELEMETRY_GAME_SUCCESS || CurrentGameSession().epoch != slot.epoch) {
                return SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE;
            }
            if (result != nullptr) *result = &slot;
            return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE;
    }
    bool ValidDailyEvent(const SmedleyDailyEventV1 *event)
    {
        if (event == nullptr || event->struct_size != sizeof(*event) || event->version != SMEDLEY_DAILY_EVENT_VERSION_V1
            || event->country_tag[3] != '\0') return false;
        for (const auto value : event->reserved) if (value != 0) return false;
        for (uint32_t index = 0; index < 3; ++index) {
            const char value = event->country_tag[index];
            if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) return false;
        }
        return true;
    }
    CountryRef ResolveObservationCountry(const void *context, int32_t ordinal)
    {
        return ResolveCountry(*static_cast<const GameStateRef *>(context), ordinal);
    }
    ProvinceRef ResolveObservationProvince(const void *context, int32_t id)
    {
        return ResolveProvince(*static_cast<const GameStateRef *>(context), id);
    }
    uint32_t ObservationCountryFlags(const TelemetryCountrySnapshot &value)
    {
        return (value.daily_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DAILY : 0)
            | (value.power_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POWER : 0)
            | (value.politics_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POLITICS : 0)
            | (value.military_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_MILITARY : 0)
            | (value.diplomacy_status_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_STATUS : 0)
            | (value.diplomacy_relations_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_RELATIONS : 0);
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL OpenObservationSession(
        SmedleyTelemetrySession parent_session, SmedleyTelemetryObservationSession *session)
    {
        if (session == nullptr) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        if (const auto status = TelemetrySessionStatus(parent_session); status != SMEDLEY_TELEMETRY_GAME_SUCCESS) {
            return status == SMEDLEY_TELEMETRY_GAME_WRONG_THREAD ? SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD
                : SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        *session = 0;
        const auto current = CurrentGameSession();
        for (auto &slot : telemetry_observation_sessions) {
            if (slot.handle != 0) continue;
            slot.handle = next_telemetry_observation_session++;
            if (slot.handle == 0) slot.handle = next_telemetry_observation_session++;
            slot.epoch = current.epoch;
            slot.parent = parent_session;
            slot.thread = std::this_thread::get_id();
            *session = slot.handle;
            return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_OBSERVATION_CAPACITY;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL CloseObservationSession(
        SmedleyTelemetryObservationSession session)
    {
        if (!IsServiceOwnerThread()) return SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        for (auto &slot : telemetry_observation_sessions) {
            if (slot.handle != session || session == 0) continue;
            slot = {};
            return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
        }
        return SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE;
    }
    void RetireTelemetryObservations(SmedleyTelemetrySession session)
    {
        for (auto &observation : telemetry_observation_sessions) {
            if (observation.parent == session) observation = {};
        }
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationWorld(
        SmedleyTelemetryObservationSession session, SmedleyTelemetryWorldObservationV1 *world)
    {
        if (!ValidRecord(world, 1)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        TelemetryCurrentState source{};
        if (!ReadTelemetryCurrentState(&source)) return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        world->date_raw = source.date_raw;
        world->country_count = source.country_count();
        world->country_ai_count = source.country_ai_count();
        world->human_control_present = source.has_human_controlled_country() ? 1u : 0u;
        world->province_count = static_cast<uint32_t>(source.province_count());
        world->ongoing_war_count = source.ongoing_war_count_value;
        world->availability_flags = (source.world_daily_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_DAILY : 0)
            | (source.military_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_MILITARY : 0);
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationMarket(
        SmedleyTelemetryObservationSession session, uint32_t groups, SmedleyTelemetryMarketObservationV1 *markets,
        uint32_t capacity, uint32_t *count)
    {
        if (count == nullptr || (capacity != 0 && markets == nullptr)
            || capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_GOODS || groups == 0
            || (groups & ~(SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_PRICE | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SUPPLY
                | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_DEMAND | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SALES)) != 0) {
            return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *count = 0;
        static std::array<WorldMarketSnapshot, SMEDLEY_TELEMETRY_OBSERVATION_MAX_GOODS> source{};
        uint32_t source_count = 0;
        uint32_t source_groups = 0;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_PRICE) != 0) source_groups |= MARKET_PRICE;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SUPPLY) != 0) source_groups |= MARKET_SUPPLY;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_DEMAND) != 0) source_groups |= MARKET_DEMAND;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SALES) != 0) source_groups |= MARKET_SALES;
        if (!CollectWorldMarketGroups(CurrentGameSession().game_state, source.data(), source.size(), &source_count, source_groups)) {
            return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        }
        const uint32_t copied = (std::min)(capacity, source_count);
        for (uint32_t index = 0; index < copied; ++index) {
            auto &out = markets[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.good_ordinal = source[index].good_ordinal;
            out.availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_MARKET;
            out.group_flags = groups;
            out.price_raw = source[index].price_raw; out.last_price_raw = source[index].last_price_raw;
            out.supply_raw = source[index].supply_raw; out.last_supply_raw = source[index].last_supply_raw;
            out.worldmarket_stock_raw = source[index].worldmarket_stock_raw; out.demand_raw = source[index].demand_raw;
            out.real_demand_raw = source[index].real_demand_raw; out.actual_sold_raw = source[index].actual_sold_raw;
            out.actual_sold_world_raw = source[index].actual_sold_world_raw;
        }
        *count = copied;
        return copied == source_count ? SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS : SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ResolveObservationDailyCountry(
        SmedleyTelemetryObservationSession session, const SmedleyDailyEventV1 *event, int32_t *country_ordinal)
    {
        if (!ValidDailyEvent(event) || country_ordinal == nullptr) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *country_ordinal = -1;
        uint32_t countries = 0;
        const auto game_state = CurrentGameSession().game_state;
        if (!ReadCountryCount(game_state, &countries)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        for (uint32_t ordinal = 0; ordinal < countries; ++ordinal) {
            const auto country = ResolveCountry(game_state, static_cast<int32_t>(ordinal));
            if (!country) continue;
            TelemetryCountrySnapshot source{};
            if (!ReadTelemetryCountry(country, &source)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
            if (std::memcmp(source.tag().value, event->country_tag, sizeof(event->country_tag)) == 0) {
                *country_ordinal = static_cast<int32_t>(ordinal);
                return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
            }
        }
        return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationCountry(
        SmedleyTelemetryObservationSession session, int32_t ordinal, SmedleyTelemetryCountryObservationV1 *country)
    {
        if (!ValidRecord(country, 1) || ordinal < 0) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        const auto source_country = ResolveCountry(CurrentGameSession().game_state, ordinal);
        TelemetryCountrySnapshot source{};
        if (!source_country || !ReadTelemetryCountry(source_country, &source)) return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        country->ordinal = ordinal;
        std::memcpy(country->tag, source.tag().value, sizeof(country->tag));
        std::memcpy(country->overlord_tag, source.overlord_candidate().value, sizeof(country->overlord_tag));
        std::memcpy(country->sphere_leader_tag, source.sphere_leader_candidate().value, sizeof(country->sphere_leader_tag));
        country->availability_flags = ObservationCountryFlags(source);
        country->mobilized = source.mobilized_candidate() ? 1u : 0u;
        country->substate = source.substate_candidate() ? 1u : 0u;
        country->vassal = source.vassal_candidate() ? 1u : 0u;
        country->unit_count = source.unit_count_candidate_value;
        country->scheduled_mobilization_count = source.scheduled_mobilization_count_candidate_value;
        country->sphereling_count = source.sphereling_count_candidate_value;
        country->vassal_count = source.vassal_count_candidate_value;
        country->ally_count = source.ally_count_candidate_value;
        country->guarantee_count = source.guaranteed_count_candidate_value;
        country->neighbor_count = source.neighbor_count_candidate_value;
        country->ranking = source.ranking_candidate(); country->military_ranking = source.military_ranking_candidate();
        country->industrial_ranking = source.industrial_ranking_candidate(); country->prestige_ranking = source.prestige_ranking_candidate();
        country->treasury_raw = source.treasury_raw(); country->prestige_raw = source.prestige_candidate_raw();
        country->infamy_raw = source.infamy_candidate_raw(); country->plurality_raw = source.plurality_candidate_raw();
        country->war_exhaustion_raw = source.war_exhaustion_candidate_raw();
        country->diplomatic_points_raw = source.diplomatic_points_candidate_raw();
        country->research_points_raw = source.research_points_candidate_raw(); country->leadership_raw = source.leadership_candidate_raw();
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationProvince(
        SmedleyTelemetryObservationSession session, int32_t province_id, SmedleyTelemetryProvinceObservationV1 *province)
    {
        if (!ValidRecord(province, 1) || province_id < 0) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        TelemetryProvinceSnapshot source{};
        const auto value = ResolveProvince(CurrentGameSession().game_state, province_id);
        if (!value || !ReadTelemetryProvince(value, &source)) return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        province->province_id = source.id_candidate();
        std::memcpy(province->owner_tag, source.owner_candidate().value, sizeof(province->owner_tag));
        std::memcpy(province->controller_tag, source.controller_candidate().value, sizeof(province->controller_tag));
        province->colonial_level = source.colonial_level_candidate();
        province->life_rating = source.life_rating_candidate();
        province->infrastructure_raw = source.infrastructure_candidate();
        province->building_slot_count = static_cast<uint32_t>(source.building_slot_count_value);
        province->construction_count = source.construction_count_value;
        province->availability_flags = (source.daily_available()
                ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_DAILY : 0)
            | (source.production_available() ? SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_PRODUCTION : 0);
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationCountryEconomy(
        SmedleyTelemetryObservationSession session, int32_t ordinal, SmedleyTelemetryCountryEconomyObservationV1 *economy,
        SmedleyTelemetryCreditorDestinationObservationV1 *destinations, uint32_t capacity, uint32_t *count)
    {
        if (!ValidRecord(economy, 1) || ordinal < 0 || count == nullptr || (capacity != 0 && destinations == nullptr)
            || capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_CREDITOR_DESTINATIONS) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *count = 0;
        const auto game_state = CurrentGameSession().game_state;
        const auto country = ResolveCountry(game_state, ordinal);
        if (!country) return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        int32_t date_raw = 0;
        if (!ReadCurrentDate(game_state, &date_raw)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        const auto source = ReadCountryEconomy(country, date_raw, ResolveObservationCountry, ResolveObservationProvince,
            &game_state);
        economy->country_ordinal = ordinal; economy->date_raw = source.date_raw;
        economy->state_count_reported = source.state_count_reported; std::memcpy(economy->country_tag, source.country_tag, 4);
        economy->states_walked = source.states_walked; economy->province_element_candidates = source.province_element_candidates;
        economy->states_with_savings = source.states_with_savings; economy->states_with_interest = source.states_with_interest;
        economy->creditor_count = source.creditor_count; economy->creditor_destinations = source.creditor_destinations;
        economy->creditors_was_paid = source.creditors_was_paid; economy->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ECONOMY;
        economy->source_flags = source.flags; economy->treasury_raw = source.treasury_raw;
        economy->state_savings_raw = source.state_savings_raw; economy->state_interest_raw = source.state_interest_raw;
        economy->bank_interest_raw = source.bank_interest_raw; economy->creditor_interest_raw = source.creditor_interest_raw;
        economy->creditor_debt_raw = source.creditor_debt_raw; economy->destination_bank_interest_raw = source.destination_bank_interest_raw;
        economy->destination_state_savings_raw = source.destination_state_savings_raw;
        economy->destination_state_interest_raw = source.destination_state_interest_raw;
        economy->destination_pop_savings_raw = source.destination_pop_savings_raw;
        economy->destination_pop_savings_state_scale_raw = source.destination_pop_savings_state_scale_raw;
        const uint32_t copied = (std::min)(capacity, source.creditor_destinations);
        for (uint32_t index = 0; index < copied; ++index) {
            auto &out = destinations[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            std::memcpy(out.tag, &source.destination_keys[index], sizeof(out.tag));
            out.country_ordinal = source.destination_ordinals[index];
            out.bank_interest_raw = source.destination_bank_interests_raw[index];
            out.availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ECONOMY;
        }
        *count = copied;
        if (source.flags != 0) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        return copied == source.creditor_destinations ? SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS : SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationPops(
        SmedleyTelemetryObservationSession session, int32_t ordinal, SmedleyTelemetryPopObservationV1 *pops,
        uint32_t capacity, uint32_t *count, uint32_t *source_flags)
    {
        if (ordinal < 0 || count == nullptr || source_flags == nullptr || (capacity != 0 && pops == nullptr)
            || capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_POP_RECORDS) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        TelemetryObservationSessionSlot *slot = nullptr;
        if (const auto status = ObservationSessionStatus(session, &slot); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *count = 0; *source_flags = 0;
        static std::array<PopCandidate, max_sample_pops> candidates{};
        uint32_t candidate_count = 0;
        CountryEconomySnapshot quality{};
        const auto game_state = CurrentGameSession().game_state;
        const auto country = ResolveCountry(game_state, ordinal);
        if (!country || !CollectCountryPops(country, game_state, 0, candidates.data(), candidates.size(),
                max_sample_destination_provinces, &candidate_count, &quality)) {
            *source_flags = quality.flags;
            return quality.flags == 0 ? SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE : SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        }
        *source_flags = quality.flags;
        const uint32_t copied = (std::min)(capacity, candidate_count);
        for (uint32_t index = 0; index < copied; ++index) {
            PopDetailSnapshot source{};
            if (!ReadPopDetailSnapshot(candidates[index].address, &source)) {
                *source_flags = quality.flags;
                return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
            }
            auto &out = pops[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.pop = OpaqueTelemetryEntityId(FindTelemetrySession(slot->parent), candidates[index].address.address());
            if (out.pop == 0) return SMEDLEY_TELEMETRY_OBSERVATION_CAPACITY;
            out.pop_id = source.pop_id; out.province_id_candidate = source.province_id_candidate;
            out.pop_type_id_candidate = source.pop_type_id_candidate; out.size_candidate = source.size_candidate;
            out.employed_candidate = source.employed_candidate;
            out.availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_DETAIL;
            out.money_raw = source.economy.money_raw; out.savings_raw = source.economy.savings_raw;
            out.interest_cash_flow_raw = source.economy.interest_cash_flow_raw;
            out.total_cash_flow_raw = source.economy.total_cash_flow_raw;
            out.consciousness_candidate_raw = source.consciousness_candidate_raw;
            out.militancy_candidate_raw = source.militancy_candidate_raw; out.literacy_candidate_raw = source.literacy_candidate_raw;
        }
        *count = copied;
        return copied == candidate_count && (quality.flags & SAMPLE_POP_LIMIT) == 0
            ? SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS : SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED;
    }
    SmedleyTelemetryObservationResult ObservationPop(SmedleyTelemetryObservationSession session,
        SmedleyTelemetryOpaquePop pop, TelemetryObservationSessionSlot **slot, PopRef *source)
    {
        if (slot == nullptr || source == nullptr) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        const auto status = ObservationSessionStatus(session, slot);
        if (status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        const uintptr_t address = OpaqueTelemetryEntityAddress(FindTelemetrySession((*slot)->parent), pop);
        if (address == 0) return SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE;
        *source = PopRef{reinterpret_cast<const void *>(address)};
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationPopIdentity(
        SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, SmedleyTelemetryPopIdentityObservationV1 *identity)
    {
        if (!ValidRecord(identity, 1)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        TelemetryObservationSessionSlot *slot = nullptr; PopRef source{};
        if (const auto status = ObservationPop(session, pop, &slot, &source); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        PopIdentityDimensions values{};
        if (!ReadPopIdentityDimensions(source, &values)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        identity->pop = pop; std::memcpy(identity->pop_type_tag, values.pop_type_tag_candidate, sizeof(identity->pop_type_tag));
        std::memcpy(identity->culture_tag, values.culture_tag_candidate, sizeof(identity->culture_tag));
        std::memcpy(identity->religion_tag, values.religion_tag_candidate, sizeof(identity->religion_tag));
        identity->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_IDENTITY;
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationPopNeeds(
        SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, SmedleyTelemetryPopNeedsObservationV1 *needs)
    {
        if (!ValidRecord(needs, 1)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        TelemetryObservationSessionSlot *slot = nullptr; PopRef source{};
        if (const auto status = ObservationPop(session, pop, &slot, &source); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        PopNeedsSnapshot values{};
        if (!ReadPopNeedsSnapshot(source, &values)) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        needs->pop = pop; needs->life_satisfaction_raw = values.life_satisfaction_candidate_raw;
        needs->everyday_satisfaction_raw = values.everyday_satisfaction_candidate_raw;
        needs->luxury_satisfaction_raw = values.luxury_satisfaction_candidate_raw;
        needs->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_NEEDS;
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationArtisan(
        SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, uint32_t groups, SmedleyTelemetryArtisanObservationV1 *artisan,
        SmedleyTelemetryArtisanInputObservationV1 *inputs, uint32_t capacity, uint32_t *count, SmedleyTelemetryArtisanFailureV1 *failure)
    {
        constexpr uint32_t all_groups = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_IDENTITY
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_PRODUCTION | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_INPUTS
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_FINANCE;
        if (!ValidRecord(artisan, 1) || count == nullptr || (capacity != 0 && inputs == nullptr)
            || capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_GOODS || groups == 0 || (groups & ~all_groups) != 0
            || (failure != nullptr && !ValidRecord(failure, 1))) {
            return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        TelemetryObservationSessionSlot *slot = nullptr; PopRef source{};
        if (const auto status = ObservationPop(session, pop, &slot, &source); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *count = 0;
        int32_t inactive_pop_id = -1;
        if (ReadInactiveArtisan(source, &inactive_pop_id)) {
            *artisan = {};
            artisan->struct_size = sizeof(*artisan); artisan->version = 1;
            artisan->pop = pop; artisan->pop_id = inactive_pop_id; artisan->inactive = 1;
            artisan->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ARTISAN;
            return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
        }
        static std::array<ArtisanInputSnapshot, SMEDLEY_TELEMETRY_OBSERVATION_MAX_GOODS> source_inputs{};
        ArtisanSnapshot values{}; ArtisanReadFailure source_failure{}; uint32_t source_count = 0;
        uint32_t source_groups = 0;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_IDENTITY) != 0) source_groups |= ARTISAN_IDENTITY;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_PRODUCTION) != 0) source_groups |= ARTISAN_PRODUCTION;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_INPUTS) != 0) source_groups |= ARTISAN_INPUTS;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_FINANCE) != 0) source_groups |= ARTISAN_FINANCE;
        if (!ReadArtisanSnapshot(source, &values, source_inputs.data(), source_inputs.size(), &source_count,
                source_groups, &source_failure)) {
            if (failure != nullptr) {
                failure->reason = static_cast<uint32_t>(source_failure.reason); failure->pop_id = source_failure.pop_id;
                failure->offending_raw = source_failure.offending_raw;
            }
            return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        }
        artisan->pop = pop; artisan->pop_id = values.pop_id; artisan->output_good_ordinal = values.output_good_ordinal;
        std::memcpy(artisan->production_type, values.production_type, sizeof(artisan->production_type));
        std::memcpy(artisan->output_good, values.output_good, sizeof(artisan->output_good));
        artisan->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ARTISAN;
        artisan->group_flags = groups;
        artisan->base_output_raw = values.base_output_raw; artisan->current_producing_raw = values.current_producing_raw;
        artisan->gross_output_raw = values.gross_output_raw; artisan->last_spending_raw = values.last_spending_raw;
        artisan->percent_afforded_raw = values.percent_afforded_raw; artisan->percent_sold_domestic_raw = values.percent_sold_domestic_raw;
        artisan->percent_sold_export_raw = values.percent_sold_export_raw; artisan->leftover_raw = values.leftover_raw;
        artisan->throttle_raw = values.throttle_raw; artisan->needs_cost_raw = values.needs_cost_raw;
        artisan->production_income_raw = values.production_income_raw;
        const uint32_t copied = (std::min)(capacity, source_count);
        for (uint32_t index = 0; index < copied; ++index) {
            auto &out = inputs[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.good_ordinal = source_inputs[index].good_ordinal; out.stockpile_raw = source_inputs[index].stockpile_raw;
            out.need_raw = source_inputs[index].need_raw;
        }
        *count = copied;
        return copied == source_count ? SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS : SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationFactories(
        SmedleyTelemetryObservationSession session, int32_t ordinal, uint32_t groups, SmedleyTelemetryFactoryObservationV1 *factories,
        uint32_t factory_capacity, uint32_t *factory_count, SmedleyTelemetryFactoryInputObservationV1 *inputs,
        uint32_t input_capacity, uint32_t *input_count, uint32_t *source_flags)
    {
        constexpr uint32_t all_groups = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_IDENTITY
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_EMPLOYMENT | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_PRODUCTION
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_FINANCE | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_INPUTS;
        if (ordinal < 0 || factory_count == nullptr || input_count == nullptr || source_flags == nullptr
            || (factory_capacity != 0 && factories == nullptr) || (input_capacity != 0 && inputs == nullptr)
            || factory_capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_RECORDS
            || input_capacity > SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_INPUTS || groups == 0 || (groups & ~all_groups) != 0) {
            return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        TelemetryObservationSessionSlot *slot = nullptr;
        if (const auto status = ObservationSessionStatus(session, &slot); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        *factory_count = 0; *input_count = 0; *source_flags = 0;
        static std::array<FactorySnapshot, max_sample_factories> source{};
        static std::array<FactoryInputSnapshot, max_sample_factory_inputs> source_inputs{};
        uint32_t source_count = 0, source_input_count = 0;
        const auto country = ResolveCountry(CurrentGameSession().game_state, ordinal);
        uint32_t source_groups = 0;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_IDENTITY) != 0) source_groups |= FACTORY_IDENTITY;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_EMPLOYMENT) != 0) source_groups |= FACTORY_EMPLOYMENT;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_PRODUCTION) != 0) source_groups |= FACTORY_PRODUCTION;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_FINANCE) != 0) source_groups |= FACTORY_FINANCE;
        if ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_INPUTS) != 0) source_groups |= FACTORY_INPUTS;
        if (!country || !CollectCountryFactories(country, source.data(), source.size(), &source_count,
                source_inputs.data(), source_inputs.size(), &source_input_count,
                source_groups, source_flags)) {
            return *source_flags == 0 ? SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE : SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE;
        }
        const uint32_t copied_factories = (std::min)(factory_capacity, source_count);
        for (uint32_t index = 0; index < copied_factories; ++index) {
            auto &out = factories[index]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.factory = OpaqueTelemetryEntityId(FindTelemetrySession(slot->parent), source[index].address.address());
            if (out.factory == 0) return SMEDLEY_TELEMETRY_OBSERVATION_CAPACITY;
            out.observation_index = index; out.state_index = source[index].state_index; out.factory_index = source[index].factory_index;
            out.availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_FACTORY;
            out.group_flags = groups;
            out.state_id = source[index].state_id; out.anchor_province_id_candidate = source[index].anchor_province_id_candidate;
            out.level = source[index].level; out.employee_count = source[index].employee_count;
            out.craftsmen_count = source[index].craftsmen_count; out.clerk_count = source[index].clerk_count;
            out.output_raw = source[index].output_raw; out.output_good_ordinal = source[index].output_good_ordinal;
            out.base_output_raw = source[index].base_output_raw; out.subsidized = source[index].subsidized ? 1 : 0;
            out.closed = source[index].closed ? 1 : 0;
            std::memcpy(out.state_region_key, source[index].state_region_key, sizeof(out.state_region_key));
            std::memcpy(out.factory_type, source[index].factory_type, sizeof(out.factory_type));
            std::memcpy(out.output_good, source[index].output_good, sizeof(out.output_good));
            out.budget_raw = source[index].budget_raw; out.market_spending_raw = source[index].market_spending_raw;
            out.sales_income_raw = source[index].sales_income_raw; out.paychecks_raw = source[index].paychecks_raw;
            out.investment_raw = source[index].investment_raw;
        }
        bool inputs_omitted = false;
        for (uint32_t index = 0; index < source_input_count; ++index) {
            if (source_inputs[index].factory_snapshot_index >= copied_factories) continue;
            if (*input_count == input_capacity) { inputs_omitted = true; continue; }
            auto &out = inputs[*input_count]; out = {}; out.struct_size = sizeof(out); out.version = 1;
            out.factory = factories[source_inputs[index].factory_snapshot_index].factory;
            out.factory_observation_index = source_inputs[index].factory_snapshot_index;
            out.good_ordinal = source_inputs[index].good_ordinal; out.stockpile_raw = source_inputs[index].stockpile_raw;
            out.requested_raw = source_inputs[index].requested_raw;
            ++*input_count;
        }
        *factory_count = copied_factories;
        const bool factories_complete = copied_factories == source_count;
        const bool inputs_requested = (groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_INPUTS) != 0;
        return factories_complete && (!inputs_requested || !inputs_omitted)
                && (*source_flags & FACTORY_LIMIT) == 0
            ? SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS : SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED;
    }
    SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL ReadObservationRgo(
        SmedleyTelemetryObservationSession session, int32_t province_id, uint32_t groups, SmedleyTelemetryRgoObservationV1 *rgo)
    {
        constexpr uint32_t all_groups = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_IDENTITY | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_EMPLOYMENT
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_PRODUCTION | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_FINANCE
            | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_MODIFIERS | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_SALES;
        if (!ValidRecord(rgo, 1) || province_id < 0 || groups == 0 || (groups & ~all_groups) != 0) {
            return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
        }
        std::lock_guard<std::recursive_mutex> lock(metadata_mutex);
        if (const auto status = ObservationSessionStatus(session); status != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return status;
        const auto game_state = CurrentGameSession().game_state;
        const auto province = ResolveProvince(game_state, province_id);
        const auto registry = ResolveStateEmploymentRegistry();
        RgoSnapshot source{};
        TelemetryCurrentState world{};
        if (!province || !registry || !ReadTelemetryCurrentState(&world) || !world.province_count_available_value
            || !ReadProvinceRgo(registry, province, province_id, world.province_count_value,
                ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_IDENTITY) ? RGO_IDENTITY : 0)
                    | ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_EMPLOYMENT) ? RGO_EMPLOYMENT : 0)
                    | ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_PRODUCTION) ? RGO_PRODUCTION : 0)
                    | ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_FINANCE) ? RGO_FINANCE : 0)
                    | ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_MODIFIERS) ? RGO_MODIFIERS : 0)
                    | ((groups & SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_SALES) ? RGO_SALES : 0), &source)) {
            return SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE;
        }
        rgo->province_id = source.province_id; rgo->output_good_ordinal = source.output_good_ordinal;
        rgo->employment_capacity = source.employment_capacity; rgo->employed = source.employed;
        rgo->owner_population = source.owner_population; rgo->state_rgo_employment_capacity = source.state_rgo_employment_capacity;
        std::memcpy(rgo->production_type, source.production_type, sizeof(rgo->production_type));
        std::memcpy(rgo->output_good, source.output_good, sizeof(rgo->output_good));
        rgo->availability_flags = SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_RGO;
        rgo->group_flags = groups;
        rgo->base_output_per_size_raw = source.base_output_per_size_raw; rgo->base_size_raw = source.base_size_raw;
        rgo->output_efficiency_raw = source.output_efficiency_raw; rgo->throughput_raw = source.throughput_raw;
        rgo->gross_output_raw = source.gross_output_raw; rgo->owner_output_modifier_raw = source.owner_output_modifier_raw;
        rgo->income_raw = source.income_raw; rgo->percent_sold_domestic_raw = source.percent_sold_domestic_raw;
        rgo->percent_sold_export_raw = source.percent_sold_export_raw; rgo->leftover_raw = source.leftover_raw;
        return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }

    struct HookSubscriptionSlot
    {
        uint64_t handle = 0;
        SmedleyTelemetrySession session = 0;
        uint64_t epoch = 0;
        uint32_t hooks = 0;
        std::thread::id thread{};
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
        return slot == nullptr ? 0 : OpaqueTelemetryEntityId(FindTelemetrySession(slot->session), address);
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

SMEDLEY_CAMPAIGN_AUTOMATION_EXPORT SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL
SmedleyGetCampaignAutomationApiV1(SmedleyCampaignAutomationApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api)
        || api->version != SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT;
    api->install = &InstallCampaignAutomation;
    api->deactivate = &DeactivateCampaignAutomation;
    api->set_observer_mode = &SetAutomationObserverMode;
    api->read_console_state = &ReadAutomationConsoleState;
    api->read_observer_country_by_tag = &ReadObserverCountryByTag;
    api->start_observer_tag_switch = &StartAutomationObserverTagSwitch;
    api->set_popup_suppression = &SetAutomationPopupSuppression;
    api->read_popup_state = &ReadAutomationPopupState;
    api->read_process_metrics = &ReadAutomationProcessMetrics;
    return SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS;
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

SMEDLEY_TELEMETRY_OBSERVATION_EXPORT SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL
SmedleyGetTelemetryObservationApiV1(SmedleyTelemetryObservationApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api)
        || api->version != SMEDLEY_TELEMETRY_OBSERVATION_API_VERSION_V1
        || api->reserved[0] != 0 || api->reserved[1] != 0) return SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT;
    api->open_session = &OpenObservationSession;
    api->close_session = &CloseObservationSession;
    api->read_world = &ReadObservationWorld;
    api->read_market = &ReadObservationMarket;
    api->resolve_daily_country = &ResolveObservationDailyCountry;
    api->read_country = &ReadObservationCountry;
    api->read_province = &ReadObservationProvince;
    api->read_country_economy = &ReadObservationCountryEconomy;
    api->read_pops = &ReadObservationPops;
    api->read_pop_identity = &ReadObservationPopIdentity;
    api->read_pop_needs = &ReadObservationPopNeeds;
    api->read_artisan = &ReadObservationArtisan;
    api->read_factories = &ReadObservationFactories;
    api->read_rgo = &ReadObservationRgo;
    return SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
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
static_assert(sizeof(SmedleyCampaignAutomationApiV1) == 52, "campaign automation API v1 layout changed");
static_assert(sizeof(SmedleyCampaignAutomationOptionsV1) == 48, "campaign automation options ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignFrontendCaptureV1) == 32, "campaign frontend capture ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignAnnexationV1) == 32, "campaign annexation ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignConsoleCaptureV1) == 40, "campaign console capture ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignTagV1) == 28, "campaign tag ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignConsoleStateV1) == 32, "campaign console state ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignConsoleCommandResultV1) == 160, "campaign console result ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignPopupSnapshotV1) == 32, "campaign popup ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignProcessMetricsV1) == 56, "campaign process metrics ABI v1 layout changed");
static_assert(sizeof(SmedleyInterestPoolApiV1) == 32, "interest pool API v1 layout changed");
static_assert(sizeof(SmedleyTelemetryGameApiV1) == 60, "telemetry game API v1 layout changed");
static_assert(sizeof(SmedleyTelemetryWorldSnapshotV1) == 44, "telemetry world ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryMarketSnapshotV1) == 72, "telemetry market ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryCountrySnapshotV1) == 48, "telemetry country ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryProvinceSnapshotV1) == 64, "telemetry province ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryPopSnapshotV1) == 64, "telemetry POP ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryFactorySnapshotV1) == 64, "telemetry factory ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryObservationApiV1) == 72, "telemetry observation API ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryWorldObservationV1) == 48, "telemetry observation world ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryProvinceObservationV1) == 64, "telemetry observation province ABI v1 layout changed");
static_assert(sizeof(SmedleyTelemetryFactoryObservationV1) == 328, "telemetry observation factory ABI v1 layout changed");
