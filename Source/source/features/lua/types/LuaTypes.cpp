
#include <features/lua/types/LuaTypes.h>

extern "C" {
#include <lua.hpp>


}

#include <cmath>
#include <cstring>
#include <cstdio>


struct LuaVec3 { float x, y, z; };
static const char* k_v3_mt = "Ivory.Vector3";

static LuaVec3* checkv3(lua_State* L, int idx) {
    return (LuaVec3*)luaL_checkudata(L, idx, k_v3_mt);
}
static void pushv3(lua_State* L, float x, float y, float z) {
    auto* v = (LuaVec3*)lua_newuserdata(L, sizeof(LuaVec3));
    v->x = x; v->y = y; v->z = z;
    luaL_getmetatable(L, k_v3_mt); lua_setmetatable(L, -2);
}
static int v3_new(lua_State* L) {
    float x=(float)luaL_optnumber(L,1,0), y=(float)luaL_optnumber(L,2,0), z=(float)luaL_optnumber(L,3,0);
    pushv3(L, x, y, z); return 1;
}
static int v3_index(lua_State* L) {
    LuaVec3* v = checkv3(L,1); const char* k = luaL_checkstring(L,2);
    if (k[1]==0) { if(k[0]=='X'||k[0]=='x'){lua_pushnumber(L,v->x);return 1;} if(k[0]=='Y'||k[0]=='y'){lua_pushnumber(L,v->y);return 1;} if(k[0]=='Z'||k[0]=='z'){lua_pushnumber(L,v->z);return 1;} }
    if (strcmp(k,"Magnitude")==0){lua_pushnumber(L,sqrtf(v->x*v->x+v->y*v->y+v->z*v->z));return 1;}
    if (strcmp(k,"Unit")==0){float m=sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);if(m<1e-8f)m=1e-8f;pushv3(L,v->x/m,v->y/m,v->z/m);return 1;}
    lua_pushnil(L); return 1;
}
static int v3_add(lua_State* L){LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);pushv3(L,a->x+b->x,a->y+b->y,a->z+b->z);return 1;}
static int v3_sub(lua_State* L){LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);pushv3(L,a->x-b->x,a->y-b->y,a->z-b->z);return 1;}
static int v3_mul(lua_State* L){
    if(lua_isnumber(L,1)){float s=(float)lua_tonumber(L,1);LuaVec3*v=checkv3(L,2);pushv3(L,s*v->x,s*v->y,s*v->z);return 1;}
    if(lua_isnumber(L,2)){LuaVec3*v=checkv3(L,1);float s=(float)lua_tonumber(L,2);pushv3(L,v->x*s,v->y*s,v->z*s);return 1;}
    LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);pushv3(L,a->x*b->x,a->y*b->y,a->z*b->z);return 1;
}
static int v3_div(lua_State* L){LuaVec3*v=checkv3(L,1);float s=(float)luaL_checknumber(L,2);if(s==0.f)s=1e-8f;pushv3(L,v->x/s,v->y/s,v->z/s);return 1;}
static int v3_unm(lua_State* L){LuaVec3*v=checkv3(L,1);pushv3(L,-v->x,-v->y,-v->z);return 1;}
static int v3_eq(lua_State* L){LuaVec3*a=(LuaVec3*)luaL_testudata(L,1,k_v3_mt);LuaVec3*b=(LuaVec3*)luaL_testudata(L,2,k_v3_mt);lua_pushboolean(L,a&&b&&a->x==b->x&&a->y==b->y&&a->z==b->z);return 1;}
static int v3_tostr(lua_State* L){LuaVec3*v=checkv3(L,1);char buf[80];snprintf(buf,sizeof(buf),"%.4g, %.4g, %.4g",v->x,v->y,v->z);lua_pushstring(L,buf);return 1;}
static int v3_dot(lua_State* L){LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);lua_pushnumber(L,a->x*b->x+a->y*b->y+a->z*b->z);return 1;}
static int v3_cross(lua_State* L){LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);pushv3(L,a->y*b->z-a->z*b->y,a->z*b->x-a->x*b->z,a->x*b->y-a->y*b->x);return 1;}
static int v3_lerp(lua_State* L){LuaVec3*a=checkv3(L,1);LuaVec3*b=checkv3(L,2);float t=(float)luaL_checknumber(L,3);pushv3(L,a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a->z+(b->z-a->z)*t);return 1;}

