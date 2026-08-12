#include "scripting_runtime.hpp"

#include <smedley/campaign_control_api.h>
#include <smedley/event_api.h>
#include <smedley/logging_api.h>
#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>

namespace scripting_plugin
{
    namespace
    {
        void ReportLoadFailure(const char *message) noexcept
        {
            static constexpr char component[] = "scripting";
            const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
            if (kernel == nullptr) return;
            const auto get_api = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(
                GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
            if (get_api == nullptr) return;
            SmedleyLoggingApiV1 api{};
            api.struct_size = sizeof(api);
            api.version = SMEDLEY_LOGGING_API_VERSION_V1;
            if (get_api(&api) != SMEDLEY_LOGGING_SUCCESS) return;
            api.write(SMEDLEY_LOG_FAILURE, component, sizeof(component) - 1, message,
                static_cast<uint32_t>((std::min)(strlen(message),
                    static_cast<size_t>(SMEDLEY_LOGGING_MAX_MESSAGE_BYTES))));
        }

        std::vector<std::wstring> CommandLineArguments()
        {
            int count = 0;
            wchar_t **values = CommandLineToArgvW(GetCommandLineW(), &count);
            if (values == nullptr) return {};
            std::vector<std::wstring> arguments(values + 1, values + count);
            LocalFree(values);
            return arguments;
        }

    }

    class Plugin final
    {
    public:
        void OnLoad()
        {
            const auto arguments = CommandLineArguments();
            const bool configured = std::any_of(arguments.begin(), arguments.end(), [](const std::wstring &argument) {
                return argument.rfind(L"-smedley-script", 0) == 0;
            });
            if (!configured) return;
            smedley::scripting::Config config;
            std::string error;
            if (!smedley::scripting::ParseLaunchArguments(arguments, &config, &error)) {
                throw std::runtime_error("scripting arguments are invalid: " + error);
            }
            AcquireServices();
            pause_request_state_.store(0, std::memory_order_release);
            runtime_ = std::make_unique<smedley::scripting::Runtime>(std::move(config),
                [this](bool failure, const std::string &message) {
                    Log(failure, message);
                },
                [this] {
                    int expected = 0;
                    return pause_request_state_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel);
                },
                [this] {
                    int expected = 3;
                    pause_request_state_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
                });
            if (!runtime_->Start(&error)) {
                pause_request_state_.store(2, std::memory_order_release);
                runtime_.reset();
                throw std::runtime_error("scripting runtime did not start: " + error);
            }
            try {
                if (event_api_.register_daily(&NotifyDailyUpdate, this, &daily_registration_)
                    != SMEDLEY_EVENT_SUCCESS) {
                    throw std::runtime_error("scripting daily event registration failed");
                }
            } catch (...) {
                pause_request_state_.store(2, std::memory_order_release);
                runtime_->Stop();
                runtime_.reset();
                throw;
            }
            WriteLog(SMEDLEY_LOG_INFO, "started constrained Lua 5.1 scripting worker");
        }

        void OnUnload()
        {
            const int previous_pause_state = pause_request_state_.exchange(2, std::memory_order_acq_rel);
            if (daily_registration_ != 0) {
                event_api_.unregister(daily_registration_);
                daily_registration_ = 0;
            }
            if (!runtime_) return;
            if (previous_pause_state == 1) runtime_->ReportPauseResult(smedley::scripting::PauseResult::Shutdown);
            runtime_->Stop();
            const auto stats = runtime_->stats();
            WriteLog(SMEDLEY_LOG_INFO, "scripting stopped: accepted=" + std::to_string(stats.accepted)
                + " processed=" + std::to_string(stats.processed) + " dropped=" + std::to_string(stats.dropped)
                + " script_errors=" + std::to_string(stats.script_errors)
                + " disabled_scripts=" + std::to_string(stats.disabled_scripts)
                + " high_water=" + std::to_string(stats.high_water)
                + " worker_failed=" + (stats.worker_failed ? "true" : "false"));
            runtime_.reset();
        }

    private:
        void Log(bool failure, const std::string &message)
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            WriteLog(failure ? SMEDLEY_LOG_FAILURE : SMEDLEY_LOG_INFO, message);
        }

        void WriteLog(SmedleyLogLevel level, const std::string &message) noexcept
        {
            static constexpr char component[] = "scripting";
            if (logging_api_.write != nullptr) {
                logging_api_.write(level, component, sizeof(component) - 1, message.data(),
                    static_cast<uint32_t>(std::min<size_t>(message.size(), SMEDLEY_LOGGING_MAX_MESSAGE_BYTES)));
            }
        }

