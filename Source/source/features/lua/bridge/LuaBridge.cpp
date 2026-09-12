#include <features/lua/bridge/LuaBridge.h>
#include <features/lua/vm/LuaVM.h>
#include <features/lua/types/LuaTypes.h>
#include <features/instance.new/creation/InstanceCreate.h>
#include <features/instance.new/creation/InstanceNew.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/cache.h>
#include <sdk/offsets/offsets.h>
#include <sdk/math/math.h>

extern "C" {
#include <lua.hpp>
}

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace LuaBridge {
namespace {

constexpr const char* k_mt = "Ivory.Instance";

struct LuaInstance { uintptr_t address; };

LuaInstance* CheckInst(lua_State* L, int idx = 1) {
    return (LuaInstance*)luaL_checkudata(L, idx, k_mt);
}
bool ValidAddr(uintptr_t addr) {
    return addr != 0 && Mem::Get().IsValid(addr);
}

static std::string GetClassName(uintptr_t addr) {
    if (!addr) return {};
    uintptr_t desc = Mem::Get().Read<uintptr_t>(addr + Offsets::Instance::ClassDescriptor);
    if (!desc) return {};
    uintptr_t namePtr = Mem::Get().Read<uintptr_t>(desc + Offsets::Instance::ClassName);
    if (!namePtr) return {};
    return Mem::Get().ReadString(namePtr);
}
static std::string GetName(uintptr_t addr) { return Mem::Get().GetInstanceName(addr); }
static uintptr_t GetParent(uintptr_t addr) { return addr ? Mem::Get().Read<uintptr_t>(addr + Offsets::Instance::Parent) : 0; }

bool IsBasePartClass(const std::string& cls) {
    return cls=="Part"||cls=="MeshPart"||cls=="BasePart"||cls=="UnionOperation"||
           cls=="TrussPart"||cls=="WedgePart"||cls=="CornerWedgePart"||
           cls=="SpawnLocation"||cls=="Seat"||cls=="VehicleSeat";
}
bool IsGuiObjectClass(const std::string& cls) {
    return cls=="GuiObject"||cls=="Frame"||cls=="TextLabel"||cls=="TextButton"||
           cls=="TextBox"||cls=="ImageLabel"||cls=="ImageButton"||
           cls=="ScrollingFrame"||cls=="ViewportFrame"||cls=="CanvasGroup"||cls=="VideoFrame";
}
bool ClassIsA(const std::string& cls, const char* query) {
    if (!query||!query[0]) return false;
    if (cls==query) return true;
    if (strcmp(query,"Instance")==0) return true;
    if (strcmp(query,"BasePart")==0||strcmp(query,"PVInstance")==0) return IsBasePartClass(cls);
    if (strcmp(query,"GuiObject")==0||strcmp(query,"GuiBase2d")==0||strcmp(query,"GuiBase")==0) return IsGuiObjectClass(cls);
    if (strcmp(query,"ValueBase")==0) return cls=="BoolValue"||cls=="IntValue"||cls=="NumberValue"||cls=="StringValue"||cls=="ObjectValue"||cls=="Vector3Value"||cls=="CFrameValue"||cls=="Color3Value";
    if (strcmp(query,"Model")==0) return cls=="Model"||cls=="WorldModel"||cls=="Actor";
    if (strcmp(query,"LuaSourceContainer")==0) return cls=="LocalScript"||cls=="Script"||cls=="ModuleScript";
    if (strcmp(query,"Accoutrement")==0) return cls=="Accoutrement"||cls=="Hat"||cls=="Accessory";
    if (strcmp(query,"LayerCollector")==0) return cls=="ScreenGui"||cls=="BillboardGui"||cls=="SurfaceGui";
    return false;
}


int l_tostring(lua_State* L) {
    LuaInstance* ud = CheckInst(L);
    if (!ValidAddr(ud->address)) { lua_pushstring(L,"Instance(nil)"); return 1; }
    char buf[256];
    std::snprintf(buf,sizeof(buf),"%s (%s) @ 0x%llX",
        GetName(ud->address).c_str(), GetClassName(ud->address).c_str(),
        (unsigned long long)ud->address);
    lua_pushstring(L, buf); return 1;
}

int l_eq(lua_State* L) {
    auto* a=(LuaInstance*)luaL_testudata(L,1,k_mt);
    auto* b=(LuaInstance*)luaL_testudata(L,2,k_mt);
    lua_pushboolean(L, a&&b&&a->address==b->address); return 1;
}


int l_GetChildren(lua_State* L) {
    LuaInstance* ud = CheckInst(L);
    lua_newtable(L);
    if (!ValidAddr(ud->address)) return 1;
    auto kids = Mem::Get().GetChildren(ud->address);
    int i=1;
    for (auto c : kids) {
        if (!ValidAddr(c)) continue;
        PushInstance(L, c); lua_rawseti(L,-2,i++);
    }
    return 1;
}


static uintptr_t FindChildNameOrClass(uintptr_t parent, const char* name) {
    if (!ValidAddr(parent)||!name||!name[0]) return 0;
    for (auto c : Mem::Get().GetChildren(parent)) {
        if (!ValidAddr(c)) continue;
        if (GetName(c)==name||GetClassName(c)==name) return c;
    }
    return 0;
}

int l_FindFirstChild(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* name=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)) { lua_pushnil(L); return 1; }
    uintptr_t hit=FindChildNameOrClass(ud->address,name);
    if (!hit) lua_pushnil(L); else PushInstance(L,hit);
    return 1;
}
int l_FindFirstChildOfClass(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* cls=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)) { lua_pushnil(L); return 1; }
    for (auto c : Mem::Get().GetChildren(ud->address)) {
        if (ValidAddr(c)&&GetClassName(c)==cls) { PushInstance(L,c); return 1; }
    }
    lua_pushnil(L); return 1;
}
int l_FindFirstChildWhichIsA(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* cls=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)) { lua_pushnil(L); return 1; }
    for (auto c : Mem::Get().GetChildren(ud->address)) {
        if (ValidAddr(c)&&ClassIsA(GetClassName(c),cls)) { PushInstance(L,c); return 1; }
    }
    lua_pushnil(L); return 1;
}
int l_FindFirstAncestorOfClass(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* cls=luaL_checkstring(L,2);
    uintptr_t cur=ud->address;
    for (int i=0;i<64;++i) {
        uintptr_t p=GetParent(cur); if(!ValidAddr(p)) break;
        if (ClassIsA(GetClassName(p),cls)) { PushInstance(L,p); return 1; }
        cur=p;
    }
    lua_pushnil(L); return 1;
}

