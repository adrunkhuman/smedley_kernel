#include <smedley/launcher/launcher.hpp>

#include <iostream>
#include <cwctype>
#include <optional>
#include <stdexcept>

namespace launcher = smedley::launcher;
namespace fs = std::filesystem;

struct Options
{
    std::optional<fs::path> profile_path;
    std::optional<fs::path> game_dir;
    std::optional<fs::path> kernel;
    std::optional<fs::path> save;
    std::optional<std::wstring> view_tag;
    std::vector<fs::path> mods;
    std::vector<fs::path> plugins;
    std::optional<bool> inject;
    std::optional<bool> detach;
    std::optional<bool> observer;
    std::optional<int> speed;
    std::optional<bool> start_paused;
    std::optional<int> run_days;
    std::optional<int> run_until_date_raw;
    std::optional<int> run_timeout_seconds;
    std::optional<bool> telemetry_enabled;
    std::optional<fs::path> telemetry_output;
    std::vector<std::string> telemetry_categories;
    std::vector<std::string> telemetry_country_tags;
    std::optional<int> telemetry_start_date_raw;
    std::optional<int> telemetry_end_date_raw;
    std::optional<int> telemetry_sample_days;
    std::optional<int> telemetry_queue_capacity;
    std::optional<bool> telemetry_overwrite;
    std::vector<fs::path> scripts;
    std::optional<int> script_instruction_budget;
    std::optional<int> script_memory_bytes;
    std::optional<int> script_queue_capacity;
    bool dry_run = false;
    bool discover = false;
    bool history = false;
    bool help = false;
};

void PrintUsage()
{
    std::cout
        << "Usage: smedley_cli (--game-dir PATH | --profile PATH) [options]\n\n"
        << "  --profile PATH  Load a launcher profile\n"
        << "  --game-dir PATH Game directory\n"
        << "  --kernel PATH   Kernel DLL (default: GAME_DIR/smedley_kernel.dll)\n"
        << "  --mod PATH      Mod descriptor under GAME_DIR/mod; may be repeated\n"
        << "  --plugin PATH   Plugin TOML under GAME_DIR/plugins; may be repeated\n"
        << "  --no-inject     Launch the original game without Smedley\n"
        << "  --save PATH     Save file for campaign_runner\n"
        << "  --observe       Return the player country to AI control before unpausing\n"
        << "  --view-tag TAG  Select an initial observer view\n"
        << "  --speed N       Set initial campaign speed from 1 through 5 (default: 5)\n"
        << "  --start-paused  Leave a loaded campaign paused; incompatible with --observe\n"
        << "  --run-days N    Benchmark exactly N game days (1 through 1000000)\n"
        << "  --run-until-date-raw N  Benchmark to an absolute raw date; incompatible with --run-days\n"
        << "  --run-timeout-seconds N  Benchmark timeout (1 through 86400; default: 600)\n"
        << "  --telemetry     Enable the built-in structured telemetry plugin\n"
        << "  --telemetry-output PATH  JSON Lines trace path (default: %LOCALAPPDATA%\\Smedley\\traces\\<run-id>.jsonl)\n"
        << "  --telemetry-category NAME  lifecycle or state; may be repeated\n"
        << "  --telemetry-country TAG  Three-character country tag; may be repeated\n"
        << "  --telemetry-start-date-raw N  Optional inclusive raw game date\n"
        << "  --telemetry-end-date-raw N  Optional inclusive raw game date\n"
        << "  --telemetry-sample-days N  State sample interval from 1 through 365 (default: 1)\n"
        << "  --telemetry-queue-capacity N  Bounded record queue from 64 through 8192 (default: 1024)\n"
        << "  --telemetry-overwrite  Replace an existing telemetry output file\n"
        << "  --script PATH   Lua source under GAME_DIR/scripts; may be repeated\n"
        << "  --script-instruction-budget N  Per-callback Lua instruction limit (default: 100000)\n"
        << "  --script-memory-bytes N  Memory limit per Lua script (default: 8388608)\n"
        << "  --script-queue-capacity N  Bounded event queue from 16 through 4096 (default: 256)\n"
        << "  --detach        Return after Victoria 2 starts\n"
        << "  --discover      List GAME_DIR plugins and mods\n"
        << "  --history       List the 20 most recent launcher runs\n"
        << "  --dry-run       Print diagnostics and the resolved launch plan\n"
        << "  --help          Show this help\n";
}

