#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/console.hpp"


using namespace smedley;

void ConsoleCmdManagerInitHook(v2::CConsoleCmdManager *mgr)
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

    ConsoleCmdManagerInitEvent::ConsoleCmdManagerInitEvent(v2::CConsoleCmdManager *cmd_mgr)
        : Event(false), _cmd_mgr(cmd_mgr)
    {
    }

    void ConsoleCmdManagerInitEvent::InstallHook()
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 5, nullptr);
    }

}
