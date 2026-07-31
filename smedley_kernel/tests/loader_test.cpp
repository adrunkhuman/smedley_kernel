#include "loader.hpp"

#include <gtest/gtest.h>

TEST(PluginLoaderTest, ParsesQuotedPluginPaths)
{
    const auto plugins = smedley::PluginLoader::ParsePluginArguments(
        L"v2game.exe \"-plugin=C:\\Game Dir\\plugins\\first plugin.toml\" -plugin=plugins\\second.toml");

    ASSERT_EQ(plugins.size(), 2u);
    EXPECT_EQ(plugins[0], std::filesystem::path(L"C:\\Game Dir\\plugins\\first plugin.toml"));
    EXPECT_EQ(plugins[1], std::filesystem::path(L"plugins\\second.toml"));
}

TEST(PluginLoaderTest, PreservesUnicodePluginPaths)
{
    const auto plugins = smedley::PluginLoader::ParsePluginArguments(
        L"v2game.exe \"-plugin=C:\\Games\\Smedley \u03A9\\plugin.toml\"");

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0], std::filesystem::path(L"C:\\Games\\Smedley \u03A9\\plugin.toml"));
}
