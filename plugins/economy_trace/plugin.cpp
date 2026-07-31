#include <smedley/events/dailyupdate.hpp>
#include <smedley/memory.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>
#include <windows.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <string>

namespace economy_trace
{
    namespace fs = std::filesystem;
    class Plugin;
    Plugin *plugin_instance = nullptr;
    uintptr_t load_save_return_address = 0;
    uintptr_t pause_candidate_return_address = 0;
    uintptr_t frontend_constructor_return_address = 0;
    std::atomic<void *> frontend_controller = nullptr;
    uintptr_t main_menu_return_address = 0;
    std::atomic<void *> main_menu_controller = nullptr;

    void __stdcall TraceLoadSave(
        const smedley::sstd::string *filename,
        uintptr_t return_address,
        void *caller_object);
    void __stdcall TracePauseCandidate(void *controller);
    void __stdcall CaptureFrontendController(void *controller);
    void __stdcall CaptureMainMenuController(void *controller);

    __declspec(naked) void LoadSaveTrampoline()
    {
        __asm {
            pushfd
            pushad
            mov edx, dword ptr [esp]
            mov ecx, dword ptr [esp + 0x24]
            mov eax, dword ptr [esp + 0x2c]
            push edx
            push ecx
            push eax
            call TraceLoadSave
            popad
            popfd

            push ebp
            mov ebp, esp
            push 0xffffffff
            jmp load_save_return_address
        }
    }

