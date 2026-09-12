#define IMGUI_DEFINE_MATH_OPERATORS
#include <features/lua/vm/LuaVM.h>
#include <features/lua/bridge/LuaBridge.h>
#include <features/lua/types/LuaTypes.h>
#include <features/lua/drawing/LuaDrawing.h>
#include <features/lua/gc/LuaGc.h>
#include <features/lua/mem/LuaMem.h>

#include <features/lua/mem/MemCompat.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/cache.h>
#include <sdk/offsets/offsets.h>
#include <sdk/math/math.h>
#include <core/logger/logger.h>

#include <imgui/imgui.h>

extern "C" {
#include <lua.hpp>
}

#include <Windows.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

namespace LuaVM {
lua_State* g_L = nullptr;
std::mutex  g_lua_mu;
std::atomic<bool> g_busy{ false };
std::atomic<bool> g_tick_run{ false };
std::thread g_tick_th;


std::function<void(const char*, bool)> g_log_cb;

void SetLogCallback(std::function<void(const char*, bool)> cb) {
    g_log_cb = cb;
}

namespace {

void LogInfo(const char* msg) {
    if (g_log_cb) g_log_cb(msg, false);
}
void LogErr(const char* msg) {
    if (g_log_cb) g_log_cb(msg, true);
}


struct WaitEntry { int ref = LUA_NOREF; float left = 0.f; };
std::vector<WaitEntry> g_waits;

struct ConnEntry {
    int fn_ref = LUA_NOREF;
    bool alive = false;
    bool once = false;
    int kind = 0;
    std::uint64_t owner = 0;
    std::uint32_t seq = 0;
    std::string prop;
};
std::vector<ConnEntry> g_conns;
std::uint32_t g_token_seq = 0;

struct ConnUd { int idx = -1; std::uint32_t seq = 0; };

struct SigWait {
    int thr_ref = LUA_NOREF;
    bool alive = false;
    int kind = 0;
    std::uint64_t owner = 0;
    std::string prop;
};
std::vector<SigWait> g_sigwaits;

std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> g_kids_snap;
std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> g_desc_snap;
std::unordered_map<std::string, std::string> g_prop_snap;
constexpr size_t k_max_prop_polls = 16;
size_t g_prop_cursor = 0;

struct DelayEntry {
    int fn_ref = LUA_NOREF;
    float left = 0.f;
    bool alive = false;
    std::uint32_t seq = 0;
};
std::vector<DelayEntry> g_delays;
struct DelayUd { int idx = -1; std::uint32_t seq = 0; };

unsigned g_poll_tick = 0;


static std::string RoReadClassName(uintptr_t addr) {
    if (!addr) return {};
    uintptr_t desc = Mem::Get().Read<uintptr_t>(addr + Offsets::Instance::ClassDescriptor);
    if (!desc) return {};
    uintptr_t namePtr = Mem::Get().Read<uintptr_t>(desc + Offsets::Instance::ClassName);
    if (!namePtr) return {};
    return Mem::Get().ReadString(namePtr);
}

static std::string RoReadName(uintptr_t addr) {
    return Mem::Get().GetInstanceName(addr);
}

static uintptr_t RoFindChild(uintptr_t parent, const char* name) {
    return Mem::Get().FindChild(parent, name);
}

static uintptr_t RoGetWorkspace() {
    uintptr_t dm = Rbx::Get().DataModel;
    if (!dm) return 0;
    return Mem::Get().Read<uintptr_t>(dm + Offsets::DataModel::Workspace);
}

static uintptr_t RoGetPlayersService() {
    return Rbx::Get().ServicesData.PlayersService;
}


void schedule_wait(lua_State* L, float sec) {
    if (!lua_isyieldable(L)) return;
    if (sec < 0.f) sec = 0.f;
    lua_pushthread(L);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_waits.push_back({ ref, sec });
}


int l_print(lua_State* L) {
    const int n = lua_gettop(L);
    std::string line;
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            LogErr(lua_tostring(L, -1)); lua_pop(L, 1); continue;
        }
        const char* s = lua_tostring(L, -1);
        if (i > 1) line.push_back('\t');
        line += s ? s : "nil";
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    LogInfo(line.c_str());
    return 0;
}

int l_warn(lua_State* L) {
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        const char* s = luaL_tolstring(L, i, nullptr);
        if (i > 1) line.push_back('\t');
        line += s ? s : "nil";
        lua_pop(L, 1);
    }
    if (g_log_cb) g_log_cb(("[WARN] " + line).c_str(), false);
    return 0;
}

int l_identifyexecutor(lua_State* L) {
    lua_pushstring(L, "Ivory");
    lua_pushstring(L, "1.0");
    return 2;
}

int l_wait(lua_State* L) {
    float t = static_cast<float>(luaL_optnumber(L, 1, 0.03));
    if (!lua_isyieldable(L)) return 0;
    schedule_wait(L, t);
    return lua_yield(L, 0);
}

int l_spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_State* co = lua_newthread(L);
    lua_pushvalue(L, 1);
    lua_xmove(L, co, 1);
    int nres = 0;
    const int status = lua_resume(co, L, 0, &nres);
    if (status != LUA_OK && status != LUA_YIELD) {
        LogErr(lua_tostring(co, -1));
        lua_pop(co, 1);
    }
    return 1;
}

int alloc_delay(float sec, lua_State* L, int fn_idx) {
    if (sec < 0.f) sec = 0.f;
    int idx = -1;
    for (int i = 0; i < (int)g_delays.size(); ++i)
        if (!g_delays[i].alive && g_delays[i].fn_ref == LUA_NOREF) { idx = i; break; }
    if (idx < 0) { idx = (int)g_delays.size(); g_delays.push_back({}); }
    lua_pushvalue(L, fn_idx);
    DelayEntry& d = g_delays[idx];
    d.fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    d.left = sec; d.alive = true; d.seq = ++g_token_seq;
    return idx;
}

void push_task_token(lua_State* L, int idx) {
    auto* ud = static_cast<DelayUd*>(lua_newuserdata(L, sizeof(DelayUd)));
    ud->idx = idx; ud->seq = g_delays[idx].seq;
    luaL_getmetatable(L, "Ivory.Task");
    lua_setmetatable(L, -2);
}

int l_delay(lua_State* L) {
    float t = (float)luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    push_task_token(L, alloc_delay(t, L, 2));
    return 1;
}

int l_defer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    push_task_token(L, alloc_delay(0.f, L, 1));
    return 1;
}

