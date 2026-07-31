#pragma once

#include "../clausewitz/persistent.hpp"
#include "../clausewitz/types.hpp"
#include "../std/string.hpp"
#include <cstddef>

namespace smedley::v2
{

    struct CFlag
    {
        sstd::string key;
        bool val;
    };

    static_assert(offsetof(CFlag, val) == 0x1c);
    static_assert(sizeof(CFlag) == 0x20);

    /**
     * A set of flag values. Stores the flags referenced by effects/triggers like
     * set_country_flag and has_country_flag.
     */
    class CFlags : public clausewitz::CTernary<CFlag *>, public clausewitz::CPersistent
    {
    public:
        bool Has(const char *key)
        {
            if (key == nullptr) {
                return false;
            }
            const auto *flag = Get(key);
            return flag != nullptr && flag->val;
        }
    };

    static_assert(sizeof(CFlags) == 0x2c);

}
