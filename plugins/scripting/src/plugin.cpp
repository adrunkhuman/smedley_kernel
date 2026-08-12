#include "scripting_runtime.hpp"

#include <smedley/campaign_control_api.h>
#include <smedley/event_api.h>
#include <smedley/plugin.hpp>

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace scripting_plugin
{
    namespace
    {
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

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
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
            logger().Info("started constrained Lua 5.1 scripting worker");
        }

        void OnUnload() override
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
            logger().Info("scripting stopped: accepted=" + std::to_string(stats.accepted)
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
            if (failure) logger().Failure(message);
            else logger().Info(message);
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
            if (get_event_api == nullptr || get_campaign_control_api == nullptr) {
                throw std::runtime_error("required smedley kernel services are unavailable");
            }
            event_api_ = {};
            event_api_.struct_size = sizeof(event_api_);
            event_api_.version = SMEDLEY_EVENT_API_VERSION_V1;
            campaign_control_api_ = {};
            campaign_control_api_.struct_size = sizeof(campaign_control_api_);
            campaign_control_api_.version = SMEDLEY_CAMPAIGN_CONTROL_API_VERSION_V1;
            if (get_event_api(&event_api_) != SMEDLEY_EVENT_SUCCESS
                || get_campaign_control_api(&campaign_control_api_) != SMEDLEY_CAMPAIGN_CONTROL_SUCCESS) {
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
        SmedleyEventRegistration daily_registration_ = 0;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new scripting_plugin::Plugin{};
}