Options ParseArguments(int argc, wchar_t **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--help") { options.help = true; continue; }
        if (argument == L"--dry-run") { options.dry_run = true; continue; }
        if (argument == L"--discover") { options.discover = true; continue; }
        if (argument == L"--history") { options.history = true; continue; }
        if (argument == L"--detach") { options.detach = true; continue; }
        if (argument == L"--observe") { options.observer = true; continue; }
        if (argument == L"--no-inject") { options.inject = false; continue; }
        if (argument == L"--start-paused") { options.start_paused = true; continue; }
        if (argument == L"--telemetry") { options.telemetry_enabled = true; continue; }
        if (argument == L"--no-telemetry") { options.telemetry_enabled = false; continue; }
        if (argument == L"--telemetry-overwrite") { options.telemetry_overwrite = true; continue; }
        if (argument == L"--speed" || argument == L"--telemetry-sample-days" || argument == L"--telemetry-queue-capacity"
            || argument == L"--telemetry-start-date-raw" || argument == L"--telemetry-end-date-raw"
            || argument == L"--run-days" || argument == L"--run-until-date-raw" || argument == L"--run-timeout-seconds"
            || argument == L"--script-instruction-budget" || argument == L"--script-memory-bytes"
            || argument == L"--script-queue-capacity") {
            if (++i == argc) throw std::runtime_error("missing numeric argument value");
            const std::wstring value = argv[i];
            size_t parsed = 0;
            try {
                const int number = std::stoi(value, &parsed);
                if (argument == L"--speed") options.speed = number;
                else if (argument == L"--telemetry-sample-days") options.telemetry_sample_days = number;
                else if (argument == L"--telemetry-queue-capacity") options.telemetry_queue_capacity = number;
                else if (argument == L"--telemetry-start-date-raw") options.telemetry_start_date_raw = number;
                else if (argument == L"--telemetry-end-date-raw") options.telemetry_end_date_raw = number;
                else if (argument == L"--run-days") options.run_days = number;
                else if (argument == L"--run-until-date-raw") options.run_until_date_raw = number;
                else if (argument == L"--run-timeout-seconds") options.run_timeout_seconds = number;
                else if (argument == L"--script-instruction-budget") options.script_instruction_budget = number;
                else if (argument == L"--script-memory-bytes") options.script_memory_bytes = number;
                else options.script_queue_capacity = number;
            } catch (const std::exception &) {
                throw std::runtime_error("numeric options must be integers");
            }
            if (parsed != value.size()) throw std::runtime_error("numeric options must be integers");
            continue;
        }
        if (argument != L"--profile" && argument != L"--game-dir" && argument != L"--kernel"
            && argument != L"--mod" && argument != L"--plugin" && argument != L"--save" && argument != L"--view-tag"
            && argument != L"--telemetry-output" && argument != L"--telemetry-category" && argument != L"--telemetry-country"
            && argument != L"--script") {
            throw std::runtime_error("unknown argument");
        }
        if (++i == argc) throw std::runtime_error("missing argument value");
        const fs::path value = argv[i];
        if (argument == L"--profile") options.profile_path = value;
        else if (argument == L"--game-dir") options.game_dir = value;
        else if (argument == L"--kernel") options.kernel = value;
        else if (argument == L"--mod") options.mods.push_back(value);
        else if (argument == L"--plugin") options.plugins.push_back(value);
        else if (argument == L"--save") options.save = value;
        else if (argument == L"--telemetry-output") options.telemetry_output = value;
        else if (argument == L"--script") options.scripts.push_back(value);
        else if (argument == L"--telemetry-category") {
            const auto category = value.wstring();
            std::string narrow_category;
            for (const wchar_t character : category) {
                if (character > 0x7f) throw std::runtime_error("--telemetry-category must be ASCII");
                narrow_category += static_cast<char>(character);
            }
            options.telemetry_categories.push_back(std::move(narrow_category));
        } else if (argument == L"--telemetry-country") {
            const auto tag = value.wstring();
            if (tag.size() != 3) throw std::runtime_error("--telemetry-country must be exactly three ASCII alphanumeric characters");
            std::string normalized;
            for (const wchar_t character : tag) {
                if ((character < L'A' || character > L'Z') && (character < L'a' || character > L'z')
                    && (character < L'0' || character > L'9')) {
                    throw std::runtime_error("--telemetry-country must be exactly three ASCII alphanumeric characters");
                }
                normalized += static_cast<char>(towupper(character));
            }
            options.telemetry_country_tags.push_back(std::move(normalized));
        }
        else options.view_tag = value.wstring();
    }
    return options;
}

