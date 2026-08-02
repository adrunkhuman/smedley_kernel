#include <smedley/launcher/launcher.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace launcher = smedley::launcher;
namespace fs = std::filesystem;

namespace
{
    class LauncherCoreTest : public testing::Test
    {
    protected:
        fs::path root;

        void SetUp() override
        {
            wchar_t temporary[MAX_PATH];
            ASSERT_NE(GetTempPathW(MAX_PATH, temporary), 0u);
            root = fs::path(temporary) / (L"smedley launcher test " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(GetTickCount64()));
            fs::create_directories(root / L"plugins");
            fs::create_directories(root / L"mod" / L"Example Content");
        }

        void TearDown() override
        {
            std::error_code error;
            fs::remove_all(root, error);
        }

        void Write(const fs::path &path, const std::string &content)
        {
            std::ofstream output(path);
            ASSERT_TRUE(output);
            output << content;
        }

        fs::path BuiltCampaignPlugin() const
        {
            wchar_t executable[MAX_PATH];
            EXPECT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
            const auto configuration_dir = fs::path(executable).parent_path();
            return fs::path(SMEDLEY_BUILD_DIR)
                / L"plugins" / L"campaign_runner" / configuration_dir.filename() / L"campaign_runner.dll";
        }

        fs::path BuiltTelemetryPlugin() const
        {
            wchar_t executable[MAX_PATH];
            EXPECT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
            const auto configuration_dir = fs::path(executable).parent_path();
            return fs::path(SMEDLEY_BUILD_DIR)
                / L"plugins" / L"telemetry" / configuration_dir.filename() / L"telemetry.dll";
        }

        fs::path BuiltScriptingPlugin() const
        {
            wchar_t executable[MAX_PATH];
            EXPECT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
            const auto configuration_dir = fs::path(executable).parent_path();
            return fs::path(SMEDLEY_BUILD_DIR)
                / L"plugins" / L"scripting" / configuration_dir.filename() / L"scripting.dll";
        }

        fs::path BuiltAbiFixture() const
        {
            wchar_t executable[MAX_PATH];
            EXPECT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
            const auto configuration_dir = fs::path(executable).parent_path();
            return fs::path(SMEDLEY_BUILD_DIR)
                / L"smedley_kernel" / configuration_dir.filename() / L"smedley_plugin_abi_test_plugin.dll";
        }
    };
}

TEST_F(LauncherCoreTest, DiscoversSortedPluginsAndReportsDuplicateIds)
{
    Write(root / L"plugins" / L"z.toml", "id = \"same\"\nname = \"Z\"\nversion = \"1\"\nmodule = \"z.dll\"\ndependencies = [\"base\"]\nconflicts = [\"old\"]\n");
    Write(root / L"plugins" / L"a.toml", "id = \"same\"\nname = \"A\"\nversion = \"1\"\nmodule = \"a.dll\"\n");
    Write(root / L"plugins" / L"z.dll", "module");
    Write(root / L"plugins" / L"a.dll", "module");

    const auto discovery = launcher::DiscoverPlugins(root);

    ASSERT_EQ(discovery.plugins.size(), 2u);
    EXPECT_LT(discovery.plugins[0].manifest_path.filename(), discovery.plugins[1].manifest_path.filename());
    ASSERT_EQ(discovery.diagnostics.size(), 1u);
    EXPECT_EQ(discovery.diagnostics[0].code, "plugin.duplicate_id");
    const auto z = std::find_if(discovery.plugins.begin(), discovery.plugins.end(), [](const auto &plugin) {
        return plugin.name == "Z";
    });
    ASSERT_NE(z, discovery.plugins.end());
    EXPECT_EQ(z->dependencies, (std::vector<std::string>{"base"}));
    EXPECT_EQ(z->conflicts, (std::vector<std::string>{"old"}));
}

TEST_F(LauncherCoreTest, DiscoversModDescriptorAndDependencies)
{
    Write(root / L"mod" / L"Example.mod",
          "name = \"Example\"\npath = \"mod/Example Content\"\nuser_dir = \"Example User\"\ndependencies = { \"Base\", \"Patch\" }\n");

    const auto discovery = launcher::DiscoverMods(root);

    ASSERT_FALSE(launcher::HasErrors(discovery.diagnostics));
    ASSERT_EQ(discovery.mods.size(), 1u);
    EXPECT_EQ(discovery.mods[0].name, "Example");
    EXPECT_EQ(discovery.mods[0].dependencies, (std::vector<std::string>{"Base", "Patch"}));
    EXPECT_EQ(discovery.mods[0].content_path, root / L"mod" / L"Example Content");
}

