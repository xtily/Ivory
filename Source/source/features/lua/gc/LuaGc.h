#pragma once
struct lua_State;
namespace LuaGc { void Register(lua_State* L); void Stop(); }