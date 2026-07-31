#pragma once

#include <windows.h>

#include <atomic>
#include <string>

namespace smedley
{
    class Logger;
    namespace v2
    {
        class CConsoleCmdManager;
    }
}

namespace campaign_runner
{
    class CampaignLauncher
    {
    public:
        explicit CampaignLauncher(smedley::Logger &logger) noexcept;

        bool Start(std::wstring save_path, bool observe);
        void Stop();
        void CaptureConsoleCommandManager(smedley::v2::CConsoleCmdManager *manager);
        void CaptureFrontendController(void *controller);
        void CaptureMainMenuController(void *controller);

    private:
        static void CALLBACK SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD);

        bool ScheduleTimer(UINT delay, const char *failure_message);
        bool CheckSignatures() const;
        bool InstallControllerHooks();
        bool DispatchMainMenuSinglePlayer();
        bool DispatchControlSignal(const char *name);

        smedley::Logger &logger_;
        std::wstring save_path_;
        UINT_PTR save_timer_ = 0;
        int save_attempts_ = 0;
        int campaign_attempts_ = 0;
        int observer_attempts_ = 0;
        int observer_target_ordinal_ = 0;
        long observed_suppressed_messages_ = 0;
        bool lobby_requested_ = false;
        bool save_selection_requested_ = false;
        bool play_requested_ = false;
        bool observe_ = false;
        bool observer_ai_ready_ = false;
        bool observer_monitoring_ = false;
        bool observer_view_switch_pending_ = false;
        bool speed_ready_ = false;
        size_t observer_ai_count_before_switch_ = 0;
        std::string observer_target_tag_;
        std::atomic<smedley::v2::CConsoleCmdManager *> console_manager_ = nullptr;
        std::atomic<void *> frontend_controller_ = nullptr;
        std::atomic<void *> main_menu_controller_ = nullptr;
    };
}
