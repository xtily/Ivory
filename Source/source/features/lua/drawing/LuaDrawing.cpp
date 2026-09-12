
#include <features/lua/drawing/LuaDrawing.h>
#include <imgui/imgui.h>

extern "C" {
#include <lua.hpp>

}

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <mutex>

namespace LuaDrawing {
namespace {

enum class DrawType { Line, Circle, Square, Text, Triangle, Quad, Image };

struct Color4 { float r, g, b, a; };

struct DrawObj {
    DrawType type;
    bool visible = true;
    bool destroyed = false;
    Color4 color{ 1,1,1,1 };
    float thickness = 1.f;
    float transparency = 0.f;
    int zindex = 0;

    float x1=0,y1=0,x2=0,y2=0;

    float cx=0,cy=0,radius=10.f;
    bool filled = false;
    int num_sides = 24;

    float sx=0,sy=0,sw=50,sh=50;

    std::string text;
    float font_size = 14.f;
    float tx=0,ty=0;
    bool outline = false;
    Color4 outline_color{0,0,0,1};
    bool center = false;

    float t1x=0,t1y=0,t2x=0,t2y=0,t3x=0,t3y=0;
};

std::vector<std::shared_ptr<DrawObj>> g_objects;
std::mutex g_mutex;

static const char* k_mt = "Ivory.Drawing";

std::shared_ptr<DrawObj> CheckObj(lua_State* L, int idx = 1) {
    void* p = luaL_checkudata(L, idx, k_mt);
    return *static_cast<std::shared_ptr<DrawObj>*>(p);
}

static Color4 LuaColor(lua_State* L, int idx, float alpha = 1.f) {
    if (lua_istable(L, idx)) {
        lua_rawgeti(L,idx,1); float r=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_rawgeti(L,idx,2); float g=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_rawgeti(L,idx,3); float b=(float)lua_tonumber(L,-1); lua_pop(L,1);
        return {r,g,b,alpha};
    }
    if (void* ud = luaL_testudata(L, idx, "Ivory.Color3")) {
        float* f = (float*)ud;
        return {f[0],f[1],f[2],alpha};
    }
    return {1,1,1,alpha};
}

static ImVec4 ToImVec4(const Color4& c) { return {c.r,c.g,c.b,c.a*(1.f-0.f)}; }
static ImU32  ToImU32(const Color4& c)  { return ImGui::ColorConvertFloat4ToU32({c.r,c.g,c.b,c.a}); }


static int obj_index(lua_State* L) {
    std::shared_ptr<DrawObj> obj = CheckObj(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!obj || obj->destroyed) { lua_pushnil(L); return 1; }

    bool b_val = false; float f_val = 0.f; int i_val = 0; std::string s_val;
    float xa = 0.f, ya = 0.f, xb = 0.f, yb = 0.f;
    Color4 c_val{};
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (strcmp(k,"Visible")==0)          { b_val = obj->visible; }
        else if (strcmp(k,"Thickness")==0)   { f_val = obj->thickness; }
        else if (strcmp(k,"Transparency")==0){ f_val = obj->transparency; }
        else if (strcmp(k,"ZIndex")==0)      { i_val = obj->zindex; }
        else if (strcmp(k,"Text")==0)        { s_val = obj->text; }
        else if (strcmp(k,"TextSize")==0||strcmp(k,"Size")==0) {
            f_val = (obj->type==DrawType::Text) ? obj->font_size : obj->radius;
        }
        else if (strcmp(k,"Filled")==0)      { b_val = obj->filled; }
        else if (strcmp(k,"NumSides")==0)    { i_val = obj->num_sides; }
        else if (strcmp(k,"Outline")==0)     { b_val = obj->outline; }
        else if (strcmp(k,"Center")==0)      { b_val = obj->center; }
        else if (strcmp(k,"From")==0||strcmp(k,"PointA")==0) { xa = obj->x1; ya = obj->y1; }
        else if (strcmp(k,"To")==0||strcmp(k,"PointB")==0)   { xb = obj->x2; yb = obj->y2; }
        else if (strcmp(k,"Position")==0)    {
            if (obj->type==DrawType::Circle) { xa = obj->cx; ya = obj->cy; }
            else                             { xa = obj->sx; ya = obj->sy; }
        }
        else if (strcmp(k,"Radius")==0)      { f_val = obj->radius; }
        else if (strcmp(k,"Width")==0)       { f_val = obj->sw; }
        else if (strcmp(k,"Height")==0)      { f_val = obj->sh; }
        else if (strcmp(k,"Color")==0)       { c_val = obj->color; }
    }

