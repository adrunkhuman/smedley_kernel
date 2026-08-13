#pragma once

#include "campaign_services.hpp"
#include "campaign_telemetry.hpp"
#include "benchmark_controller.hpp"
#include "campaign_launch_arguments.hpp"
#include <windows.h>

#include <atomic>
#include <optional>
#include <string>

namespace smedley
{
    class Logger;
}

namespace campaign_runner
{
    class CampaignLauncher
    {
    public:
        explicit CampaignLauncher(smedley::Logger &logger) noexcept;

        bool Start(std::wstring save_path, bool observe, std::wstring observer_view_tag, int speed, bool start_paused,
                   bool quit_after_run, CampaignRunCondition condition);
        void Stop();
        void OnConsoleCommandManagerCaptured(smedley::game_state::CampaignConsoleCaptureStatus status);
        bool QueueConsoleCommand(uint32_t command, uint32_t argument_count, uint32_t arguments_valid,
                                 const char *first_argument) noexcept;
        // Called before native Annex captures the player tag. Observer mode
        // changes only the view and logs failure if no healthy AI target exists.
        void PrepareObserverForAnnexation(int annexed_ordinal);

        static void __stdcall NotifyFrontendControllerCaptured(
            smedley::game_state::FrontendControllerKind kind) noexcept;
        static void __stdcall NotifyObserverAnnexation(int annexed_ordinal) noexcept;
        static void __stdcall NotifyConsoleCommandManagerCaptured(
            smedley::game_state::CampaignConsoleCaptureStatus status) noexcept;

    private:
        static void CALLBACK SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD) noexcept;

        bool ScheduleTimer(UINT delay, const char *failure_message);
        bool SelectSpeed();
        void StartBenchmark(const smedley::game_state::CampaignRuntimeSnapshot &runtime);
        bool TickBenchmark(const smedley::game_state::CampaignRuntimeSnapshot &runtime, bool observer_valid);
        void FinishBenchmark(const char *reason, std::optional<int> actual_date_raw, std::optional<bool> paused);
        void FinishInvalidBenchmark(const smedley::game_state::CampaignRuntimeSnapshot &runtime);
        bool DrainTelemetryBeforeQuit();
        void QuitAfterRun();
        void ReportTelemetryResult(SmedleyTelemetryResult result);
        bool ObserverInvariantsValid() const;
        bool EmitObserverConfiguredIfReady();
        void OnFrontendControllerCaptured(smedley::game_state::FrontendControllerKind kind);
        bool RequestObserverSwitch(std::string requested_tag, std::string *message = nullptr);
        void DrainHookWork();
        void DrainConsoleRequest();

        smedley::Logger &logger_;
        std::wstring save_path_;
        UINT_PTR save_timer_ = 0;
        int save_attempts_ = 0;
        int campaign_attempts_ = 0;
        int observer_attempts_ = 0;
        int observer_target_ordinal_ = 0;
        long observed_suppressed_messages_ = 0;
        uint64_t next_observer_watchdog_us_ = 0;
        bool lobby_requested_ = false;
        bool save_selection_requested_ = false;
        bool play_requested_ = false;
        bool observe_ = false;
        bool observer_enabled_ = false;
        bool observer_ai_ready_ = false;
        bool observer_console_ready_ = false;
        bool observer_monitoring_ = false;
        bool observer_view_switch_pending_ = false;
        bool speed_ready_ = false;
        int target_speed_ = 5;
        bool start_paused_ = false;
        bool quit_after_run_ = false;
        CampaignRunCondition run_condition_;
        BenchmarkController benchmark_;
        bool benchmark_started_ = false;
        bool benchmark_terminal_ = false;
        std::optional<int64_t> benchmark_process_cpu_start_us_;
        std::optional<int64_t> benchmark_working_set_start_bytes_;
        std::optional<int64_t> benchmark_private_bytes_start_;
        bool final_pause_recorded_ = false;
        std::optional<bool> pause_before_configuration_;
        size_t observer_ai_count_before_switch_ = 0;
        std::string observer_target_tag_;
        std::string initial_observer_view_tag_;
        CampaignTelemetry telemetry_;
        bool telemetry_invalid_logged_ = false;
        bool telemetry_dropped_logged_ = false;
        smedley::game_state::FrontendControllerToken frontend_controller_;
        smedley::game_state::FrontendControllerToken main_menu_controller_;
        std::atomic<bool> frontend_captured_{false};
        std::atomic<uint32_t> console_capture_status_{UINT32_MAX};
        std::atomic<uint32_t> console_request_state_{0};
        std::atomic<uint32_t> console_request_argument_count_{0};
        std::atomic<uint32_t> console_request_arguments_valid_{0};
        std::atomic<char> console_request_argument_[SMEDLEY_CAMPAIGN_CONSOLE_MAX_ARGUMENT_BYTES]{};
    };
}
