#include "campaign_services.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace
{
    using FrontendCallback = void (__stdcall *)(smedley::game_state::FrontendControllerKind);

    struct Services {
        SmedleyLoggingApiV1 logging{sizeof(logging), SMEDLEY_LOGGING_API_VERSION_V1};
        SmedleyCampaignRuntimeApiV2 runtime{sizeof(runtime), SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V2};
        SmedleyCampaignAutomationApiV1 automation{sizeof(automation), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1};
        SmedleyEventServicesApiV1 events{sizeof(events), SMEDLEY_EVENT_SERVICES_API_VERSION_V1};
        SmedleyCampaignSession session = 0;
        SmedleyCampaignAutomation automation_handle = 0;
        smedley::game_state::CampaignAutomationCallbacks callbacks{};
    } services;
    std::atomic<FrontendCallback> frontend_callback{nullptr};

    bool RuntimeOk(SmedleyCampaignRuntimeResult result) { return result == SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS; }
    bool AutomationOk(SmedleyCampaignAutomationResult result) { return result == SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS; }
    void CopyCountry(const SmedleyObserverCountrySnapshotV1 &source, smedley::game_state::ObserverCountrySnapshot *target) {
        std::memcpy(target->tag.value, source.tag, sizeof(target->tag.value));
        target->tag.ordinal = source.ordinal;
        target->exists = source.exists != 0; target->human_controlled = source.human_controlled != 0;
        target->has_ai = source.has_ai != 0; target->ai_scheduled = source.ai_scheduled != 0;
    }
    void CopyState(const SmedleyObserverStateSnapshotV1 &source, smedley::game_state::ObserverStateSnapshot *target) {
        CopyCountry(source.view_country, &target->view_country);
        target->country_count = source.country_count; target->country_ai_count = source.country_ai_count;
        target->human_control_present = source.human_control_present != 0;
        target->full_map_visibility_enabled = source.full_map_visibility_enabled != 0;
    }
    SmedleyObserverCountrySnapshotV1 ToAbi(const smedley::game_state::ObserverCountrySnapshot &country) {
        SmedleyObserverCountrySnapshotV1 result{sizeof(result), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1};
        std::memcpy(result.tag, country.tag.value, sizeof(result.tag)); result.ordinal = country.tag.ordinal;
        result.exists = country.exists; result.human_controlled = country.human_controlled;
        result.has_ai = country.has_ai; result.ai_scheduled = country.ai_scheduled;
        return result;
    }
    SmedleyObserverStateSnapshotV1 ObserverStateAbi() {
        SmedleyObserverStateSnapshotV1 result{sizeof(result), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1};
        result.view_country.struct_size = sizeof(result.view_country);
        result.view_country.version = SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1;
        return result;
    }
    SmedleyCampaignTagV1 ToAbi(const smedley::game_state::ObserverTag &tag) {
        SmedleyCampaignTagV1 result{sizeof(result), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1};
        std::memcpy(result.tag, tag.value, sizeof(result.tag)); result.ordinal = tag.ordinal; return result;
    }
    SmedleyCampaignAutomationCallbackResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL Frontend(uint64_t, const SmedleyCampaignFrontendCaptureV1 *event) {
        const auto callback = frontend_callback.load(std::memory_order_acquire);
        if (event != nullptr && callback != nullptr)
            callback(event->controller_kind == SMEDLEY_FRONTEND_CONTROLLER_FRONTEND
                ? smedley::game_state::FrontendControllerKind::frontend : smedley::game_state::FrontendControllerKind::main_menu);
        return SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE;
    }
    SmedleyCampaignAutomationCallbackResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL Annexation(uint64_t, const SmedleyCampaignAnnexationV1 *event) {
        if (event != nullptr && services.callbacks.annexation != nullptr) services.callbacks.annexation(event->annexed_ordinal);
        return SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE;
    }
    SmedleyCampaignAutomationCallbackResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL Console(uint64_t, const SmedleyCampaignConsoleCaptureV1 *event) {
        if (event != nullptr && services.callbacks.console_capture != nullptr) {
            services.callbacks.console_capture(static_cast<smedley::game_state::CampaignConsoleCaptureStatus>(event->status));
        }
        return SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE;
    }
}

