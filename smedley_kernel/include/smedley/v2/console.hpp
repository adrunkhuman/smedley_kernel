#pragma once

#include "../std/string.hpp"
#include "../std/vector.hpp"
#include <algorithm>
#include <cstring>

namespace smedley::v2
{

    class CConsoleCmd
    {
    public:
        struct SResult;

        /**
         * Data container for console command metadata and its
         * handler callback.
         */
        struct SCommandData
        {
            using Handler = SResult (*)(const sstd::vector<sstd::string> &argv);

            bool is_allowed; /// command is only accessible in the dev environment when false
            const char *name;
            int num_aliases;
            const char *aliases[3];
            const char *description;
            Handler handler;
            int num_args;
            const char *args[10];
        };

        static_assert(sizeof(SCommandData) == 0x4c);

        /**
         * The result of an executed console command.
         */
        struct SResult
        {
            bool success;
            sstd::string message;

            SResult(const sstd::string &message, bool success = true) : message(message), success(success)
            {
            }
        };
        
        static_assert(sizeof(SResult) == 0x20);
    };

    class CConsoleCmdManager
    {
    private:
        sstd::vector<CConsoleCmd::SCommandData *> _commands;
    public:
        sstd::vector<CConsoleCmd::SCommandData *> &commands() { return _commands; }
        const sstd::vector<CConsoleCmd::SCommandData *> &commands() const { return _commands; }

        CConsoleCmd::SCommandData *FindCommand(const char *name)
        {
            if (name == nullptr) {
                return nullptr;
            }
            for (size_t index = 0; index < _commands.size(); ++index) {
                auto *command = _commands[index];
                if (command == nullptr) {
                    continue;
                }
                if (command->name != nullptr && std::strcmp(command->name, name) == 0) {
                    return command;
                }
                const auto alias_count = (std::min)(command->num_aliases, 3);
                for (int alias = 0; alias < alias_count; ++alias) {
                    if (command->aliases[alias] != nullptr
                        && std::strcmp(command->aliases[alias], name) == 0) {
                        return command;
                    }
                }
            }
            return nullptr;
        }

        CConsoleCmd::SResult ExecuteCommand(
            const char *name,
            const sstd::vector<sstd::string> &arguments)
        {
            auto *command = FindCommand(name);
            if (command == nullptr) {
                return CConsoleCmd::SResult("not found", false);
            }
            if (command->handler == nullptr) {
                return CConsoleCmd::SResult("null handler", false);
            }
            return command->handler(arguments);
        }
    };

    static_assert(sizeof(CConsoleCmdManager) == 0x10);

    class CConsole
    {
    };

}