int l_cancel(lua_State* L) {
    if (auto* ud = static_cast<DelayUd*>(luaL_testudata(L, 1, "Ivory.Task"))) {
        if (ud->idx < 0 || ud->idx >= (int)g_delays.size()) return 0;
        DelayEntry& d = g_delays[ud->idx];
        if (!d.alive || d.seq != ud->seq) return 0;
        d.alive = false;
        if (d.fn_ref != LUA_NOREF) { luaL_unref(L, LUA_REGISTRYINDEX, d.fn_ref); d.fn_ref = LUA_NOREF; }
        return 0;
    }
    if (lua_isthread(L, 1)) {
        lua_State* co = lua_tothread(L, 1);
        for (size_t i = 0; i < g_waits.size(); ++i) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, g_waits[i].ref);
            lua_State* wco = lua_tothread(L, -1); lua_pop(L, 1);
            if (wco != co) continue;
            luaL_unref(L, LUA_REGISTRYINDEX, g_waits[i].ref);
            g_waits.erase(g_waits.begin() + i); break;
        }
        for (size_t i = 0; i < g_sigwaits.size(); ++i) {
            SigWait& w = g_sigwaits[i];
            if (!w.alive || w.thr_ref == LUA_NOREF) continue;
            lua_rawgeti(L, LUA_REGISTRYINDEX, w.thr_ref);
            lua_State* wco = lua_tothread(L, -1); lua_pop(L, 1);
            if (wco != co) continue;
            luaL_unref(L, LUA_REGISTRYINDEX, w.thr_ref);
            w.thr_ref = LUA_NOREF; w.alive = false; break;
        }
    }
    return 0;
}

int l_getmousepos(lua_State* L) {
    POINT pt{};
    GetCursorPos(&pt);
    
    HWND hwnd = GetForegroundWindow();
    if (hwnd) ScreenToClient(hwnd, &pt);
    lua_pushnumber(L, (lua_Number)pt.x);
    lua_pushnumber(L, (lua_Number)pt.y);
    return 2;
}

int l_isrbxactive(lua_State* L) {
    
    HWND fg = GetForegroundWindow();
    char title[256]{};
    GetWindowTextA(fg, title, 255);
    bool active = strstr(title, "Roblox") != nullptr;
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

int l_iskeypressed(lua_State* L) {
    int vk = 0;
    if (lua_type(L, 1) == LUA_TSTRING) {
        const char* s = lua_tostring(L, 1);
        if (!s || !s[0]) { lua_pushboolean(L, 0); return 1; }
        if (_stricmp(s, "left") == 0 || _stricmp(s, "lmb") == 0) vk = VK_LBUTTON;
        else if (_stricmp(s, "right") == 0 || _stricmp(s, "rmb") == 0) vk = VK_RBUTTON;
        else if (_stricmp(s, "middle") == 0 || _stricmp(s, "mmb") == 0) vk = VK_MBUTTON;
        else if (_stricmp(s, "space") == 0) vk = VK_SPACE;
        else if (_stricmp(s, "shift") == 0) vk = VK_SHIFT;
        else if (_stricmp(s, "ctrl") == 0) vk = VK_CONTROL;
        else if (_stricmp(s, "alt") == 0) vk = VK_MENU;
        else if (_stricmp(s, "tab") == 0) vk = VK_TAB;
        else if (_stricmp(s, "escape") == 0 || _stricmp(s, "esc") == 0) vk = VK_ESCAPE;
        else vk = (unsigned char)std::toupper((unsigned char)s[0]);
    } else vk = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, (GetAsyncKeyState(vk) & 0x8000) != 0);
    return 1;
}


void OpenSafeLibs(lua_State* L) {
    luaL_requiref(L, LUA_GNAME,     luaopen_base,      1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table,    1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME,luaopen_math,     1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8,    1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME,  luaopen_coroutine,1); lua_pop(L, 1);
    luaL_requiref(L, LUA_OSLIBNAME,   luaopen_os,       1); lua_pop(L, 1);
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package,  1); lua_pop(L, 1);
    
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
}


int l_conn_disconnect(lua_State* L) {
    auto* ud = static_cast<ConnUd*>(luaL_checkudata(L, 1, "Ivory.RBXScriptConnection"));
    if (!ud || ud->idx < 0 || ud->idx >= (int)g_conns.size()) return 0;
    ConnEntry& c = g_conns[ud->idx];
    if (!c.alive || c.seq != ud->seq) return 0;
    c.alive = false;
    if (c.fn_ref != LUA_NOREF) { luaL_unref(L, LUA_REGISTRYINDEX, c.fn_ref); c.fn_ref = LUA_NOREF; }
    return 0;
}

bool conn_owner_ok(const ConnEntry& c, int kind, std::uint64_t owner, const char* prop) {
    if (c.kind != kind) return false;
    if (kind==3||kind==6||kind==7||kind==8||kind==9) if (c.owner != owner) return false;
    if (kind==9) { if (!prop || c.prop != prop) return false; }
    return true;
}
bool wait_owner_ok(const SigWait& w, int kind, std::uint64_t owner, const char* prop) {
    if (!w.alive || w.thr_ref == LUA_NOREF || w.kind != kind) return false;
    if (kind==3||kind==6||kind==7||kind==8||kind==9) if (w.owner != owner) return false;
    if (kind==9) { if (!prop || w.prop != prop) return false; }
    return true;
}

void kill_conn(ConnEntry& c) {
    c.alive = false;
    if (c.fn_ref != LUA_NOREF && g_L) { luaL_unref(g_L, LUA_REGISTRYINDEX, c.fn_ref); c.fn_ref = LUA_NOREF; }
}

void fire_conns(int kind, std::uint64_t owner, const char* prop, int nargs) {
    if (!g_L || !lua_checkstack(g_L, nargs + 2)) return;
    const int arg0 = lua_gettop(g_L) - nargs + 1;
    const size_t n = g_conns.size();
    for (size_t i = 0; i < n && i < g_conns.size(); ++i) {
        if (!g_conns[i].alive || g_conns[i].fn_ref == LUA_NOREF) continue;
        if (!conn_owner_ok(g_conns[i], kind, owner, prop)) continue;
        const std::uint32_t seq = g_conns[i].seq;
        const bool once = g_conns[i].once;
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_conns[i].fn_ref);
        for (int a = 0; a < nargs; ++a) lua_pushvalue(g_L, arg0 + a);
        if (lua_pcall(g_L, nargs, 0, 0) != LUA_OK) { LogErr(lua_tostring(g_L, -1)); lua_pop(g_L, 1); }
        if (once && i < g_conns.size() && g_conns[i].seq == seq) kill_conn(g_conns[i]);
    }
}

