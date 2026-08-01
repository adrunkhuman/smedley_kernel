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

    struct TelemetryCaptureRule
    {
        std::string family;
        std::string cadence = "daily";
        std::vector<std::string> fields;
        std::vector<std::string> country_tags;
        std::vector<int> province_ids;
        std::optional<int> start_date_raw;
        std::optional<int> end_date_raw;
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
        std::optional<int> run_days;
        std::optional<int> run_until_date_raw;
        bool quit_after_run = false;
        int run_timeout_seconds = 600;
        std::optional<std::string> run_parse_error;
        bool telemetry_enabled = false;
        std::optional<fs::path> telemetry_output;
        std::vector<std::string> telemetry_categories = {"lifecycle", "state"};
        std::vector<std::string> telemetry_country_tags;
        std::optional<int> telemetry_start_date_raw;
        std::optional<int> telemetry_end_date_raw;
        std::optional<std::string> telemetry_filter_parse_error;
        int telemetry_sample_days = 1;
        int telemetry_queue_capacity = 1024;
        bool telemetry_overwrite = false;
        std::vector<TelemetryCaptureRule> telemetry_captures;
        std::vector<fs::path> scripts;
        int script_instruction_budget = 100000;
        int script_memory_bytes = 8388608;
        int script_queue_capacity = 256;
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

    enum class RunStatus
    {
        PreflightFailed,
        CreateFailed,
        InjectionFailed,
        Started,
        Exited,
    };

    struct RunPlugin
    {
        std::string id;
        fs::path manifest_path;
    };

    struct RunLinks
    {
        std::optional<fs::path> smedley_log;
        std::optional<fs::path> victoria_system_log;
        std::optional<fs::path> victoria_user_dir;
        std::optional<fs::path> telemetry_trace;
        std::optional<fs::path> source_save;
    };

    struct RunRecord
    {
        int schema_version = 1;
        std::string run_id;
        std::string started_at_utc;
        RunStatus status = RunStatus::PreflightFailed;
        std::optional<std::uint32_t> process_id;
        std::optional<std::uint32_t> exit_code;
        std::string profile_name;
        bool injected = false;
        bool safe_mode = true;
        fs::path executable;
        std::wstring command_line;
        std::vector<fs::path> mod_descriptors;
        std::vector<RunPlugin> plugins;
        std::vector<fs::path> scripts;
        std::optional<fs::path> save;
        bool observer = false;
        int speed = 5;
        bool start_paused = false;
        RunLinks links;
        std::vector<Diagnostic> diagnostics;
        fs::path metadata_path;
    };

    struct LaunchResult
    {
        bool started = false;
        std::uint32_t process_id = 0;
        std::uint32_t exit_code = 0;
        std::string run_id;
        fs::path metadata_path;
        std::vector<Diagnostic> diagnostics;
    };

    PluginDiscovery DiscoverPlugins(const fs::path &game_dir);
    ModDiscovery DiscoverMods(const fs::path &game_dir);
    bool LoadProfile(const fs::path &path, Profile *profile, std::vector<Diagnostic> *diagnostics);
    bool SaveProfile(const fs::path &path, const Profile &profile, std::vector<Diagnostic> *diagnostics);
    LaunchPlan BuildLaunchPlan(Profile profile);
    RunRecord CreateRunRecord(const LaunchPlan &plan);
    LaunchResult Launch(const LaunchPlan &plan);

    const char *RunStatusName(RunStatus status);
    fs::path DefaultRunDirectory();
    fs::path DefaultTraceDirectory();
    bool SaveRunRecord(const fs::path &run_directory, const RunRecord &record, std::vector<Diagnostic> *diagnostics);
    std::vector<RunRecord> LoadRunHistory(const fs::path &run_directory, size_t limit, std::vector<Diagnostic> *diagnostics);
    std::vector<RunRecord> LoadRunHistory(size_t limit, std::vector<Diagnostic> *diagnostics);

    bool HasErrors(const std::vector<Diagnostic> &diagnostics);
    bool IsPathContained(const fs::path &root, const fs::path &path);
    std::optional<fs::path> ResolveVictoriaUserDirectory(const fs::path &base,
                                                         const std::vector<ModDescriptor> &mods);
    std::wstring QuoteWindowsArgument(const std::wstring &argument);
    std::wstring BuildWindowsCommandLine(const std::vector<std::wstring> &arguments);
    std::wstring BuildInjectedCommandLine(const LaunchPlan &plan, const RunRecord &record);
}
