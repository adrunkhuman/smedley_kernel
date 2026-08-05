#include "scripting_runtime.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/game_state/runtime.hpp>
#include <smedley/plugin.hpp>

#include <shellapi.h>

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
            if (!smedley::game_state::IsPauseOperationAvailable()) {
                throw std::runtime_error("scripting pause operation signature does not match");
            }
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
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "scripting.daily", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
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
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("scripting.daily");
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
            switch (smedley::game_state::PauseGame()) {
            case smedley::game_state::PauseOperationStatus::completed:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::Completed);
                break;
            case smedley::game_state::PauseOperationStatus::outside_campaign:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::OutsideCampaign);
                break;
            case smedley::game_state::PauseOperationStatus::invalid_state:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::InvalidState);
                break;
            case smedley::game_state::PauseOperationStatus::signature_mismatch:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::SignatureMismatch);
                break;
            case smedley::game_state::PauseOperationStatus::readback_failed:
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::ReadbackFailed);
                break;
            }
        }

        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            if (!runtime_) return;
            smedley::game_state::DailyUpdateSnapshot event_snapshot{};
            if (!smedley::game_state::ReadDailyUpdateSnapshot(event, &event_snapshot)) return;
            smedley::scripting::EventSnapshot snapshot;
            snapshot.date_raw = event_snapshot.date_raw;
            snapshot.treasury_raw = event_snapshot.treasury_raw;
            snapshot.country_slot_count = event_snapshot.country_slot_count;
            snapshot.ai_scheduler_entry_count = event_snapshot.ai_scheduler_entry_count;
            snapshot.country_tag = {event_snapshot.country_tag.value[0], event_snapshot.country_tag.value[1],
                event_snapshot.country_tag.value[2], '\0'};
            snapshot.country_exists = event_snapshot.country_exists;
            snapshot.human_control_present = event_snapshot.human_control_present;
            ApplyPauseRequest();
            runtime_->TryPush(snapshot);
        }

        std::unique_ptr<smedley::scripting::Runtime> runtime_;
        // Pause-request states: 0 = accepted, 1 = pending, 2 = closed, and
        // 3 = awaiting worker-side result consumption.
        std::atomic<int> pause_request_state_{2};
        std::mutex log_mutex_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new scripting_plugin::Plugin{};
}
