#pragma once
#include <sdk/math/math.h>

struct lua_State;
using Matrix4 = Matrix4x4;

namespace LuaTypes {
    void Register(lua_State* L);
    
    void PushVector3(lua_State* L, const Vector3& v);
    void PushVector2(lua_State* L, const Vector2& v);
    void PushCFrame(lua_State* L, const Vector3& pos, const Matrix4x4& rot);
    void PushColor3(lua_State* L, float r, float g, float b);
    void PushUDim2(lua_State* L, float xs, float xo, float ys, float yo);
    
    bool IsVector3(lua_State* L, int idx);
    Vector3 ToVector3(lua_State* L, int idx);
}