int l_IsA(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* cls=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)) { lua_pushboolean(L,0); return 1; }
    lua_pushboolean(L, ClassIsA(GetClassName(ud->address),cls)); return 1;
}
int l_IsDescendantOf(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    LuaInstance* anc=(LuaInstance*)luaL_testudata(L,2,k_mt);
    if (!ValidAddr(ud->address)||!anc||!ValidAddr(anc->address)) { lua_pushboolean(L,0); return 1; }
    uintptr_t cur=ud->address;
    for (int i=0;i<64;++i) {
        uintptr_t p=GetParent(cur); if(!ValidAddr(p)) break;
        if (p==anc->address) { lua_pushboolean(L,1); return 1; }
        cur=p;
    }
    lua_pushboolean(L,0); return 1;
}

int l_GetPropertyChangedSignal(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* prop=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)||!prop) { lua_pushnil(L); return 1; }
    LuaVM::PushSignal(L, 9, ud->address, prop); return 1;
}


struct wfc_t { uintptr_t parent; float left; char name[128]; };
constexpr float k_wfc_poll=0.05f;
constexpr int k_wfc_slot=4;

int wfc_cont(lua_State* L, int status, lua_KContext ctx);
int l_WaitForChild(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* name=luaL_checkstring(L,2);
    float timeout=(float)luaL_optnumber(L,3,5.0);
    if (timeout<0.f)timeout=0.f; if(timeout>3600.f)timeout=3600.f;
    if (!ValidAddr(ud->address)||!name) { lua_pushnil(L); return 1; }
    uintptr_t hit=FindChildNameOrClass(ud->address,name);
    if (hit) { PushInstance(L,hit); return 1; }
    if (timeout<=0.f||!lua_isyieldable(L)) { lua_pushnil(L); return 1; }
    lua_settop(L,3);
    wfc_t* w=(wfc_t*)lua_newuserdatauv(L,sizeof(wfc_t),0);
    w->parent=ud->address; w->left=timeout;
    size_t n=strlen(name); if(n>=sizeof(w->name))n=sizeof(w->name)-1;
    memcpy(w->name,name,n); w->name[n]=0;
    LuaVM::ScheduleWait(L,k_wfc_poll);
    return lua_yieldk(L,0,0,wfc_cont);
}
int wfc_cont(lua_State* L, int, lua_KContext) {
    wfc_t* w=(wfc_t*)lua_touserdata(L,k_wfc_slot);
    if (!w) { lua_pushnil(L); return 1; }
    uintptr_t hit=FindChildNameOrClass(w->parent,w->name);
    if (hit) { PushInstance(L,hit); return 1; }
    w->left-=k_wfc_poll;
    if (w->left<=0.f) { lua_pushnil(L); return 1; }
    LuaVM::ScheduleWait(L,k_wfc_poll);
    return lua_yieldk(L,0,0,wfc_cont);
}

