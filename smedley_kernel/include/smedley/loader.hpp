#pragma once

#include "plugin.hpp"
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
        std::string _gamedir;
        std::string _userdir;
        std::string _plugindir;
        std::string _log_filepath;

        std::unique_ptr<Logger> _logger;

        bool _loaded;
        std::vector<PluginDefinition> _plugin_defs;
        std::vector<Plugin *> _plugins;

        static PluginLoader *_instance;
    public:
        PluginLoader();

        void LoadPlugins();
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