    if (strcmp(k,"Visible")==0)     { lua_pushboolean(L, b_val ? 1 : 0); return 1; }
    if (strcmp(k,"Color")==0)       { lua_newtable(L); lua_pushnumber(L,c_val.r); lua_rawseti(L,-2,1); lua_pushnumber(L,c_val.g); lua_rawseti(L,-2,2); lua_pushnumber(L,c_val.b); lua_rawseti(L,-2,3); return 1; }
    if (strcmp(k,"Thickness")==0)   { lua_pushnumber(L, f_val); return 1; }
    if (strcmp(k,"Transparency")==0){ lua_pushnumber(L, f_val); return 1; }
    if (strcmp(k,"ZIndex")==0)      { lua_pushinteger(L, i_val); return 1; }
    if (strcmp(k,"Text")==0)        { lua_pushstring(L, s_val.c_str()); return 1; }
    if (strcmp(k,"TextSize")==0||strcmp(k,"Size")==0) { lua_pushnumber(L, f_val); return 1; }
    if (strcmp(k,"Filled")==0)      { lua_pushboolean(L, b_val ? 1 : 0); return 1; }
    if (strcmp(k,"NumSides")==0)    { lua_pushinteger(L, i_val); return 1; }
    if (strcmp(k,"Outline")==0)     { lua_pushboolean(L, b_val ? 1 : 0); return 1; }
    if (strcmp(k,"Center")==0)      { lua_pushboolean(L, b_val ? 1 : 0); return 1; }
    if (strcmp(k,"From")==0||strcmp(k,"PointA")==0) { lua_newtable(L); lua_pushnumber(L,xa); lua_setfield(L,-2,"X"); lua_pushnumber(L,ya); lua_setfield(L,-2,"Y"); return 1; }
    if (strcmp(k,"To")==0||strcmp(k,"PointB")==0)   { lua_newtable(L); lua_pushnumber(L,xb); lua_setfield(L,-2,"X"); lua_pushnumber(L,yb); lua_setfield(L,-2,"Y"); return 1; }
    if (strcmp(k,"Position")==0)    { lua_newtable(L); lua_pushnumber(L,xa); lua_setfield(L,-2,"X"); lua_pushnumber(L,ya); lua_setfield(L,-2,"Y"); return 1; }
    if (strcmp(k,"Radius")==0)      { lua_pushnumber(L, f_val); return 1; }
    if (strcmp(k,"Width")==0)       { lua_pushnumber(L, f_val); return 1; }
    if (strcmp(k,"Height")==0)      { lua_pushnumber(L, f_val); return 1; }

    if (strcmp(k,"Remove")==0||strcmp(k,"Destroy")==0) {
        lua_pushcfunction(L, [](lua_State* Lx)->int{
            std::shared_ptr<DrawObj> o=CheckObj(Lx);
            if(o){std::lock_guard<std::mutex>lk(g_mutex);o->destroyed=true;}return 0;});
        return 1;
    }
    lua_pushnil(L); return 1;
}


static float ReadXY(lua_State* L, int idx, float& ox, float& oy) {
    if (lua_istable(L,idx)){
        lua_getfield(L,idx,"X"); ox=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L,idx,"Y"); oy=(float)lua_tonumber(L,-1); lua_pop(L,1);
    } else if (void* ud=luaL_testudata(L,idx,"Ivory.Vector2")){
        float*f=(float*)ud; ox=f[0]; oy=f[1];
    }
    return 0;
}