void wake_waits(int kind, std::uint64_t owner, const char* prop, int nargs) {
    if (!g_L || !lua_checkstack(g_L, nargs + 2)) return;
    const int arg0 = lua_gettop(g_L) - nargs + 1;
    const size_t n = g_sigwaits.size();
    for (size_t i = 0; i < n && i < g_sigwaits.size(); ++i) {
        if (!wait_owner_ok(g_sigwaits[i], kind, owner, prop)) continue;
        const int ref = g_sigwaits[i].thr_ref;
        g_sigwaits[i].alive = false; g_sigwaits[i].thr_ref = LUA_NOREF;
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, ref);
        lua_State* co = lua_tothread(g_L, -1);
        luaL_unref(g_L, LUA_REGISTRYINDEX, ref);
        if (!co || !lua_checkstack(co, nargs + 1)) { lua_pop(g_L, 1); continue; }
        for (int a = 0; a < nargs; ++a) lua_pushvalue(g_L, arg0 + a);
        lua_xmove(g_L, co, nargs);
        int nres = 0;
        const int st = lua_resume(co, g_L, nargs, &nres);
        if (st != LUA_OK && st != LUA_YIELD) { LogErr(lua_tostring(co, -1)); lua_pop(co, 1); }
        lua_pop(g_L, 1);
    }
}

void fire_sig(int kind, std::uint64_t owner, const char* prop, int nargs) {
    if (!g_L) return;
    fire_conns(kind, owner, prop, nargs);
    wake_waits(kind, owner, prop, nargs);
    if (nargs > 0) lua_pop(g_L, nargs);
}