int l_GetFullName(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    if (!ValidAddr(ud->address)) { lua_pushstring(L,""); return 1; }
    std::vector<std::string> parts;
    uintptr_t cur=ud->address;
    for (int i=0;i<64&&ValidAddr(cur);++i) {
        if (GetClassName(cur)=="DataModel") break;
        parts.push_back(GetName(cur));
        uintptr_t p=GetParent(cur); if(!ValidAddr(p)) break; cur=p;
    }
    std::string full;
    for (int i=(int)parts.size()-1;i>=0;--i) { if(!full.empty())full+='.'; full+=parts[i]; }
    lua_pushstring(L,full.c_str()); return 1;
}

int l_GetDescendants(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    lua_newtable(L);
    if (!ValidAddr(ud->address)) return 1;
    std::vector<uintptr_t> out;
    std::vector<uintptr_t> bfs={ud->address};
    int i=1;
    for (size_t bi=0; bi<bfs.size()&&(int)out.size()<2000; ++bi) {
        for (auto c : Mem::Get().GetChildren(bfs[bi])) {
            if (!ValidAddr(c)) continue;
            out.push_back(c); bfs.push_back(c);
            if((int)out.size()>=2000) break;
        }
    }
    for (auto addr : out) { PushInstance(L,addr); lua_rawseti(L,-2,i++); }
    return 1;
}

int l_GetService(lua_State* L) {
    LuaInstance* ud=CheckInst(L); const char* name=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)||!name) { lua_pushnil(L); return 1; }
    
    if (_stricmp(name,"RunService")==0)       { lua_getglobal(L,"RunService");       return 1; }
    if (_stricmp(name,"UserInputService")==0) { lua_getglobal(L,"UserInputService"); return 1; }
    if (_stricmp(name,"TweenService")==0)     { lua_getglobal(L,"TweenService");     return 1; }
    if (_stricmp(name,"HttpService")==0)      { lua_getglobal(L,"HttpService");       return 1; }
    
    for (auto c : Mem::Get().GetChildren(ud->address)) {
        if (!ValidAddr(c)) continue;
        if (GetClassName(c)==name||GetName(c)==name) { PushInstance(L,c); return 1; }
    }
    lua_pushnil(L); return 1;
}

int l_GetPlayers(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    lua_newtable(L);
    if (!ValidAddr(ud->address)) return 1;
    int i=1;
    for (auto c : Mem::Get().GetChildren(ud->address)) {
        if (ValidAddr(c)&&GetClassName(c)=="Player") { PushInstance(L,c); lua_rawseti(L,-2,i++); }
    }
    return 1;
}


static Vector2 WorldToScreen(const Vector3& pos, const Matrix4& vm, const Vector2& dims) {
    float x = vm.m[0][0] * pos.x + vm.m[0][1] * pos.y + vm.m[0][2] * pos.z + vm.m[0][3];
    float y = vm.m[1][0] * pos.x + vm.m[1][1] * pos.y + vm.m[1][2] * pos.z + vm.m[1][3];
    float z = vm.m[2][0] * pos.x + vm.m[2][1] * pos.y + vm.m[2][2] * pos.z + vm.m[2][3];
    float w = vm.m[3][0] * pos.x + vm.m[3][1] * pos.y + vm.m[3][2] * pos.z + vm.m[3][3];

    if (w < 0.1f) return Vector2{ -1.f, -1.f };

    float ndc_x = x / w;
    float ndc_y = y / w;

    float screen_x = (dims.x / 2.0f) * (1.0f + ndc_x);
    float screen_y = (dims.y / 2.0f) * (1.0f - ndc_y);

    return Vector2{ screen_x, screen_y };
}