void PrintDiagnostics(const std::vector<launcher::Diagnostic> &diagnostics)
{
    for (const auto &diagnostic : diagnostics) {
        const char *severity = diagnostic.severity == launcher::Severity::Error ? "error" :
                               diagnostic.severity == launcher::Severity::Warning ? "warning" : "info";
        std::cerr << severity << " [" << diagnostic.code << "]: " << diagnostic.message;
        if (!diagnostic.path.empty()) std::cerr << " (" << diagnostic.path.string() << ')';
        std::cerr << '\n';
    }
}

void PrintPlan(const launcher::LaunchPlan &plan)
{
    std::wcout << L"game:    " << plan.game_executable << L"\n"
               << L"inject:  " << (plan.profile.inject ? L"enabled" : L"disabled") << L"\n";
    if (plan.profile.inject) std::wcout << L"kernel:  " << plan.kernel << L"\n";
    if (plan.profile.telemetry_enabled) std::wcout << L"telemetry: enabled\n";
    for (const auto &script : plan.profile.scripts) std::wcout << L"script:  " << script << L"\n";
    for (const auto &mod : plan.mods) std::wcout << L"mod:     " << mod.descriptor_path << L"\n";
    for (const auto &plugin : plan.plugins) std::wcout << L"plugin:  " << plugin.manifest_path << L"\n";
    if (plan.profile.save) std::wcout << L"save:    " << *plan.profile.save << L"\n";
    std::wcout << L"command: " << plan.command_line << L"\n";
}