int l_signal_connect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int kind     = (int)lua_tointeger(L, lua_upvalueindex(1));
    const std::uint64_t owner = (std::uint64_t)lua_tointeger(L, lua_upvalueindex(2));
    const char* prop   = lua_tostring(L, lua_upvalueindex(3));
    const bool once    = lua_toboolean(L, lua_upvalueindex(4)) != 0;
    int idx = -1;
    for (int i = 0; i < (int)g_conns.size(); ++i)
        if (!g_conns[i].alive && g_conns[i].fn_ref == LUA_NOREF) { idx = i; break; }
    if (idx < 0) { idx = (int)g_conns.size(); g_conns.push_back({}); }
    lua_pushvalue(L, 2);
    ConnEntry& c = g_conns[idx];
    c.fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    c.alive = true; c.once = once; c.kind = kind;
    c.owner = owner; c.prop = prop ? prop : ""; c.seq = ++g_token_seq;
    auto* ud = static_cast<ConnUd*>(lua_newuserdata(L, sizeof(ConnUd)));
    ud->idx = idx; ud->seq = c.seq;
    if (luaL_newmetatable(L, "Ivory.RBXScriptConnection")) {
        lua_pushcfunction(L, l_conn_disconnect);
        lua_setfield(L, -2, "Disconnect");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int l_signal_wait(lua_State* L) {
    const int kind     = (int)lua_tointeger(L, lua_upvalueindex(1));
    const std::uint64_t owner = (std::uint64_t)lua_tointeger(L, lua_upvalueindex(2));
    const char* prop   = lua_tostring(L, lua_upvalueindex(3));
    if (!lua_isyieldable(L)) return 0;
    lua_pushthread(L);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    SigWait w{};
    w.thr_ref = ref; w.alive = true; w.kind = kind; w.owner = owner; w.prop = prop ? prop : "";
    g_sigwaits.push_back(std::move(w));
    return lua_yield(L, 0);
}

int l_signal_fire(lua_State* L) { (void)L; return 0; }

void push_signal(lua_State* L, int kind, std::uint64_t owner, const char* prop) {
    lua_newtable(L);
    auto bind = [&](const char* name, bool once) {
        lua_pushinteger(L, kind);
        lua_pushinteger(L, (lua_Integer)owner);
        lua_pushstring(L, prop ? prop : "");
        lua_pushboolean(L, once ? 1 : 0);
        lua_pushcclosure(L, l_signal_connect, 4);
        lua_setfield(L, -2, name);
    };
    bind("Connect", false);
    bind("Once", true);
    lua_pushinteger(L, kind);
    lua_pushinteger(L, (lua_Integer)owner);
    lua_pushstring(L, prop ? prop : "");
    lua_pushcclosure(L, l_signal_wait, 3);
    lua_setfield(L, -2, "Wait");
    lua_pushinteger(L, kind);
    lua_pushinteger(L, (lua_Integer)owner);
    lua_pushstring(L, prop ? prop : "");
    lua_pushcclosure(L, l_signal_fire, 3);
    lua_setfield(L, -2, "Fire");
}

void fire_inst_sig(int kind, std::uint64_t owner, std::uint64_t inst) {
    if (!g_L || !inst) return;
    LuaBridge::PushInstance(g_L, (uintptr_t)inst);
    fire_sig(kind, owner, nullptr, 1);
}


bool g_plr_seeded = false;
std::unordered_set<std::uint64_t> g_seen_plr;
std::unordered_map<std::uint64_t, std::uint64_t> g_seen_char;

bool has_kind(int k0, int k1 = -1) {
    for (const auto& c : g_conns) if (c.alive && (c.kind==k0||c.kind==k1)) return true;
    for (const auto& w : g_sigwaits) if (w.alive && (w.kind==k0||w.kind==k1)) return true;
    return false;
}

void poll_players() {
    if (!has_kind(1, 2) && !has_kind(3)) {
        if (g_plr_seeded) { g_plr_seeded = false; g_seen_plr.clear(); g_seen_char.clear(); }
        return;
    }
    uintptr_t pads = RoGetPlayersService();
    if (!pads || !Mem::Get().IsValid(pads)) return;
    std::unordered_set<std::uint64_t> now;
    auto kids = Mem::Get().GetChildren(pads);
    for (auto c : kids) {
        if (!Mem::Get().IsValid(c)) continue;
        if (RoReadClassName(c) != "Player") continue;
        now.insert(c);
        if (!g_plr_seeded) {
            g_seen_plr.insert(c);
            uintptr_t ch = Mem::Get().Read<uintptr_t>(c + Offsets::Player::ModelInstance);
            g_seen_char[c] = ch;
            continue;
        }
        if (!g_seen_plr.count(c)) { g_seen_plr.insert(c); fire_inst_sig(1, 0, c); }
        uintptr_t ch = Mem::Get().Read<uintptr_t>(c + Offsets::Player::ModelInstance);
        uintptr_t prev = g_seen_char.count(c) ? g_seen_char[c] : 0;
        if (ch && ch != prev && Mem::Get().IsValid(ch)) fire_inst_sig(3, c, ch);
        g_seen_char[c] = ch;
    }
    if (g_plr_seeded) {
        for (std::uint64_t old : g_seen_plr)
            if (!now.count(old)) { fire_inst_sig(2, 0, old); g_seen_char.erase(old); }
    }
    g_seen_plr.swap(now);
    g_plr_seeded = true;
}


bool g_input_seeded = false;
bool g_key_down[256]{};

void push_input_obj(lua_State* L, int vk) {
    const char* key_name = "Unknown";
    char letter[2]{};
    if (vk == VK_LBUTTON) key_name = "MouseLeftButton";
    else if (vk == VK_RBUTTON) key_name = "MouseRightButton";
    else if (vk == VK_MBUTTON) key_name = "MouseMiddleButton";
    else if (vk == VK_SPACE) key_name = "Space";
    else if (vk == VK_SHIFT) key_name = "LeftShift";
    else if (vk == VK_CONTROL) key_name = "LeftControl";
    else if (vk == VK_MENU) key_name = "LeftAlt";
    else if (vk == VK_TAB) key_name = "Tab";
    else if (vk == VK_ESCAPE) key_name = "Escape";
    else if ((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')) { letter[0]=(char)vk; key_name=letter; }
    const char* uit = "Keyboard";
    if (vk==VK_LBUTTON) uit="MouseButton1";
    else if (vk==VK_RBUTTON) uit="MouseButton2";
    else if (vk==VK_MBUTTON) uit="MouseButton3";
    lua_newtable(L);
    lua_newtable(L); lua_pushinteger(L,vk); lua_setfield(L,-2,"Value"); lua_pushstring(L,key_name); lua_setfield(L,-2,"Name"); lua_setfield(L,-2,"KeyCode");
    lua_newtable(L); lua_pushstring(L,uit); lua_setfield(L,-2,"Name"); lua_setfield(L,-2,"UserInputType");
    lua_pushboolean(L,0); lua_setfield(L,-2,"IsProcessed");
}

void fire_input_sig(int kind, int vk) {
    if (!g_L) return;
    push_input_obj(g_L, vk);
    lua_pushboolean(g_L, 0);
    fire_sig(kind, 0, nullptr, 2);
}

void poll_input() {
    if (!has_kind(4, 5)) return;
    static const int k_vks[] = {
        VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,
        VK_SPACE,VK_SHIFT,VK_CONTROL,VK_MENU,VK_TAB,VK_ESCAPE,
        VK_LEFT,VK_UP,VK_RIGHT,VK_DOWN,
        'Q','W','E','R','T','Y','U','I','O','P',
        'A','S','D','F','G','H','J','K','L',
        'Z','X','C','V','B','N','M',
        '0','1','2','3','4','5','6','7','8','9',
    };
    for (int vk : k_vks) {
        if (vk<0||vk>255) continue;
        const bool now = (GetAsyncKeyState(vk)&0x8000)!=0;
        if (!g_input_seeded) { g_key_down[vk]=now; continue; }
        if (now&&!g_key_down[vk]) fire_input_sig(4,vk);
        else if (!now&&g_key_down[vk]) fire_input_sig(5,vk);
        g_key_down[vk]=now;
    }
    g_input_seeded=true;
}


void collect_desc(std::uint64_t root, std::unordered_set<std::uint64_t>& out, size_t& nodes, int depth=0) {
    if (!Mem::Get().IsValid(root)||nodes>1500||depth>24) return;
    for (auto c : Mem::Get().GetChildren(root)) {
        if (!Mem::Get().IsValid(c)) continue;
        if (!out.insert(c).second) continue;
        ++nodes;
        collect_desc(c, out, nodes, depth+1);
    }
}

void poll_hierarchy() {
    if (!g_L) return;
    std::unordered_set<std::uint64_t> watch_kids, watch_desc;
    for (const auto& c : g_conns) {
        if (!c.alive) continue;
        if (c.kind==6||c.kind==7) watch_kids.insert(c.owner);
        if (c.kind==8) watch_desc.insert(c.owner);
    }
    for (const auto& w : g_sigwaits) {
        if (!w.alive) continue;
        if (w.kind==6||w.kind==7) watch_kids.insert(w.owner);
        if (w.kind==8) watch_desc.insert(w.owner);
    }
    for (auto it=g_kids_snap.begin();it!=g_kids_snap.end();)
        it=watch_kids.count(it->first)?std::next(it):g_kids_snap.erase(it);
    for (auto it=g_desc_snap.begin();it!=g_desc_snap.end();)
        it=watch_desc.count(it->first)?std::next(it):g_desc_snap.erase(it);
    if ((g_poll_tick%2)==0) {
        for (std::uint64_t owner : watch_kids) {
            if (!Mem::Get().IsValid(owner)) continue;
            std::unordered_set<std::uint64_t> now;
            for (auto c : Mem::Get().GetChildren(owner)) if (Mem::Get().IsValid(c)) now.insert(c);
            auto it=g_kids_snap.find(owner);
            if (it==g_kids_snap.end()) { g_kids_snap.emplace(owner,std::move(now)); continue; }
            for (std::uint64_t a : now) if (!it->second.count(a)) fire_inst_sig(6,owner,a);
            for (std::uint64_t a : it->second) if (!now.count(a)) fire_inst_sig(7,owner,a);
            it->second.swap(now);
        }
    }
    if (!watch_desc.empty()&&(g_poll_tick%8)==0) {
        for (std::uint64_t owner : watch_desc) {
            if (!Mem::Get().IsValid(owner)) continue;
            std::unordered_set<std::uint64_t> now; size_t nodes=0;
            collect_desc(owner,now,nodes,0);
            auto it=g_desc_snap.find(owner);
            if (it==g_desc_snap.end()) { g_desc_snap[owner]=std::move(now); continue; }
            int fired=0; bool capped=false;
            for (std::uint64_t a : now) {
                if (it->second.count(a)) continue;
                if (fired>=32) { capped=true; break; }
                fire_inst_sig(8,owner,a); ++fired; it->second.insert(a);
            }
            if (!capped) it->second.swap(now);
        }
    }
}


std::string read_prop_snap(std::uint64_t addr, const std::string& prop) {
    if (!Mem::Get().IsValid(addr)||prop.empty()) return {};
    if (prop=="Name") return RoReadName(addr);
    if (prop=="ClassName") return RoReadClassName(addr);
    if (prop=="Parent") {
        uintptr_t p = Mem::Get().Read<uintptr_t>(addr + Offsets::Instance::Parent);
        return std::to_string(p);
    }
    if (RoReadClassName(addr)=="Humanoid") {
        if (prop=="Health") return std::to_string(Mem::Get().Read<float>(addr+Offsets::Humanoid::Health));
        if (prop=="MaxHealth") return std::to_string(Mem::Get().Read<float>(addr+Offsets::Humanoid::MaxHealth));
        if (prop=="WalkSpeed") return std::to_string(Mem::Get().Read<float>(addr+Offsets::Humanoid::WalkSpeed));
    }
    return {};
}

void poll_props() {
    if (!g_L) return;
    struct key_t { std::uint64_t addr; std::string prop; std::string mapk; };
    std::vector<key_t> keys; std::unordered_set<std::string> seen;
    auto add = [&](std::uint64_t a, const std::string& p) {
        if (!a||p.empty()) return;
        std::string mapk = std::to_string(a)+"|"+p;
        if (!seen.insert(mapk).second) return;
        keys.push_back({a,p,std::move(mapk)});
    };
    for (const auto& c : g_conns) if (c.alive&&c.kind==9) add(c.owner,c.prop);
    for (const auto& w : g_sigwaits) if (w.alive&&w.kind==9) add(w.owner,w.prop);
    if (keys.empty()) { g_prop_snap.clear(); g_prop_cursor=0; return; }
    if (g_prop_snap.size()>keys.size())
        for (auto it=g_prop_snap.begin();it!=g_prop_snap.end();)
            it=seen.count(it->first)?std::next(it):g_prop_snap.erase(it);
    if (g_prop_cursor>=keys.size()) g_prop_cursor=0;
    const size_t budget = keys.size()<k_max_prop_polls?keys.size():k_max_prop_polls;
    for (size_t n=0;n<budget;++n) {
        const key_t& k=keys[g_prop_cursor];
        g_prop_cursor=(g_prop_cursor+1)%keys.size();
        const std::string cur=read_prop_snap(k.addr,k.prop);
        auto it=g_prop_snap.find(k.mapk);
        if (it==g_prop_snap.end()) { g_prop_snap.emplace(k.mapk,cur); continue; }
        if (it->second==cur) continue;
        it->second=cur;
        fire_sig(9,k.addr,k.prop.c_str(),0);
    }
}


void RegisterRunService(lua_State* L) {
    lua_newtable(L);
    push_signal(L,0,0,nullptr); lua_setfield(L,-2,"Heartbeat");
    push_signal(L,0,0,nullptr); lua_setfield(L,-2,"RenderStepped");
    push_signal(L,0,0,nullptr); lua_setfield(L,-2,"Stepped");
    lua_setglobal(L,"RunService");
}
void RegisterUserInputService(lua_State* L) {
    lua_newtable(L);
    push_signal(L,4,0,nullptr); lua_setfield(L,-2,"InputBegan");
    push_signal(L,5,0,nullptr); lua_setfield(L,-2,"InputEnded");
    lua_setglobal(L,"UserInputService");
}


static std::uint32_t bit_u32(lua_State* L,int i) { return (std::uint32_t)(std::int64_t)luaL_checknumber(L,i); }
int l_bit_band(lua_State* L){int n=lua_gettop(L);std::uint32_t r=0xFFFFFFFFu;for(int i=1;i<=n;++i)r&=bit_u32(L,i);lua_pushinteger(L,(lua_Integer)r);return 1;}
int l_bit_bor(lua_State* L){int n=lua_gettop(L);std::uint32_t r=0;for(int i=1;i<=n;++i)r|=bit_u32(L,i);lua_pushinteger(L,(lua_Integer)r);return 1;}
int l_bit_bxor(lua_State* L){int n=lua_gettop(L);std::uint32_t r=0;for(int i=1;i<=n;++i)r^=bit_u32(L,i);lua_pushinteger(L,(lua_Integer)r);return 1;}
int l_bit_bnot(lua_State* L){lua_pushinteger(L,(lua_Integer)~bit_u32(L,1));return 1;}
int l_bit_lshift(lua_State* L){std::uint32_t x=bit_u32(L,1);int d=(int)luaL_checkinteger(L,2)&31;lua_pushinteger(L,(lua_Integer)(x<<d));return 1;}
int l_bit_rshift(lua_State* L){std::uint32_t x=bit_u32(L,1);int d=(int)luaL_checkinteger(L,2)&31;lua_pushinteger(L,(lua_Integer)(x>>d));return 1;}
int l_bit_arshift(lua_State* L){std::int32_t x=(std::int32_t)bit_u32(L,1);int d=(int)luaL_checkinteger(L,2)&31;lua_pushinteger(L,(lua_Integer)(std::uint32_t)(x>>d));return 1;}
int l_bit_lrotate(lua_State* L){std::uint32_t x=bit_u32(L,1);int d=(int)luaL_checkinteger(L,2)&31;lua_pushinteger(L,(lua_Integer)((x<<d)|(x>>((32-d)&31))));return 1;}
int l_bit_rrotate(lua_State* L){std::uint32_t x=bit_u32(L,1);int d=(int)luaL_checkinteger(L,2)&31;lua_pushinteger(L,(lua_Integer)((x>>d)|(x<<((32-d)&31))));return 1;}
int l_bit_extract(lua_State* L){std::uint32_t n=bit_u32(L,1);int f=(int)luaL_checkinteger(L,2);int w=(int)luaL_optinteger(L,3,1);if(f<0)f=0;if(f>31)f=31;if(w<1)w=1;if(w>32-f)w=32-f;std::uint32_t mask=(w>=32)?0xFFFFFFFFu:((1u<<w)-1u);lua_pushinteger(L,(lua_Integer)((n>>f)&mask));return 1;}
int l_bit_replace(lua_State* L){std::uint32_t n=bit_u32(L,1);std::uint32_t v=bit_u32(L,2);int f=(int)luaL_checkinteger(L,3);int w=(int)luaL_optinteger(L,4,1);if(f<0)f=0;if(f>31)f=31;if(w<1)w=1;if(w>32-f)w=32-f;std::uint32_t mask=(w>=32)?0xFFFFFFFFu:((1u<<w)-1u);n=(n&~(mask<<f))|((v&mask)<<f);lua_pushinteger(L,(lua_Integer)n);return 1;}
void RegisterBit32(lua_State* L) {
    lua_newtable(L);
    struct { const char* n; lua_CFunction f; } t[] = {
        {"band",l_bit_band},{"bor",l_bit_bor},{"bxor",l_bit_bxor},{"bnot",l_bit_bnot},
        {"lshift",l_bit_lshift},{"rshift",l_bit_rshift},{"arshift",l_bit_arshift},
        {"lrotate",l_bit_lrotate},{"rrotate",l_bit_rrotate},
        {"extract",l_bit_extract},{"replace",l_bit_replace},
    };
    for (auto& e:t) { lua_pushcfunction(L,e.f); lua_setfield(L,-2,e.n); }
    lua_setglobal(L,"bit32");
}


int l_tween_play(lua_State* L){(void)L;return 0;}
int l_tween_create(lua_State* L){
    (void)luaL_checkany(L,1);
    lua_newtable(L);
    lua_pushcfunction(L,l_tween_play); lua_setfield(L,-2,"Play");
    lua_pushcfunction(L,l_tween_play); lua_setfield(L,-2,"Cancel");
    lua_pushcfunction(L,l_tween_play); lua_setfield(L,-2,"Pause");
    lua_pushcfunction(L,l_tween_play); lua_setfield(L,-2,"Destroy");
    push_signal(L,99,0,nullptr); lua_setfield(L,-2,"Completed");
    return 1;
}
void RegisterTweenService(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L,l_tween_create); lua_setfield(L,-2,"Create");
    lua_setglobal(L,"TweenService");
}


bool parse_url(const std::string& url, bool& https, std::wstring& host, INTERNET_PORT& port, std::wstring& path) {
    https=false; host.clear(); path=L"/"; port=80;
    std::string u=url;
    if (u.rfind("https://",0)==0){https=true;port=443;u=u.substr(8);}
    else if (u.rfind("http://",0)==0) u=u.substr(7);
    else return false;
    size_t slash=u.find('/');
    std::string h=(slash==std::string::npos)?u:u.substr(0,slash);
    std::string p=(slash==std::string::npos)?"/":u.substr(slash);
    size_t colon=h.find(':');
    if (colon!=std::string::npos){port=(INTERNET_PORT)std::atoi(h.c_str()+colon+1);h=h.substr(0,colon);}
    host.assign(h.begin(),h.end()); path.assign(p.begin(),p.end());
    return !host.empty();
}
bool http_request_raw(const std::string& method,const std::string& url,const std::string& body,const std::string& hdrs,std::string& out,int& status){
    out.clear();status=0;
    bool https=false; std::wstring host,path; INTERNET_PORT port=80;
    if(!parse_url(url,https,host,port,path))return false;
    HINTERNET ses=WinHttpOpen(L"Ivory/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!ses)return false;
    HINTERNET con=WinHttpConnect(ses,host.c_str(),port,0);
    if(!con){WinHttpCloseHandle(ses);return false;}
    std::wstring meth(method.begin(),method.end());
    DWORD flags=https?WINHTTP_FLAG_SECURE:0;
    HINTERNET req=WinHttpOpenRequest(con,meth.c_str(),path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!req){WinHttpCloseHandle(con);WinHttpCloseHandle(ses);return false;}
    WinHttpSetTimeouts(req,5000,5000,15000,15000);
    std::wstring wh; if(!hdrs.empty())wh.assign(hdrs.begin(),hdrs.end());
    BOOL ok=WinHttpSendRequest(req,wh.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:wh.c_str(),wh.empty()?0:(DWORD)(-1L),body.empty()?WINHTTP_NO_REQUEST_DATA:(LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0);
    if(!ok||!WinHttpReceiveResponse(req,nullptr)){WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);return false;}
    DWORD st=0,sz=sizeof(st);
    WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&st,&sz,WINHTTP_NO_HEADER_INDEX);
    status=(int)st;
    for(;;){DWORD avail=0;if(!WinHttpQueryDataAvailable(req,&avail)||avail==0)break;size_t old=out.size();out.resize(old+avail);DWORD read=0;if(!WinHttpReadData(req,out.data()+old,avail,&read)){out.resize(old);break;}out.resize(old+read);if(out.size()>(8u<<20))break;}
    WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);
    return true;
}
void push_http_result(lua_State* L,bool ok,int status,const std::string& body){
    lua_newtable(L);
    lua_pushboolean(L,ok&&status>=200&&status<300); lua_setfield(L,-2,"Success");
    lua_pushinteger(L,status); lua_setfield(L,-2,"StatusCode");
    lua_pushlstring(L,body.data(),body.size()); lua_setfield(L,-2,"Body");
    lua_pushstring(L,ok?"OK":"failed"); lua_setfield(L,-2,"StatusMessage");
    lua_newtable(L); lua_setfield(L,-2,"Headers");
}
int l_request(lua_State* L){
    luaL_checktype(L,1,LUA_TTABLE);
    std::string url; lua_getfield(L,1,"Url"); if(const char*u=lua_tostring(L,-1))url=u; lua_pop(L,1);
    if(url.empty()){lua_getfield(L,1,"url");if(const char*u=lua_tostring(L,-1))url=u;lua_pop(L,1);}
    if(url.empty())return luaL_error(L,"request: Url required");
    std::string method="GET"; lua_getfield(L,1,"Method"); if(lua_isstring(L,-1))method=lua_tostring(L,-1); lua_pop(L,1);
    std::string body; lua_getfield(L,1,"Body"); if(lua_isstring(L,-1)){size_t n=0;const char*b=lua_tolstring(L,-1,&n);body.assign(b,n);}lua_pop(L,1);
    std::string hdrs; lua_getfield(L,1,"Headers"); if(lua_istable(L,-1)){lua_pushnil(L);while(lua_next(L,-2)){if(lua_type(L,-2)==LUA_TSTRING&&lua_type(L,-1)==LUA_TSTRING){hdrs+=lua_tostring(L,-2);hdrs+=": ";hdrs+=lua_tostring(L,-1);hdrs+="\r\n";}lua_pop(L,1);}}lua_pop(L,1);
    std::string out; int status=0; bool ok=http_request_raw(method,url,body,hdrs,out,status);
    push_http_result(L,ok,status,out); return 1;
}
int l_httpget(lua_State* L){
    const char* url=luaL_checkstring(L,1); int status=0; std::string out;
    if(http_request_raw("GET",url,{},{},out,status)&&status>=200&&status<300){lua_pushlstring(L,out.data(),out.size());return 1;}
    return luaL_error(L,"HttpGet failed (%d)",status);
}
int l_hs_arg(lua_State* L){return (lua_istable(L,1)&&lua_gettop(L)>=2)?2:1;}
int l_hs_getasync(lua_State* L){const int i=l_hs_arg(L);lua_pushvalue(L,i);lua_replace(L,1);lua_settop(L,1);return l_httpget(L);}
int l_hs_requestasync(lua_State* L){const int i=l_hs_arg(L);if(i==2)lua_remove(L,1);return l_request(L);}

int l_json_encode(lua_State* L){
    const int i=l_hs_arg(L); int t=lua_type(L,i);
    if(t==LUA_TNIL||lua_gettop(L)<i){lua_pushstring(L,"null");return 1;}
    if(t==LUA_TBOOLEAN){lua_pushstring(L,lua_toboolean(L,i)?"true":"false");return 1;}
    if(t==LUA_TNUMBER){char buf[64];std::snprintf(buf,sizeof(buf),"%.17g",lua_tonumber(L,i));lua_pushstring(L,buf);return 1;}
    if(t==LUA_TSTRING){size_t n=0;const char*s=lua_tolstring(L,i,&n);std::string o="\"";for(size_t k=0;k<n;++k){char c=s[k];if(c=='"'||c=='\\'){o.push_back('\\');o.push_back(c);}else if(c=='\n')o+="\\n";else o.push_back(c);}o.push_back('"');lua_pushlstring(L,o.data(),o.size());return 1;}
    lua_pushstring(L,"null"); return 1;
}
int l_json_decode(lua_State* L){
    const char*s=luaL_checkstring(L,l_hs_arg(L));
    if(!s||std::strcmp(s,"null")==0){lua_pushnil(L);return 1;}
    if(std::strcmp(s,"true")==0){lua_pushboolean(L,1);return 1;}
    if(std::strcmp(s,"false")==0){lua_pushboolean(L,0);return 1;}
    if(s[0]=='"'){std::string o;for(const char*p=s+1;*p&&*p!='"';++p){if(*p=='\\'&&p[1]){++p;if(*p=='n')o.push_back('\n');else o.push_back(*p);}else o.push_back(*p);}lua_pushlstring(L,o.data(),o.size());return 1;}
    char*end=nullptr;double d=std::strtod(s,&end);if(end!=s){lua_pushnumber(L,d);return 1;}
    lua_pushnil(L);return 1;
}
void RegisterHttp(lua_State* L) {
    lua_pushcfunction(L,l_request); lua_setglobal(L,"request");
    lua_pushcfunction(L,l_request); lua_setglobal(L,"http_request");
    lua_pushcfunction(L,l_httpget); lua_setglobal(L,"HttpGet");
    lua_newtable(L);
    lua_pushcfunction(L,l_hs_getasync); lua_setfield(L,-2,"GetAsync");
    lua_pushcfunction(L,l_hs_requestasync); lua_setfield(L,-2,"RequestAsync");
    lua_pushcfunction(L,l_json_encode); lua_setfield(L,-2,"JSONEncode");
    lua_pushcfunction(L,l_json_decode); lua_setfield(L,-2,"JSONDecode");
    lua_setglobal(L,"HttpService");
}


void RegisterApi(lua_State* L) {
    lua_pushcfunction(L,l_print);            lua_setglobal(L,"print");
    lua_pushcfunction(L,l_warn);             lua_setglobal(L,"warn");
    lua_pushcfunction(L,l_identifyexecutor); lua_setglobal(L,"identifyexecutor");
    lua_pushcfunction(L,l_wait);             lua_setglobal(L,"wait");
    lua_pushcfunction(L,l_spawn);            lua_setglobal(L,"spawn");
    lua_pushcfunction(L,l_getmousepos);      lua_setglobal(L,"getmousepos");
    lua_pushcfunction(L,l_isrbxactive);      lua_setglobal(L,"isrbxactive");
    lua_pushcfunction(L,l_iskeypressed);     lua_setglobal(L,"iskeypressed");

    luaL_newmetatable(L,"Ivory.Task"); lua_pop(L,1);

    lua_newtable(L);
    lua_pushcfunction(L,l_wait);   lua_setfield(L,-2,"wait");
    lua_pushcfunction(L,l_spawn);  lua_setfield(L,-2,"spawn");
    lua_pushcfunction(L,l_delay);  lua_setfield(L,-2,"delay");
    lua_pushcfunction(L,l_defer);  lua_setfield(L,-2,"defer");
    lua_pushcfunction(L,l_cancel); lua_setfield(L,-2,"cancel");
    lua_setglobal(L,"task");

    LuaTypes::Register(L);
    LuaDrawing::Register(L);
    LuaGc::Register(L);
    LuaMem::Register(L);
    LuaBridge::Register(L);
    RegisterRunService(L);
    RegisterUserInputService(L);
    RegisterBit32(L);
    RegisterTweenService(L);
    RegisterHttp(L);
}

} 