    __declspec(naked) void PauseCandidateTrampoline()
    {
        __asm {
            pushfd
            pushad
            mov eax, dword ptr [esp + 0x18]
            push eax
            call TracePauseCandidate
            popad
            popfd

            push ebp
            mov ebp, esp
            mov eax, dword ptr fs:[0]
            jmp pause_candidate_return_address
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

    class Plugin final : public smedley::Plugin
    {
        std::ofstream output;
        int last_date = (std::numeric_limits<int>::min)();
        std::wstring save_path;
        UINT_PTR save_timer = 0;
        int save_attempts = 0;
        bool lobby_requested = false;
        bool save_selection_requested = false;
        bool frontend_trace_supported = false;

    public:
        void OnLoad() override
        {
            output.open("economy_trace.csv", std::ios::trunc);
            if (!output) {
                logger().Failure("cannot open economy_trace.csv in the game directory");
                return;
            }
            output << "date_raw,country,treasury_raw,treasury_shadow_raw\n";
            output.flush();
            AddEventHandler<smedley::events::DailyUpdateEvent>(
                "economy_trace.daily",
                [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
            logger().Info("writing daily country treasury data to economy_trace.csv");
            plugin_instance = this;
            const bool load_save_supported = InstallLoadSaveTrace();
            InstallPauseCandidateTrace();
            const bool automation_supported = CheckAutomationSignatures();

            save_path = ReadSaveArgument();
            if (!save_path.empty() && load_save_supported && automation_supported) {
                frontend_trace_supported = InstallFrontendTrace();
                if (frontend_trace_supported) {
                    logger().Info("waiting for the frontend before unattended save loading");
                }
            } else if (save_path.empty()) {
                logger().Warn("no unattended save argument found");
            }
        }

        void OnUnload() override
        {
            if (save_timer != 0) {
                KillTimer(nullptr, save_timer);
                save_timer = 0;
            }
            output.flush();
        }

        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            const auto *country = event.GetCountry();
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            if (country == nullptr || game_state == nullptr || !output) {
                return;
            }
            const int date = game_state->current_date_raw();
            if (date != last_date) {
                output.flush();
                last_date = date;
            }
            output << date << ','
                   << country->tag().str() << ','
                   << country->treasury_raw() << ','
                   << country->treasury_shadow_raw() << '\n';
        }

        void LogNativeLoadSave(
            const smedley::sstd::string *filename,
            uintptr_t return_address,
            void *caller_object)
        {
            const std::string path(filename->c_str(), filename->size());
            std::ostringstream message;
            message << "native LoadSave argument=" << path
                    << " return=" << reinterpret_cast<void *>(return_address)
                    << " caller_object=" << caller_object;
            logger().Info(message.str());
        }

        void LogPauseCandidate(void *controller, unsigned char flag)
        {
            std::ostringstream message;
            message << "pause candidate this=" << controller
                    << " flag_1538=" << static_cast<int>(flag)
                    << " game_state=" << smedley::v2::CCurrentGameState::instance();
            logger().Info(message.str());
        }

        void LogFrontendController(void *controller)
        {
            std::ostringstream message;
            message << "captured frontend controller=" << controller;
            logger().Info(message.str());
            if (!save_path.empty()
                && frontend_trace_supported
                && save_timer == 0
                && save_attempts == 0) {
                save_timer = SetTimer(nullptr, 0, 10'000, SaveTimerCallback);
                if (save_timer == 0) {
                    logger().Failure("failed to schedule save loading on the frontend thread");
                } else {
                    logger().Info("scheduled save loading on the frontend thread");
                }
            }
        }

    private:
        static bool InstallLoadSaveTrace()
        {
            const auto address = smedley::memory::Map::base_addr + 0x27f1d0;
            constexpr unsigned char expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
            if (std::memcmp(reinterpret_cast<const void *>(address), expected, sizeof(expected)) != 0) {
                plugin_instance->logger().Failure("LoadSave signature mismatch; tracing disabled");
                return false;
            }
            load_save_return_address = address + sizeof(expected);
            smedley::memory::Hook(
                address,
                reinterpret_cast<void *>(&LoadSaveTrampoline),
                sizeof(expected),
                nullptr);
            return true;
        }

        static void InstallPauseCandidateTrace()
        {
            const auto address = smedley::memory::Map::base_addr + 0x26a2c0;
            constexpr unsigned char expected[] = {0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
            if (std::memcmp(reinterpret_cast<const void *>(address), expected, sizeof(expected)) != 0) {
                plugin_instance->logger().Failure("pause candidate signature mismatch; tracing disabled");
                return;
            }
            pause_candidate_return_address = address + sizeof(expected);
            smedley::memory::Hook(
                address,
                reinterpret_cast<void *>(&PauseCandidateTrampoline),
                sizeof(expected),
                nullptr);
        }

        static bool InstallFrontendTrace()
        {
            const auto constructor = smedley::memory::Map::base_addr + 0x36a2f0;
            constexpr unsigned char constructor_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
            const auto main_menu = smedley::memory::Map::base_addr + 0x354a00;
            constexpr unsigned char main_menu_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
            if (std::memcmp(
                    reinterpret_cast<const void *>(constructor),
                    constructor_expected,
                    sizeof(constructor_expected)) != 0
                || std::memcmp(
                    reinterpret_cast<const void *>(main_menu),
                    main_menu_expected,
                    sizeof(main_menu_expected)) != 0) {
                plugin_instance->logger().Failure("frontend constructor signature mismatch; save loading disabled");
                return false;
            }
            frontend_constructor_return_address = constructor + sizeof(constructor_expected);
            main_menu_return_address = main_menu + sizeof(main_menu_expected);
            smedley::memory::Hook(
                constructor,
                reinterpret_cast<void *>(&FrontendConstructorTrampoline),
                sizeof(constructor_expected),
                nullptr);
            smedley::memory::Hook(
                main_menu,
                reinterpret_cast<void *>(&MainMenuTrampoline),
                sizeof(main_menu_expected),
                nullptr);
            return true;
        }

        static bool CheckAutomationSignatures()
        {
            const auto press_dispatch = smedley::memory::Map::base_addr + 0x5ee510;
            constexpr unsigned char press_expected[] = {
                0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
            const auto release_dispatch = smedley::memory::Map::base_addr + 0x5ee550;
            constexpr unsigned char release_expected[] = {
                0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
            if (std::memcmp(
                    reinterpret_cast<const void *>(press_dispatch),
                    press_expected,
                    sizeof(press_expected)) != 0
                || std::memcmp(
                    reinterpret_cast<const void *>(release_dispatch),
                    release_expected,
                    sizeof(release_expected)) != 0) {
                plugin_instance->logger().Failure(
                    "GUI dispatch signature mismatch; save automation disabled");
                return false;
            }
            return true;
        }

        static std::wstring ReadSaveArgument()
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) {
                return {};
            }
            constexpr wchar_t prefix[] = L"-smedley-save=";
            std::wstring result;
            for (int index = 1; index < argc; ++index) {
                const std::wstring argument = argv[index];
                if (argument.rfind(prefix, 0) == 0) {
                    result = argument.substr(std::size(prefix) - 1);
                    break;
                }
            }
            LocalFree(argv);
            return result;
        }

        static void CALLBACK SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD)
        {
            if (plugin_instance == nullptr || timer != plugin_instance->save_timer) {
                return;
            }
            KillTimer(nullptr, timer);
            plugin_instance->save_timer = 0;
            auto *controller = frontend_controller.load(std::memory_order_acquire);
            if (controller == nullptr) {
                plugin_instance->logger().Failure("frontend controller is unavailable for save selection");
                return;
            }
            if (!plugin_instance->lobby_requested) {
                if (main_menu_controller.load(std::memory_order_acquire) == nullptr) {
                    plugin_instance->save_timer = SetTimer(nullptr, 0, 1'000, SaveTimerCallback);
                    return;
                }
                if (!plugin_instance->DispatchMainMenuSinglePlayer()) {
                    return;
                }
                plugin_instance->lobby_requested = true;
                plugin_instance->save_timer = SetTimer(nullptr, 0, 3'000, SaveTimerCallback);
                if (plugin_instance->save_timer == 0) {
                    plugin_instance->logger().Failure("failed to schedule lobby save selection");
                }
                return;
            }
            if (!plugin_instance->save_selection_requested) {
                plugin_instance->save_selection_requested = true;
                auto *selected_save = reinterpret_cast<smedley::sstd::string *>(
                    reinterpret_cast<unsigned char *>(controller) + 0x590);
                if (selected_save->size() == 0) {
                    const auto filename = fs::path(plugin_instance->save_path).filename().string();
                    new (selected_save) smedley::sstd::string(filename.c_str());
                }
                *(reinterpret_cast<unsigned char *>(controller) + 0x5bc) = 1;
                *(reinterpret_cast<unsigned char *>(controller) + 0x5bd) = 0;
                plugin_instance->save_timer = SetTimer(nullptr, 0, 5'000, SaveTimerCallback);
                return;
            }
            if (*(reinterpret_cast<unsigned char *>(controller) + 0x5bd) != 0) {
                plugin_instance->DispatchControlSignal("play_button");
                return;
            }
            ++plugin_instance->save_attempts;
            if (plugin_instance->save_attempts < 24) {
                plugin_instance->save_timer = SetTimer(nullptr, 0, 5'000, SaveTimerCallback);
            } else {
                plugin_instance->logger().Failure("save selection did not finish within 120 seconds");
            }
        }

        bool DispatchMainMenuSinglePlayer()
        {
            auto *controller = main_menu_controller.load(std::memory_order_acquire);
            auto *gui = controller == nullptr
                ? nullptr
                : *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(controller) + 0x704);
            if (gui == nullptr) {
                logger().Failure("main-menu GUI registry is unavailable");
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
                logger().Failure("mainmenu_panel is unavailable for native dispatch");
                return false;
            }
            const auto panel_vtable = *reinterpret_cast<uintptr_t **>(panel);
            auto *button = reinterpret_cast<FindControl>(panel_vtable[0x34 / sizeof(uintptr_t)])(
                panel,
                &button_name);
            if (button == nullptr) {
                logger().Failure("single_player_button is unavailable for native dispatch");
                return false;
            }
            auto *signal = reinterpret_cast<unsigned char *>(button) + 0x54;
            const auto press = smedley::memory::Map::base_addr + 0x5ee510;
            const auto release = smedley::memory::Map::base_addr + 0x5ee550;
            __asm mov eax, signal
            __asm call press
            __asm mov eax, signal
            __asm call release
            logger().Info("dispatched native main-menu Single Player signal");
            return true;
        }

        bool DispatchControlSignal(const char *name)
        {
            auto *controller = frontend_controller.load(std::memory_order_acquire);
            auto *gui = controller == nullptr
                ? nullptr
                : *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(controller) + 0x278);
            if (gui == nullptr) {
                logger().Failure("frontend GUI registry is unavailable for control dispatch");
                return false;
            }
            const smedley::sstd::string control_name(name);
            const auto vtable = *reinterpret_cast<uintptr_t **>(gui);
            using FindControl = void *(__thiscall *)(void *, const smedley::sstd::string *);
            auto *control = reinterpret_cast<FindControl>(vtable[0x34 / sizeof(uintptr_t)])(
                gui,
                &control_name);
            if (control == nullptr) {
                logger().Failure(std::string(name) + " is unavailable for native dispatch");
                return false;
            }
            auto *signal = reinterpret_cast<unsigned char *>(control) + 0x54;
            std::ostringstream message;
            message << "dispatching native control signal: " << name
                    << " control=" << control
                    << " vtable=" << *reinterpret_cast<void **>(control)
                    << " signal=" << static_cast<void *>(signal);
            logger().Info(message.str());
            const auto press = smedley::memory::Map::base_addr + 0x5ee510;
            const auto release = smedley::memory::Map::base_addr + 0x5ee550;
            __asm mov eax, signal
            __asm call press
            __asm mov eax, signal
            __asm call release
            logger().Info(std::string("dispatched native control signal: ") + name);
            return true;
        }
    };

    void __stdcall TraceLoadSave(
        const smedley::sstd::string *filename,
        uintptr_t return_address,
        void *caller_object)
    {
        if (plugin_instance != nullptr && filename != nullptr) {
            plugin_instance->LogNativeLoadSave(filename, return_address, caller_object);
        }
    }

    void __stdcall TracePauseCandidate(void *controller)
    {
        static int traces_remaining = 8;
        if (plugin_instance == nullptr || controller == nullptr || traces_remaining-- <= 0) {
            return;
        }
        const auto flag = *(reinterpret_cast<const unsigned char *>(controller) + 0x1538);
        plugin_instance->LogPauseCandidate(controller, flag);
    }

    void __stdcall CaptureFrontendController(void *controller)
    {
        if (plugin_instance != nullptr && controller != nullptr) {
            const auto previous = frontend_controller.exchange(controller, std::memory_order_acq_rel);
            if (previous == controller) {
                return;
            }
            plugin_instance->LogFrontendController(controller);
        }
    }

    void __stdcall CaptureMainMenuController(void *controller)
    {
        if (controller != nullptr) {
            void *expected = nullptr;
            main_menu_controller.compare_exchange_strong(
                expected,
                controller,
                std::memory_order_release,
                std::memory_order_relaxed);
        }
    }

}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new economy_trace::Plugin();
}
