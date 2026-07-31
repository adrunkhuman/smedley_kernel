#include <smedley/launcher/launcher.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
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
            return configuration_dir.parent_path().parent_path().parent_path()
                / L"plugins" / L"campaign_runner" / configuration_dir.filename() / L"campaign_runner.dll";
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
    EXPECT_FALSE(loaded.inject);
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