static void reg_v3(lua_State* L) {
    luaL_newmetatable(L, k_v3_mt);
    lua_pushcfunction(L, v3_index);   lua_setfield(L, -2, "__index"); 
    lua_pushcfunction(L, v3_add);     lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, v3_sub);     lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, v3_mul);     lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, v3_div);     lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, v3_unm);     lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, v3_eq);      lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, v3_tostr);   lua_setfield(L, -2, "__tostring");
    
    lua_newtable(L);
    lua_pushcfunction(L, v3_dot);   lua_setfield(L, -2, "Dot");
    lua_pushcfunction(L, v3_cross); lua_setfield(L, -2, "Cross");
    lua_pushcfunction(L, v3_lerp);  lua_setfield(L, -2, "Lerp");
    
    lua_pushcfunction(L, v3_index); lua_setfield(L, -2, "__index_prop"); 
    
    lua_setfield(L, -2, "__index_tbl");
    lua_pushcfunction(L, v3_index); lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    
    lua_newtable(L);
    lua_pushcfunction(L, v3_new); lua_setfield(L, -2, "new");
    
    pushv3(L,0,0,0); lua_setfield(L,-2,"zero");
    pushv3(L,1,0,0); lua_setfield(L,-2,"xAxis");
    pushv3(L,0,1,0); lua_setfield(L,-2,"yAxis");
    pushv3(L,0,0,1); lua_setfield(L,-2,"zAxis");
    pushv3(L,1,1,1); lua_setfield(L,-2,"one");
    
    lua_newtable(L);
    lua_pushcfunction(L, v3_new); lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "Vector3");
}


struct LuaVec2 { float x, y; };
static const char* k_v2_mt = "Ivory.Vector2";
static void pushv2(lua_State* L, float x, float y) {
    auto* v = (LuaVec2*)lua_newuserdata(L, sizeof(LuaVec2));
    v->x = x; v->y = y;
    luaL_getmetatable(L, k_v2_mt); lua_setmetatable(L, -2);
}
static LuaVec2* checkv2(lua_State* L, int idx) { return (LuaVec2*)luaL_checkudata(L, idx, k_v2_mt); }
static int v2_new(lua_State* L){float x=(float)luaL_optnumber(L,1,0),y=(float)luaL_optnumber(L,2,0);pushv2(L,x,y);return 1;}
static int v2_index(lua_State* L){LuaVec2*v=checkv2(L,1);const char*k=luaL_checkstring(L,2);
    if((k[0]=='X'||k[0]=='x')&&k[1]==0){lua_pushnumber(L,v->x);return 1;}
    if((k[0]=='Y'||k[0]=='y')&&k[1]==0){lua_pushnumber(L,v->y);return 1;}
    if(strcmp(k,"Magnitude")==0){lua_pushnumber(L,sqrtf(v->x*v->x+v->y*v->y));return 1;}
    lua_pushnil(L);return 1;}
static int v2_tostr(lua_State* L){LuaVec2*v=checkv2(L,1);char buf[64];snprintf(buf,sizeof(buf),"%.4g, %.4g",v->x,v->y);lua_pushstring(L,buf);return 1;}
static int v2_add(lua_State* L){LuaVec2*a=checkv2(L,1);LuaVec2*b=checkv2(L,2);pushv2(L,a->x+b->x,a->y+b->y);return 1;}
static int v2_sub(lua_State* L){LuaVec2*a=checkv2(L,1);LuaVec2*b=checkv2(L,2);pushv2(L,a->x-b->x,a->y-b->y);return 1;}
static int v2_mul(lua_State* L){if(lua_isnumber(L,2)){LuaVec2*v=checkv2(L,1);float s=(float)lua_tonumber(L,2);pushv2(L,v->x*s,v->y*s);return 1;}LuaVec2*a=checkv2(L,1);LuaVec2*b=checkv2(L,2);pushv2(L,a->x*b->x,a->y*b->y);return 1;}

