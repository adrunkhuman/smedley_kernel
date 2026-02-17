#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/addtosphere.hpp"


namespace smedley::v2 {
    class CCountry;
    class CCountryTag;
}

using namespace smedley;


void AddToSphereEventHook(v2::CCountry* source, v2::CCountryTag* target)
{
    smedley::EventRegistry<events::AddToSphereEvent>::Notify(events::AddToSphereEvent(source, target));
}


namespace smedley::events
{
    void __declspec(naked) AddToSphereEvent::HookTrampoline()
    {
        __asm {
            // save context
            push esi

            // use free register to push the "this" country ptr as variable to func
            mov edx, [ebp + 0x8]
            push edx

            call AddToSphereEventHook
            // restore context
            pop edx
            pop esi

            // patched instructions
            mov eax, DWORD PTR[esi]
            mov ecx, DWORD PTR ds : 0x1e587e4

            jmp hook_ret_addr
        }
    }

    AddToSphereEvent::AddToSphereEvent(v2::CCountry* source, v2::CCountryTag* target ) : Event(false)
    {
        _target = target;
        _source = source;
    }
    v2::CCountry* AddToSphereEvent::GetSource() {
        return _source;
    }
    v2::CCountryTag* AddToSphereEvent::GetTarget() {
        return _target;
    }
    void AddToSphereEvent::InstallHook()
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 8, nullptr);
    }

}