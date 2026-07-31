#include <smedley/events/dailyupdate.hpp>
#include <smedley/memory.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <chrono>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>

namespace economy_trace
{
    namespace fs = std::filesystem;
    constexpr UINT load_save_message = WM_APP + 0x534d;
    class Plugin;
    Plugin *plugin_instance = nullptr;
    uintptr_t load_save_return_address = 0;
    uintptr_t pause_candidate_return_address = 0;

    void __stdcall TraceLoadSave(
        const smedley::sstd::string *filename,
        uintptr_t return_address,
        void *caller_object);
    void __stdcall TracePauseCandidate(void *controller);

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

    class Plugin final : public smedley::Plugin
    {
        std::ofstream output;
        int last_date = (std::numeric_limits<int>::min)();
        std::wstring save_path;
        HWND game_window = nullptr;
        std::atomic<HHOOK> window_hook = nullptr;

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

            save_path = ReadSaveArgument();
            if (!save_path.empty() && load_save_supported) {
                std::thread([this] { ScheduleSaveLoad(); }).detach();
            }
        }

        void OnUnload() override
        {
            const auto hook = window_hook.exchange(nullptr);
            if (hook != nullptr) {
                UnhookWindowsHookEx(hook);
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

        static std::string NativeSavePath(const std::wstring &save_path)
        {
            PWSTR documents = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents) != S_OK) {
                return {};
            }
            const fs::path save_root = fs::path(documents)
                / L"Paradox Interactive" / L"Victoria II" / L"save games";
            CoTaskMemFree(documents);

            std::error_code error;
            const auto canonical_root = fs::weakly_canonical(save_root, error);
            if (error) {
                return {};
            }
            const auto canonical_save = fs::weakly_canonical(fs::path(save_path), error);
            if (error) {
                return {};
            }

            auto root = canonical_root.native();
            root.push_back(L'\\');
            const auto save = canonical_save.native();
            if (save.size() <= root.size() || _wcsnicmp(save.c_str(), root.c_str(), root.size()) != 0) {
                return {};
            }
            return (fs::path(L"save games") / save.substr(root.size())).generic_string();
        }

        static BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
        {
            DWORD process_id = 0;
            GetWindowThreadProcessId(window, &process_id);
            if (process_id != GetCurrentProcessId() || !IsWindowVisible(window)) {
                return TRUE;
            }
            *reinterpret_cast<HWND *>(parameter) = window;
            return FALSE;
        }

        void ScheduleSaveLoad()
        {
            using namespace std::chrono_literals;
            // The loading screen briefly pumps messages before frontend state exists.
            std::this_thread::sleep_for(20s);
            int responsive_seconds = 0;
            for (int elapsed = 0; elapsed < 180; ++elapsed) {
                game_window = nullptr;
                EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&game_window));
                DWORD_PTR ignored = 0;
                const bool responsive = game_window != nullptr
                    && SendMessageTimeoutW(game_window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 500, &ignored) != 0;
                responsive_seconds = responsive ? responsive_seconds + 1 : 0;
                if (responsive_seconds >= 5) {
                    break;
                }
                std::this_thread::sleep_for(1s);
            }
            if (responsive_seconds < 5) {
                logger().Failure("main menu did not become responsive; save was not loaded");
                return;
            }

            const auto window_thread = GetWindowThreadProcessId(game_window, nullptr);
            const auto hook = SetWindowsHookExW(WH_CALLWNDPROC, WindowMessageHook, nullptr, window_thread);
            if (hook == nullptr) {
                logger().Failure("failed to schedule save loading on the game UI thread");
                return;
            }
            window_hook.store(hook, std::memory_order_release);
            if (!PostMessageW(game_window, load_save_message, 0, 0)) {
                window_hook.store(nullptr, std::memory_order_release);
                UnhookWindowsHookEx(hook);
                logger().Failure("failed to post save loading to the game UI thread");
            }
        }

        static LRESULT CALLBACK WindowMessageHook(int code, WPARAM wparam, LPARAM lparam)
        {
            if (code >= 0 && plugin_instance != nullptr) {
                const auto *message = reinterpret_cast<const CWPSTRUCT *>(lparam);
                if (message->hwnd == plugin_instance->game_window
                    && message->message == load_save_message) {
                    const auto hook = plugin_instance->window_hook.exchange(nullptr);
                    if (hook != nullptr) {
                        UnhookWindowsHookEx(hook);
                    }
                    plugin_instance->LoadSaveOnUiThread();
                }
            }
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        void LoadSaveOnUiThread()
        {
            const std::string native_path = NativeSavePath(save_path);
            if (native_path.empty()) {
                logger().Failure("save path is not inside the Victoria II save games directory");
                return;
            }
            const smedley::sstd::string game_path(native_path.c_str());
            auto *game_state = smedley::v2::CCurrentGameState::instance();
            const bool loaded = game_state != nullptr && game_state->LoadSave(game_path);
            if (loaded) {
                logger().Info("loaded save: " + native_path);
                logger().Info("frontend transition is still required before simulation can run");
            } else {
                logger().Failure("failed to load save: " + native_path);
            }
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
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new economy_trace::Plugin();
}