static void reg_v2(lua_State* L) {
    luaL_newmetatable(L, k_v2_mt);
    lua_pushcfunction(L, v2_index);  lua_setfield(L,-2,"__index");
    lua_pushcfunction(L, v2_add);    lua_setfield(L,-2,"__add");
    lua_pushcfunction(L, v2_sub);    lua_setfield(L,-2,"__sub");
    lua_pushcfunction(L, v2_mul);    lua_setfield(L,-2,"__mul");
    lua_pushcfunction(L, v2_tostr);  lua_setfield(L,-2,"__tostring");
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, v2_new); lua_setfield(L,-2,"new");
    lua_newtable(L); lua_pushcfunction(L,v2_new); lua_setfield(L,-2,"__call"); lua_setmetatable(L,-2);
    lua_setglobal(L, "Vector2");
}


struct LuaColor3 { float r, g, b; };
static const char* k_c3_mt = "Ivory.Color3";
static void pushc3(lua_State* L, float r, float g, float b) {
    auto* v = (LuaColor3*)lua_newuserdata(L, sizeof(LuaColor3));
    v->r=r; v->g=g; v->b=b;
    luaL_getmetatable(L, k_c3_mt); lua_setmetatable(L, -2);
}
static int c3_new(lua_State* L){float r=(float)luaL_optnumber(L,1,0),g=(float)luaL_optnumber(L,2,0),b=(float)luaL_optnumber(L,3,0);pushc3(L,r,g,b);return 1;}
static int c3_fromrgb(lua_State* L){float r=(float)luaL_optnumber(L,1,0)/255.f,g=(float)luaL_optnumber(L,2,0)/255.f,b=(float)luaL_optnumber(L,3,0)/255.f;if(lua_istable(L,1)){lua_rawgeti(L,1,1);r=(float)lua_tonumber(L,-1)/255.f;lua_pop(L,1);lua_rawgeti(L,1,2);g=(float)lua_tonumber(L,-1)/255.f;lua_pop(L,1);lua_rawgeti(L,1,3);b=(float)lua_tonumber(L,-1)/255.f;lua_pop(L,1);}pushc3(L,r,g,b);return 1;}
static int c3_index(lua_State* L){LuaColor3*v=(LuaColor3*)luaL_checkudata(L,1,k_c3_mt);const char*k=luaL_checkstring(L,2);
    if((k[0]=='R'||k[0]=='r')&&k[1]==0){lua_pushnumber(L,v->r);return 1;}
    if((k[0]=='G'||k[0]=='g')&&k[1]==0){lua_pushnumber(L,v->g);return 1;}
    if((k[0]=='B'||k[0]=='b')&&k[1]==0){lua_pushnumber(L,v->b);return 1;}
    lua_pushnil(L);return 1;}
static int c3_tostr(lua_State* L){LuaColor3*v=(LuaColor3*)luaL_checkudata(L,1,k_c3_mt);char buf[64];snprintf(buf,sizeof(buf),"%.3f, %.3f, %.3f",v->r,v->g,v->b);lua_pushstring(L,buf);return 1;}

static void reg_c3(lua_State* L) {
    luaL_newmetatable(L, k_c3_mt);
    lua_pushcfunction(L,c3_index); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,c3_tostr); lua_setfield(L,-2,"__tostring");
    lua_pop(L,1);
    lua_newtable(L);
    lua_pushcfunction(L,c3_new);    lua_setfield(L,-2,"new");
    lua_pushcfunction(L,c3_fromrgb);lua_setfield(L,-2,"fromRGB");
    lua_pushcfunction(L,c3_fromrgb);lua_setfield(L,-2,"FromRGB");
    lua_newtable(L); lua_pushcfunction(L,c3_new); lua_setfield(L,-2,"__call"); lua_setmetatable(L,-2);
    lua_setglobal(L,"Color3");
}


