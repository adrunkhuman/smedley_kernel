#pragma once

#include <smedley/v2/console.hpp>
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

namespace smedley::v2
{
    class CCurrentGameState;
    class CInGameIdler;
}

namespace campaign_runner
{
    class CampaignLauncher
    {
    public:
        explicit CampaignLauncher(smedley::Logger &logger) noexcept;

        bool Start(std::wstring save_path, bool observe, std::wstring observer_view_tag, int speed, bool start_paused,
                    CampaignRunCondition condition);
        void Stop();
        void CaptureConsoleCommandManager(smedley::v2::CConsoleCmdManager *manager);
        void CaptureFrontendController(void *controller);
        void CaptureMainMenuController(void *controller);
        // Called before native Annex captures the player tag. Observer mode
        // changes only the view and logs failure if no healthy AI target exists.
        void PrepareObserverForAnnexation(int annexed_ordinal);

        static smedley::v2::CConsoleCmd::SResult HandleObserverSwitch(
            const smedley::sstd::vector<smedley::sstd::string> &arguments);
        static smedley::v2::CConsoleCmd::SResult RejectNativeTag(
            const smedley::sstd::vector<smedley::sstd::string> &arguments);

    private:
        static void CALLBACK SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD);

        bool ScheduleTimer(UINT delay, const char *failure_message);
        bool CheckSignatures() const;
        bool SelectSpeed(smedley::v2::CCurrentGameState *game_state);
        void StartBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler);
        bool TickBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler);
        void FinishBenchmark(const char *reason, std::optional<int> actual_date_raw, std::optional<bool> paused);
        void FinishInvalidBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler);
        void ReportTelemetryResult(SmedleyTelemetryResult result);
        bool ObserverInvariantsValid(smedley::v2::CCurrentGameState *game_state) const;
        bool EmitObserverConfiguredIfReady(smedley::v2::CCurrentGameState *game_state);
        bool InstallControllerHooks();
        bool DispatchMainMenuSinglePlayer();
        bool DispatchControlSignal(const char *name);
        smedley::v2::CConsoleCmd::SResult RequestObserverSwitch(
            const smedley::sstd::vector<smedley::sstd::string> &arguments);

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
        smedley::v2::CConsoleCmd::SCommandData *native_tag_command_ = nullptr;
        smedley::v2::CConsoleCmd::SCommandData *observer_switch_command_ = nullptr;
        smedley::v2::CConsoleCmdManager *observer_command_manager_ = nullptr;
        smedley::v2::CConsoleCmd::SCommandData::Handler native_tag_handler_ = nullptr;
        std::atomic<smedley::v2::CConsoleCmdManager *> console_manager_ = nullptr;
        std::atomic<void *> frontend_controller_ = nullptr;
        std::atomic<void *> main_menu_controller_ = nullptr;
    };
}
