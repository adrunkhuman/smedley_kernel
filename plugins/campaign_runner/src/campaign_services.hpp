#pragma once

#include <smedley/campaign_automation_api.h>
#include <smedley/campaign_runtime_api.h>
#include <smedley/event_services_api.h>
#include <smedley/logging_api.h>

#include <optional>
#include <string>

namespace smedley
{
    class Logger
    {
    public:
        void Info(const std::string &message) const noexcept;
        void Warn(const std::string &message) const noexcept;
        void Failure(const std::string &message) const noexcept;
    };

    namespace game_state
    {
        enum class FrontendControllerKind { frontend, main_menu };
        enum class FrontendOperationStatus { completed, unavailable, invalid_state };
        enum class CampaignRuntimeObservationStatus { completed, unavailable };
        enum class CampaignOperationStatus { completed, unavailable, invalid_state };
        enum class ObserverObservationStatus { completed, unavailable, not_found };
        enum class ObserverOperationStatus { completed, unavailable, invalid_state };
        enum class CampaignConsoleCaptureStatus { observer_disabled, completed, already_configured, command_conflict, native_tag_unavailable };
        enum class CampaignConsoleCommand : uint8_t { native_tag, observer_switch };

        struct FrontendControllerToken { uint64_t value = 0; };
        struct CampaignRuntimeSnapshot { int date_raw = 0; int speed_index = 0; bool paused = false; };
        struct FrontendSaveSnapshot { bool request_pending = false; bool completed = false; char selected_basename[SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES]{}; };
        struct ObserverTag { char value[4]{}; int32_t ordinal = -1; bool normalized_candidate() const noexcept; const char *str() const noexcept { return value; } };
        struct ObserverCountrySnapshot {
            ObserverTag tag{};
            bool exists = false, human_controlled = false, has_ai = false, ai_scheduled = false;
            bool healthy_ai() const noexcept { return exists && !human_controlled && has_ai && ai_scheduled; }
        };
        struct ObserverStateSnapshot {
            ObserverCountrySnapshot view_country{};
            uint32_t country_count = 0, country_ai_count = 0;
            bool human_control_present = false, full_map_visibility_enabled = false;
        };
        struct CampaignConsoleCommandResult { bool success = false, message_available = false; char message[SMEDLEY_CAMPAIGN_AUTOMATION_MESSAGE_BYTES]{}; };
        struct ProcessMetricsSnapshot {
            std::optional<int64_t> process_cpu_us, working_set_bytes, private_bytes, process_peak_working_set_bytes;
        };

        bool AcquireServices(std::string *error);
        void ReleaseServices() noexcept;
        bool RegisterCampaignConsole(SmedleyCampaignConsoleCallbackV1Fn callback, void *context,
                                     SmedleyEventServicesRegistration *registration);
        void UnregisterCampaignConsole(SmedleyEventServicesRegistration registration) noexcept;
        Logger &LoggerInstance() noexcept;
        FrontendOperationStatus InstallFrontendAutomationHooks();
        FrontendOperationStatus RollbackFrontendAutomationHooks();
        FrontendOperationStatus SetFrontendControllerCaptureCallback(void (__stdcall *callback)(FrontendControllerKind));
        void DeactivateFrontendAutomation() noexcept;
        CampaignOperationStatus InstallCampaignAutomationHooks(struct CampaignAutomationCallbacks callbacks);
        void DeactivateCampaignAutomation() noexcept;
        void SetCampaignObserverMode(bool enabled) noexcept;
        void SetCampaignMessagePopupSuppression(bool enabled) noexcept;
        long CampaignSuppressedMessageCount() noexcept;
        bool IsCampaignObserverConsoleReady() noexcept;
        CampaignRuntimeObservationStatus ReadCampaignRuntime(CampaignRuntimeSnapshot *snapshot);
        CampaignOperationStatus SetCampaignPaused(bool paused);
        CampaignOperationStatus SetCampaignSpeedIndex(int speed_index);
        CampaignOperationStatus RequestCampaignQuit();
        FrontendOperationStatus AcquireFrontendController(FrontendControllerKind kind, FrontendControllerToken *token);
        FrontendOperationStatus ReleaseFrontendController(FrontendControllerToken token);
        FrontendOperationStatus DispatchMainMenuSinglePlayer(FrontendControllerToken token);
        FrontendOperationStatus RequestFrontendSave(FrontendControllerToken token, const char *basename);
        FrontendOperationStatus ObserveFrontendSave(FrontendControllerToken token, FrontendSaveSnapshot *snapshot);
        FrontendOperationStatus DispatchFrontendControl(FrontendControllerToken token, const char *name);
        ObserverObservationStatus ReadObserverState(ObserverStateSnapshot *snapshot);
        ObserverObservationStatus ReadObserverCountry(int32_t ordinal, ObserverCountrySnapshot *snapshot);
        ObserverObservationStatus ResolveObserverCountry(const char tag[4], ObserverCountrySnapshot *snapshot);
        ObserverObservationStatus FindHealthyObserverCountry(int32_t excluded_ordinal, ObserverCountrySnapshot *snapshot);
        ObserverOperationStatus SetObserverViewCountry(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after = nullptr);
        ObserverOperationStatus ReturnObserverCountryToAI(const ObserverCountrySnapshot &country, ObserverStateSnapshot *after = nullptr);
        ObserverOperationStatus EnableObserverFullMapVisibility();
        ObserverOperationStatus StartNativeObserverTagSwitch(const ObserverTag &tag, CampaignConsoleCommandResult *result = nullptr);
        ProcessMetricsSnapshot SampleProcessMetrics();

        using CampaignAnnexationCallback = void (__stdcall *)(int32_t);
        using CampaignConsoleCaptureCallback = void (__stdcall *)(CampaignConsoleCaptureStatus);
        struct CampaignAutomationCallbacks {
            CampaignAnnexationCallback annexation = nullptr;
            CampaignConsoleCaptureCallback console_capture = nullptr;
        };
    }
}
