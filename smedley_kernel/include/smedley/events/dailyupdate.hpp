#pragma once
#include <cstdint>
#include "../event.hpp"

namespace smedley::v2 {
    class CCountry;
}

namespace smedley::events
{

    /**
    When the daily update is executed for a country, this event is notified
     */
    class DailyUpdateEvent : public Event
    {
        v2::CCountry* _country;
        static constexpr uintptr_t hook_addr = 0x001085ae;
        inline static uintptr_t hook_ret_addr = 0;
        static void HookTrampoline();
        
    public:
        /// @brief returns the country being updated
        v2::CCountry* GetCountry();
        DailyUpdateEvent(v2::CCountry* country);
        /// @brief installs the hook needed to trigger the event
        static void InstallHook();

    };

}