TEST_F(LauncherCoreTest, SavesAndLoadsProfileWithSpaces)
{
    launcher::Profile original;
    original.name = "Space profile";
    original.game_dir = root / L"Victoria 2";
    original.kernel = root / L"Kernel Dir" / L"smedley kernel.dll";
    original.inject = false;
    original.mods = {fs::path(L"mod/My Mod.mod")};
    original.plugins = {fs::path(L"plugins/My Plugin.toml")};
    original.save = root / L"save games" / L"my save.v2";
    original.observer = true;
    original.view_tag = L"ENG";
    original.speed = 3;
    original.start_paused = true;
    original.detach = true;
    original.run_days = 365;
    original.quit_after_run = true;
    original.run_timeout_seconds = 900;
    original.scripts = {fs::path(L"scripts/observer.lua"), fs::path(L"scripts/report.lua")};
    original.script_instruction_budget = 250000;
    original.script_memory_bytes = 4194304;
    original.script_queue_capacity = 128;
    std::vector<launcher::Diagnostic> diagnostics;

    ASSERT_TRUE(launcher::SaveProfile(root / L"profile.toml", original, &diagnostics));
    launcher::Profile loaded;
    ASSERT_TRUE(launcher::LoadProfile(root / L"profile.toml", &loaded, &diagnostics));

    EXPECT_EQ(loaded.name, original.name);
    EXPECT_EQ(loaded.game_dir, original.game_dir);
    EXPECT_EQ(loaded.kernel, original.kernel);
    EXPECT_EQ(loaded.mods, original.mods);
    EXPECT_EQ(loaded.plugins, original.plugins);
    EXPECT_EQ(loaded.save, original.save);
    EXPECT_EQ(loaded.view_tag, original.view_tag);
    EXPECT_TRUE(loaded.observer);
    EXPECT_EQ(loaded.speed, original.speed);
    EXPECT_TRUE(loaded.start_paused);
    EXPECT_TRUE(loaded.detach);
    EXPECT_EQ(loaded.run_days, original.run_days);
    EXPECT_TRUE(loaded.quit_after_run);
    EXPECT_EQ(loaded.run_timeout_seconds, 900);
    EXPECT_EQ(loaded.scripts, original.scripts);
    EXPECT_EQ(loaded.script_instruction_budget, 250000);
    EXPECT_EQ(loaded.script_memory_bytes, 4194304);
    EXPECT_EQ(loaded.script_queue_capacity, 128);
    EXPECT_FALSE(loaded.inject);
    EXPECT_FALSE(loaded.telemetry_enabled);
    EXPECT_EQ(loaded.telemetry_categories, (std::vector<std::string>{"lifecycle", "state"}));
}

TEST_F(LauncherCoreTest, SavesAndLoadsUnicodeProfilePaths)
{
    launcher::Profile original;
    original.name = "Unicode profile";
    original.game_dir = root / L"Victoria \u03A9";
    original.mods = {fs::path(L"mod/\u03A9.mod")};
    const auto profile_path = root / L"profile \u03A9.toml";
    std::vector<launcher::Diagnostic> diagnostics;

    ASSERT_TRUE(launcher::SaveProfile(profile_path, original, &diagnostics));
    launcher::Profile loaded;
    ASSERT_TRUE(launcher::LoadProfile(profile_path, &loaded, &diagnostics));

    EXPECT_EQ(loaded.game_dir, original.game_dir);
    EXPECT_EQ(loaded.mods, original.mods);
    EXPECT_EQ(loaded.speed, 5);
    EXPECT_FALSE(loaded.start_paused);
    EXPECT_FALSE(loaded.quit_after_run);
}