namespace smedley
{
    void Logger::Info(const std::string &message) const noexcept { if (services.logging.write) services.logging.write(SMEDLEY_LOG_INFO, "campaign_runner", 15, message.data(), static_cast<uint32_t>((std::min)(message.size(), size_t{SMEDLEY_LOGGING_MAX_MESSAGE_BYTES}))); }
    void Logger::Warn(const std::string &message) const noexcept { if (services.logging.write) services.logging.write(SMEDLEY_LOG_WARN, "campaign_runner", 15, message.data(), static_cast<uint32_t>((std::min)(message.size(), size_t{SMEDLEY_LOGGING_MAX_MESSAGE_BYTES}))); }
    void Logger::Failure(const std::string &message) const noexcept { if (services.logging.write) services.logging.write(SMEDLEY_LOG_FAILURE, "campaign_runner", 15, message.data(), static_cast<uint32_t>((std::min)(message.size(), size_t{SMEDLEY_LOGGING_MAX_MESSAGE_BYTES}))); }

    namespace game_state
    {
        bool ObserverTag::normalized_candidate() const noexcept { return ordinal > 0 && value[0] && value[1] && value[2] && !value[3]; }
        bool AcquireServices(std::string *error) {
            const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
            if (!kernel) { *error = "smedley kernel is not loaded"; return false; }
            const auto get_logging = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
            const auto get_runtime = reinterpret_cast<SmedleyGetCampaignRuntimeApiV2Fn>(GetProcAddress(kernel, SMEDLEY_CAMPAIGN_RUNTIME_GET_API_V2_SYMBOL));
            const auto get_automation = reinterpret_cast<SmedleyGetCampaignAutomationApiV1Fn>(GetProcAddress(kernel, SMEDLEY_CAMPAIGN_AUTOMATION_GET_API_V1_SYMBOL));
            const auto get_events = reinterpret_cast<SmedleyGetEventServicesApiV1Fn>(GetProcAddress(kernel, SMEDLEY_EVENT_SERVICES_GET_API_V1_SYMBOL));
            if (!get_logging || !get_runtime || !get_automation || !get_events || get_logging(&services.logging) != SMEDLEY_LOGGING_SUCCESS
                || !RuntimeOk(get_runtime(&services.runtime)) || !AutomationOk(get_automation(&services.automation))
                || get_events(&services.events) != SMEDLEY_EVENT_SERVICES_SUCCESS
                || !RuntimeOk(services.runtime.open_session(&services.session))) { *error = "required campaign C services are unavailable"; return false; }
            return true;
        }
        void ReleaseServices() noexcept { if (services.automation_handle) services.automation.deactivate(services.automation_handle); frontend_callback.store(nullptr, std::memory_order_release); if (services.session) services.runtime.close_session(services.session); services = {}; }
        bool RegisterCampaignConsole(SmedleyCampaignConsoleCallbackV1Fn callback, void *context, SmedleyEventServicesRegistration *registration) { return services.events.register_campaign_console(callback, context, registration) == SMEDLEY_EVENT_SERVICES_SUCCESS; }
        void UnregisterCampaignConsole(SmedleyEventServicesRegistration registration) noexcept { if (registration) services.events.unregister(registration); }
        Logger &LoggerInstance() noexcept { static Logger logger; return logger; }
        FrontendOperationStatus InstallFrontendAutomationHooks() { return FrontendOperationStatus::completed; }
        FrontendOperationStatus RollbackFrontendAutomationHooks() { return FrontendOperationStatus::completed; }
        FrontendOperationStatus SetFrontendControllerCaptureCallback(void (__stdcall *callback)(FrontendControllerKind)) { frontend_callback.store(callback, std::memory_order_release); return FrontendOperationStatus::completed; }
        void DeactivateFrontendAutomation() noexcept { frontend_callback.store(nullptr, std::memory_order_release); }
        CampaignOperationStatus InstallCampaignAutomationHooks(CampaignAutomationCallbacks callbacks) {
            services.callbacks = callbacks;
            SmedleyCampaignAutomationOptionsV1 options{sizeof(options), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1};
            options.frontend_capture = &Frontend; options.annexation = &Annexation; options.console_capture = &Console;
            return AutomationOk(services.automation.install(services.session, &options, &services.automation_handle)) ? CampaignOperationStatus::completed : CampaignOperationStatus::unavailable;
        }
        void DeactivateCampaignAutomation() noexcept { if (services.automation_handle) services.automation.deactivate(services.automation_handle); services.automation_handle = 0; services.callbacks = {}; }
        void SetCampaignObserverMode(bool enabled) noexcept { if (services.automation_handle) services.automation.set_observer_mode(services.automation_handle, enabled); }
        void SetCampaignMessagePopupSuppression(bool enabled) noexcept { if (services.automation_handle) services.automation.set_popup_suppression(services.automation_handle, enabled); }
        long CampaignSuppressedMessageCount() noexcept { SmedleyCampaignPopupSnapshotV1 state{sizeof(state), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1}; return services.automation_handle && AutomationOk(services.automation.read_popup_state(services.automation_handle, &state)) ? static_cast<long>(state.suppressed_count) : 0; }
        bool IsCampaignObserverConsoleReady() noexcept { SmedleyCampaignConsoleStateV1 state{sizeof(state), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1}; return services.automation_handle && AutomationOk(services.automation.read_console_state(services.automation_handle, &state)) && state.ready; }
        CampaignRuntimeObservationStatus ReadCampaignRuntime(CampaignRuntimeSnapshot *snapshot) { SmedleyCampaignRuntimeSnapshotV2 value{sizeof(value), SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V2}; if (!snapshot || !RuntimeOk(services.runtime.read_campaign(services.session, &value))) return CampaignRuntimeObservationStatus::unavailable; *snapshot = {value.game_date_raw, value.speed_index, value.paused != 0, value.game_over != 0}; return CampaignRuntimeObservationStatus::completed; }
        CampaignOperationStatus SetCampaignPaused(bool paused) { return RuntimeOk(services.runtime.set_paused(services.session, paused)) ? CampaignOperationStatus::completed : CampaignOperationStatus::unavailable; }
        CampaignOperationStatus SetCampaignSpeedIndex(int speed) { return RuntimeOk(services.runtime.set_speed_index(services.session, speed)) ? CampaignOperationStatus::completed : CampaignOperationStatus::unavailable; }
        CampaignOperationStatus RequestCampaignQuit() { return RuntimeOk(services.runtime.request_quit(services.session)) ? CampaignOperationStatus::completed : CampaignOperationStatus::unavailable; }
        FrontendOperationStatus AcquireFrontendController(FrontendControllerKind kind, FrontendControllerToken *token) { SmedleyFrontendController value{}; if (!token || !RuntimeOk(services.runtime.acquire_frontend(services.session, kind == FrontendControllerKind::frontend ? SMEDLEY_FRONTEND_CONTROLLER_FRONTEND : SMEDLEY_FRONTEND_CONTROLLER_MAIN_MENU, &value))) return FrontendOperationStatus::unavailable; token->value = value; return FrontendOperationStatus::completed; }
        FrontendOperationStatus ReleaseFrontendController(FrontendControllerToken token) { return RuntimeOk(services.runtime.release_frontend(token.value)) ? FrontendOperationStatus::completed : FrontendOperationStatus::unavailable; }
        FrontendOperationStatus DispatchMainMenuSinglePlayer(FrontendControllerToken token) { return RuntimeOk(services.runtime.dispatch_main_menu_single_player(token.value)) ? FrontendOperationStatus::completed : FrontendOperationStatus::unavailable; }
        FrontendOperationStatus RequestFrontendSave(FrontendControllerToken token, const char *name) { return RuntimeOk(services.runtime.request_save(token.value, name, static_cast<uint32_t>(std::strlen(name)))) ? FrontendOperationStatus::completed : FrontendOperationStatus::unavailable; }
        FrontendOperationStatus ObserveFrontendSave(FrontendControllerToken token, FrontendSaveSnapshot *snapshot) { SmedleyFrontendSaveSnapshotV1 value{sizeof(value), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1}; if (!snapshot || !RuntimeOk(services.runtime.read_save(token.value, &value))) return FrontendOperationStatus::unavailable; snapshot->request_pending = value.request_pending != 0; snapshot->completed = value.completed != 0; std::memcpy(snapshot->selected_basename, value.selected_basename, sizeof(snapshot->selected_basename)); return FrontendOperationStatus::completed; }
        FrontendOperationStatus DispatchFrontendControl(FrontendControllerToken token, const char *name) { return RuntimeOk(services.runtime.dispatch_frontend_control(token.value, name, static_cast<uint32_t>(std::strlen(name)))) ? FrontendOperationStatus::completed : FrontendOperationStatus::unavailable; }
        ObserverObservationStatus ReadObserverState(ObserverStateSnapshot *snapshot) { auto value = ObserverStateAbi(); if (!snapshot || !RuntimeOk(services.runtime.read_observer_state(services.session, &value))) return ObserverObservationStatus::unavailable; CopyState(value, snapshot); return ObserverObservationStatus::completed; }
        ObserverObservationStatus ReadObserverCountry(int32_t ordinal, ObserverCountrySnapshot *snapshot) { SmedleyObserverCountrySnapshotV1 value{sizeof(value), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1}; if (!snapshot || !RuntimeOk(services.runtime.read_observer_country(services.session, ordinal, &value))) return ObserverObservationStatus::unavailable; CopyCountry(value, snapshot); return ObserverObservationStatus::completed; }
        ObserverObservationStatus ResolveObserverCountry(const char tag[4], ObserverCountrySnapshot *snapshot) { SmedleyCampaignTagV1 value{sizeof(value), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1}; std::memcpy(value.tag, tag, sizeof(value.tag)); SmedleyObserverCountrySnapshotV1 country{sizeof(country), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1}; if (!snapshot || !AutomationOk(services.automation.read_observer_country_by_tag(services.session, &value, &country))) return ObserverObservationStatus::unavailable; CopyCountry(country, snapshot); return ObserverObservationStatus::completed; }
        ObserverObservationStatus FindHealthyObserverCountry(int32_t ordinal, ObserverCountrySnapshot *snapshot) { SmedleyObserverCountrySnapshotV1 value{sizeof(value), SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1}; if (!snapshot || !RuntimeOk(services.runtime.find_observer_country(services.session, ordinal, &value))) return ObserverObservationStatus::unavailable; CopyCountry(value, snapshot); return ObserverObservationStatus::completed; }
        ObserverOperationStatus SetObserverViewCountry(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after) { auto state = ObserverStateAbi(); const auto result = services.runtime.set_observer_view_country(services.session, &ToAbi(country), &state); if (!RuntimeOk(result)) return ObserverOperationStatus::unavailable; if (after) CopyState(state, after); return ObserverOperationStatus::completed; }
        ObserverOperationStatus ReturnObserverCountryToAI(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after) { auto state = ObserverStateAbi(); const auto result = services.runtime.return_observer_country(services.session, &ToAbi(country), &state); if (!RuntimeOk(result)) return ObserverOperationStatus::unavailable; if (after) CopyState(state, after); return ObserverOperationStatus::completed; }
        ObserverOperationStatus EnableObserverFullMapVisibility() { return RuntimeOk(services.runtime.enable_observer_fow(services.session)) ? ObserverOperationStatus::completed : ObserverOperationStatus::unavailable; }
        ObserverOperationStatus StartNativeObserverTagSwitch(const ObserverTag &tag, CampaignConsoleCommandResult *result) { SmedleyCampaignConsoleCommandResultV1 value{sizeof(value), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1}; if (!services.automation_handle || !AutomationOk(services.automation.start_observer_tag_switch(services.automation_handle, &ToAbi(tag), &value))) return ObserverOperationStatus::unavailable; if (result) { result->success = value.success != 0; result->message_available = value.message_available != 0; std::memcpy(result->message, value.message, sizeof(result->message)); } return ObserverOperationStatus::completed; }
        ProcessMetricsSnapshot SampleProcessMetrics() { SmedleyCampaignProcessMetricsV1 value{sizeof(value), SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1}; ProcessMetricsSnapshot result; if (!AutomationOk(services.automation.read_process_metrics(services.session, &value))) return result; if (value.availability_flags & 1) result.process_cpu_us = value.process_cpu_us; if (value.availability_flags & 2) result.working_set_bytes = value.working_set_bytes; if (value.availability_flags & 4) result.private_bytes = value.private_bytes; if (value.availability_flags & 8) result.process_peak_working_set_bytes = value.peak_working_set_bytes; return result; }
    }
}
