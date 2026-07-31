#include "campaign_launcher.hpp"

#include <smedley/plugin.hpp>

#include <shellapi.h>
#include <windows.h>

#include <memory>
#include <string>

namespace campaign_runner
{
    namespace
    {
        std::wstring ReadSaveArgument()
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
    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            launcher_ = std::make_unique<CampaignLauncher>(logger());
            launcher_->Start(ReadSaveArgument());
        }

        void OnUnload() override
        {
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