TEST_F(LauncherCoreTest, RejectsMalformedCampaignRunControlTypes)
{
    launcher::Profile profile;
    std::vector<launcher::Diagnostic> diagnostics;
    Write(root / L"bad-speed.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nspeed = \"2\"\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-speed.toml", &profile, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    diagnostics.clear();
    Write(root / L"bad-pause.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nstart_paused = \"true\"\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-pause.toml", &profile, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    diagnostics.clear();
    Write(root / L"bad-run.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nrun_days = \"365\"\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-run.toml", &profile, &diagnostics));
    EXPECT_EQ(diagnostics.back().code, "profile.schema");
    diagnostics.clear();
    Write(root / L"bad-quit-after-run.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nquit_after_run = \"true\"\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-quit-after-run.toml", &profile, &diagnostics));
    EXPECT_EQ(diagnostics.back().code, "profile.schema");
    diagnostics.clear();
    Write(root / L"quit-without-target.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nquit_after_run = true\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"quit-without-target.toml", &profile, &diagnostics));
    EXPECT_EQ(diagnostics.back().code, "campaign.quit_after_run");
    diagnostics.clear();
    Write(root / L"bad-timeout.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\nrun_timeout_seconds = 0\n");
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-timeout.toml", &profile, &diagnostics));
    EXPECT_EQ(diagnostics.back().code, "campaign.run_timeout");
}

TEST_F(LauncherCoreTest, SavesLoadsAndRejectsMalformedTelemetryProfileFields)
{
    launcher::Profile original;
    original.name = "Telemetry";
    original.game_dir = root;
    original.telemetry_enabled = true;
    original.telemetry_output = root / L"traces" / L"telemetry.jsonl";
    original.telemetry_categories = {"state"};
    original.telemetry_country_tags = {"ENG", "D01"};
    original.telemetry_start_date_raw = -7;
    original.telemetry_end_date_raw = 12;
    original.telemetry_sample_days = 7;
    original.telemetry_queue_capacity = 512;
    original.telemetry_overwrite = true;
    original.telemetry_gold_to_cash_rate = 0.5;
    original.telemetry_captures = {
        {"world.economy", "monthly", {"health", "holdings"}, {}, {}, -7, 12},
        {"country.daily", "daily", {"treasury_raw"}, {"ENG", "D01"}, {}, std::nullopt, std::nullopt},
    };
    std::vector<launcher::Diagnostic> diagnostics;

    ASSERT_TRUE(launcher::SaveProfile(root / L"telemetry.toml", original, &diagnostics));
    launcher::Profile loaded;
    ASSERT_TRUE(launcher::LoadProfile(root / L"telemetry.toml", &loaded, &diagnostics));
    EXPECT_TRUE(loaded.telemetry_enabled);
    EXPECT_EQ(loaded.telemetry_output, original.telemetry_output);
    EXPECT_EQ(loaded.telemetry_categories, original.telemetry_categories);
    EXPECT_EQ(loaded.telemetry_country_tags, original.telemetry_country_tags);
    EXPECT_EQ(loaded.telemetry_start_date_raw, original.telemetry_start_date_raw);
    EXPECT_EQ(loaded.telemetry_end_date_raw, original.telemetry_end_date_raw);
    EXPECT_EQ(loaded.telemetry_sample_days, 7);
    EXPECT_EQ(loaded.telemetry_queue_capacity, 512);
    EXPECT_TRUE(loaded.telemetry_overwrite);
    ASSERT_TRUE(loaded.telemetry_gold_to_cash_rate);
    EXPECT_DOUBLE_EQ(*loaded.telemetry_gold_to_cash_rate, 0.5);
    ASSERT_EQ(loaded.telemetry_captures.size(), 2u);
    EXPECT_EQ(loaded.telemetry_captures[0].family, "world.economy");
    EXPECT_EQ(loaded.telemetry_captures[0].cadence, "monthly");
    EXPECT_EQ(loaded.telemetry_captures[0].fields, (std::vector<std::string>{"health", "holdings"}));
    EXPECT_EQ(loaded.telemetry_captures[1].country_tags, (std::vector<std::string>{"ENG", "D01"}));

    Write(root / L"bad-telemetry.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\ntelemetry_enabled = \"true\"\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-telemetry.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    Write(root / L"bad-telemetry-tags.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\ntelemetry_country_tags = [\"eng\", \"ENG\"]\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-telemetry-tags.toml", &loaded, &diagnostics));
    EXPECT_EQ(diagnostics.back().code, "telemetry.country_tags");

    Write(root / L"bad-telemetry-overwrite.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\ntelemetry_overwrite = \"true\"\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-telemetry-overwrite.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    Write(root / L"bad-telemetry-categories.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\ntelemetry_categories = \"state\"\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-telemetry-categories.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    Write(root / L"bad-telemetry-sample.toml", "name = \"Bad\"\ngame_dir = \"C:/Game\"\ntelemetry_sample_days = \"7\"\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-telemetry-sample.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");
}

TEST_F(LauncherCoreTest, RejectsPathTraversalOutsideGameDirectory)
{
    const auto root_with_space = root / L"game dir";
    fs::create_directories(root_with_space / L"mod");
    fs::create_directories(root_with_space / L"outside");
    Write(root_with_space / L"mod" / L"Bad.mod", "name = \"Bad\"\npath = \"../outside\"\n");

    EXPECT_TRUE(launcher::IsPathContained(root_with_space, root_with_space / L"mod" / L"Bad.mod"));
    EXPECT_FALSE(launcher::IsPathContained(root_with_space / L"mod", root_with_space / L"outside"));
    const auto discovery = launcher::DiscoverMods(root_with_space);
    ASSERT_EQ(discovery.diagnostics.size(), 1u);
    EXPECT_EQ(discovery.diagnostics[0].code, "mod.path_traversal");
}

TEST_F(LauncherCoreTest, QuotesArgumentsAndBuildsModLaunchPlan)
{
    const auto game = root / L"game dir";
    fs::create_directories(game / L"mod" / L"Example Content");
    Write(game / L"mod" / L"Example.mod", "name = \"Example\"\npath = \"mod/Example Content\"\n");

    EXPECT_EQ(launcher::BuildWindowsCommandLine({L"C:\\Game Dir\\v2game.exe", L"-plugin=C:\\Game Dir\\plugins\\a plugin.toml"}),
              L"\"C:\\Game Dir\\v2game.exe\" \"-plugin=C:\\Game Dir\\plugins\\a plugin.toml\"");

    launcher::Profile profile;
    profile.game_dir = game;
    profile.inject = false;
    profile.mods = {L"mod/Example.mod"};
    const auto plan = launcher::BuildLaunchPlan(profile);

    ASSERT_EQ(plan.mods.size(), 1u);
    EXPECT_NE(plan.command_line.find(L"-mod=mod/Example.mod"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"\"" + (game / L"v2game.exe").wstring() + L"\""), std::wstring::npos);
}

TEST(LauncherArgumentTest, QuotesEmptyEmbeddedAndTrailingSlashArguments)
{
    EXPECT_EQ(launcher::QuoteWindowsArgument(L""), L"\"\"");
    EXPECT_EQ(launcher::QuoteWindowsArgument(L"a\\\"b"), L"\"a\\\\\\\"b\"");
    EXPECT_EQ(launcher::QuoteWindowsArgument(L"C:\\Game Dir\\"), L"\"C:\\Game Dir\\\\\"");
}

TEST_F(LauncherCoreTest, NormalizesDynamicObserverViewTag)
{
    launcher::Profile profile;
    profile.game_dir = root;
    profile.inject = true;
    profile.save = root / L"save games" / L"campaign.v2";
    profile.observer = true;
    profile.view_tag = L"d01";
    const auto plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::none_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "observer.view_tag";
    }));
    EXPECT_EQ(plan.profile.view_tag, std::optional<std::wstring>(L"D01"));
    EXPECT_NE(plan.command_line.find(L"-smedley-view-tag=D01"), std::wstring::npos);

    profile.view_tag = L"D-1";
    const auto invalid = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(invalid.diagnostics.begin(), invalid.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "observer.view_tag";
    }));
}

TEST_F(LauncherCoreTest, NoInjectionIgnoresStaleAutomationSettings)
{
    launcher::Profile profile;
    profile.game_dir = root;
    profile.inject = false;
    profile.plugins = {L"plugins/missing.toml"};
    profile.save = root / L"missing save.v2";
    profile.observer = true;
    profile.view_tag = L"not-a-tag";
    profile.speed = 3;
    profile.start_paused = true;
    profile.run_days = 365;
    profile.quit_after_run = true;

    const auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_TRUE(std::none_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code.rfind("observer.", 0) == 0 || diagnostic.code == "save.missing";
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.observer_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.speed_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.start_paused_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.quit_after_run_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
}

TEST_F(LauncherCoreTest, AddsCampaignRunControlArgumentsAndRejectsPausedObserver)
{
    const auto plugin_binary = BuiltCampaignPlugin();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"campaign_runner.dll");
    Write(root / L"plugins" / L"campaign_runner.toml",
          "id = \"campaign_runner\"\nname = \"Campaign Runner\"\nversion = \"1\"\nmodule = \"campaign_runner.dll\"\n");

    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/campaign_runner.toml"};
    profile.save = root / L"missing.v2";
    profile.speed = 3;
    profile.start_paused = true;
    auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_NE(plan.command_line.find(L"-smedley-speed=3"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-start-paused"), std::wstring::npos);
    EXPECT_FALSE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.plugin" || diagnostic.code == "campaign.save" || diagnostic.code == "campaign.speed";
    }));

    profile.observer = true;
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "observer.start_paused";
    }));
}