void PushSignal(lua_State* L, int kind, std::uint64_t owner, const char* prop) {
    push_signal(L, kind, owner, prop);
}
void FireSignal(lua_State* L, int kind, std::uint64_t owner, const char* prop) {
    (void)L; (void)kind; (void)owner; (void)prop; 
}
void ScheduleWait(lua_State* L, float sec) {
    schedule_wait(L, sec);
}
static std::vector<LogEntry> g_vm_logs;
static std::mutex g_vm_log_mu;

void LogOutput(const char* msg, bool is_error) {
    if (msg) {
        std::lock_guard<std::mutex> lk(g_vm_log_mu);
        // Cap the log so a chatty script can't grow it without bound (and keep
        // the per-frame GetLogs() copy cheap).
        if (g_vm_logs.size() >= 1000) {
            g_vm_logs.erase(g_vm_logs.begin());
        }
        g_vm_logs.push_back({ msg, is_error });
    }
    if (is_error) LogErr(msg); else LogInfo(msg);
}

std::vector<LogEntry> GetLogs() {
    std::lock_guard<std::mutex> lk(g_vm_log_mu);
    return g_vm_logs;
}

void ClearLogs() {
    std::lock_guard<std::mutex> lk(g_vm_log_mu);
    g_vm_logs.clear();
}


