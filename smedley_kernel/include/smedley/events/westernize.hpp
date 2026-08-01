#pragma once
#include "../event.hpp"

namespace smedley::v2 {
    class CCountry;
}



namespace smedley::events
{

    /**
     * Raised when a country westernizes.
     */
    class WesternizeEvent : public Event
    {
        v2::CCountry* _country;
        static void  HookTrampoline();
        static constexpr uintptr_t hook_addr = 0x0014238e;
        inline static uintptr_t hook_ret_addr = NULL;
    public:
        WesternizeEvent(v2::CCountry* country);

        /// @brief Returns the country that is westernizing.
        v2::CCountry* GetCountry();

        /// @brief Installs the hook that raises the event.
        static void InstallHook();
    };

}
