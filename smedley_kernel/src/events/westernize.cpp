#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/westernize.hpp"


namespace smedley::v2 {
    class CCountry;
}

using namespace smedley;

uintptr_t WESTERNIZE_FLAG_ADDR = 0;

void WesternizeEventHook(v2::CCountry* country)
{
    smedley::EventRegistry<events::WesternizeEvent>::Notify(events::WesternizeEvent(country));
}


namespace smedley::events
{
    void __declspec(naked) WesternizeEvent::HookTrampoline()
    {
        __asm {
            // Save registers needed by the displaced code.
            push eax
            push fs

            // Pass the country pointer from the stack frame to the event hook.
            mov ecx, [ebp+0x8]
            push ecx

            call WesternizeEventHook
            // Discard the callback argument and restore FS and EAX.
            pop ecx
            pop fs
            pop eax

            // Replay the displaced instructions.
            push eax
            mov eax, WESTERNIZE_FLAG_ADDR
            cmp DWORD PTR[eax], 0x0
            pop eax

            jmp hook_ret_addr
        }
    }

    WesternizeEvent::WesternizeEvent(v2::CCountry* country) : Event(false)
    {
        _country = country;
    }
    v2::CCountry* WesternizeEvent::GetCountry() {
        return _country;
    }
    void WesternizeEvent::InstallHook()
    {
        WESTERNIZE_FLAG_ADDR = memory::Map::base_addr + 0xe5eadc;
        hook_ret_addr = memory::Map::base_addr + hook_addr + 7;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 7, nullptr);
    }

}