struct LuaUDim2 { float xs, xo, ys, yo; };
static const char* k_ud2_mt = "Ivory.UDim2";
static void pushud2(lua_State* L, float xs, float xo, float ys, float yo){
    auto* v=(LuaUDim2*)lua_newuserdata(L,sizeof(LuaUDim2));
    v->xs=xs;v->xo=xo;v->ys=ys;v->yo=yo;
    luaL_getmetatable(L,k_ud2_mt);lua_setmetatable(L,-2);
}
static int ud2_new(lua_State* L){float xs=(float)luaL_optnumber(L,1,0),xo=(float)luaL_optnumber(L,2,0),ys=(float)luaL_optnumber(L,3,0),yo=(float)luaL_optnumber(L,4,0);pushud2(L,xs,xo,ys,yo);return 1;}
static int ud2_fromscale(lua_State* L){float xs=(float)luaL_optnumber(L,1,0),ys=(float)luaL_optnumber(L,2,0);pushud2(L,xs,0,ys,0);return 1;}
static int ud2_fromoffset(lua_State* L){float xo=(float)luaL_optnumber(L,1,0),yo=(float)luaL_optnumber(L,2,0);pushud2(L,0,xo,0,yo);return 1;}
static int ud2_index(lua_State* L){LuaUDim2*v=(LuaUDim2*)luaL_checkudata(L,1,k_ud2_mt);const char*k=luaL_checkstring(L,2);
    if(strcmp(k,"X.Scale")==0||strcmp(k,"XScale")==0){lua_pushnumber(L,v->xs);return 1;}
    if(strcmp(k,"X.Offset")==0||strcmp(k,"XOffset")==0){lua_pushnumber(L,v->xo);return 1;}
    if(strcmp(k,"Y.Scale")==0||strcmp(k,"YScale")==0){lua_pushnumber(L,v->ys);return 1;}
    if(strcmp(k,"Y.Offset")==0||strcmp(k,"YOffset")==0){lua_pushnumber(L,v->yo);return 1;}
    lua_pushnil(L);return 1;}
static int ud2_tostr(lua_State* L){LuaUDim2*v=(LuaUDim2*)luaL_checkudata(L,1,k_ud2_mt);char buf[96];snprintf(buf,sizeof(buf),"{%.4g,%.4g},{%.4g,%.4g}",v->xs,v->xo,v->ys,v->yo);lua_pushstring(L,buf);return 1;}

static void reg_ud2(lua_State* L) {
    luaL_newmetatable(L,k_ud2_mt);
    lua_pushcfunction(L,ud2_index); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,ud2_tostr); lua_setfield(L,-2,"__tostring");
    lua_pop(L,1);
    lua_newtable(L);
    lua_pushcfunction(L,ud2_new);        lua_setfield(L,-2,"new");
    lua_pushcfunction(L,ud2_fromscale);  lua_setfield(L,-2,"fromScale");
    lua_pushcfunction(L,ud2_fromoffset); lua_setfield(L,-2,"fromOffset");
    lua_newtable(L); lua_pushcfunction(L,ud2_new); lua_setfield(L,-2,"__call"); lua_setmetatable(L,-2);
    lua_setglobal(L,"UDim2");
}


struct LuaCFrame { float m[12]; }; 
static const char* k_cf_mt = "Ivory.CFrame";
static void pushcf(lua_State* L, float px, float py, float pz,
    float r00=1,float r01=0,float r02=0,
    float r10=0,float r11=1,float r12=0,
    float r20=0,float r21=0,float r22=1) {
    auto* v=(LuaCFrame*)lua_newuserdata(L,sizeof(LuaCFrame));
    v->m[0]=px;v->m[1]=py;v->m[2]=pz;
    v->m[3]=r00;v->m[4]=r01;v->m[5]=r02;
    v->m[6]=r10;v->m[7]=r11;v->m[8]=r12;
    v->m[9]=r20;v->m[10]=r21;v->m[11]=r22;
    luaL_getmetatable(L,k_cf_mt);lua_setmetatable(L,-2);
}
static int cf_new(lua_State* L) {
    float px=(float)luaL_optnumber(L,1,0),py=(float)luaL_optnumber(L,2,0),pz=(float)luaL_optnumber(L,3,0);
    pushcf(L,px,py,pz); return 1;
}
static int cf_index(lua_State* L) {
    LuaCFrame*v=(LuaCFrame*)luaL_checkudata(L,1,k_cf_mt);const char*k=luaL_checkstring(L,2);
    if(strcmp(k,"X")==0){lua_pushnumber(L,v->m[0]);return 1;}
    if(strcmp(k,"Y")==0){lua_pushnumber(L,v->m[1]);return 1;}
    if(strcmp(k,"Z")==0){lua_pushnumber(L,v->m[2]);return 1;}
    if(strcmp(k,"Position")==0){pushv3(L,v->m[0],v->m[1],v->m[2]);return 1;}
    if(strcmp(k,"p")==0){pushv3(L,v->m[0],v->m[1],v->m[2]);return 1;}
    if(strcmp(k,"LookVector")==0){pushv3(L,-v->m[5],-v->m[8],-v->m[11]);return 1;}
    if(strcmp(k,"RightVector")==0){pushv3(L,v->m[3],v->m[6],v->m[9]);return 1;}
    if(strcmp(k,"UpVector")==0){pushv3(L,v->m[4],v->m[7],v->m[10]);return 1;}
    lua_pushnil(L);return 1;
}
static int cf_tostr(lua_State* L){LuaCFrame*v=(LuaCFrame*)luaL_checkudata(L,1,k_cf_mt);char buf[128];snprintf(buf,sizeof(buf),"CFrame(%.3f, %.3f, %.3f)",v->m[0],v->m[1],v->m[2]);lua_pushstring(L,buf);return 1;}
static int cf_mul(lua_State* L) {
    LuaCFrame*a=(LuaCFrame*)luaL_checkudata(L,1,k_cf_mt);
    
    if (LuaVec3* bv = (LuaVec3*)luaL_testudata(L,2,k_v3_mt)) {
        float x=a->m[0]+a->m[3]*bv->x+a->m[4]*bv->y+a->m[5]*bv->z;
        float y=a->m[1]+a->m[6]*bv->x+a->m[7]*bv->y+a->m[8]*bv->z;
        float z=a->m[2]+a->m[9]*bv->x+a->m[10]*bv->y+a->m[11]*bv->z;
        pushv3(L,x,y,z); return 1;
    }
    
    LuaCFrame*b=(LuaCFrame*)luaL_checkudata(L,2,k_cf_mt);
    float px=a->m[0]+a->m[3]*b->m[0]+a->m[4]*b->m[1]+a->m[5]*b->m[2];
    float py=a->m[1]+a->m[6]*b->m[0]+a->m[7]*b->m[1]+a->m[8]*b->m[2];
    float pz=a->m[2]+a->m[9]*b->m[0]+a->m[10]*b->m[1]+a->m[11]*b->m[2];
    pushcf(L,px,py,pz); return 1;
}

