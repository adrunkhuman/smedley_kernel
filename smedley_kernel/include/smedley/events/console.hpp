#pragma once

#include "../event.hpp"
#include "../v2/console.hpp"

namespace smedley::events
{

    /**
     * Raised when the game initializes the console command manager after the
     * player starts a new campaign. At this point, all base-game commands have
     * been added.
     */
    class ConsoleCmdManagerInitEvent : public Event
    {
        v2::CConsoleCmdManager *_cmd_mgr;
        static constexpr uintptr_t hook_addr = 0x00023a43;
        inline static uintptr_t hook_ret_addr = NULL;
        static void HookTrampoline();
    public:
        ConsoleCmdManagerInitEvent(v2::CConsoleCmdManager *cmd_mgr);
        /// @brief Returns the console command manager being initialized.
        v2::CConsoleCmdManager *cmd_mgr() { return _cmd_mgr; }

        /// @brief Installs the hook that raises the event.
        static void InstallHook();
    };

}
