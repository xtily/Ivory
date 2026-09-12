

#include <features/lua/mem/LuaMem.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/offsets/offsets.h>
#include <features/instance.new/callgate/CallGate.h>
#include <features/instance.new/creation/InstanceCreate.h>
#include <features/instance.new/reflection/Reflect.h>
#include <sdk/cache/core/cache.h>

extern "C" {
#include <lua.hpp>

}

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace LuaMem {
namespace {

constexpr const char* k_inst_mt = "Ivory.Instance";

bool Plausible(std::uint64_t a) { return a >= 0x10000 && a < 0x7FFFFFFFFFFFull; }

std::uint64_t Addr(lua_State* L, int idx) {
    if (lua_isnumber(L, idx)) return (std::uint64_t)luaL_checkinteger(L, idx);
    void* ud = luaL_testudata(L, idx, k_inst_mt);
    if (!ud) return 0;
    return *static_cast<std::uint64_t*>(ud);
}

template<typename T> int ReadAs(lua_State* L) {
    const auto a = Addr(L, 1);
    if (!Plausible(a)) { lua_pushnil(L); return 1; }
    T v{};
    if (!Mem::Get().ReadMemory((uintptr_t)a, &v, sizeof(T))) { lua_pushnil(L); return 1; }
    if constexpr (std::is_floating_point_v<T>) lua_pushnumber(L, (lua_Number)v);
    else lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

template<typename T> int WriteAs(lua_State* L) {
    const auto a = Addr(L, 1);
    if (!Plausible(a)) { lua_pushboolean(L, 0); return 1; }
    T v{};
    if constexpr (std::is_floating_point_v<T>) v = (T)luaL_checknumber(L, 2);
    else v = (T)luaL_checkinteger(L, 2);
    Mem::Get().Write<T>((uintptr_t)a, v);
    lua_pushboolean(L, 1);
    return 1;
}

int l_readcstr(lua_State* L) {
    const auto a = Addr(L, 1);
    auto max = (int)luaL_optinteger(L, 2, 64);
    if (max < 1) max = 1; if (max > 512) max = 512;
    if (!Plausible(a)) { lua_pushnil(L); return 1; }
    char buf[513]{};
    Mem::Get().ReadMemory((uintptr_t)a, buf, (size_t)max);
    buf[max] = '\0';
    lua_pushstring(L, buf);
    return 1;
}

int l_readstdstr(lua_State* L) {
    const auto a = Addr(L, 1);
    if (!Plausible(a)) { lua_pushnil(L); return 1; }
    lua_pushstring(L, Mem::Get().ReadString((uintptr_t)a).c_str());
    return 1;
}

int l_writestdstr(lua_State* L) {
    const auto a = Addr(L, 1);
    const char* text = luaL_checkstring(L, 2);
    bool ok = Plausible(a) && text && InstanceCreate::SetString(a, text);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int l_writecontent(lua_State* L) {
    const auto a = Addr(L, 1);
    const char* text = luaL_checkstring(L, 2);
    bool ok = Plausible(a) && text && InstanceCreate::SetContent(a, text);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int l_gatecall(lua_State* L) {
    const auto fn  = (std::uint64_t)luaL_checkinteger(L, 1);
    const auto a0  = (std::uint64_t)luaL_optinteger(L, 2, 0);
    const auto a1  = (std::uint64_t)luaL_optinteger(L, 3, 0);
    const auto a2  = (std::uint64_t)luaL_optinteger(L, 4, 0);
    const auto a3  = (std::uint64_t)luaL_optinteger(L, 5, 0);
    if (!Plausible(fn)) { lua_pushnil(L); lua_pushstring(L, "bad fn"); return 2; }
    if (!CallGate::Ready() && !CallGate::Install()) { lua_pushnil(L); lua_pushstring(L, "no callgate"); return 2; }
    std::uint64_t ret = 0;
    if (!CallGate::Invoke(fn, a0, a1, a2, a3, &ret)) { lua_pushnil(L); lua_pushstring(L, "gate timeout"); return 2; }
    lua_pushinteger(L, (lua_Integer)ret);
    return 1;
}

int l_gatescratch(lua_State* L) { lua_pushinteger(L, (lua_Integer)CallGate::Scratch()); return 1; }
int l_rbxbase(lua_State* L) { lua_pushinteger(L, (lua_Integer)memory->m_base_address); return 1; }
int l_addrof(lua_State* L)  { lua_pushinteger(L, (lua_Integer)Addr(L, 1)); return 1; }

int l_refname(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    uintptr_t base = memory->m_base_address;
    if (!base || !s) { lua_pushnil(L); return 1; }
    const auto n = Reflect::Name(base, s);
    if (!n) lua_pushnil(L); else lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

int l_refcreator(lua_State* L) {
    uintptr_t base = memory->m_base_address;
    std::uint64_t name = 0;
    if (lua_isstring(L, 1) && !lua_isnumber(L, 1))
        name = base ? Reflect::Name(base, lua_tostring(L, 1)) : 0;
    else name = (std::uint64_t)luaL_checkinteger(L, 1);
    const auto c = (base && name) ? Reflect::Creator(base, name) : 0;
    if (!c) lua_pushnil(L); else lua_pushinteger(L, (lua_Integer)c);
    return 1;
}

int l_classdesc(lua_State* L) {
    const auto inst = Addr(L, 1);
    if (!Plausible(inst)) { lua_pushnil(L); return 1; }
    const auto cd = Mem::Get().Read<std::uint64_t>((uintptr_t)inst + Offsets::Instance::ClassDescriptor);
    if (!Plausible(cd)) lua_pushnil(L); else lua_pushinteger(L, (lua_Integer)cd);
    return 1;
}

int l_scanptr(lua_State* L) {
    const auto a = Addr(L, 1);
    const auto needle = (std::uint64_t)luaL_checkinteger(L, 2);
    auto span = (int)luaL_optinteger(L, 3, 0x400);
    if (span < 8) span = 8; if (span > 0x10000) span = 0x10000;
    lua_newtable(L);
    if (!Plausible(a) || !needle) return 1;
    std::vector<std::uint8_t> buf((size_t)span);
    Mem::Get().ReadMemory((uintptr_t)a, buf.data(), (size_t)span);
    int n = 0;
    for (size_t off = 0; off + 8 <= buf.size(); off += 8) {
        std::uint64_t v = 0; std::memcpy(&v, buf.data() + off, 8);
        if (v != needle) continue;
        lua_pushinteger(L, (lua_Integer)off); lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int l_get_class(lua_State* L) {
    const auto a = Addr(L, 1);
    if (!Plausible(a)) { lua_pushnil(L); return 1; }
    uintptr_t desc = Mem::Get().Read<uintptr_t>((uintptr_t)a + Offsets::Instance::ClassDescriptor);
    if (!desc) { lua_pushnil(L); return 1; }
    uintptr_t namePtr = Mem::Get().Read<uintptr_t>(desc + Offsets::Instance::ClassName);
    if (!namePtr) { lua_pushnil(L); return 1; }
    lua_pushstring(L, Mem::Get().ReadString(namePtr).c_str());
    return 1;
}

int l_get_name(lua_State* L) {
    const auto a = Addr(L, 1);
    if (!Plausible(a)) { lua_pushnil(L); return 1; }
    lua_pushstring(L, Mem::Get().GetInstanceName((uintptr_t)a).c_str());
    return 1;
}

void Set(lua_State* L, const char* name, lua_CFunction fn) { lua_pushcfunction(L, fn); lua_setglobal(L, name); }

} 

void Register(lua_State* L) {
    Set(L, "rdq", ReadAs<std::uint64_t>);
    Set(L, "rdd", ReadAs<std::uint32_t>);
    Set(L, "rdw", ReadAs<std::uint16_t>);
    Set(L, "rdb", ReadAs<std::uint8_t>);
    Set(L, "rdf", ReadAs<float>);
    Set(L, "rdcstr", l_readcstr);
    Set(L, "rdstr", l_readstdstr);
    Set(L, "wrq", WriteAs<std::uint64_t>);
    Set(L, "wrd", WriteAs<std::uint32_t>);
    Set(L, "wrw", WriteAs<std::uint16_t>);
    Set(L, "wrb", WriteAs<std::uint8_t>);
    Set(L, "wrf", WriteAs<float>);
    Set(L, "wrstr", l_writestdstr);
    Set(L, "wrcontent", l_writecontent);
    Set(L, "gatecall", l_gatecall);
    Set(L, "gatescratch", l_gatescratch);
    Set(L, "rbxbase", l_rbxbase);
    Set(L, "addrof", l_addrof);
    Set(L, "refname", l_refname);
    Set(L, "refcreator", l_refcreator);
    Set(L, "classdesc", l_classdesc);
    Set(L, "scanptr", l_scanptr);
    Set(L, "get_class", l_get_class);
    Set(L, "get_name", l_get_name);
    
    lua_newtable(L);
    struct { const char* n; lua_CFunction f; } t[] = {
        {"rdq",ReadAs<std::uint64_t>},{"rdd",ReadAs<std::uint32_t>},{"rdw",ReadAs<std::uint16_t>},
        {"rdb",ReadAs<std::uint8_t>},{"rdf",ReadAs<float>},{"rdcstr",l_readcstr},{"rdstr",l_readstdstr},
        {"wrq",WriteAs<std::uint64_t>},{"wrd",WriteAs<std::uint32_t>},{"wrw",WriteAs<std::uint16_t>},
        {"wrb",WriteAs<std::uint8_t>},{"wrf",WriteAs<float>},{"wrstr",l_writestdstr},{"wrcontent",l_writecontent},
        {"gatecall",l_gatecall},{"gatescratch",l_gatescratch},{"rbxbase",l_rbxbase},{"addrof",l_addrof},
        {"refname",l_refname},{"refcreator",l_refcreator},{"classdesc",l_classdesc},{"scanptr",l_scanptr},
        {"get_class",l_get_class},{"get_name",l_get_name},
    };
    for (auto& e:t) { lua_pushcfunction(L,e.f); lua_setfield(L,-2,e.n); }
    lua_setglobal(L, "mem");
}

} 