static void reg_cf(lua_State* L) {
    luaL_newmetatable(L,k_cf_mt);
    lua_pushcfunction(L,cf_index); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,cf_tostr); lua_setfield(L,-2,"__tostring");
    lua_pushcfunction(L,cf_mul);   lua_setfield(L,-2,"__mul");
    lua_pop(L,1);
    lua_newtable(L);
    lua_pushcfunction(L,cf_new);   lua_setfield(L,-2,"new");
    lua_newtable(L); lua_pushcfunction(L,cf_new); lua_setfield(L,-2,"__call"); lua_setmetatable(L,-2);
    lua_setglobal(L,"CFrame");
}


static int enum_index(lua_State* L) { lua_pushnil(L); return 1; }

namespace LuaTypes {

void Register(lua_State* L) {
    reg_v3(L);
    reg_v2(L);
    reg_c3(L);
    reg_ud2(L);
    reg_cf(L);
    
    lua_newtable(L);
    luaL_newmetatable(L, "Ivory.Enum");
    lua_pushcfunction(L, enum_index); lua_setfield(L,-2,"__index");
    lua_setmetatable(L,-2);
    lua_setglobal(L,"Enum");
}

void PushVector3(lua_State* L, const Vector3& v) { pushv3(L, v.x, v.y, v.z); }
void PushVector2(lua_State* L, const Vector2& v) { pushv2(L, v.x, v.y); }
void PushCFrame(lua_State* L, const Vector3& pos, const Matrix4x4& rot) {
    pushcf(L, pos.x, pos.y, pos.z,
        rot.m[0][0], rot.m[0][1], rot.m[0][2],
        rot.m[1][0], rot.m[1][1], rot.m[1][2],
        rot.m[2][0], rot.m[2][1], rot.m[2][2]);
}
void PushColor3(lua_State* L, float r, float g, float b) { pushc3(L, r, g, b); }
void PushUDim2(lua_State* L, float xs, float xo, float ys, float yo) { pushud2(L, xs, xo, ys, yo); }

bool IsVector3(lua_State* L, int idx) { return luaL_testudata(L, idx, k_v3_mt) != nullptr; }
Vector3 ToVector3(lua_State* L, int idx) {
    if (LuaVec3* v = (LuaVec3*)luaL_testudata(L, idx, k_v3_mt))
        return Vector3{ v->x, v->y, v->z };
    return Vector3{ 0,0,0 };
}

} 