bool g_show_standalone_executor = true;

void RenderStandaloneWindow() {
    if (!g_show_standalone_executor) return;

    static char g_standalone_buf[8192] =
        "-- Ivory Standalone Executor\n"
        "print('Hello from Ivory Standalone Window!')\n";

    ImGui::SetNextWindowSize(ImVec2(640, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lua 9.7 ", &g_show_standalone_executor, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Lua Script Editor:");
        ImGui::InputTextMultiline("##standalone_ed", g_standalone_buf, sizeof(g_standalone_buf), ImVec2(-1, ImGui::GetContentRegionAvail().y - 36));

        ImGui::Spacing();
        if (ImGui::Button("Execute Script", ImVec2(130, 26))) {
            LuaVM::Execute(g_standalone_buf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Editor", ImVec2(100, 26))) {
            g_standalone_buf[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Wipe Drawing", ImVec2(110, 26))) {
            LuaDrawing::Clear();
        }
    }
    ImGui::End();
}

void wipe_script_signals() {
    if (!g_L) return;
    for (auto& c:g_conns){if(c.fn_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,c.fn_ref);c.fn_ref=LUA_NOREF;c.alive=false;}g_conns.clear();
    for (auto& w:g_sigwaits){if(w.thr_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,w.thr_ref);w.thr_ref=LUA_NOREF;w.alive=false;}g_sigwaits.clear();
    for (auto& w:g_waits){if(w.ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,w.ref);}g_waits.clear();
    for (auto& d:g_delays){if(d.fn_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,d.fn_ref);d.fn_ref=LUA_NOREF;d.alive=false;}g_delays.clear();
    g_kids_snap.clear();g_desc_snap.clear();g_prop_snap.clear();g_prop_cursor=0;
    lua_gc(g_L,LUA_GCCOLLECT,0);
}

void ExecuteLocked(const std::string& source, const std::string& name) {
    if (!g_L) { LogErr("lua vm init failed"); return; }
    wipe_script_signals();
    LuaBridge::RefreshGlobals(g_L);
    if (luaL_loadbuffer(g_L, source.data(), source.size(), name.c_str()) != LUA_OK) {
        LogErr(lua_tostring(g_L,-1)); lua_pop(g_L,1); return;
    }
    lua_State* co = lua_newthread(g_L);
    lua_pushvalue(g_L,-2); lua_xmove(g_L,co,1); lua_pop(g_L,1);
    int nres=0;
    const int status=lua_resume(co,g_L,0,&nres);
    if (status==LUA_OK) {
        lua_pop(g_L,1);
        LogInfo("[ok]");
        return;
    }
    if (status==LUA_YIELD) { lua_pop(g_L,1); return; }
    LogErr(lua_tostring(co,-1));
    lua_pop(co,1); lua_pop(g_L,1);
}


void TickLoop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    while (g_tick_run.load()) {
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now-last).count();
        last = now;
        if (dt<0.f) dt=0.f;
        if (dt>0.1f) dt=0.1f;
        Tick(dt);
        Sleep(4);
    }
}