TEST_F(LauncherCoreTest, AcceptsPluginWithOnlyTheCAbiV1Export)
{
    const auto plugin_binary = BuiltAbiFixture();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"abi_fixture.dll");
    Write(root / L"plugins" / L"abi_fixture.toml",
          "id = \"abi_fixture\"\nname = \"ABI Fixture\"\nversion = \"1\"\nmodule = \"abi_fixture.dll\"\n");

    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/abi_fixture.toml"};
    const auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_TRUE(std::none_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "plugin.export";
    }));
    ASSERT_EQ(plan.plugins.size(), 1u);
    EXPECT_EQ(plan.plugins.front().id, "abi_fixture");
}

TEST_F(LauncherCoreTest, WiresBenchmarkTargetsAndRejectsUnsafeCombinations)
{
    const auto plugin_binary = BuiltCampaignPlugin();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"campaign_runner.dll");
    Write(root / L"plugins" / L"campaign_runner.toml",
          "id = \"campaign_runner\"\nname = \"Campaign Runner\"\nversion = \"1\"\nmodule = \"campaign_runner.dll\"\n");
    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/campaign_runner.toml"};
    profile.save = root / L"missing.v2";
    profile.detach = true;
    profile.run_days = 365;
    profile.quit_after_run = true;
    profile.run_timeout_seconds = 900;
    auto plan = launcher::BuildLaunchPlan(profile);
    EXPECT_NE(plan.command_line.find(L"-smedley-run-days=365"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-run-timeout-seconds=900"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-quit-after-run"), std::wstring::npos);
    EXPECT_FALSE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code.rfind("campaign.run", 0) == 0;
    }));

    profile.detach = false;
    profile.view_tag = L"ENG";
    profile.observer = true;
    profile.run_until_date_raw = 240;
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.run_target" || diagnostic.code == "campaign.run_detach" || diagnostic.code == "campaign.run_view_tag";
    }));
    EXPECT_EQ(plan.command_line.find(L"-smedley-quit-after-run"), std::wstring::npos);
    profile.inject = false;
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.run_target_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
}

