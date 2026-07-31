#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace smedley::launcher
{
    namespace fs = std::filesystem;

    enum class Severity
    {
        Info,
        Warning,
        Error,
    };

    struct Diagnostic
    {
        Severity severity = Severity::Error;
        std::string code;
        std::string message;
        fs::path path;
    };

    struct PluginManifest
    {
        std::string id;
        std::string name;
        std::string module;
        std::string version;
        std::vector<std::string> dependencies;
        std::vector<std::string> conflicts;
        fs::path manifest_path;
        fs::path module_path;
    };

    struct ModDescriptor
    {
        std::string name;
        std::string path;
        std::string user_dir;
        std::vector<std::string> dependencies;
        fs::path descriptor_path;
        fs::path content_path;
    };

    struct Profile
    {
        std::string name;
        fs::path game_dir;
        std::optional<fs::path> kernel;
        bool inject = true;
        std::vector<fs::path> mods;
        std::vector<fs::path> plugins;
        std::optional<fs::path> save;
        bool observer = false;
        std::optional<std::wstring> view_tag;
        int speed = 5;
        bool start_paused = false;
        bool detach = false;
    };

    struct PluginDiscovery
    {
        std::vector<PluginManifest> plugins;
        std::vector<Diagnostic> diagnostics;
    };

    struct ModDiscovery
    {
        std::vector<ModDescriptor> mods;
        std::vector<Diagnostic> diagnostics;
    };

    struct LaunchPlan
    {
        Profile profile;
        fs::path game_executable;
        fs::path kernel;
        std::vector<PluginManifest> plugins;
        std::vector<ModDescriptor> mods;
        std::wstring command_line;
        std::vector<Diagnostic> diagnostics;
    };

    struct LaunchResult
    {
        bool started = false;
        std::uint32_t process_id = 0;
        std::uint32_t exit_code = 0;
        std::vector<Diagnostic> diagnostics;
    };

    PluginDiscovery DiscoverPlugins(const fs::path &game_dir);
    ModDiscovery DiscoverMods(const fs::path &game_dir);
    bool LoadProfile(const fs::path &path, Profile *profile, std::vector<Diagnostic> *diagnostics);
    bool SaveProfile(const fs::path &path, const Profile &profile, std::vector<Diagnostic> *diagnostics);
    LaunchPlan BuildLaunchPlan(Profile profile);
    LaunchResult Launch(const LaunchPlan &plan);

    bool HasErrors(const std::vector<Diagnostic> &diagnostics);
    bool IsPathContained(const fs::path &root, const fs::path &path);
    std::wstring QuoteWindowsArgument(const std::wstring &argument);
    std::wstring BuildWindowsCommandLine(const std::vector<std::wstring> &arguments);
}
