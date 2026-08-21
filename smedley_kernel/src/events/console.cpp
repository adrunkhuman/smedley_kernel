#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/console.hpp"

#include <stdexcept>

using namespace smedley;

void ConsoleCmdManagerInitHook(void *mgr)
{
    smedley::EventRegistry<events::ConsoleCmdManagerInitEvent>::Notify(events::ConsoleCmdManagerInitEvent(mgr));
}

namespace smedley::events
{

    void __declspec(naked) ConsoleCmdManagerInitEvent::HookTrampoline()
    {
        __asm {
            // Save registers needed by the displaced code.
            push eax
            push ecx
            push edx
            push esi

            push esi
            call ConsoleCmdManagerInitHook
            pop esi

            // Restore the saved registers.
            pop esi
            pop edx
            pop ecx
            pop eax

            // Replay the displaced instructions.
            pop edi
            pop esi
            pop ebx
            mov esp, ebp

            jmp hook_ret_addr
        }
    }

    ConsoleCmdManagerInitEvent::ConsoleCmdManagerInitEvent(void *cmd_mgr)
        : Event(false), _cmd_mgr(cmd_mgr)
    {
    }

    void ConsoleCmdManagerInitEvent::InstallHook(const uint8_t *expected, size_t size,
                                                  memory::RawHook *installed)
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        if (!memory::InstallRawHook(memory::Map::base_addr + hook_addr, HookTrampoline,
                                    expected, size, installed)) {
            throw std::runtime_error("could not install console manager hook");
        }
    }

}
