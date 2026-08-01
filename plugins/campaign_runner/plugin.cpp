#include "campaign_launcher.hpp"
#include "campaign_launch_arguments.hpp"

#include <smedley/events/console.hpp>
#include <smedley/plugin.hpp>

#include <shellapi.h>
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace campaign_runner
{
    namespace
    {
        bool ParseCommandLineArguments(CampaignLaunchArguments *arguments, std::string *error)
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) {
                *error = "could not parse the process command line";
                return false;
            }
            std::vector<std::wstring> values(argv, argv + argc);
            LocalFree(argv);
            return ParseCampaignLaunchArguments(values, arguments, error);
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
            CampaignLaunchArguments arguments;
            std::string error;
            if (!ParseCommandLineArguments(&arguments, &error)) {
                logger().Failure("campaign launcher arguments are invalid: " + error);
                return;
            }
            launcher_->Start(
                arguments.save.value_or(std::wstring{}),
                arguments.observe,
                arguments.view_tag.value_or(std::wstring{}),
                arguments.speed,
                arguments.start_paused,
                arguments.quit_after_run,
                arguments.run_condition);
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
