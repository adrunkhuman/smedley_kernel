#include "eventregistry.hpp"
#include "event_abi_runtime.hpp"
#include "memory.hpp"
#include "events/dailyupdate.hpp"

namespace smedley::v2 {
    class CCountry;
}

using namespace smedley;

void DailyUpdateEventHook(v2::CCountry* country)
{
    smedley::NotifyDailyEventApi(country);
    smedley::EventRegistry<events::DailyUpdateEvent>::Notify(events::DailyUpdateEvent(country));
}


namespace smedley::events
{

    void __declspec(naked) DailyUpdateEvent::HookTrampoline()
    {
        __asm {
            // Save registers needed by the displaced code.
            push eax
            push fs
            push ebx

            // Pass the country pointer from the stack frame to the event hook.
            mov ebx, [ebp+0x8]
            push ebx

            call DailyUpdateEventHook

            // Discard the callback argument and restore the saved registers.
            pop ebx
            pop ebx
            pop fs
            pop eax


            // Replay the displaced instructions.
            push ebx
            mov ebx, DWORD PTR[ebp + 0x8]
            mov al, BYTE PTR[ebx + 0x15bc]

            jmp hook_ret_addr
        }
    }

    DailyUpdateEvent::DailyUpdateEvent(v2::CCountry* country) : Event(false)
    {
        _country = country;
    }

   v2::CCountry* DailyUpdateEvent::GetCountry() {
       return _country;
   }

    void DailyUpdateEvent::InstallHook()
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 10;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 10, nullptr);
    }

}