bool Initialize() {
    if (g_L) return true;
    g_L = luaL_newstate();
    if (!g_L) return false;
    OpenSafeLibs(g_L);
    RegisterApi(g_L);
    if (!g_tick_run.load()) {
        g_tick_run.store(true);
        g_tick_th = std::thread(TickLoop);
    }
    return true;
}

void Shutdown() {
    g_tick_run.store(false);
    if (g_tick_th.joinable()) g_tick_th.join();
    for (int i=0;i<200&&g_busy.load();++i) Sleep(10);
    std::lock_guard<std::mutex> lock(g_lua_mu);
    LuaDrawing::Clear();
    if (!g_L) return;
    for (auto& w:g_waits)  if(w.ref!=LUA_NOREF) luaL_unref(g_L,LUA_REGISTRYINDEX,w.ref);
    g_waits.clear();
    for (auto& c:g_conns)  { if(c.fn_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,c.fn_ref);c.fn_ref=LUA_NOREF;c.alive=false; }
    g_conns.clear();
    for (auto& w:g_sigwaits){ if(w.thr_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,w.thr_ref);w.thr_ref=LUA_NOREF;w.alive=false; }
    g_sigwaits.clear();
    for (auto& d:g_delays)  { if(d.fn_ref!=LUA_NOREF)luaL_unref(g_L,LUA_REGISTRYINDEX,d.fn_ref);d.fn_ref=LUA_NOREF;d.alive=false; }
    g_delays.clear();
    g_plr_seeded=false; g_seen_plr.clear(); g_seen_char.clear();
    g_input_seeded=false; std::memset(g_key_down,0,sizeof(g_key_down));
    g_kids_snap.clear(); g_desc_snap.clear(); g_prop_snap.clear(); g_prop_cursor=0;
    g_poll_tick=0;
    lua_close(g_L); g_L=nullptr;
}