TEST_F(LauncherCoreTest, ValidatesTelemetryPluginAndBuildsPerRunTraceCommand)
{
    const auto plugin_binary = BuiltTelemetryPlugin();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"telemetry.dll");
    Write(root / L"plugins" / L"telemetry.toml",
          "id = \"telemetry\"\nname = \"Telemetry\"\nversion = \"1\"\nmodule = \"telemetry.dll\"\n");

    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/telemetry.toml"};
    profile.telemetry_enabled = true;
    profile.telemetry_categories = {"lifecycle", "state"};
    profile.telemetry_sample_days = 2;
    profile.telemetry_queue_capacity = 256;
    profile.telemetry_country_tags = {"ENG"};
    profile.telemetry_start_date_raw = 3;
    profile.telemetry_end_date_raw = 9;
    profile.telemetry_gold_to_cash_rate = 0.5;
    profile.telemetry_captures = {
        {"world.daily", "yearly", {"country_slot_count"}, {}, {}, std::nullopt, std::nullopt},
        {"country.daily", "daily", {"treasury_raw"}, {"ENG"}, {}, 3, 9},
        {"country.metrics", "monthly", {"power", "politics"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"state.factory", "daily", {"identity", "employment", "production", "finance", "inputs", "flows", "sales"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"world.market", "daily", {"price", "supply", "demand", "sales"}, {}, {}, std::nullopt, std::nullopt},
        {"province.rgo", "daily", {"identity", "employment", "production", "finance", "modifiers", "sales"}, {"PRU"}, {549, 687}, std::nullopt, std::nullopt},
        {"pop.artisan", "daily", {"identity", "production", "inputs", "finance", "flows", "sales"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.economy", "daily", {"money_raw"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.demographics", "daily", {"size_candidate"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.identity", "daily", {"pop_type_tag_candidate", "culture_tag_candidate", "religion_tag_candidate"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.needs", "daily", {"life_satisfaction_candidate_raw", "everyday_satisfaction_candidate_raw", "luxury_satisfaction_candidate_raw"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.lifecycle", "daily", {"summary", "appeared", "disappeared", "scope_changed"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"pop.aggregate", "daily", {"size_candidate"}, {"PRU"}, {}, std::nullopt, std::nullopt},
        {"country.economy", "yearly", {"totals", "components", "per_capita"}, {"ENG", "PRU"}, {}, std::nullopt, std::nullopt},
    };
    const auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_FALSE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.plugin" || diagnostic.code == "telemetry.categories";
    }));
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-categories=lifecycle,state"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-sample-days=2"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-queue-capacity=256"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-country-tags=ENG"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-start-date-raw=3"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-gold-to-cash-rate=0.500000"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=world.daily|yearly|country_slot_count||||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=country.daily|daily|treasury_raw|ENG||3|9"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=country.metrics|monthly|power,politics|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=state.factory|daily|identity,employment,production,finance,inputs,flows,sales|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=world.market|daily|price,supply,demand,sales||||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=province.rgo|daily|identity,employment,production,finance,modifiers,sales|PRU|549,687||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.artisan|daily|identity,production,inputs,finance,flows,sales|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.economy|daily|money_raw|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.demographics|daily|size_candidate|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.identity|daily|pop_type_tag_candidate,culture_tag_candidate,religion_tag_candidate|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.needs|daily|life_satisfaction_candidate_raw,everyday_satisfaction_candidate_raw,luxury_satisfaction_candidate_raw|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.lifecycle|daily|summary,appeared,disappeared,scope_changed|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=pop.aggregate|daily|size_candidate|PRU|||"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-telemetry-capture=country.economy|yearly|totals,components,per_capita|ENG,PRU|||"), std::wstring::npos);

    auto missing_gold_rate = profile;
    missing_gold_rate.telemetry_gold_to_cash_rate.reset();
    const auto missing_gold_plan = launcher::BuildLaunchPlan(missing_gold_rate);
    EXPECT_TRUE(std::any_of(missing_gold_plan.diagnostics.begin(), missing_gold_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.gold_to_cash_rate" && diagnostic.severity == launcher::Severity::Error;
    }));

    auto lifecycle_only = profile;
    lifecycle_only.telemetry_categories = {"lifecycle"};
    const auto lifecycle_only_plan = launcher::BuildLaunchPlan(lifecycle_only);
    EXPECT_TRUE(std::any_of(lifecycle_only_plan.diagnostics.begin(), lifecycle_only_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.capture" && diagnostic.severity == launcher::Severity::Error;
    }));

    auto nondaily_sales = profile;
    nondaily_sales.telemetry_captures[3].cadence = "monthly";
    const auto nondaily_sales_plan = launcher::BuildLaunchPlan(nondaily_sales);
    EXPECT_TRUE(std::any_of(nondaily_sales_plan.diagnostics.begin(), nondaily_sales_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.capture_cadence" && diagnostic.severity == launcher::Severity::Error;
    }));

    auto nondaily_lifecycle = profile;
    const auto lifecycle_rule = std::find_if(nondaily_lifecycle.telemetry_captures.begin(),
        nondaily_lifecycle.telemetry_captures.end(), [](const auto &rule) { return rule.family == "pop.lifecycle"; });
    ASSERT_NE(lifecycle_rule, nondaily_lifecycle.telemetry_captures.end());
    lifecycle_rule->cadence = "monthly";
    const auto nondaily_lifecycle_plan = launcher::BuildLaunchPlan(nondaily_lifecycle);
    EXPECT_TRUE(std::any_of(nondaily_lifecycle_plan.diagnostics.begin(), nondaily_lifecycle_plan.diagnostics.end(),
        [](const auto &diagnostic) {
            return diagnostic.code == "telemetry.capture_cadence"
                && diagnostic.severity == launcher::Severity::Error;
        }));

    auto cashflow = profile;
    cashflow.telemetry_captures.push_back(
        {"pop.cashflow", "daily", {"summary", "account", "components"}, {"PRU"}, {}, std::nullopt, std::nullopt});
    cashflow.telemetry_captures.push_back(
        {"pop.cashflow.aggregate", "daily", {"summary", "account", "components"}, {}, {}, std::nullopt, std::nullopt});
    const auto cashflow_plan = launcher::BuildLaunchPlan(cashflow);
    EXPECT_FALSE(std::any_of(cashflow_plan.diagnostics.begin(), cashflow_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code.rfind("telemetry.capture", 0) == 0 && diagnostic.severity == launcher::Severity::Error;
    }));

    cashflow.telemetry_captures.back().cadence = "monthly";
    const auto nondaily_cashflow_plan = launcher::BuildLaunchPlan(cashflow);
    EXPECT_TRUE(std::any_of(nondaily_cashflow_plan.diagnostics.begin(), nondaily_cashflow_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.capture_cadence" && diagnostic.severity == launcher::Severity::Error;
    }));

    profile.telemetry_output = root / L"trace.txt";
    const auto bad_extension_plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(bad_extension_plan.diagnostics.begin(), bad_extension_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.output";
    }));
    profile.telemetry_output = root / L"plugins" / L"telemetry.toml";
    profile.telemetry_overwrite = true;
    const auto collision_plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(collision_plan.diagnostics.begin(), collision_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.output_collision";
    }));

    const auto aliased_output = root / L"trace.jsonl";
    ASSERT_TRUE(CreateHardLinkW(aliased_output.c_str(), (root / L"plugins" / L"telemetry.toml").c_str(), nullptr));
    profile.telemetry_output = aliased_output;
    const auto alias_collision_plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(alias_collision_plan.diagnostics.begin(), alias_collision_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.output_collision";
    }));
    profile.telemetry_output.reset();

    auto record = launcher::CreateRunRecord(plan);
    record.run_id = "run-id";
    record.links.telemetry_trace = root / L"traces" / L"run-id.jsonl";
    const auto command = launcher::BuildInjectedCommandLine(plan, record);
    EXPECT_NE(command.find(L"-smedley-run-id=run-id"), std::wstring::npos);
    EXPECT_NE(command.find(L"-smedley-telemetry-output="), std::wstring::npos);

    profile.plugins.clear();
    const auto invalid_plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(invalid_plan.diagnostics.begin(), invalid_plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "telemetry.plugin";
    }));
}

TEST_F(LauncherCoreTest, RejectsRunControlsWithoutSaveAndCampaignRunner)
{
    launcher::Profile profile;
    profile.game_dir = root;
    profile.speed = 2;

    const auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.plugin";
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.save";
    }));
}

TEST_F(LauncherCoreTest, ValidatesAndBuildsConstrainedScriptingArguments)
{
    const auto plugin_binary = BuiltScriptingPlugin();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"scripting.dll");
    Write(root / L"plugins" / L"scripting.toml",
          "id = \"scripting\"\nname = \"Lua scripting\"\nversion = \"1\"\nmodule = \"scripting.dll\"\n");
    fs::create_directories(root / L"scripts");
    Write(root / L"scripts" / L"observer.lua", "function on_daily(event) end\n");

    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/scripting.toml"};
    profile.scripts = {L"scripts/observer.lua"};
    profile.script_instruction_budget = 200000;
    profile.script_memory_bytes = 4194304;
    profile.script_queue_capacity = 128;
    auto plan = launcher::BuildLaunchPlan(profile);

    EXPECT_FALSE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code.rfind("scripting.", 0) == 0 && diagnostic.severity == launcher::Severity::Error;
    }));
    EXPECT_EQ(plan.profile.scripts, (std::vector<fs::path>{root / L"scripts" / L"observer.lua"}));
    EXPECT_NE(plan.command_line.find(L"-smedley-script="), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-script-instruction-budget=200000"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-script-memory-bytes=4194304"), std::wstring::npos);
    EXPECT_NE(plan.command_line.find(L"-smedley-script-queue-capacity=128"), std::wstring::npos);

    profile.plugins.clear();
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "scripting.plugin";
    }));

    profile.inject = false;
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "safe_mode.scripts_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
    EXPECT_EQ(plan.command_line.find(L"-smedley-script="), std::wstring::npos);
}