int l_WorldToScreen(lua_State* L) {
    Vector3 v = LuaTypes::ToVector3(L, 1);
    uintptr_t visualEngine = Rbx::Get().VisualEngine;
    if (!visualEngine) {
        lua_pushnil(L);
        lua_pushboolean(L, false);
        return 2;
    }
    Matrix4 vm = Mem::Get().Read<Matrix4>(visualEngine + Offsets::VisualEngine::ViewMatrix);
    Vector2 dims = Mem::Get().Read<Vector2>(visualEngine + Offsets::VisualEngine::Dimensions);
    Vector2 s = WorldToScreen(v, vm, dims);
    if (s.x == -1.f && s.y == -1.f) {
        lua_pushnil(L);
        lua_pushboolean(L, false);
        return 2;
    }
    LuaTypes::PushVector2(L, s);
    lua_pushboolean(L, true);
    return 2;
}



int l_instance_new(lua_State* L) {
    const char* cls=luaL_checkstring(L,1);
    uintptr_t parent=0;
    if (lua_gettop(L)>=2&&!lua_isnil(L,2)) {
        LuaInstance* p=(LuaInstance*)luaL_testudata(L,2,k_mt);
        if (p) parent=p->address;
    }
    uintptr_t inst=InstanceNew::Create(std::string(cls), parent, nullptr, 2000);
    if (!inst) return luaL_error(L,"Instance.new: failed to create '%s' (fail=%d)",cls,InstanceCreate::LastFail());
    PushInstance(L,inst); return 1;
}


