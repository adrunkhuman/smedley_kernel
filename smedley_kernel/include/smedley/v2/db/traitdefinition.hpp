#pragma once

#include "../../std/string.hpp"
#include "../../std/vector.hpp"
#include "../country.hpp"
#include <cstring>
#include <string>

namespace smedley::v2
{

    class CTraitDefinitionArray
    {
        CTraitDefinition* _traitDefinitions[10];
    public:
        /*[[[cog
        from codegen import print_class_model_fns
        print_class_model_fns('./models/v2/classes/CTraitDefinitionArray.toml')
        ]]]*/
        static CTraitDefinitionArray * instance()
        {
        const uintptr_t _addr = memory::Map::base_addr + 0xe5fe10;
        return *(reinterpret_cast<CTraitDefinitionArray **>(_addr));
        }
        // [[[end]]]
        CTraitDefinition* operator [](int index) {
            return _traitDefinitions[index];
        }

    };

    static_assert(sizeof(CTraitDefinitionArray) == 0x28);

}