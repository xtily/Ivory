
#include <features/lua/gc/LuaGc.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/offsets/offsets.h>
#include <sdk/cache/core/cache.h>
#include <features/lua/bridge/LuaBridge.h>

extern "C" {
#include <lua.hpp>

}

#include <vector>
#include <unordered_set>
#include <cstdint>

namespace LuaGc {
namespace {

constexpr size_t k_max_nodes = 4000;

void collect_all(uintptr_t root, std::vector<uintptr_t>& out, std::unordered_set<uintptr_t>& seen, int depth = 0) {
    if (!root || depth > 32 || out.size() >= k_max_nodes) return;
    if (!Mem::Get().IsValid(root)) return;
    if (!seen.insert(root).second) return;
    out.push_back(root);
    auto kids = Mem::Get().GetChildren(root);
    for (auto c : kids) {
        if (out.size() >= k_max_nodes) break;
        collect_all(c, out, seen, depth + 1);
    }
}

int l_getinstances(lua_State* L) {
    uintptr_t dm = Rbx::Get().DataModel;
    lua_newtable(L);
    if (!dm || !Mem::Get().IsValid(dm)) return 1;
    std::vector<uintptr_t> out;
    std::unordered_set<uintptr_t> seen;
    out.reserve(256);
    collect_all(dm, out, seen, 0);
    int i = 1;
    for (auto addr : out) {
        LuaBridge::PushInstance(L, addr);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_getgc(lua_State* L) {
    
    return l_getinstances(L);
}

int l_getnilinstances(lua_State* L) {
    
    uintptr_t dm = Rbx::Get().DataModel;
    lua_newtable(L);
    if (!dm || !Mem::Get().IsValid(dm)) return 1;
    std::vector<uintptr_t> out;
    std::unordered_set<uintptr_t> seen;
    collect_all(dm, out, seen, 0);
    int i = 1;
    for (auto addr : out) {
        uintptr_t parent = Mem::Get().Read<uintptr_t>(addr + Offsets::Instance::Parent);
        if (parent) continue;
        LuaBridge::PushInstance(L, addr);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

} 

void Register(lua_State* L) {
    lua_pushcfunction(L, l_getinstances); lua_setglobal(L, "getinstances");
    lua_pushcfunction(L, l_getgc);        lua_setglobal(L, "getgc");
    lua_pushcfunction(L, l_getnilinstances); lua_setglobal(L, "getnilinstances");
}
void Stop() {}

} 