int l_index(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    const char* key=luaL_checkstring(L,2);

    
    luaL_getmetatable(L, k_mt);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L,-1)) { lua_remove(L,-2); return 1; }
    lua_pop(L, 2);

    if (!ValidAddr(ud->address)) { lua_pushnil(L); return 1; }

    
    if (strcmp(key,"Name")==0)        { lua_pushstring(L,GetName(ud->address).c_str()); return 1; }
    if (strcmp(key,"ClassName")==0)   { lua_pushstring(L,GetClassName(ud->address).c_str()); return 1; }
    if (strcmp(key,"Address")==0)     { lua_pushinteger(L,(lua_Integer)ud->address); return 1; }
    if (strcmp(key,"Parent")==0) {
        uintptr_t p=GetParent(ud->address);
        if (!ValidAddr(p)) lua_pushnil(L); else PushInstance(L,p);
        return 1;
    }
    if (strcmp(key,"ChildAdded")==0)      { LuaVM::PushSignal(L,6,ud->address); return 1; }
    if (strcmp(key,"ChildRemoved")==0)    { LuaVM::PushSignal(L,7,ud->address); return 1; }
    if (strcmp(key,"DescendantAdded")==0) { LuaVM::PushSignal(L,8,ud->address); return 1; }

    const std::string cls = GetClassName(ud->address);

    
    if (cls=="Highlight") {
        if (strcmp(key,"FillTransparency")==0)    { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Highlight::FillTransparency)); return 1; }
        if (strcmp(key,"OutlineTransparency")==0) { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Highlight::OutlineTransparency)); return 1; }
        if (strcmp(key,"FillColor")==0)           { Vector3 c=Mem::Get().Read<Vector3>(ud->address+Offsets::Highlight::FillColor); LuaTypes::PushColor3(L,c.x,c.y,c.z); return 1; }
        if (strcmp(key,"OutlineColor")==0)        { Vector3 c=Mem::Get().Read<Vector3>(ud->address+Offsets::Highlight::OutlineColor); LuaTypes::PushColor3(L,c.x,c.y,c.z); return 1; }
        if (strcmp(key,"Enabled")==0)             { lua_pushboolean(L,Mem::Get().Read<uint8_t>(ud->address+Offsets::Highlight::Enabled)?1:0); return 1; }
    }

    
    if (cls=="Humanoid") {
        if (strcmp(key,"Health")==0)     { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Humanoid::Health)); return 1; }
        if (strcmp(key,"MaxHealth")==0)  { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Humanoid::MaxHealth)); return 1; }
        if (strcmp(key,"WalkSpeed")==0)  { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Humanoid::WalkSpeed)); return 1; }
        if (strcmp(key,"JumpPower")==0)  { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::Humanoid::JumpPower)); return 1; }
        if (strcmp(key,"HumanoidStateType")==0) { lua_pushinteger(L,0); return 1; }
    }
    
    if (IsBasePartClass(cls)) {
        uintptr_t prim=Mem::Get().Read<uintptr_t>(ud->address+Offsets::BasePart::Primitive);
        if (strcmp(key,"Position")==0&&prim)  { Vector3 p=Mem::Get().Read<Vector3>(prim+Offsets::Primitive::Position); LuaTypes::PushVector3(L,p); return 1; }
        if (strcmp(key,"Size")==0&&prim)      { Vector3 s=Mem::Get().Read<Vector3>(prim+Offsets::Primitive::Size); LuaTypes::PushVector3(L,s); return 1; }
        if (strcmp(key,"Transparency")==0)    { lua_pushnumber(L,Mem::Get().Read<float>(ud->address+Offsets::BasePart::Transparency)); return 1; }
    }
    
    if (cls=="Player") {
        if (strcmp(key,"Name")==0)         { lua_pushstring(L,GetName(ud->address).c_str()); return 1; }
        if (strcmp(key,"Character")==0) {
            uintptr_t ch=Mem::Get().Read<uintptr_t>(ud->address+Offsets::Player::ModelInstance);
            if (ValidAddr(ch)) PushInstance(L,ch); else lua_pushnil(L);
            return 1;
        }
        if (strcmp(key,"CharacterAdded")==0) { LuaVM::PushSignal(L,3,ud->address); return 1; }
        if (strcmp(key,"UserId")==0) {
            uintptr_t uid=Mem::Get().Read<uintptr_t>(ud->address+Offsets::Player::UserId);
            lua_pushinteger(L,(lua_Integer)uid); return 1;
        }
    }
    
    if (cls=="Players") {
        if (strcmp(key,"LocalPlayer")==0) {
            uintptr_t lp=Mem::Get().Read<uintptr_t>(ud->address+Offsets::Player::LocalPlayer);
            if (ValidAddr(lp)) PushInstance(L,lp); else lua_pushnil(L);
            return 1;
        }
        if (strcmp(key,"PlayerAdded")==0)   { LuaVM::PushSignal(L,1,0); return 1; }
        if (strcmp(key,"PlayerRemoving")==0) { LuaVM::PushSignal(L,2,0); return 1; }
        if (strcmp(key,"GetPlayers")==0)    { lua_pushcfunction(L,l_GetPlayers); return 1; }
    }
    
    if (cls=="BoolValue"||cls=="IntValue"||cls=="NumberValue"||cls=="StringValue") {
        if (strcmp(key,"Value")==0) {
            if (cls=="BoolValue")   { lua_pushboolean(L,Mem::Get().Read<uint8_t>(ud->address+Offsets::Misc::Value)?1:0); return 1; }
            if (cls=="IntValue")    { lua_pushinteger(L,Mem::Get().Read<int32_t>(ud->address+Offsets::Misc::Value)); return 1; }
            if (cls=="NumberValue") { lua_pushnumber(L,Mem::Get().Read<double>(ud->address+Offsets::Misc::Value)); return 1; }
            if (cls=="StringValue") { lua_pushstring(L,Mem::Get().ReadString(ud->address+Offsets::Misc::Value).c_str()); return 1; }
        }
        if (strcmp(key,"Changed")==0) { LuaVM::PushSignal(L,9,ud->address,"Value"); return 1; }
    }

    
    uintptr_t child=FindChildNameOrClass(ud->address,key);
    if (child) { PushInstance(L,child); return 1; }

    lua_pushnil(L); return 1;
}


