#pragma once

#include "eventregistry.hpp"
#include "log.hpp"
#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

#define PLUGIN_API extern "C" __declspec(dllexport)

namespace smedley
{

    /**
     * Plugin metadata.
     */
    struct PluginDefinition
    {
        struct Version
        {
            std::string str;
            std::unique_ptr<int[]> versions;
            int num_versions;

            static Version Parse(const std::string &s);

            friend bool operator==(const Version &lhs, const Version &rhs);
            friend bool operator!=(const Version &lhs, const Version &rhs);
        };

        struct Dependency
        {
            std::string id;
            std::optional<Version> eq;
            std::optional<Version> gt;
            std::optional<Version> lt;
        };

        std::string id;
        std::string name;
        std::string description;
        std::string module_name;
        Version version;
        std::vector<Dependency> dependencies;

        PluginDefinition() {}
        PluginDefinition(const PluginDefinition &def) : id(def.id), name(def.name), description(def.description), module_name(def.module_name) {}

        void operator=(const PluginDefinition &def) { id = def.id; name = def.name; description = def.description; module_name = def.module_name; }

        static PluginDefinition Read(const std::filesystem::path &filename);
    };

    /**
     * Base interface for Smedley plugins. Each legacy plugin module must define
     * a Plugin subclass. The base class provides plugin helpers and the interface
     * used by PluginLoader.
     */
    class Plugin
    {
    private: // fields populated by the loader
        HMODULE _hmod;
        PluginDefinition _definition;
        uint32_t _checksum;

        std::unique_ptr<Logger> _logger;
    public:
        /// The constructor must only initialize the object. The loader populates
        /// its properties immediately after construction, so accessing them in
        /// the constructor is undefined behavior.
        Plugin();
        virtual ~Plugin() {};

        /// @brief Called after the loader initializes the plugin.
        virtual void OnLoad() {};
        /// @brief Called when the loader requests plugin unloading.
        virtual void OnUnload() {};

        const PluginDefinition &definition() const noexcept { return _definition; }
        uint32_t checksum() const noexcept { return _checksum; }
        HMODULE mod_handle() const noexcept { return _hmod; }

        friend class PluginLoader;
    protected:
        /**
         * Registers a plugin event handler with an event registry.
         */
        template <class Ev>
        inline void AddEventHandler(const std::string &id, std::function<void(Ev &)> handler, EventHandlerPriority priority = EventHandlerPriority::LOWEST)
        {
            EventRegistry<Ev>::Register(this, id, handler, priority);
        }

        /**
         * Unregisters a plugin event handler.
         */
        template <class Ev>
        inline void RemoveEventHandler(const std::string &id)
        {
            EventRegistry<Ev>::Unregister(this, id);
        }

        Logger &logger() const noexcept { return *_logger; }
    };

    

}