TEST_F(LauncherCoreTest, RejectsUnsafeDuplicateAndMalformedScripts)
{
    fs::create_directories(root / L"scripts");
    Write(root / L"scripts" / L"duplicate.lua", "return true\n");
    Write(root / L"outside.lua", "return true\n");
    launcher::Profile profile;
    profile.game_dir = root;
    profile.scripts = {L"scripts/duplicate.lua", L"scripts/duplicate.lua", L"outside.lua"};
    const auto plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "scripting.duplicate";
    }));
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "scripting.path_traversal";
    }));

    Write(root / L"bad-script-profile.toml",
          "name = \"Bad\"\ngame_dir = \"C:/Game\"\nscript_memory_bytes = \"large\"\n");
    launcher::Profile loaded;
    std::vector<launcher::Diagnostic> diagnostics;
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-script-profile.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");

    Write(root / L"bad-script-list.toml",
          "name = \"Bad\"\ngame_dir = \"C:/Game\"\nscripts = \"scripts/one.lua\"\n");
    diagnostics.clear();
    EXPECT_FALSE(launcher::LoadProfile(root / L"bad-script-list.toml", &loaded, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "profile.schema");
}

TEST_F(LauncherCoreTest, IgnoresCustomBenchmarkTimeoutWithoutTarget)
{
    launcher::Profile profile;
    profile.game_dir = root;
    profile.run_timeout_seconds = 900;
    const auto plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.run_timeout_ignored" && diagnostic.severity == launcher::Severity::Warning;
    }));
    EXPECT_FALSE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "campaign.plugin" || diagnostic.code == "campaign.save";
    }));
    EXPECT_EQ(plan.command_line.find(L"-smedley-run-timeout-seconds="), std::wstring::npos);
}

TEST_F(LauncherCoreTest, OrdersPluginDependenciesAndRejectsCycles)
{
    const auto plugin_binary = BuiltCampaignPlugin();
    ASSERT_TRUE(fs::is_regular_file(plugin_binary));
    fs::copy_file(plugin_binary, root / L"plugins" / L"base.dll");
    fs::copy_file(plugin_binary, root / L"plugins" / L"dependent.dll");
    Write(root / L"plugins" / L"base.toml",
          "id = \"base\"\nname = \"Base\"\nversion = \"1\"\nmodule = \"base.dll\"\n");
    Write(root / L"plugins" / L"dependent.toml",
          "id = \"dependent\"\nname = \"Dependent\"\nversion = \"1\"\nmodule = \"dependent.dll\"\ndependencies = [\"base\"]\n");

    launcher::Profile profile;
    profile.game_dir = root;
    profile.plugins = {L"plugins/dependent.toml", L"plugins/base.toml"};
    auto plan = launcher::BuildLaunchPlan(profile);

    ASSERT_EQ(plan.plugins.size(), 2u);
    EXPECT_EQ(plan.plugins[0].id, "base");
    EXPECT_EQ(plan.plugins[1].id, "dependent");

    Write(root / L"plugins" / L"base.toml",
          "id = \"base\"\nname = \"Base\"\nversion = \"1\"\nmodule = \"base.dll\"\ndependencies = [\"dependent\"]\n");
    plan = launcher::BuildLaunchPlan(profile);
    EXPECT_TRUE(std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "plugin.dependency_cycle";
    }));
}

