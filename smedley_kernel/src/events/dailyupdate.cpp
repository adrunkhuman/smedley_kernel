#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/dailyupdate.hpp"

namespace smedley::v2 {
    class CCountry;
}

using namespace smedley;

void DailyUpdateEventHook(v2::CCountry* country)
{
    smedley::EventRegistry<events::DailyUpdateEvent>::Notify(events::DailyUpdateEvent(country));
}


namespace smedley::events
{

    void __declspec(naked) DailyUpdateEvent::HookTrampoline()
    {
        __asm {
            // save register context
            push eax
            push fs
            push ebx

            // push "this"ptr variable to func
            mov ebx, [ebp+0x8]
            push ebx

            call DailyUpdateEventHook

            //clean up context and restore registers
            pop ebx
            pop ebx
            pop fs
            pop eax


            //patched instructions
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
