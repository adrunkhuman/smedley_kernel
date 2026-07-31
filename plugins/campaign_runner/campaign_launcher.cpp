#include "campaign_launcher.hpp"

#include <smedley/log.hpp>
#include <smedley/memory.hpp>
#include <smedley/std/string.hpp>

#include <cstring>
#include <filesystem>
#include <new>
#include <sstream>
#include <utility>

namespace campaign_runner
{
    namespace
    {
        namespace fs = std::filesystem;

        CampaignLauncher *launcher_instance = nullptr;
        uintptr_t frontend_constructor_return_address = 0;
        uintptr_t main_menu_return_address = 0;

        void __stdcall CaptureFrontendController(void *controller)
        {
            if (launcher_instance != nullptr) {
                launcher_instance->CaptureFrontendController(controller);
            }
        }

        void __stdcall CaptureMainMenuController(void *controller)
        {
            if (launcher_instance != nullptr) {
                launcher_instance->CaptureMainMenuController(controller);
            }
        }

        __declspec(naked) void FrontendConstructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call CaptureFrontendController
                popad
                popfd

                push ebp
                mov ebp, esp
                push 0xffffffff
                jmp frontend_constructor_return_address
            }
        }

        __declspec(naked) void MainMenuTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call CaptureMainMenuController
                popad
                popfd

                push ebp
                mov ebp, esp
                push 0xffffffff
                jmp main_menu_return_address
            }
        }
    }

    CampaignLauncher::CampaignLauncher(smedley::Logger &logger) noexcept
        : logger_(logger)
    {
    }

    bool CampaignLauncher::Start(std::wstring save_path)
    {
        save_path_ = std::move(save_path);
        launcher_instance = this;
        if (save_path_.empty()) {
            logger_.Warn("no unattended save argument found");
            return false;
        }
        if (!CheckSignatures() || !InstallControllerHooks()) {
            return false;
        }
        logger_.Info("waiting for the frontend before unattended save loading");
        return true;
    }

    void CampaignLauncher::Stop()
    {
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        launcher_instance = nullptr;
    }

    void CampaignLauncher::CaptureFrontendController(void *controller)
    {
        if (controller == nullptr) {
            return;
        }
        const auto previous = frontend_controller_.exchange(controller, std::memory_order_acq_rel);
        if (previous == controller) {
            return;
        }
        std::ostringstream message;
        message << "captured frontend controller=" << controller;
        logger_.Info(message.str());
        if (save_timer_ != 0 || save_attempts_ != 0) {
            return;
        }
        save_timer_ = SetTimer(nullptr, 0, 10'000, SaveTimerCallback);
        if (save_timer_ == 0) {
            logger_.Failure("failed to schedule save loading on the frontend thread");
        } else {
            logger_.Info("scheduled save loading on the frontend thread");
        }
    }

    void CampaignLauncher::CaptureMainMenuController(void *controller)
    {
        if (controller == nullptr) {
            return;
        }
        void *expected = nullptr;
        main_menu_controller_.compare_exchange_strong(
            expected,
            controller,
            std::memory_order_release,
            std::memory_order_relaxed);
    }

    bool CampaignLauncher::CheckSignatures() const
    {
        const auto load_save = smedley::memory::Map::base_addr + 0x27f1d0;
        constexpr unsigned char load_save_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto press_dispatch = smedley::memory::Map::base_addr + 0x5ee510;
        constexpr unsigned char press_expected[] = {
            0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
        const auto release_dispatch = smedley::memory::Map::base_addr + 0x5ee550;
        constexpr unsigned char release_expected[] = {
            0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
        if (std::memcmp(reinterpret_cast<const void *>(load_save), load_save_expected, sizeof(load_save_expected)) != 0
            || std::memcmp(reinterpret_cast<const void *>(press_dispatch), press_expected, sizeof(press_expected)) != 0
            || std::memcmp(reinterpret_cast<const void *>(release_dispatch), release_expected, sizeof(release_expected)) != 0) {
            logger_.Failure("campaign automation signature mismatch; save loading disabled");
            return false;
        }
        return true;
    }

    bool CampaignLauncher::InstallControllerHooks()
    {
        const auto frontend_constructor = smedley::memory::Map::base_addr + 0x36a2f0;
        constexpr unsigned char frontend_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto main_menu_constructor = smedley::memory::Map::base_addr + 0x354a00;
        constexpr unsigned char main_menu_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        if (std::memcmp(
                reinterpret_cast<const void *>(frontend_constructor),
                frontend_expected,
                sizeof(frontend_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(main_menu_constructor),
                main_menu_expected,
                sizeof(main_menu_expected)) != 0) {
            logger_.Failure("frontend constructor signature mismatch; save loading disabled");
            return false;
        }
        frontend_constructor_return_address = frontend_constructor + sizeof(frontend_expected);
        main_menu_return_address = main_menu_constructor + sizeof(main_menu_expected);
        smedley::memory::Hook(
            frontend_constructor,
            reinterpret_cast<void *>(&FrontendConstructorTrampoline),
            sizeof(frontend_expected),
            nullptr);
        smedley::memory::Hook(
            main_menu_constructor,
            reinterpret_cast<void *>(&MainMenuTrampoline),
            sizeof(main_menu_expected),
            nullptr);
        return true;
    }

    void CALLBACK CampaignLauncher::SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD)
    {
        auto *launcher = launcher_instance;
        if (launcher == nullptr || timer != launcher->save_timer_) {
            return;
        }
        KillTimer(nullptr, timer);
        launcher->save_timer_ = 0;
        auto *controller = launcher->frontend_controller_.load(std::memory_order_acquire);
        if (controller == nullptr) {
            launcher->logger_.Failure("frontend controller is unavailable for save selection");
            return;
        }
        if (!launcher->lobby_requested_) {
            if (launcher->main_menu_controller_.load(std::memory_order_acquire) == nullptr) {
                launcher->save_timer_ = SetTimer(nullptr, 0, 1'000, SaveTimerCallback);
                return;
            }
            if (!launcher->DispatchMainMenuSinglePlayer()) {
                return;
            }
            launcher->lobby_requested_ = true;
            launcher->save_timer_ = SetTimer(nullptr, 0, 3'000, SaveTimerCallback);
            if (launcher->save_timer_ == 0) {
                launcher->logger_.Failure("failed to schedule lobby save selection");
            }
            return;
        }
        if (!launcher->save_selection_requested_) {
            launcher->save_selection_requested_ = true;
            auto *selected_save = reinterpret_cast<smedley::sstd::string *>(
                reinterpret_cast<unsigned char *>(controller) + 0x590);
            if (selected_save->size() == 0) {
                const auto filename = fs::path(launcher->save_path_).filename().string();
                new (selected_save) smedley::sstd::string(filename.c_str());
            }
            *(reinterpret_cast<unsigned char *>(controller) + 0x5bc) = 1;
            *(reinterpret_cast<unsigned char *>(controller) + 0x5bd) = 0;
            launcher->save_timer_ = SetTimer(nullptr, 0, 5'000, SaveTimerCallback);
            return;
        }
        if (*(reinterpret_cast<unsigned char *>(controller) + 0x5bd) != 0) {
            launcher->DispatchControlSignal("play_button");
            return;
        }
        ++launcher->save_attempts_;
        if (launcher->save_attempts_ < 24) {
            launcher->save_timer_ = SetTimer(nullptr, 0, 5'000, SaveTimerCallback);
        } else {
            launcher->logger_.Failure("save selection did not finish within 120 seconds");
        }
    }

    bool CampaignLauncher::DispatchMainMenuSinglePlayer()
    {
        auto *controller = main_menu_controller_.load(std::memory_order_acquire);
        auto *gui = controller == nullptr
            ? nullptr
            : *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(controller) + 0x704);
        if (gui == nullptr) {
            logger_.Failure("main-menu GUI registry is unavailable");
            return false;
        }
        const smedley::sstd::string panel_name("mainmenu_panel");
        const smedley::sstd::string button_name("single_player_button");
        using FindControl = void *(__thiscall *)(void *, const smedley::sstd::string *);
        const auto gui_vtable = *reinterpret_cast<uintptr_t **>(gui);
        auto *panel = reinterpret_cast<FindControl>(gui_vtable[0x6c / sizeof(uintptr_t)])(
            gui,
            &panel_name);
        if (panel == nullptr) {
            logger_.Failure("mainmenu_panel is unavailable for native dispatch");
            return false;
        }
        const auto panel_vtable = *reinterpret_cast<uintptr_t **>(panel);
        auto *button = reinterpret_cast<FindControl>(panel_vtable[0x34 / sizeof(uintptr_t)])(
            panel,
            &button_name);
        if (button == nullptr) {
            logger_.Failure("single_player_button is unavailable for native dispatch");
            return false;
        }
        auto *signal = reinterpret_cast<unsigned char *>(button) + 0x54;
        const auto press = smedley::memory::Map::base_addr + 0x5ee510;
        const auto release = smedley::memory::Map::base_addr + 0x5ee550;
        __asm mov eax, signal
        __asm call press
        __asm mov eax, signal
        __asm call release
        logger_.Info("dispatched native main-menu Single Player signal");
        return true;
    }

    bool CampaignLauncher::DispatchControlSignal(const char *name)
    {
        auto *controller = frontend_controller_.load(std::memory_order_acquire);
        auto *gui = controller == nullptr
            ? nullptr
            : *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(controller) + 0x278);
        if (gui == nullptr) {
            logger_.Failure("frontend GUI registry is unavailable for control dispatch");
            return false;
        }
        const smedley::sstd::string control_name(name);
        const auto vtable = *reinterpret_cast<uintptr_t **>(gui);
        using FindControl = void *(__thiscall *)(void *, const smedley::sstd::string *);
        auto *control = reinterpret_cast<FindControl>(vtable[0x34 / sizeof(uintptr_t)])(
            gui,
            &control_name);
        if (control == nullptr) {
            logger_.Failure(std::string(name) + " is unavailable for native dispatch");
            return false;
        }
        auto *signal = reinterpret_cast<unsigned char *>(control) + 0x54;
        std::ostringstream message;
        message << "dispatching native control signal: " << name
                << " control=" << control
                << " vtable=" << *reinterpret_cast<void **>(control)
                << " signal=" << static_cast<void *>(signal);
        logger_.Info(message.str());
        const auto press = smedley::memory::Map::base_addr + 0x5ee510;
        const auto release = smedley::memory::Map::base_addr + 0x5ee550;
        __asm mov eax, signal
        __asm call press
        __asm mov eax, signal
        __asm call release
        logger_.Info(std::string("dispatched native control signal: ") + name);
        return true;
    }
}
