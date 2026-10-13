#pragma once

#include <smedley/plugin_abi.h>

#include <string>
#include <vector>

namespace smedley
{
    bool ValidatePluginApiV1(const SmedleyPluginApiV1 &api, std::string *error);

    class PluginAbiV1Instance
    {
    public:
        explicit PluginAbiV1Instance(SmedleyPluginApiV1 api);
        ~PluginAbiV1Instance();
        PluginAbiV1Instance(const PluginAbiV1Instance &) = delete;
        PluginAbiV1Instance &operator=(const PluginAbiV1Instance &) = delete;

        bool Start(std::string *error);
        void Stop(std::vector<std::string> *errors = nullptr) noexcept;

    private:
        void CleanupAfterLoadAttempt(std::vector<std::string> *errors) noexcept;

        SmedleyPluginApiV1 api_{};
        void *storage_ = nullptr;
        bool created_ = false;
        bool load_attempted_ = false;
        bool started_ = false;
    };
}