lua_State* State() { return g_L; }
bool Ready() { return g_L != nullptr; }

bool Execute(const std::string& source, const char* chunk_name) {
    if (!Initialize()) { LogErr("lua vm init failed"); return false; }
    bool expected=false;
    if (!g_busy.compare_exchange_strong(expected,true)) { LogErr("script already running"); return false; }
    std::string src=source, name=chunk_name?chunk_name:"script";
    std::thread([src=std::move(src),name=std::move(name)](){
        { std::lock_guard<std::mutex> lock(g_lua_mu); ExecuteLocked(src,name); }
        g_busy.store(false);
    }).detach();
    return true;
}

void Tick(float dt) {
    std::unique_lock<std::mutex> lock(g_lua_mu, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (!g_L) return;
    ++g_poll_tick;
    poll_players();
    poll_input();
    poll_hierarchy();
    if ((g_poll_tick%2)==0) poll_props();

    
    { lua_pushnumber(g_L, dt); fire_sig(0, 0, nullptr, 1); }

    
    const size_t ndelay=g_delays.size();
    for (size_t i=0;i<ndelay&&i<g_delays.size();++i) {
        if (!g_delays[i].alive||g_delays[i].fn_ref==LUA_NOREF) continue;
        g_delays[i].left-=dt;
        if (g_delays[i].left>0.f) continue;
        const int fn=g_delays[i].fn_ref;
        g_delays[i].alive=false; g_delays[i].fn_ref=LUA_NOREF;
        lua_rawgeti(g_L,LUA_REGISTRYINDEX,fn);
        luaL_unref(g_L,LUA_REGISTRYINDEX,fn);
        if (lua_pcall(g_L,0,0,0)!=LUA_OK) { LogErr(lua_tostring(g_L,-1)); lua_pop(g_L,1); }
    }

    if (g_waits.empty()) return;
    constexpr int k_max_resumes=64;
    static std::vector<int> ready; ready.clear();
    for (size_t i=0;i<g_waits.size();) {
        g_waits[i].left-=dt;
        if (g_waits[i].left>0.f) { ++i; continue; }
        if ((int)ready.size()>=k_max_resumes) { g_waits[i].left=0.f; ++i; continue; }
        ready.push_back(g_waits[i].ref);
        g_waits.erase(g_waits.begin()+(std::ptrdiff_t)i);
    }
    for (int ref:ready) {
        lua_rawgeti(g_L,LUA_REGISTRYINDEX,ref);
        lua_State* co=lua_tothread(g_L,-1);
        luaL_unref(g_L,LUA_REGISTRYINDEX,ref);
        if (!co) { lua_pop(g_L,1); continue; }
        int nres=0;
        const int status=lua_resume(co,g_L,0,&nres);
        if (status==LUA_OK) {}
        else if (status!=LUA_YIELD) { LogErr(lua_tostring(co,-1)); lua_pop(co,1); }
        lua_pop(g_L,1);
    }
}

}