#pragma once
#include <cstdint>
struct lua_State;

namespace LuaBridge {
    void Register(lua_State* L);
    void PushInstance(lua_State* L, std::uintptr_t address);
    void RefreshGlobals(lua_State* L);
    std::uintptr_t CheckAddress(lua_State* L, int idx = 1);
}