TEST_F(LauncherCoreTest, SavesAndLoadsCompleteRunRecordInConfiguredDirectory)
{
    launcher::RunRecord original;
    original.run_id = "a1b2c3d4-e5f6-7890-abcd-ef0123456789";
    original.started_at_utc = "2026-07-31T12:34:56.789Z";
    original.status = launcher::RunStatus::Exited;
    original.process_id = 42;
    original.exit_code = 7;
    original.profile_name = u8"Observer \"\u03a9\"\nprofile";
    original.injected = true;
    original.safe_mode = false;
    original.executable = root / L"Victoria \u03a9" / L"v2game.exe";
    original.command_line = L"\"C:\\Victoria \u03a9\\v2game.exe\" \"-mod=quoted \\\"mod\\\"\"";
    original.mod_descriptors = {root / L"mod" / L"\u03a9.mod"};
    original.plugins = {{"telemetry", root / L"plugins" / L"telemetry.toml"}};
    original.scripts = {root / L"scripts" / L"observer.lua"};
    original.save = root / L"save games" / L"source \u03a9.v2";
    original.observer = true;
    original.speed = 4;
    original.start_paused = true;
    original.links.smedley_log = root / L"logs" / L"smedley.log";
    original.links.source_save = original.save;
    original.diagnostics = {
        {launcher::Severity::Warning, "mod.descriptor_missing", "selected descriptor was rejected", root / L"mod" / L"missing.mod"},
        {launcher::Severity::Error, "launch.create_process", "CreateProcessW failed", {}},
    };
    std::vector<launcher::Diagnostic> diagnostics;
    const auto run_directory = root / L"configured runs";

    ASSERT_TRUE(launcher::SaveRunRecord(run_directory, original, &diagnostics));
    const auto records = launcher::LoadRunHistory(run_directory, 10, &diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    ASSERT_EQ(records.size(), 1u);
    const auto &loaded = records.front();
    EXPECT_EQ(loaded.run_id, original.run_id);
    EXPECT_EQ(loaded.status, launcher::RunStatus::Exited);
    EXPECT_EQ(loaded.process_id, original.process_id);
    EXPECT_EQ(loaded.exit_code, original.exit_code);
    EXPECT_EQ(loaded.profile_name, original.profile_name);
    EXPECT_EQ(loaded.executable, original.executable);
    EXPECT_EQ(loaded.command_line, original.command_line);
    EXPECT_EQ(loaded.mod_descriptors, original.mod_descriptors);
    EXPECT_EQ(loaded.scripts, original.scripts);
    ASSERT_EQ(loaded.plugins.size(), 1u);
    EXPECT_EQ(loaded.plugins[0].id, "telemetry");
    EXPECT_EQ(loaded.plugins[0].manifest_path, original.plugins[0].manifest_path);
    EXPECT_EQ(loaded.links.source_save, original.links.source_save);
    EXPECT_EQ(loaded.metadata_path, run_directory / fs::u8path(original.run_id + ".toml"));
    ASSERT_EQ(loaded.diagnostics.size(), original.diagnostics.size());
    EXPECT_EQ(loaded.diagnostics[0].severity, launcher::Severity::Warning);
    EXPECT_EQ(loaded.diagnostics[0].code, "mod.descriptor_missing");
    EXPECT_EQ(loaded.diagnostics[0].message, "selected descriptor was rejected");
    EXPECT_EQ(loaded.diagnostics[0].path, original.diagnostics[0].path);
}

TEST_F(LauncherCoreTest, RunHistorySkipsMalformedRecordAndSortsNewestFirst)
{
    const auto run_directory = root / L"runs";
    launcher::RunRecord older;
    older.run_id = "11111111-1111-1111-1111-111111111111";
    older.started_at_utc = "2026-07-30T00:00:00.000Z";
    older.status = launcher::RunStatus::Started;
    older.profile_name = "Older";
    older.executable = root / L"v2game.exe";
    launcher::RunRecord newer = older;
    newer.run_id = "22222222-2222-2222-2222-222222222222";
    newer.started_at_utc = "2026-07-31T00:00:00.000Z";
    newer.status = launcher::RunStatus::CreateFailed;
    std::vector<launcher::Diagnostic> diagnostics;
    ASSERT_TRUE(launcher::SaveRunRecord(run_directory, older, &diagnostics));
    ASSERT_TRUE(launcher::SaveRunRecord(run_directory, newer, &diagnostics));
    Write(run_directory / L"broken.toml", "schema_version = 1\nrun_id = \"broken\"\n");

    const auto records = launcher::LoadRunHistory(run_directory, 10, &diagnostics);

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].run_id, newer.run_id);
    EXPECT_EQ(records[1].run_id, older.run_id);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "run.schema" || diagnostic.code == "run.parse";
    }));
}

TEST(LauncherRunRecordTest, SerializesEveryLaunchStatusWithoutStartingGame)
{
    const auto root = fs::temp_directory_path() / (L"smedley run status " + std::to_wstring(GetTickCount64()));
    const auto run_directory = root / L"runs";
    std::vector<launcher::Diagnostic> diagnostics;
    const std::array<launcher::RunStatus, 5> statuses = {
        launcher::RunStatus::PreflightFailed, launcher::RunStatus::CreateFailed, launcher::RunStatus::InjectionFailed,
        launcher::RunStatus::Started, launcher::RunStatus::Exited};
    for (size_t index = 0; index < statuses.size(); ++index) {
        launcher::RunRecord record;
        record.run_id = "status-" + std::to_string(index);
        record.started_at_utc = "2026-07-31T00:00:0" + std::to_string(index) + ".000Z";
        record.status = statuses[index];
        record.executable = L"C:\\Victoria 2\\v2game.exe";
        ASSERT_TRUE(launcher::SaveRunRecord(run_directory, record, &diagnostics));
    }

    const auto records = launcher::LoadRunHistory(run_directory, 10, &diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    ASSERT_EQ(records.size(), statuses.size());
    for (const auto status : statuses) {
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [status](const auto &record) { return record.status == status; }));
    }
    std::error_code error;
    fs::remove_all(root, error);
}

TEST_F(LauncherCoreTest, EscapesDelInProfileViewTag)
{
    launcher::Profile original;
    original.name = "DEL";
    original.game_dir = root;
    original.view_tag = std::wstring{L'E', static_cast<wchar_t>(0x7f), L'G'};
    std::vector<launcher::Diagnostic> diagnostics;
    const auto profile_path = root / L"del-profile.toml";

    ASSERT_TRUE(launcher::SaveProfile(profile_path, original, &diagnostics));
    launcher::Profile loaded;
    ASSERT_TRUE(launcher::LoadProfile(profile_path, &loaded, &diagnostics));

    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(loaded.view_tag, original.view_tag);
    std::ifstream input(profile_path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(contents.find("\\u007f"), std::string::npos);
}

TEST_F(LauncherCoreTest, RejectsHostileNumericAndMismatchedRunRecords)
{
    const auto run_directory = root / L"runs";
    launcher::RunRecord valid;
    valid.run_id = "valid-record";
    valid.started_at_utc = "2026-07-31T12:34:56.000Z";
    valid.status = launcher::RunStatus::Started;
    valid.executable = root / L"v2game.exe";
    std::vector<launcher::Diagnostic> diagnostics;
    ASSERT_TRUE(launcher::SaveRunRecord(run_directory, valid, &diagnostics));
    fs::copy_file(run_directory / L"valid-record.toml", run_directory / L"wrong-name.toml");
    Write(run_directory / L"hostile.toml", "schema_version = 1\nrun_id = \"../hostile\"\n");
    Write(run_directory / L"numeric.toml",
          "schema_version = 1\nrun_id = \"numeric\"\nstarted_at_utc = \"2026-02-28T12:00:00.000Z\"\n"
          "status = \"started\"\nprofile_name = \"x\"\ninjected = false\nsafe_mode = true\n"
          "executable = \"C:/v2game.exe\"\ncommand_line = \"x\"\nobserver = false\nspeed = 5\nstart_paused = false\n"
          "process_id = 4294967296\n");

    const auto records = launcher::LoadRunHistory(run_directory, 10, &diagnostics);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].run_id, valid.run_id);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "run.schema" || diagnostic.code == "run.parse";
    }));
}

