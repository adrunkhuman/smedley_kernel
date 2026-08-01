#pragma once
#include "../event.hpp"

namespace smedley::v2 {
    class CCountry;
}

namespace smedley::events
{

    /**
     * Raised when the monthly update runs for a country.
     */
    class MonthlyUpdateEvent : public Event
    {
        v2::CCountry* _country;
        static void  HookTrampoline();
        static constexpr uintptr_t hook_addr = 0x0010c2a6;
        inline static uintptr_t hook_ret_addr = NULL;
    public:
        MonthlyUpdateEvent(v2::CCountry* country);
        /// @brief Returns the country being updated.
        v2::CCountry* GetCountry();


        /// @brief Installs the hook that raises the event.
        static void InstallHook();
    };
}
