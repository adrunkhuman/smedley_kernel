#pragma once
#include "../event.hpp"

namespace smedley::v2 {
    class CCountry;
    class CCountryTag;
}

namespace smedley::events
{

    /**
     * Raised when one country spheres another. The game also appears to raise
     * it for every sphere pair while loading a save.
     */
    class AddToSphereEvent : public Event
    {
        v2::CCountry* _source;
        v2::CCountryTag* _target;
        static void  HookTrampoline();
        static constexpr uintptr_t hook_addr = 0x00133e59;
        inline static uintptr_t hook_ret_addr = NULL;
    public:
        AddToSphereEvent(v2::CCountry* source, v2::CCountryTag* target);

        /// @brief Returns the country that sphered the target.
        v2::CCountry* GetSource();


        /// @brief Returns the tag of the country that was sphered.
        v2::CCountryTag* GetTarget();

        /// @brief Installs the hook that raises the event.
        static void InstallHook();
    };

}
