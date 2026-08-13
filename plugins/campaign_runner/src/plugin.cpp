#include "campaign_launcher.hpp"
#include "campaign_launch_arguments.hpp"
#include "campaign_services.hpp"

#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
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
            if (argv == nullptr) { *error = "could not parse the process command line"; return false; }
            std::vector<std::wstring> values(argv, argv + argc);
            LocalFree(argv);
            return ParseCampaignLaunchArguments(values, arguments, error);
        }

        SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL ConsoleCallback(
            void *context, const SmedleyCampaignConsoleInputV1 *input, SmedleyCampaignConsoleResultV1 *result) noexcept
        {
            if (result == nullptr) return SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
            *result = {sizeof(*result), SMEDLEY_CAMPAIGN_CONSOLE_RESULT_VERSION_V1, 1, 0, 0};
            static constexpr char queued[] = "campaign console request queued";
            const bool valid = input != nullptr && input->struct_size == sizeof(*input)
                && input->version == SMEDLEY_CAMPAIGN_CONSOLE_INPUT_VERSION_V1;
            result->message_bytes = sizeof(queued) - 1;
            std::memcpy(result->message, queued, result->message_bytes);
            auto *launcher = static_cast<CampaignLauncher *>(context);
            const bool queued_request = launcher != nullptr && valid
                && launcher->QueueConsoleCommand(input->command, input->argument_count,
                    input->arguments_valid, input->first_argument);
            if (!queued_request) {
                static constexpr char busy[] = "campaign console request busy";
                result->success = 0;
                result->message_bytes = sizeof(busy) - 1;
                std::memcpy(result->message, busy, result->message_bytes);
            }
            return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
        }
    }

    class Plugin final
    {
    public:
        void OnLoad()
        {
            std::string error;
            if (!smedley::game_state::AcquireServices(&error)) throw std::runtime_error(error);
            launcher_ = std::make_unique<CampaignLauncher>(smedley::game_state::LoggerInstance());
            if (!smedley::game_state::RegisterCampaignConsole(&ConsoleCallback, launcher_.get(), &console_registration_))
                throw std::runtime_error("campaign console registration failed");
            CampaignLaunchArguments arguments;
            if (!ParseCommandLineArguments(&arguments, &error)) {
                smedley::game_state::LoggerInstance().Failure("campaign launcher arguments are invalid: " + error);
                return;
            }
            launcher_->Start(arguments.save.value_or(std::wstring{}), arguments.observe,
                arguments.view_tag.value_or(std::wstring{}), arguments.speed, arguments.start_paused,
                arguments.quit_after_run, arguments.run_condition);
        }

        void OnUnload()
        {
            if (console_registration_) smedley::game_state::UnregisterCampaignConsole(console_registration_);
            console_registration_ = 0;
            if (launcher_) launcher_->Stop();
            launcher_.reset();
            smedley::game_state::ReleaseServices();
        }

    private:
        std::unique_ptr<CampaignLauncher> launcher_;
        SmedleyEventServicesRegistration console_registration_ = 0;
    };
}

namespace
{
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Create(void *instance, uint32_t size)
    {
        if (!instance || size != sizeof(campaign_runner::Plugin)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        new (instance) campaign_runner::Plugin(); return SMEDLEY_PLUGIN_SUCCESS;
    }
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Load(void *instance)
    {
        if (!instance) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try { static_cast<campaign_runner::Plugin *>(instance)->OnLoad(); return SMEDLEY_PLUGIN_SUCCESS; }
        catch (...) { return SMEDLEY_PLUGIN_FAILURE; }
    }
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Unload(void *instance)
    {
        if (!instance) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        static_cast<campaign_runner::Plugin *>(instance)->OnUnload(); return SMEDLEY_PLUGIN_SUCCESS;
    }
    void SMEDLEY_PLUGIN_CALL Destroy(void *instance) { if (instance) static_cast<campaign_runner::Plugin *>(instance)->~Plugin(); }
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (!api || api->struct_size != sizeof(*api) || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1
        || api->reserved[0] || api->reserved[1] || api->reserved[2]) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    api->instance_size = sizeof(campaign_runner::Plugin); api->instance_alignment = alignof(campaign_runner::Plugin);
    api->create = &Create; api->load = &Load; api->unload = &Unload; api->destroy = &Destroy;
    return SMEDLEY_PLUGIN_SUCCESS;
}
