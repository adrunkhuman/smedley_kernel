#pragma once

#include <lua.hpp>
#include <windows.h>

namespace smedley::clausewitz
{
    /*[[[cog
    from codegen import print_fn_file
    print_fn_file('./codegen/models/clausewitz/functions/lua.toml')
    ]]]*/
    lua_State ** GetLuaState(int index)
    {
    const uintptr_t _addr = memory::Map::base_addr + 6021504;
    __asm mov edi, index __asm call _addr

    }
    //[[[end]]]
}