int wmain(int argc, wchar_t **argv)
{
    static_assert(sizeof(void *) == 4, "build smedley_cli for x86 Victoria 2");
    try {
        const auto options = ParseArguments(argc, argv);
        if (options.help) {
            PrintUsage();
            return 0;
        }
        if (options.history) {
            std::vector<launcher::Diagnostic> diagnostics;
            const auto records = launcher::LoadRunHistory(20, &diagnostics);
            PrintDiagnostics(diagnostics);
            for (const auto &record : records) {
                std::cout << record.started_at_utc << " " << launcher::RunStatusName(record.status)
                          << " " << record.profile_name << " PID=";
                if (record.process_id) std::cout << *record.process_id;
                else std::cout << "-";
                std::cout << " " << record.metadata_path.string() << '\n';
            }
            return launcher::HasErrors(diagnostics) ? 1 : 0;
        }
        launcher::Profile profile;
        std::vector<launcher::Diagnostic> diagnostics;
        if (options.profile_path && !launcher::LoadProfile(*options.profile_path, &profile, &diagnostics)) {
            PrintDiagnostics(diagnostics);
            return 1;
        }
        if (options.game_dir) profile.game_dir = *options.game_dir;
        if (options.kernel) profile.kernel = *options.kernel;
        if (options.save) profile.save = *options.save;
        if (options.view_tag) profile.view_tag = *options.view_tag;
        if (options.inject) profile.inject = *options.inject;
        if (options.detach) profile.detach = *options.detach;
        if (options.observer) profile.observer = *options.observer;
        if (options.speed) profile.speed = *options.speed;
        if (options.start_paused) profile.start_paused = *options.start_paused;
        if (options.run_days) profile.run_days = *options.run_days;
        if (options.run_until_date_raw) profile.run_until_date_raw = *options.run_until_date_raw;
        if (options.run_timeout_seconds) profile.run_timeout_seconds = *options.run_timeout_seconds;
        if (options.telemetry_enabled) profile.telemetry_enabled = *options.telemetry_enabled;
        if (options.telemetry_output) profile.telemetry_output = *options.telemetry_output;
        if (!options.telemetry_categories.empty()) profile.telemetry_categories = options.telemetry_categories;
        if (!options.telemetry_country_tags.empty()) profile.telemetry_country_tags = options.telemetry_country_tags;
        if (options.telemetry_start_date_raw) profile.telemetry_start_date_raw = *options.telemetry_start_date_raw;
        if (options.telemetry_end_date_raw) profile.telemetry_end_date_raw = *options.telemetry_end_date_raw;
        if (options.telemetry_sample_days) profile.telemetry_sample_days = *options.telemetry_sample_days;
        if (options.telemetry_queue_capacity) profile.telemetry_queue_capacity = *options.telemetry_queue_capacity;
        if (options.telemetry_overwrite) profile.telemetry_overwrite = *options.telemetry_overwrite;
        if (options.script_instruction_budget) profile.script_instruction_budget = *options.script_instruction_budget;
        if (options.script_memory_bytes) profile.script_memory_bytes = *options.script_memory_bytes;
        if (options.script_queue_capacity) profile.script_queue_capacity = *options.script_queue_capacity;
        profile.mods.insert(profile.mods.end(), options.mods.begin(), options.mods.end());
        profile.plugins.insert(profile.plugins.end(), options.plugins.begin(), options.plugins.end());
        profile.scripts.insert(profile.scripts.end(), options.scripts.begin(), options.scripts.end());
        if (profile.game_dir.empty()) throw std::runtime_error("--game-dir is required unless supplied by --profile");

        if (options.discover) {
            const auto plugins = launcher::DiscoverPlugins(profile.game_dir);
            const auto mods = launcher::DiscoverMods(profile.game_dir);
            PrintDiagnostics(plugins.diagnostics);
            PrintDiagnostics(mods.diagnostics);
            for (const auto &plugin : plugins.plugins) std::cout << "plugin " << plugin.id << " " << plugin.manifest_path.string() << '\n';
            for (const auto &mod : mods.mods) std::cout << "mod " << mod.name << " " << mod.descriptor_path.string() << '\n';
            return launcher::HasErrors(plugins.diagnostics) || launcher::HasErrors(mods.diagnostics) ? 1 : 0;
        }

        const auto plan = launcher::BuildLaunchPlan(std::move(profile));
        if (options.dry_run) {
            PrintDiagnostics(plan.diagnostics);
            PrintPlan(plan);
            return launcher::HasErrors(plan.diagnostics) ? 1 : 0;
        }
        const auto result = launcher::Launch(plan);
        PrintDiagnostics(result.diagnostics);
        if (!result.metadata_path.empty()) std::cout << "Run metadata: " << result.metadata_path.string() << '\n';
        if (!result.started || launcher::HasErrors(result.diagnostics)) return 1;
        std::cout << "Victoria 2 started (PID " << result.process_id << ")\n";
        return static_cast<int>(result.exit_code);
    } catch (const std::exception &error) {
        std::cerr << "smedley_cli: " << error.what() << '\n';
        return 1;
    }
}
