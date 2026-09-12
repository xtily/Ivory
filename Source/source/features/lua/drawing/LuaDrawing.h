#pragma once
struct lua_State;
namespace LuaDrawing {
    void Register(lua_State* L);
    void Render();  
    void Clear();   
}