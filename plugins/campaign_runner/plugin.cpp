#include "campaign_launcher.hpp"

#include <smedley/events/console.hpp>
#include <smedley/plugin.hpp>

#include <shellapi.h>
#include <windows.h>

#include <memory>
#include <optional>
#include <string>

namespace campaign_runner
{
    namespace
    {
        struct LaunchArguments
        {
            std::optional<std::wstring> save;
            std::optional<std::wstring> view_tag;
            bool observe = false;
            int speed = 5;
            bool speed_requested = false;
            bool start_paused = false;
        };

        bool ParseLaunchArguments(LaunchArguments *arguments, std::string *error)
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) {
                *error = "could not parse the process command line";
                return false;
            }
            bool valid = true;
            auto fail = [&](const char *message) {
                *error = message;
                valid = false;
            };
            for (int index = 1; index < argc; ++index) {
                const std::wstring argument = argv[index];
                if (argument.rfind(L"-smedley-save=", 0) == 0) {
                    if (arguments->save || argument.size() == 14) fail("-smedley-save requires one non-empty value");
                    else arguments->save = argument.substr(14);
                } else if (argument.rfind(L"-smedley-save", 0) == 0) {
                    fail("malformed -smedley-save argument");
                } else if (argument.rfind(L"-smedley-view-tag=", 0) == 0) {
                    if (arguments->view_tag || argument.size() == 18) fail("-smedley-view-tag requires one non-empty value");
                    else arguments->view_tag = argument.substr(18);
                } else if (argument.rfind(L"-smedley-view-tag", 0) == 0) {
                    fail("malformed -smedley-view-tag argument");
                } else if (argument.rfind(L"-smedley-speed=", 0) == 0) {
                    const auto value = argument.substr(15);
                    if (arguments->speed_requested || value.size() != 1 || value[0] < L'1' || value[0] > L'5') {
                        fail("-smedley-speed must appear once with a value from 1 through 5");
                    } else {
                        arguments->speed = value[0] - L'0';
                        arguments->speed_requested = true;
                    }
                } else if (argument.rfind(L"-smedley-speed", 0) == 0) {
                    fail("malformed -smedley-speed argument");
                } else if (argument == L"-smedley-observe") {
                    if (arguments->observe) fail("-smedley-observe must not be repeated");
                    arguments->observe = true;
                } else if (argument.rfind(L"-smedley-observe", 0) == 0) {
                    fail("malformed -smedley-observe argument");
                } else if (argument == L"-smedley-start-paused") {
                    if (arguments->start_paused) fail("-smedley-start-paused must not be repeated");
                    arguments->start_paused = true;
                } else if (argument.rfind(L"-smedley-start-paused", 0) == 0) {
                    fail("malformed -smedley-start-paused argument");
                }
                if (!valid) {
                    break;
                }
            }
            LocalFree(argv);
            if (!valid) return false;
            if ((arguments->speed_requested || arguments->start_paused) && !arguments->save) {
                *error = "speed and start-paused controls require -smedley-save";
                return false;
            }
            if (arguments->observe && !arguments->save) {
                *error = "-smedley-observe requires -smedley-save";
                return false;
            }
            if (arguments->view_tag && !arguments->observe) {
                *error = "-smedley-view-tag requires -smedley-observe";
                return false;
            }
            if (arguments->observe && arguments->start_paused) {
                *error = "observer mode cannot start paused because its watchdog requires advancement";
                return false;
            }
            return true;
        }
    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            launcher_ = std::make_unique<CampaignLauncher>(logger());
            AddEventHandler<smedley::events::ConsoleCmdManagerInitEvent>(
                "campaign_runner_console",
                [this](smedley::events::ConsoleCmdManagerInitEvent &event) {
                    launcher_->CaptureConsoleCommandManager(event.cmd_mgr());
                });
            LaunchArguments arguments;
            std::string error;
            if (!ParseLaunchArguments(&arguments, &error)) {
                logger().Failure("campaign launcher arguments are invalid: " + error);
                return;
            }
            launcher_->Start(
                arguments.save.value_or(std::wstring{}),
                arguments.observe,
                arguments.view_tag.value_or(std::wstring{}),
                arguments.speed,
                arguments.start_paused);
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::ConsoleCmdManagerInitEvent>("campaign_runner_console");
            if (launcher_ != nullptr) {
                launcher_->Stop();
            }
        }

    private:
        std::unique_ptr<CampaignLauncher> launcher_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new campaign_runner::Plugin();
}