static int obj_newindex(lua_State* L) {
    std::shared_ptr<DrawObj> obj = CheckObj(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!obj || obj->destroyed) return 0;

    if (strcmp(k,"Visible")==0)     { bool v=lua_toboolean(L,3)!=0; std::lock_guard<std::mutex>lk(g_mutex); obj->visible=v; return 0; }
    if (strcmp(k,"Color")==0)       { Color4 c=LuaColor(L,3,obj->color.a); std::lock_guard<std::mutex>lk(g_mutex); obj->color={c.r,c.g,c.b,obj->color.a}; return 0; }
    if (strcmp(k,"Thickness")==0)   { lua_Number v=luaL_checknumber(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->thickness=(float)v; return 0; }
    if (strcmp(k,"Transparency")==0){ lua_Number v=luaL_checknumber(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->transparency=(float)v; obj->color.a=1.f-obj->transparency; return 0; }
    if (strcmp(k,"ZIndex")==0)      { lua_Integer v=luaL_checkinteger(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->zindex=(int)v; return 0; }
    if (strcmp(k,"Text")==0)        { const char* s=luaL_checkstring(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->text=s?s:""; return 0; }
    if (strcmp(k,"TextSize")==0||strcmp(k,"Size")==0) {
        lua_Number v=luaL_checknumber(L,3);
        std::lock_guard<std::mutex>lk(g_mutex);
        if(obj->type==DrawType::Text) obj->font_size=(float)v;
        else obj->radius=(float)v;
        return 0;
    }
    if (strcmp(k,"Filled")==0)  { bool v=lua_toboolean(L,3)!=0; std::lock_guard<std::mutex>lk(g_mutex); obj->filled=v; return 0; }
    if (strcmp(k,"NumSides")==0){ lua_Integer v=luaL_checkinteger(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->num_sides=(int)v; return 0; }
    if (strcmp(k,"Outline")==0) { bool v=lua_toboolean(L,3)!=0; std::lock_guard<std::mutex>lk(g_mutex); obj->outline=v; return 0; }
    if (strcmp(k,"Center")==0)  { bool v=lua_toboolean(L,3)!=0; std::lock_guard<std::mutex>lk(g_mutex); obj->center=v; return 0; }
    if (strcmp(k,"From")==0)    { float x=0,y=0; ReadXY(L,3,x,y); std::lock_guard<std::mutex>lk(g_mutex); obj->x1=x; obj->y1=y; return 0; }
    if (strcmp(k,"To")==0)      { float x=0,y=0; ReadXY(L,3,x,y); std::lock_guard<std::mutex>lk(g_mutex); obj->x2=x; obj->y2=y; return 0; }
    if (strcmp(k,"Position")==0) {
        float x=0,y=0; ReadXY(L,3,x,y);
        std::lock_guard<std::mutex>lk(g_mutex);
        if (obj->type==DrawType::Circle) { obj->cx=x; obj->cy=y; }
        else if(obj->type==DrawType::Text) { obj->tx=x; obj->ty=y; }
        else { obj->sx=x; obj->sy=y; }
        return 0;
    }
    if (strcmp(k,"Radius")==0)  { lua_Number v=luaL_checknumber(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->radius=(float)v; return 0; }
    if (strcmp(k,"Width")==0)   { lua_Number v=luaL_checknumber(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->sw=(float)v; return 0; }
    if (strcmp(k,"Height")==0)  { lua_Number v=luaL_checknumber(L,3); std::lock_guard<std::mutex>lk(g_mutex); obj->sh=(float)v; return 0; }
    if (strcmp(k,"PointA")==0)  { float x=0,y=0; ReadXY(L,3,x,y); std::lock_guard<std::mutex>lk(g_mutex); obj->x1=x; obj->y1=y; return 0; }
    if (strcmp(k,"PointB")==0)  { float x=0,y=0; ReadXY(L,3,x,y); std::lock_guard<std::mutex>lk(g_mutex); obj->x2=x; obj->y2=y; return 0; }
    if (strcmp(k,"PointC")==0)  { float x=0,y=0; ReadXY(L,3,x,y); std::lock_guard<std::mutex>lk(g_mutex); obj->t3x=x; obj->t3y=y; return 0; }
    return 0;
}

static int obj_gc(lua_State* L) {
    void* p = lua_touserdata(L, 1);
    if (p) {
        auto* sp = static_cast<std::shared_ptr<DrawObj>*>(p);
        sp->~shared_ptr<DrawObj>();
    }
    return 0;
}

static int l_drawing_new(lua_State* L) {
    const char* t = luaL_checkstring(L, 1);
    auto obj = std::make_shared<DrawObj>();
    if      (strcmp(t,"Line")==0)     obj->type=DrawType::Line;
    else if (strcmp(t,"Circle")==0)   obj->type=DrawType::Circle;
    else if (strcmp(t,"Square")==0)   obj->type=DrawType::Square;
    else if (strcmp(t,"Text")==0)     obj->type=DrawType::Text;
    else if (strcmp(t,"Triangle")==0) obj->type=DrawType::Triangle;
    else if (strcmp(t,"Quad")==0)     obj->type=DrawType::Quad;
    else { return luaL_error(L, "Drawing.new: unknown type '%s'", t); }

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_objects.push_back(obj);
    }

    void* ud_mem = lua_newuserdata(L, sizeof(std::shared_ptr<DrawObj>));
    new (ud_mem) std::shared_ptr<DrawObj>(obj);
    if (luaL_newmetatable(L, k_mt)) {
        lua_pushcfunction(L, obj_index);    lua_setfield(L,-2,"__index");
        lua_pushcfunction(L, obj_newindex); lua_setfield(L,-2,"__newindex");
        lua_pushcfunction(L, obj_gc);       lua_setfield(L,-2,"__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

} 

void Register(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_drawing_new); lua_setfield(L,-2,"new");
    
    lua_newtable(L);
    lua_pushinteger(L,0); lua_setfield(L,-2,"UI");
    lua_pushinteger(L,1); lua_setfield(L,-2,"System");
    lua_pushinteger(L,2); lua_setfield(L,-2,"Monospace");
    lua_pushinteger(L,3); lua_setfield(L,-2,"Plex");
    lua_pushinteger(L,4); lua_setfield(L,-2,"Code");
    lua_setfield(L,-2,"Fonts");
    lua_setglobal(L,"Drawing");
}

void Render() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    for (const auto& obj_sp : g_objects) {
        DrawObj* obj = obj_sp.get();
        if (!obj || obj->destroyed || !obj->visible) continue;
        ImU32 col = ToImU32(obj->color);
        switch (obj->type) {
        case DrawType::Line:
            dl->AddLine({obj->x1,obj->y1},{obj->x2,obj->y2},col,obj->thickness);
            break;
        case DrawType::Circle:
            if (obj->filled) dl->AddCircleFilled({obj->cx,obj->cy},obj->radius,col,obj->num_sides);
            else             dl->AddCircle({obj->cx,obj->cy},obj->radius,col,obj->num_sides,obj->thickness);
            break;
        case DrawType::Square:
            if (obj->filled) dl->AddRectFilled({obj->sx,obj->sy},{obj->sx+obj->sw,obj->sy+obj->sh},col);
            else             dl->AddRect({obj->sx,obj->sy},{obj->sx+obj->sw,obj->sy+obj->sh},col,0.f,0,obj->thickness);
            break;
        case DrawType::Text: {
            float tx=obj->tx, ty=obj->ty;
            const char* txt = obj->text.c_str();
            if (obj->outline) {
                ImU32 oc=ToImU32(obj->outline_color);
                dl->AddText(nullptr,obj->font_size,{tx-1,ty-1},oc,txt);
                dl->AddText(nullptr,obj->font_size,{tx+1,ty+1},oc,txt);
                dl->AddText(nullptr,obj->font_size,{tx+1,ty-1},oc,txt);
                dl->AddText(nullptr,obj->font_size,{tx-1,ty+1},oc,txt);
            }
            dl->AddText(nullptr,obj->font_size,{tx,ty},col,txt);
            break;
        }
        case DrawType::Triangle:
            if (obj->filled) dl->AddTriangleFilled({obj->x1,obj->y1},{obj->x2,obj->y2},{obj->t3x,obj->t3y},col);
            else             dl->AddTriangle({obj->x1,obj->y1},{obj->x2,obj->y2},{obj->t3x,obj->t3y},col,obj->thickness);
            break;
        default: break;
        }
    }
    
    g_objects.erase(std::remove_if(g_objects.begin(),g_objects.end(),[](const std::shared_ptr<DrawObj>& o){return o && o->destroyed;}),g_objects.end());
}

void Clear() {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& obj : g_objects) { if (obj) obj->destroyed = true; }
    g_objects.clear();
}

} 