TEST_F(LauncherCoreTest, RejectsSemanticallyInvalidTimestampAndNonDirectoryHistoryPath)
{
    const auto run_directory = root / L"runs";
    launcher::RunRecord record;
    record.run_id = "invalid-time";
    record.started_at_utc = "2026-02-30T12:00:00.000Z";
    record.executable = root / L"v2game.exe";
    std::vector<launcher::Diagnostic> diagnostics;

    EXPECT_FALSE(launcher::SaveRunRecord(run_directory, record, &diagnostics));
    Write(root / L"not-a-directory.toml", "not history");
    const auto records = launcher::LoadRunHistory(root / L"not-a-directory.toml", 10, &diagnostics);

    EXPECT_TRUE(records.empty());
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.code == "run.write" || diagnostic.code == "run.directory";
    }));
}

TEST_F(LauncherCoreTest, DerivesModUserDirectoryOnlyWhenUnambiguous)
{
    launcher::LaunchPlan plan;
    plan.profile.game_dir = root;
    auto record = launcher::CreateRunRecord(plan);
    ASSERT_TRUE(record.links.victoria_user_dir.has_value());
    ASSERT_TRUE(record.links.victoria_system_log.has_value());

    plan.mods = {{"Example", "mod/Example", "Example User", {}, root / L"mod" / L"Example.mod", root / L"mod" / L"Example Content"}};

    record = launcher::CreateRunRecord(plan);

    ASSERT_TRUE(record.links.smedley_log.has_value());
    ASSERT_TRUE(record.links.victoria_user_dir.has_value());
    ASSERT_TRUE(record.links.victoria_system_log.has_value());
    plan.mods.push_back({"Other", "mod/Other", "Other User", {}, root / L"mod" / L"Other.mod", root / L"mod" / L"Other Content"});
    record = launcher::CreateRunRecord(plan);

    EXPECT_TRUE(record.links.smedley_log.has_value());
    EXPECT_FALSE(record.links.victoria_user_dir.has_value());
    EXPECT_FALSE(record.links.victoria_system_log.has_value());
    plan.mods = {{"Unsafe", "mod/Unsafe", "\\Windows", {}, root / L"mod" / L"Unsafe.mod", root / L"mod" / L"Unsafe"}};
    record = launcher::CreateRunRecord(plan);
    EXPECT_FALSE(record.links.victoria_user_dir.has_value());
    EXPECT_FALSE(record.links.victoria_system_log.has_value());
}

TEST_F(LauncherCoreTest, ResolvesOneSafeModUserDirectory)
{
    const launcher::ModDescriptor gfm{"GFM", "mod/GFM", "GFM", {}, {}, {}};
    const launcher::ModDescriptor same{"Submod", "mod/Submod", "gfm", {}, {}, {}};
    const launcher::ModDescriptor other{"Other", "mod/Other", "Other", {}, {}, {}};
    const launcher::ModDescriptor unsafe{"Unsafe", "mod/Unsafe", "../escape", {}, {}, {}};
    const launcher::ModDescriptor unicode_upper{"Upper", "mod/Upper", "\xC3\x84", {}, {}, {}};
    const launcher::ModDescriptor unicode_lower{"Lower", "mod/Lower", "\xC3\xA4", {}, {}, {}};

    EXPECT_EQ(launcher::ResolveVictoriaUserDirectory(root, {}), root.lexically_normal());
    EXPECT_EQ(launcher::ResolveVictoriaUserDirectory(root, {gfm}), root / L"GFM");
    EXPECT_EQ(launcher::ResolveVictoriaUserDirectory(root, {gfm, same}), root / L"GFM");
    EXPECT_EQ(launcher::ResolveVictoriaUserDirectory(root, {unicode_upper, unicode_lower}),
              root / fs::u8path("\xC3\x84"));
    EXPECT_FALSE(launcher::ResolveVictoriaUserDirectory(root, {gfm, other}).has_value());
    EXPECT_FALSE(launcher::ResolveVictoriaUserDirectory(root, {unsafe}).has_value());
}

TEST_F(LauncherCoreTest, PersistsTelemetryTraceLinksWithoutLaunching)
{
    launcher::LaunchPlan plan;
    plan.profile.game_dir = root;
    plan.profile.telemetry_enabled = true;
    plan.profile.telemetry_output = root / L"custom trace.jsonl";
    auto record = launcher::CreateRunRecord(plan);
    EXPECT_EQ(record.links.telemetry_trace, plan.profile.telemetry_output);

    plan.profile.telemetry_output.reset();
    record = launcher::CreateRunRecord(plan);
    ASSERT_TRUE(record.links.telemetry_trace.has_value());
    EXPECT_EQ(record.links.telemetry_trace->extension(), L".jsonl");
}

TEST_F(LauncherCoreTest, RejectsUnknownRunStatus)
{
    launcher::RunRecord record;
    record.run_id = "unknown-status";
    record.started_at_utc = "2026-07-31T12:34:56.000Z";
    record.status = static_cast<launcher::RunStatus>(999);
    record.executable = root / L"v2game.exe";
    std::vector<launcher::Diagnostic> diagnostics;

    EXPECT_FALSE(launcher::SaveRunRecord(root / L"runs", record, &diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.back().code, "run.write");
}