int l_newindex(lua_State* L) {
    LuaInstance* ud=CheckInst(L);
    const char* key=luaL_checkstring(L,2);
    if (!ValidAddr(ud->address)) return 0;
    const std::string cls=GetClassName(ud->address);

    if (strcmp(key,"Name")==0) {
        const char* v=luaL_checkstring(L,3);
        uintptr_t nameContainer=Mem::Get().Read<uintptr_t>(ud->address+Offsets::Instance::NameContainer);
        if (nameContainer) InstanceCreate::SetString(nameContainer+0x8,v);
        return 0;
    }
    if (strcmp(key,"Parent")==0) {
        uintptr_t parent=0;
        if (!lua_isnil(L,3)) { LuaInstance*p=(LuaInstance*)luaL_testudata(L,3,k_mt); if(p)parent=p->address; }
        InstanceCreate::SetParent(ud->address,parent);
        return 0;
    }
    if (cls=="Highlight") {
        if (strcmp(key,"FillTransparency")==0)    { Mem::Get().Write<float>(ud->address+Offsets::Highlight::FillTransparency,(float)luaL_checknumber(L,3)); return 0; }
        if (strcmp(key,"OutlineTransparency")==0) { Mem::Get().Write<float>(ud->address+Offsets::Highlight::OutlineTransparency,(float)luaL_checknumber(L,3)); return 0; }
        if (strcmp(key,"FillColor")==0)           { Vector3 v=LuaTypes::ToVector3(L,3); Mem::Get().Write<Vector3>(ud->address+Offsets::Highlight::FillColor,v); return 0; }
        if (strcmp(key,"OutlineColor")==0)        { Vector3 v=LuaTypes::ToVector3(L,3); Mem::Get().Write<Vector3>(ud->address+Offsets::Highlight::OutlineColor,v); return 0; }
        if (strcmp(key,"Enabled")==0)             { Mem::Get().Write<uint8_t>(ud->address+Offsets::Highlight::Enabled,lua_toboolean(L,3)?1:0); return 0; }
        if (strcmp(key,"DepthMode")==0)           { Mem::Get().Write<int32_t>(ud->address+Offsets::Highlight::DepthMode,(int32_t)luaL_checkinteger(L,3)); return 0; }
    }
    if (IsBasePartClass(cls)) {
        if (strcmp(key,"Transparency")==0) { Mem::Get().Write<float>(ud->address+Offsets::BasePart::Transparency,(float)luaL_checknumber(L,3)); return 0; }
        uintptr_t prim=Mem::Get().Read<uintptr_t>(ud->address+Offsets::BasePart::Primitive);
        if (prim&&strcmp(key,"Position")==0) { Vector3 v=LuaTypes::ToVector3(L,3); Mem::Get().Write<Vector3>(prim+Offsets::Primitive::Position,v); return 0; }
        if (prim&&strcmp(key,"Size")==0)     { Vector3 v=LuaTypes::ToVector3(L,3); Mem::Get().Write<Vector3>(prim+Offsets::Primitive::Size,v); return 0; }
    }
    if (cls=="Humanoid") {
        if (strcmp(key,"WalkSpeed")==0) { Mem::Get().Write<float>(ud->address+Offsets::Humanoid::WalkSpeed,(float)luaL_checknumber(L,3)); return 0; }
        if (strcmp(key,"JumpPower")==0) { Mem::Get().Write<float>(ud->address+Offsets::Humanoid::JumpPower,(float)luaL_checknumber(L,3)); return 0; }
        if (strcmp(key,"Health")==0)    { Mem::Get().Write<float>(ud->address+Offsets::Humanoid::Health,(float)luaL_checknumber(L,3)); return 0; }
    }
    if (cls=="StringValue"&&strcmp(key,"Value")==0) { InstanceCreate::SetString(ud->address+Offsets::Misc::Value,luaL_checkstring(L,3)); return 0; }
    if ((cls=="BoolValue")&&strcmp(key,"Value")==0) { Mem::Get().Write<uint8_t>(ud->address+Offsets::Misc::Value,lua_toboolean(L,3)?1:0); return 0; }
    if ((cls=="IntValue")&&strcmp(key,"Value")==0)  { Mem::Get().Write<int32_t>(ud->address+Offsets::Misc::Value,(int32_t)luaL_checkinteger(L,3)); return 0; }
    if ((cls=="NumberValue")&&strcmp(key,"Value")==0){ Mem::Get().Write<double>(ud->address+Offsets::Misc::Value,(double)luaL_checknumber(L,3)); return 0; }
    return 0;
}


