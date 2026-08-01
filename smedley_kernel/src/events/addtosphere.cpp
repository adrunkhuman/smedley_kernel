#include "eventregistry.hpp"
#include "memory.hpp"
#include "events/addtosphere.hpp"


namespace smedley::v2 {
    class CCountry;
    class CCountryTag;
}

using namespace smedley;

uintptr_t ADD_TO_SPHERE_COUNTRY_DB_ADDR = 0;

void AddToSphereEventHook(v2::CCountry* source, v2::CCountryTag* target)
{
    smedley::EventRegistry<events::AddToSphereEvent>::Notify(events::AddToSphereEvent(source, target));
}


namespace smedley::events
{
    void __declspec(naked) AddToSphereEvent::HookTrampoline()
    {
        __asm {
            // Save registers needed by the displaced code.
            push esi

            // Pass the country pointer from the stack frame to the event hook.
            mov edx, [ebp + 0x8]
            push edx

            call AddToSphereEventHook
            // Discard the callback argument and restore ESI.
            pop edx
            pop esi

            // Replay the displaced instructions.
            mov eax, DWORD PTR[esi]
            mov ecx, ADD_TO_SPHERE_COUNTRY_DB_ADDR
            mov ecx, DWORD PTR[ecx]

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
        ADD_TO_SPHERE_COUNTRY_DB_ADDR = memory::Map::base_addr + 0xe587e4;
        hook_ret_addr = memory::Map::base_addr + hook_addr + 8;
        memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 8, nullptr);
    }

}
