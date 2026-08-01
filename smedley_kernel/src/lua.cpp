/**
 * Direct linking to the Lua 5.1.4 DLL is unreliable. Building with the 2010
 * compiler might help, but would further reduce Smedley's portability. This
 * file implements the Lua API by resolving function pointers through the
 * process import table.
 */
#pragma once

#include <stdexcept>
#include <windows.h>
#include "lua.hpp"
#include "memory.hpp"

using namespace smedley;

namespace smedley::lua::addresses {
    constexpr uintptr_t lua_tolstring = 0x0088a548;
    constexpr uintptr_t lua_pcall = 0x0088a4a8;
    constexpr uintptr_t luaL_loadstring = 0x0088a478;
}

extern "C" {

const char *lua_tolstring(lua_State *L, int index, size_t *len)
{
    const uintptr_t _addr = memory::Map::base_addr + lua::addresses::lua_tolstring;
    typedef const char *_lua_tolstring_type(lua_State * L, int index, size_t *len);
    // The import-table entry points to the function pointer.
    _lua_tolstring_type **fn = (_lua_tolstring_type **)_addr;
    return (*fn)(L, index, len);
}

int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc)
{
    const uintptr_t _addr = memory::Map::base_addr + lua::addresses::lua_pcall;
    typedef int (*_lua_pcall_type)(lua_State *L, int nargs, int nresults, int errfunc);
    _lua_pcall_type *fn = *(_lua_pcall_type **)&_addr;
    return (*fn)(L, nargs, nresults, errfunc);
}

int luaL_loadstring(lua_State *L, const char *s)
{
    const uintptr_t _addr = memory::Map::base_addr + lua::addresses::luaL_loadstring;
    typedef int _luaL_loadstring_type(lua_State * L, const char *s);
    _luaL_loadstring_type **fn = (_luaL_loadstring_type **)_addr;
    return (*fn)(L, s);
}

}
