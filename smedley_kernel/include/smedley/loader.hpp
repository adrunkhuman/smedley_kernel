#pragma once

#include "log.hpp"
#include "plugin_abi_runtime.hpp"
#include <memory>
#include <filesystem>
#include <string>
#include <vector>
#include <toml.hpp>

namespace smedley
{

    /**
     * The PluginLoader is responsible for registering modules injected
     * by the bootstrapper as plugins and initializing them.
     */
    class PluginLoader
    {
        struct LoadedPlugin
        {
            std::unique_ptr<PluginAbiV1Instance> abi_v1;
        };

        std::string _gamedir;
        std::string _userdir;
        std::string _plugindir;
        std::string _log_filepath;

        std::unique_ptr<Logger> _logger;

        bool _loaded;
        std::vector<LoadedPlugin> _plugins;

        static PluginLoader *_instance;
    public:
        PluginLoader();

        bool LoadPlugins();
        void UnloadPlugins();

        static PluginLoader *instance()
        {
            if (_instance == nullptr) {
                _instance = new PluginLoader();
            }

            return _instance;
        }

        static std::vector<std::filesystem::path> ParsePluginArguments(const wchar_t *command_line);
    private:
        void LoadPluginModule();
    };

}
