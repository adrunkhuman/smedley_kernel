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
        std::optional<std::wstring> ReadArgument(const wchar_t *prefix)
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) {
                return std::nullopt;
            }
            std::optional<std::wstring> result;
            const std::wstring prefix_value(prefix);
            for (int index = 1; index < argc; ++index) {
                const std::wstring argument = argv[index];
                if (argument.rfind(prefix_value, 0) == 0) {
                    result = argument.substr(prefix_value.size());
                    break;
                }
            }
            LocalFree(argv);
            return result;
        }

        bool HasArgument(const wchar_t *expected)
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) {
                return false;
            }
            bool found = false;
            for (int index = 1; index < argc; ++index) {
                if (std::wstring(argv[index]) == expected) {
                    found = true;
                    break;
                }
            }
            LocalFree(argv);
            return found;
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
            const auto save = ReadArgument(L"-smedley-save=");
            launcher_->Start(
                save.has_value() ? *save : std::wstring{},
                HasArgument(L"-smedley-observe"),
                ReadArgument(L"-smedley-view-tag=").value_or(std::wstring{}));
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
