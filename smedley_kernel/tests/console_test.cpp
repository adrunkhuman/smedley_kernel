#include "memory.hpp"
#include "v2/console.hpp"

#include <gtest/gtest.h>
#include <windows.h>

namespace
{
    smedley::v2::CConsoleCmd::SResult HandleTestCommand(
        const smedley::sstd::vector<smedley::sstd::string> &arguments)
    {
        const bool success = arguments.size() == 1;
        return smedley::v2::CConsoleCmd::SResult(success ? "ok" : "bad arguments", success);
    }
}

TEST(ConsoleCommandManagerTest, FindsAndExecutesNativeCommandMetadata)
{
    smedley::memory::Map::game_heap = GetProcessHeap();
    smedley::v2::CConsoleCmdManager manager;
    smedley::v2::CConsoleCmd::SCommandData command{};
    command.name = "tag";
    command.num_aliases = 1;
    command.aliases[0] = "switch";
    command.handler = &HandleTestCommand;
    manager.commands().push_back(&command);

    ASSERT_EQ(manager.FindCommand("tag"), &command);
    ASSERT_EQ(manager.FindCommand("switch"), &command);
    ASSERT_EQ(manager.FindCommand("missing"), nullptr);

    smedley::sstd::vector<smedley::sstd::string> arguments;
    arguments.push_back(smedley::sstd::string("ENG"));
    const auto result = manager.ExecuteCommand("tag", arguments);
    ASSERT_TRUE(result.success);
    ASSERT_STREQ(result.message.c_str(), "ok");

    const auto missing = manager.ExecuteCommand("missing", arguments);
    ASSERT_FALSE(missing.success);
    ASSERT_STREQ(missing.message.c_str(), "not found");

    command.handler = nullptr;
    const auto null_handler = manager.ExecuteCommand("tag", arguments);
    ASSERT_FALSE(null_handler.success);
    ASSERT_STREQ(null_handler.message.c_str(), "null handler");
}
