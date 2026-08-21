#pragma once

#include "../event.hpp"

#include <cstddef>
#include <cstdint>

namespace smedley::memory {
    struct RawHook;
}

namespace smedley::events
{

    /**
     * Raised on the game thread after the native console manager has added its
     * base-game commands. The manager is opaque and non-owning. Ordinary event
     * consumers must not dereference or retain it; the kernel-owned game-state
     * implementation performs checked access and binds any retention to the
     * captured campaign session.
     */
    class ConsoleCmdManagerInitEvent : public Event
    {
        void *_cmd_mgr;
        static constexpr uintptr_t hook_addr = 0x00023a43;
        inline static uintptr_t hook_ret_addr = 0;
        static void HookTrampoline();
    public:
        explicit ConsoleCmdManagerInitEvent(void *cmd_mgr);
        /// @brief Returns the opaque native manager for this synchronous dispatch.
        void *cmd_mgr() const noexcept { return _cmd_mgr; }

        /// @brief Installs the hook that raises the event.
        static void InstallHook(const uint8_t *expected, size_t size, memory::RawHook *installed);
    };

}
