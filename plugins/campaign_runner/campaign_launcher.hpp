#pragma once

#include <windows.h>

#include <atomic>
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

        bool Start(std::wstring save_path);
        void Stop();
        void CaptureFrontendController(void *controller);
        void CaptureMainMenuController(void *controller);

    private:
        static void CALLBACK SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD);

        bool CheckSignatures() const;
        bool InstallControllerHooks();
        bool DispatchMainMenuSinglePlayer();
        bool DispatchControlSignal(const char *name);

        smedley::Logger &logger_;
        std::wstring save_path_;
        UINT_PTR save_timer_ = 0;
        int save_attempts_ = 0;
        bool lobby_requested_ = false;
        bool save_selection_requested_ = false;
        std::atomic<void *> frontend_controller_ = nullptr;
        std::atomic<void *> main_menu_controller_ = nullptr;
    };
}
