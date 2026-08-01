#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/monthlyupdate.hpp"

namespace smedley::v2 {
    class CCountry;
}

using namespace smedley;

void MonthlyUpdateEventHook(v2::CCountry* country)
{
    smedley::EventRegistry<events::MonthlyUpdateEvent>::Notify(events::MonthlyUpdateEvent(country));
}


namespace smedley::events
{

    void __declspec(naked) MonthlyUpdateEvent::HookTrampoline()
    {
        __asm {
            // Pass the country pointer from the stack frame to the event hook.
            push eax
            mov eax, [ebp+0x8]
            push eax

            call MonthlyUpdateEventHook
            // Discard the callback argument and restore EAX.
            pop eax
            pop eax

            // Replay the displaced instructions.
            push ebx
            mov ebx, DWORD PTR[ebp + 0x8]
            mov eax, DWORD PTR[ebx + 0x9dc]

            jmp hook_ret_addr
        }
    }

    MonthlyUpdateEvent::MonthlyUpdateEvent(v2::CCountry* country) : Event(false)
    {
        _country = country;
    }
    v2::CCountry* MonthlyUpdateEvent::GetCountry() {
        return _country;
    }

    void MonthlyUpdateEvent::InstallHook()
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 10;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 10, nullptr);
    }

}
