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
        std::atomic<bool> observer_enabled{false};
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
        slot->observer_enabled.store(false, std::memory_order_release);
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
        if (slot->observer_enabled.load(std::memory_order_acquire)) {
            ObserverStateSnapshot before{};
            if (ReadObserverState(&before) == ObserverObservationStatus::completed
                && before.view_country.tag.ordinal == annexed_ordinal) {
                ObserverCountrySnapshot target{};
                ObserverStateSnapshot after{};
                if (FindHealthyObserverCountry(annexed_ordinal, &target) == ObserverObservationStatus::completed) {
                    SetObserverViewCountry(target, &after);
                }
            }
        }
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
        slot.observer_enabled.store(false, std::memory_order_relaxed);
        slot.session.store(session, std::memory_order_relaxed);
        slot.epoch.store(CurrentGameSession().epoch, std::memory_order_relaxed);
        slot.handle.store(handle, std::memory_order_relaxed);
        slot.control.store(automation_active, std::memory_order_release);
        SetCampaignAutomationFrontendCaptureCallback(options->frontend_capture == nullptr ? nullptr : &OnCampaignAutomationFrontendCapture);
        SetCampaignAutomationAnnexationCallback(&OnCampaignAutomationAnnexation);
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
        slot->observer_enabled.store(enabled != 0, std::memory_order_release);
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

    void RefreshCampaignAutomationEpoch(SmedleyCampaignSession session, uint64_t epoch)
    {
        for (auto &slot : campaign_automation_slots) {
            if (slot.session.load(std::memory_order_acquire) == session) {
                slot.epoch.store(epoch, std::memory_order_release);
            }
        }
    }

}

using namespace smedley::game_state::services;

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
