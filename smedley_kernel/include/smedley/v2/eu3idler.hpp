#pragma once

#include <cstdint>

#include "../clausewitz/idler.hpp"
#include "../memory.hpp"

namespace smedley::v2
{

    class CEU3Idler : public clausewitz::CIdler
    {
    };

    /// @brief main in game ui handler
    class CInGameIdler : public CEU3Idler
    {
    public:
        /** Valid only after the frontend has transitioned into campaign mode. */
        void TogglePause()
        {
            using TogglePauseFn = void (__thiscall *)(CInGameIdler *);
            const auto fn = reinterpret_cast<TogglePauseFn>(memory::Map::base_addr + 0x26a2c0);
            fn(this);
        }

        uint8_t pause_state() const
        {
            return *(reinterpret_cast<const uint8_t *>(this) + 0x1538);
        }
    };

    class CNudgeIdler : public CEU3Idler
    {
    };

}