int l_call(lua_State* L) {
    
    lua_pushvalue(L, 1); return 1;
}

} 


void PushInstance(lua_State* L, uintptr_t address) {
    auto* ud=(LuaInstance*)lua_newuserdata(L, sizeof(LuaInstance));
    ud->address=address;
    luaL_getmetatable(L, k_mt);
    lua_setmetatable(L, -2);
}

uintptr_t CheckAddress(lua_State* L, int idx) {
    return CheckInst(L, idx)->address;
}

void RefreshGlobals(lua_State* L) {
    uintptr_t dm=Rbx::Get().DataModel;
    if (ValidAddr(dm)) { PushInstance(L,dm); lua_setglobal(L,"game"); }
    else { lua_pushnil(L); lua_setglobal(L,"game"); }

    uintptr_t ws=Rbx::Get().ServicesData.WorkspaceService;
    if (!ValidAddr(ws)&&ValidAddr(dm)) ws=Mem::Get().Read<uintptr_t>(dm+Offsets::DataModel::Workspace);
    if (ValidAddr(ws)) { PushInstance(L,ws); lua_setglobal(L,"workspace"); PushInstance(L,ws); lua_setglobal(L,"Workspace"); }
    else { lua_pushnil(L); lua_setglobal(L,"workspace"); lua_pushnil(L); lua_setglobal(L,"Workspace"); }

    uintptr_t plrs=Rbx::Get().ServicesData.PlayersService;
    uintptr_t localPlayer=plrs?Mem::Get().Read<uintptr_t>(plrs+Offsets::Player::LocalPlayer):0;
    if (ValidAddr(localPlayer)) { PushInstance(L,localPlayer); lua_setglobal(L,"LocalPlayer"); }
    else { lua_pushnil(L); lua_setglobal(L,"LocalPlayer"); }
}

void Register(lua_State* L) {
    if (luaL_newmetatable(L, k_mt)) {
        lua_pushcfunction(L, l_index);    lua_setfield(L,-2,"__index");
        lua_pushcfunction(L, l_newindex); lua_setfield(L,-2,"__newindex");
        lua_pushcfunction(L, l_tostring); lua_setfield(L,-2,"__tostring");
        lua_pushcfunction(L, l_eq);       lua_setfield(L,-2,"__eq");
        
        lua_pushcfunction(L, l_GetChildren);             lua_setfield(L,-2,"GetChildren");
        lua_pushcfunction(L, l_GetDescendants);          lua_setfield(L,-2,"GetDescendants");
        lua_pushcfunction(L, l_FindFirstChild);          lua_setfield(L,-2,"FindFirstChild");
        lua_pushcfunction(L, l_FindFirstChildOfClass);   lua_setfield(L,-2,"FindFirstChildOfClass");
        lua_pushcfunction(L, l_FindFirstChildWhichIsA);  lua_setfield(L,-2,"FindFirstChildWhichIsA");
        lua_pushcfunction(L, l_FindFirstAncestorOfClass);lua_setfield(L,-2,"FindFirstAncestorOfClass");
        lua_pushcfunction(L, l_WaitForChild);            lua_setfield(L,-2,"WaitForChild");
        lua_pushcfunction(L, l_IsA);                     lua_setfield(L,-2,"IsA");
        lua_pushcfunction(L, l_IsDescendantOf);          lua_setfield(L,-2,"IsDescendantOf");
        lua_pushcfunction(L, l_GetFullName);             lua_setfield(L,-2,"GetFullName");
        lua_pushcfunction(L, l_GetService);              lua_setfield(L,-2,"GetService");
        lua_pushcfunction(L, l_GetPlayers);              lua_setfield(L,-2,"GetPlayers");
        lua_pushcfunction(L, l_GetPropertyChangedSignal);lua_setfield(L,-2,"GetPropertyChangedSignal");
    }
    lua_pop(L, 1);

    
    lua_newtable(L);
    lua_pushcfunction(L, l_instance_new); lua_setfield(L,-2,"new");
    lua_setglobal(L, "Instance");

    
    lua_pushcfunction(L, l_WorldToScreen); lua_setglobal(L, "WorldToScreen");
    lua_pushcfunction(L, l_WorldToScreen); lua_setglobal(L, "WorldToViewportPoint");

    RefreshGlobals(L);
}

} 