        void ApplyPauseRequest()
        {
            int expected = 1;
            if (!pause_request_state_.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) return;
            switch (campaign_control_api_.set_paused(1)) {
            case SMEDLEY_CAMPAIGN_CONTROL_SUCCESS:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::Completed);
                break;
            case SMEDLEY_CAMPAIGN_CONTROL_OUTSIDE_CAMPAIGN:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::OutsideCampaign);
                break;
            case SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT:
            case SMEDLEY_CAMPAIGN_CONTROL_INVALID_STATE:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::InvalidState);
                break;
            case SMEDLEY_CAMPAIGN_CONTROL_SIGNATURE_MISMATCH:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::SignatureMismatch);
                break;
            case SMEDLEY_CAMPAIGN_CONTROL_READBACK_FAILED:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::ReadbackFailed);
                break;
            }
        }

        static SmedleyEventCallbackResult SMEDLEY_EVENT_CALL NotifyDailyUpdate(
            void *context, const SmedleyDailyEventV1 *event) noexcept
        {
            auto *plugin = static_cast<Plugin *>(context);
            if (plugin == nullptr || event == nullptr) return SMEDLEY_EVENT_CALLBACK_DISABLE;
            try {
                plugin->OnDailyUpdate(*event);
                return SMEDLEY_EVENT_CALLBACK_CONTINUE;
            } catch (...) {
                return SMEDLEY_EVENT_CALLBACK_DISABLE;
            }
        }

        void AcquireServices()
        {
            const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
            if (kernel == nullptr) throw std::runtime_error("smedley kernel is not loaded");
            const auto get_event_api = reinterpret_cast<SmedleyGetEventApiV1Fn>(
                GetProcAddress(kernel, SMEDLEY_EVENT_GET_API_V1_SYMBOL));
            const auto get_campaign_control_api = reinterpret_cast<SmedleyGetCampaignControlApiV1Fn>(
                GetProcAddress(kernel, SMEDLEY_CAMPAIGN_CONTROL_GET_API_V1_SYMBOL));
            const auto get_logging_api = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(
                GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
            if (get_event_api == nullptr || get_campaign_control_api == nullptr || get_logging_api == nullptr) {
                throw std::runtime_error("required smedley kernel services are unavailable");
            }
            event_api_ = {};
            event_api_.struct_size = sizeof(event_api_);
            event_api_.version = SMEDLEY_EVENT_API_VERSION_V1;
            campaign_control_api_ = {};
            campaign_control_api_.struct_size = sizeof(campaign_control_api_);
            campaign_control_api_.version = SMEDLEY_CAMPAIGN_CONTROL_API_VERSION_V1;
            logging_api_ = {};
            logging_api_.struct_size = sizeof(logging_api_);
            logging_api_.version = SMEDLEY_LOGGING_API_VERSION_V1;
            if (get_event_api(&event_api_) != SMEDLEY_EVENT_SUCCESS
                || get_campaign_control_api(&campaign_control_api_) != SMEDLEY_CAMPAIGN_CONTROL_SUCCESS
                || get_logging_api(&logging_api_) != SMEDLEY_LOGGING_SUCCESS) {
                throw std::runtime_error("required smedley kernel service versions are unavailable");
            }
        }

        void OnDailyUpdate(const SmedleyDailyEventV1 &event)
        {
            if (!runtime_) return;
            smedley::scripting::EventSnapshot snapshot;
            if (!smedley::scripting::CopyDailyEventSnapshot(event, &snapshot)) return;
            ApplyPauseRequest();
            runtime_->TryPush(snapshot);
        }

        std::unique_ptr<smedley::scripting::Runtime> runtime_;
        // Pause-request states: 0 = accepted, 1 = pending, 2 = closed, and
        // 3 = awaiting worker-side result consumption.
        std::atomic<int> pause_request_state_{2};
        std::mutex log_mutex_;
        SmedleyEventApiV1 event_api_{};
        SmedleyCampaignControlApiV1 campaign_control_api_{};
        SmedleyLoggingApiV1 logging_api_{};
        SmedleyEventRegistration daily_registration_ = 0;
    };
}

namespace
{
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL CreateScripting(void *instance, uint32_t size)
    {
        if (instance == nullptr || size != sizeof(scripting_plugin::Plugin)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            new (instance) scripting_plugin::Plugin{};
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (const std::exception &error) {
            scripting_plugin::ReportLoadFailure(error.what());
            return SMEDLEY_PLUGIN_FAILURE;
        } catch (...) {
            scripting_plugin::ReportLoadFailure("scripting create failed with an unknown exception");
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL LoadScripting(void *instance)
    {
        if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            static_cast<scripting_plugin::Plugin *>(instance)->OnLoad();
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (const std::exception &error) {
            scripting_plugin::ReportLoadFailure(error.what());
            return SMEDLEY_PLUGIN_FAILURE;
        } catch (...) {
            scripting_plugin::ReportLoadFailure("scripting load failed with an unknown exception");
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL UnloadScripting(void *instance)
    {
        if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            static_cast<scripting_plugin::Plugin *>(instance)->OnUnload();
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (...) {
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    void SMEDLEY_PLUGIN_CALL DestroyScripting(void *instance)
    {
        if (instance != nullptr) static_cast<scripting_plugin::Plugin *>(instance)->~Plugin();
    }
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(SmedleyPluginApiV1)
        || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    for (const uint32_t reserved : api->reserved) {
        if (reserved != 0) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    api->instance_size = sizeof(scripting_plugin::Plugin);
    api->instance_alignment = alignof(scripting_plugin::Plugin);
    api->create = &CreateScripting;
    api->load = &LoadScripting;
    api->unload = &UnloadScripting;
    api->destroy = &DestroyScripting;
    return SMEDLEY_PLUGIN_SUCCESS;
}
