#include "meshgpu.h"
#include "../mesh/assetmesh.h"
#include <core/globals.h>
#include <core/memory/memory.h>
#include <core/logger/logger.h>
#include <sdk/offsets/offsets.h>
#include <sdk/sdk.h>

#include <d3dcompiler.h>
#include <cfloat>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace meshgpu {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct PrimitiveState {
    float rot[9];
    Vec3 pos;
};

struct GpuVertex {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

struct FrameCB {
    float view0[4];
    float view1[4];
    float view2[4];
    float view3[4];
    float camera_pos[4];
    float time;
    float padding[3];
};

struct ObjectCB {
    float rot0[4];
    float rot1[4];
    float rot2[4];
    float pos[4];
    float mesh_center[4];
    float mesh_scale[4];
    float color[4];
    float outline_color[4];
    int shader_type;
    float padding[3];
};

struct FullscreenCB {
    float fill_color[4];
    float outline_color[4];
    float texel_size[2];
    float outline_thickness;
    int outline_style;
    float time;
    int outline_only;
    float padding[2];
};

struct GpuMesh {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    UINT index_count = 0;
};

struct SnapshotDraw {
    std::string part_name;
    uint64_t cache_key = 0;
    const assetmesh::parsed_mesh* mesh = nullptr;
    PrimitiveState prim{};
    Vec3 size{};
    Vec3 scale_override{ 1.0f, 1.0f, 1.0f };
    bool has_scale_override = false;
    bool is_accessory = false;
    bool is_r6_limb = false;
    size_t entity_index = 0;
};

static ID3D11Device* g_device = nullptr;
static ID3D11VertexShader* g_vs = nullptr;
static ID3D11PixelShader* g_ps = nullptr;
static ID3D11PixelShader* g_mask_ps = nullptr;
static ID3D11VertexShader* g_fullscreen_vs = nullptr;
static ID3D11PixelShader* g_fullscreen_fill_ps = nullptr;
static ID3D11PixelShader* g_fullscreen_outline_ps = nullptr;
static ID3D11InputLayout* g_layout = nullptr;
static ID3D11Buffer* g_frame_cb = nullptr;
static ID3D11Buffer* g_object_cb = nullptr;
static ID3D11Buffer* g_fullscreen_cb = nullptr;
static ID3D11BlendState* g_blend = nullptr;
static ID3D11BlendState* g_opaque_blend = nullptr;
static ID3D11BlendState* g_no_color_blend = nullptr;
static ID3D11RasterizerState* g_rasterizer = nullptr;
static ID3D11RasterizerState* g_wireframe_rasterizer = nullptr;
static ID3D11RasterizerState* g_no_cull_rasterizer = nullptr;
static ID3D11DepthStencilState* g_depth_read = nullptr;
static ID3D11DepthStencilState* g_depth_write = nullptr;
static ID3D11DepthStencilState* g_stencil_write = nullptr;
static ID3D11DepthStencilState* g_stencil_fill = nullptr;
static ID3D11DepthStencilState* g_stencil_outline = nullptr;
static ID3D11SamplerState* g_point_sampler = nullptr;
static ID3D11Texture2D* g_depth_stencil_texture = nullptr;
static ID3D11DepthStencilView* g_depth_stencil_view = nullptr;
static UINT g_depth_buffer_width = 0;
static UINT g_depth_buffer_height = 0;
static ID3D11Texture2D* g_union_mask_texture = nullptr;
static ID3D11RenderTargetView* g_union_mask_rtv = nullptr;
static ID3D11ShaderResourceView* g_union_mask_srv = nullptr;
static UINT g_union_mask_width = 0;
static UINT g_union_mask_height = 0;
static std::unordered_map<uint64_t, GpuMesh> g_gpu_meshes;
static std::unordered_map<uintptr_t, uint64_t> g_part_mesh_keys;
static size_t g_last_drawn_entities = 0;
static auto g_start_time = std::chrono::steady_clock::now();
static float g_last_depth_bias = -999.0f; // sentinel to force first-time creation

ResolvedMeshDraw default_resolver(const cache::entity_t& entity, const std::string& part_name, uintptr_t part_address) {
    if (!part_address) {
        return {};
    }

    if (g_part_mesh_keys.size() > 4000) g_part_mesh_keys.clear();

    bool is_r15 = (part_name.find("Upper") != std::string::npos ||
                   part_name.find("Lower") != std::string::npos ||
                   part_name.find("Hand") != std::string::npos ||
                   part_name.find("Foot") != std::string::npos ||
                   entity.parts.count("UpperTorso") > 0 ||
                   entity.parts.count("LowerTorso") > 0);

    uint64_t asset_id = 0;
    auto stable_it = g_part_mesh_keys.find(part_address);
    if (stable_it != g_part_mesh_keys.end()) {
        asset_id = stable_it->second;
    } else {
        asset_id = assetmesh::get_mesh_asset_id_from_part(part_address);
        if (!asset_id) {
            if (entity.user_id != 0) {
                assetmesh::request_avatar_assets(static_cast<int64_t>(entity.user_id));
                asset_id = assetmesh::get_mesh_asset_id_for_part(static_cast<int64_t>(entity.user_id), part_name.c_str(), is_r15);
            }
            if (!asset_id) {
                asset_id = assetmesh::get_default_asset_for_part(part_name.c_str(), is_r15);
            }
        }
        if (asset_id != 0) {
            g_part_mesh_keys[part_address] = asset_id;
        }
    }

    if (asset_id != 0) {
        assetmesh::request_mesh(asset_id);
    }

    auto mesh = assetmesh::get_mesh_for_part(part_name.c_str(), is_r15, asset_id);
    if (!mesh) {
        return {};
    }

    ResolvedMeshDraw resolved{};
    resolved.cache_key = (asset_id != 0) ? asset_id : (is_r15 ? (1000ULL + std::hash<std::string>{}(part_name)) : (2000ULL + std::hash<std::string>{}(part_name)));
    resolved.mesh = mesh.get();
    return resolved;
}

static bool read_raw(uint64_t address, void* buffer, size_t size) {
    if (address < 0x10000 || address >= 0x7FFFFFFFFFFFull) return false;
    return memory->read_raw(address, buffer, size);
}

static bool primitive_intersects_screen(const PrimitiveState& prim, const Vec3& size, const float view[16]) {
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;
    static const float corners[8][3] = {
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f },
        {-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f },
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f },
        {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f }
    };

    bool has_front_point = false;
    float min_ndc_x = FLT_MAX;
    float max_ndc_x = -FLT_MAX;
    float min_ndc_y = FLT_MAX;
    float max_ndc_y = -FLT_MAX;

    for (const auto& corner : corners) {
        const float lx = corner[0] * hx;
        const float ly = corner[1] * hy;
        const float lz = corner[2] * hz;
        const Vec3 world = {
            prim.pos.x + prim.rot[0] * lx + prim.rot[1] * ly + prim.rot[2] * lz,
            prim.pos.y + prim.rot[3] * lx + prim.rot[4] * ly + prim.rot[5] * lz,
            prim.pos.z + prim.rot[6] * lx + prim.rot[7] * ly + prim.rot[8] * lz
        };

        const float clip_x = world.x * view[0] + world.y * view[1] + world.z * view[2] + view[3];
        const float clip_y = world.x * view[4] + world.y * view[5] + world.z * view[6] + view[7];
        const float clip_w = world.x * view[12] + world.y * view[13] + world.z * view[14] + view[15];
        if (clip_w <= 0.01f) continue;

        has_front_point = true;
        const float ndc_x = clip_x / clip_w;
        const float ndc_y = clip_y / clip_w;
        min_ndc_x = (std::min)(min_ndc_x, ndc_x);
        max_ndc_x = (std::max)(max_ndc_x, ndc_x);
        min_ndc_y = (std::min)(min_ndc_y, ndc_y);
        max_ndc_y = (std::max)(max_ndc_y, ndc_y);

        if (ndc_x >= -1.0f && ndc_x <= 1.0f && ndc_y >= -1.0f && ndc_y <= 1.0f) {
            return true;
        }
    }

    if (!has_front_point) {
        return false;
    }

    return max_ndc_x >= -1.0f && min_ndc_x <= 1.0f &&
        max_ndc_y >= -1.0f && min_ndc_y <= 1.0f;
}

static void release_union_mask() {
    if (g_union_mask_srv) { g_union_mask_srv->Release(); g_union_mask_srv = nullptr; }
    if (g_union_mask_rtv) { g_union_mask_rtv->Release(); g_union_mask_rtv = nullptr; }
    if (g_union_mask_texture) { g_union_mask_texture->Release(); g_union_mask_texture = nullptr; }
    g_union_mask_width = 0;
    g_union_mask_height = 0;
}

static void release_depth_buffer() {
    if (g_depth_stencil_view) { g_depth_stencil_view->Release(); g_depth_stencil_view = nullptr; }
    if (g_depth_stencil_texture) { g_depth_stencil_texture->Release(); g_depth_stencil_texture = nullptr; }
    g_depth_buffer_width = 0;
    g_depth_buffer_height = 0;
}

static void release_resources() {
    for (auto& [asset_id, mesh] : g_gpu_meshes) {
        (void)asset_id;
        if (mesh.vb) mesh.vb->Release();
        if (mesh.ib) mesh.ib->Release();
    }
    g_gpu_meshes.clear();
    g_part_mesh_keys.clear();
    release_union_mask();
    release_depth_buffer();
    if (g_point_sampler) { g_point_sampler->Release(); g_point_sampler = nullptr; }
    if (g_stencil_outline) { g_stencil_outline->Release(); g_stencil_outline = nullptr; }
    if (g_stencil_fill) { g_stencil_fill->Release(); g_stencil_fill = nullptr; }
    if (g_stencil_write) { g_stencil_write->Release(); g_stencil_write = nullptr; }
    if (g_depth_read) { g_depth_read->Release(); g_depth_read = nullptr; }
    if (g_depth_write) { g_depth_write->Release(); g_depth_write = nullptr; }
    if (g_wireframe_rasterizer) { g_wireframe_rasterizer->Release(); g_wireframe_rasterizer = nullptr; }
    if (g_no_cull_rasterizer) { g_no_cull_rasterizer->Release(); g_no_cull_rasterizer = nullptr; }
    if (g_rasterizer) { g_rasterizer->Release(); g_rasterizer = nullptr; }
    if (g_no_color_blend) { g_no_color_blend->Release(); g_no_color_blend = nullptr; }
    if (g_opaque_blend) { g_opaque_blend->Release(); g_opaque_blend = nullptr; }
    if (g_blend) { g_blend->Release(); g_blend = nullptr; }
    if (g_fullscreen_cb) { g_fullscreen_cb->Release(); g_fullscreen_cb = nullptr; }
    if (g_object_cb) { g_object_cb->Release(); g_object_cb = nullptr; }
    if (g_frame_cb) { g_frame_cb->Release(); g_frame_cb = nullptr; }
    if (g_layout) { g_layout->Release(); g_layout = nullptr; }
    if (g_fullscreen_outline_ps) { g_fullscreen_outline_ps->Release(); g_fullscreen_outline_ps = nullptr; }
    if (g_fullscreen_fill_ps) { g_fullscreen_fill_ps->Release(); g_fullscreen_fill_ps = nullptr; }
    if (g_fullscreen_vs) { g_fullscreen_vs->Release(); g_fullscreen_vs = nullptr; }
    if (g_mask_ps) { g_mask_ps->Release(); g_mask_ps = nullptr; }
    if (g_ps) { g_ps->Release(); g_ps = nullptr; }
    if (g_vs) { g_vs->Release(); g_vs = nullptr; }
    g_device = nullptr;
}

static bool ensure_depth_buffer(ID3D11Device* device, UINT width, UINT height) {
    if (!device || width == 0 || height == 0) return false;
    if (g_depth_stencil_texture && g_depth_stencil_view && g_depth_buffer_width == width && g_depth_buffer_height == height) {
        return true;
    }

    release_depth_buffer();

    D3D11_TEXTURE2D_DESC depth_tex_desc{};
    depth_tex_desc.Width = width;
    depth_tex_desc.Height = height;
    depth_tex_desc.MipLevels = 1;
    depth_tex_desc.ArraySize = 1;
    depth_tex_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_tex_desc.SampleDesc.Count = 1;
    depth_tex_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HRESULT hr = device->CreateTexture2D(&depth_tex_desc, nullptr, &g_depth_stencil_texture);
    if (FAILED(hr)) return false;

    hr = device->CreateDepthStencilView(g_depth_stencil_texture, nullptr, &g_depth_stencil_view);
    if (FAILED(hr)) return false;

    g_depth_buffer_width = width;
    g_depth_buffer_height = height;
    return true;
}

static bool ensure_union_mask_target(ID3D11Device* device, UINT width, UINT height) {
    if (!device || width == 0 || height == 0) return false;
    if (g_union_mask_texture && g_union_mask_width == width && g_union_mask_height == height) {
        return true;
    }

    release_union_mask();

    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &g_union_mask_texture);
    if (FAILED(hr)) return false;
    hr = device->CreateRenderTargetView(g_union_mask_texture, nullptr, &g_union_mask_rtv);
    if (FAILED(hr)) return false;
    hr = device->CreateShaderResourceView(g_union_mask_texture, nullptr, &g_union_mask_srv);
    if (FAILED(hr)) return false;

    g_union_mask_width = width;
    g_union_mask_height = height;
    return true;
}

static bool ensure_pipeline(ID3D11Device* device) {
    if (!device) return false;
    if (g_device == device &&
        g_vs && g_ps && g_mask_ps && g_fullscreen_vs && g_fullscreen_fill_ps && g_fullscreen_outline_ps &&
        g_layout && g_frame_cb && g_object_cb && g_fullscreen_cb &&
        g_blend && g_opaque_blend && g_no_color_blend &&
        g_rasterizer && g_wireframe_rasterizer && g_no_cull_rasterizer &&
        g_depth_read && g_depth_write && g_stencil_write && g_stencil_fill && g_stencil_outline &&
        g_point_sampler) {
        return true;
    }

    release_resources();
    g_device = device;

    static const char* vs_src =
        "cbuffer FrameCB : register(b0) {"
        " float4 view0;"
        " float4 view1;"
        " float4 view2;"
        " float4 view3;"
        " float4 cameraPos;"
        " float time;"
        " float3 framePad;"
        "};"
        "cbuffer ObjectCB : register(b1) {"
        " float4 rot0;"
        " float4 rot1;"
        " float4 rot2;"
        " float4 objPos;"
        " float4 meshCenter;"
        " float4 meshScale;"
        " float4 meshColor;"
        " float4 outlineColor;"
        " int shaderType;"
        " float3 objPad;"
        "};"
        "struct VSInput {"
        " float3 pos : POSITION;"
        " float3 norm : NORMAL;"
        " float2 uv : TEXCOORD0;"
        "};"
        "struct VSOutput {"
        " float4 pos : SV_POSITION;"
        " float3 worldPos : POSITION0;"
        " float3 localPos : POSITION1;"
        " float3 normal : NORMAL0;"
        " float2 uv : TEXCOORD0;"
        " float4 color : COLOR0;"
        "};"
        "VSOutput main(VSInput input) {"
        " VSOutput o;"
        " float3 local = (input.pos - meshCenter.xyz) * meshScale.xyz;"
        " float3 world;"
        " world.x = objPos.x + dot(local, rot0.xyz);"
        " world.y = objPos.y + dot(local, rot1.xyz);"
        " world.z = objPos.z + dot(local, rot2.xyz);"
        " o.worldPos = world;"
        " o.localPos = input.pos;"
        " o.pos.x = world.x * view0.x + world.y * view0.y + world.z * view0.z + view0.w;"
        " o.pos.y = world.x * view1.x + world.y * view1.y + world.z * view1.z + view1.w;"
        " o.pos.z = world.x * view2.x + world.y * view2.y + world.z * view2.z + view2.w;"
        " o.pos.w = world.x * view3.x + world.y * view3.y + world.z * view3.z + view3.w;"
        " float3 worldNorm;"
        " worldNorm.x = dot(input.norm, rot0.xyz);"
        " worldNorm.y = dot(input.norm, rot1.xyz);"
        " worldNorm.z = dot(input.norm, rot2.xyz);"
        " float nlen = length(worldNorm);"
        " o.normal = (nlen > 0.001) ? (worldNorm / nlen) : float3(0.0, 1.0, 0.0);"
        " o.uv = input.uv;"
        " o.color = meshColor;"
        " return o;"
        "}";

    static const char* ps_src =
        "cbuffer FrameCB : register(b0) {"
        " float4 view0;"
        " float4 view1;"
        " float4 view2;"
        " float4 view3;"
        " float4 cameraPos;"
        " float time;"
        " float3 framePad;"
        "};"
        "cbuffer ObjectCB : register(b1) {"
        " float4 rot0;"
        " float4 rot1;"
        " float4 rot2;"
        " float4 objPos;"
        " float4 meshCenter;"
        " float4 meshScale;"
        " float4 meshColor;"
        " float4 outlineColor;"
        " int shaderType;"
        " float3 objPad;"
        "};"
        "struct PSInput {"
        " float4 pos : SV_POSITION;"
        " float3 worldPos : POSITION0;"
        " float3 localPos : POSITION1;"
        " float3 normal : NORMAL0;"
        " float2 uv : TEXCOORD0;"
        " float4 color : COLOR0;"
        "};"
        "float4 main(PSInput input) : SV_TARGET {"
        " float3 V = normalize(cameraPos.xyz - input.worldPos);"
        " float3 N = input.normal;"
        " float nsq = dot(N, N);"
        " if (nsq > 0.01) N = normalize(N);"
        " else {"
        "   float3 dn = cross(ddy(input.worldPos), ddx(input.worldPos));"
        "   N = normalize(dn);"
        " }"
        " float  facing = dot(N, V);"
        " float  ndv = saturate(abs(facing));"
        " float  fres = pow(saturate(1.0 - ndv), 3.0);"
        " float3 L = normalize(float3(0.35, 0.90, 0.25));"
        " float  ndl = saturate(dot(N, L));"
        " float  lambert = 1.0;"
        " float3 H = normalize(L + V);"
        " float  ndh = saturate(dot(N, H));"
        " float3 R = reflect(-V, N);"
        " float3 envSky = float3(0.82, 0.90, 1.05);"
        " float3 envGround = float3(0.06, 0.08, 0.11);"
        " float3 envHorizon = float3(1.40, 1.25, 1.05);"
        " float  horizon = exp(-R.y * R.y * 14.0);"
        " float3 env = lerp(envGround, envSky, smoothstep(-0.3, 0.55, R.y)) + envHorizon * horizon * 0.7;"
        " float4 bc = meshColor;"
        " float4 fc = (outlineColor.a > 0.01) ? outlineColor : float4(1.0, 1.0, 1.0, 1.0);"
        " int m = shaderType;"
        " float3 p = input.worldPos * 0.4;"
        " float3 wp = input.worldPos;"
        " float  rim = 0.0;"
        " float  fresnelSoft = 0.0;"
        " float4 result = bc;"
        " "
        " if (m == 0) { "
        "   result.rgb = bc.rgb;"
        "   result.a = bc.a;"
        " }"
        " else if (m == 1) {" // Chrome (Smooth Liquid Chrome with User Tint)
        "   float flow = sin(wp.y * 1.5 + wp.x * 1.0 + time * 2.0) * 0.5 + 0.5;"
        "   result.rgb = saturate(lerp(bc.rgb * 0.4, float3(0.98, 0.99, 1.0), flow) * bc.rgb * 1.2);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 2) {" // Rainbow (Dynamic Spectral Waves tinted with user color)
        "   float3 spectral = 0.5 + 0.5 * cos(time * 2.2 + wp.y * 0.8 + wp.x * 0.5 + float3(0.0, 2.094, 4.188));"
        "   result.rgb = saturate(lerp(spectral, spectral * bc.rgb * 1.5, 0.7));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 3) {" // Pearl (Iridescent Soft Sheen with User Color)
        "   float3 iri = 0.5 + 0.5 * cos(wp.y * 1.2 + time * 1.2 + float3(0.0, 2.1, 4.2));"
        "   result.rgb = saturate(bc.rgb * 0.8 + iri * 0.35);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 4) {" // Glossy (Clean Solid Enamel)
        "   result.rgb = bc.rgb;"
        "   result.a = bc.a;"
        " }"
        " else if (m == 5) {" // Holographic (Silky Cyber Scan with User Color)
        "   float scan = sin(wp.y * 12.0 - time * 4.0) * 0.5 + 0.5;"
        "   float3 holo = 0.5 + 0.5 * cos(time * 3.0 + wp.y * 1.2 + float3(0.0, 2.0, 4.0));"
        "   result.rgb = saturate(bc.rgb * 0.6 + holo * bc.rgb * 0.5 + scan * float3(0.2, 0.6, 1.0) * 0.3);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 6) {" // Fade (Flowing Chromatic Gradient tinted with user color)
        "   float t = time * 0.6;"
        "   float3 p6 = wp * 0.25;"
        "   float n1 = sin(p6.x * 1.4 + p6.y * 1.0 + t);"
        "   float n2 = sin(p6.y * 1.2 - p6.z * 1.1 + t * 0.9 + 2.1);"
        "   float n3 = sin(p6.z * 1.3 + p6.x * 0.9 - t * 0.75 + 4.2);"
        "   float w = sin(n1 * 1.5 + n2) * 0.6;"
        "   float u = saturate(0.5 + 0.5 * (n1 + w));"
        "   float v = saturate(0.5 + 0.5 * (n2 - w * 0.6));"
        "   float s = saturate(0.5 + 0.5 * (n3 + n1 * 0.4));"
        "   float3 cA = float3(0.98, 0.30, 0.72); float3 cB = float3(0.38, 0.60, 1.00); float3 cC = float3(0.25, 0.95, 0.88);"
        "   float3 cD = float3(1.00, 0.68, 0.28); float3 cE = float3(0.72, 0.35, 0.98);"
        "   float3 col = lerp(cA, cB, u); col = lerp(col, cC, v * 0.85); col = lerp(col, cD, s * 0.75); col = lerp(col, cE, (1.0 - u) * v * 0.6);"
        "   result.rgb = saturate(col * bc.rgb * 1.5);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 7) {" // Wireframe
        "   result.rgb = bc.rgb; result.a = bc.a;"
        " }"
        " else if (m == 8) {" // Glass (Smooth Translucent Tint)
        "   result.rgb = saturate(bc.rgb + float3(0.15, 0.20, 0.25));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 9) {" // Copper Ropes / Grid (Color-Aware Flow)
        "   float2 cr = float2(wp.x + wp.y, wp.y + wp.z) * 1.5 + float2(time * 1.4, time * 1.0);"
        "   float2 r = float2(cr.x + cr.y, cr.x - cr.y) * 0.7071;"
        "   float2 cell = abs(frac(r) - 0.5);"
        "   float d = min(cell.x, cell.y);"
        "   float rope_w = 1.0 - smoothstep(0.0, 0.15, d);"
        "   float3 dark = bc.rgb * 0.25;"
        "   float3 bright = saturate(bc.rgb * 1.6 + float3(0.2, 0.2, 0.2));"
        "   result.rgb = lerp(dark, bright, rope_w);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 10) {" // Liquid Metal (Undulating Liquid with User Color)
        "   float3 p10 = wp * 0.4; float t = time * 1.5;"
        "   float wA = sin(p10.x * 3.0 + p10.y * 2.0 - t * 2.5); float wB = cos(p10.y * 3.5 + p10.z * 2.2 + t * 2.0);"
        "   float flow = saturate(0.5 + 0.5 * (wA + wB * 0.85));"
        "   float3 dark = bc.rgb * 0.3;"
        "   float3 hi = saturate(bc.rgb * 1.5 + float3(0.3, 0.3, 0.3));"
        "   result.rgb = saturate(lerp(dark, hi, flow));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 11) {" // Soft Glass (Frosted Satin)
        "   result.rgb = saturate(bc.rgb + float3(0.12, 0.15, 0.20));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 12) {" // Ice (Glacial Flow with User Color)
        "   float flow = sin(wp.y * 2.0 + wp.x * 1.5 + time * 1.2) * 0.5 + 0.5;"
        "   float3 ice = lerp(bc.rgb * 0.6, saturate(bc.rgb * 1.4 + float3(0.3, 0.4, 0.5)), flow);"
        "   result.rgb = saturate(ice);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 13) {" // Ghost Pulse (Breathing Spectral Aura)
        "   float pulse = 0.70 + 0.30 * sin(time * 2.6);"
        "   result.rgb = saturate(bc.rgb * pulse);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 14) {" // Aurora (Northern Lights with User Color)
        "   float3 p14 = wp * 0.3; float t = time * 0.85;"
        "   float3 band = 0.5 + 0.5 * cos(t + p14.y * 2.5 + p14.x * 1.2 + float3(0.0, 2.094, 4.188));"
        "   result.rgb = saturate(lerp(bc.rgb * 0.5, band * bc.rgb * 1.6, 0.75));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 15) {" // Bubble (Iridescent Thin-Film with User Color)
        "   float3 iri = 0.5 + 0.5 * cos(wp.y * 1.5 + time * 2.0 + float3(0.0, 2.0, 4.0));"
        "   result.rgb = saturate(lerp(bc.rgb * 0.65, iri * bc.rgb * 1.5, 0.65));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 16) {" // Jelly (Translucent Gelatin with User Color)
        "   float3 p16 = wp * 0.5; float t = time * 3.0;"
        "   float wob = sin(p16.x * 2.5 + t) * cos(p16.y * 2.2 - t * 0.85);"
        "   float blob = 0.5 + 0.5 * wob;"
        "   result.rgb = saturate(lerp(bc.rgb * 0.75, saturate(bc.rgb * 1.35 + float3(0.2, 0.2, 0.2)), blob));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 17) {" // Mercury (Liquid Silver with User Color)
        "   float flow = sin(wp.y * 2.2 + wp.x * 1.8 - time * 2.0) * 0.5 + 0.5;"
        "   float3 silv = lerp(bc.rgb * 0.5, saturate(bc.rgb * 1.5 + float3(0.2, 0.2, 0.2)), flow);"
        "   result.rgb = saturate(silv);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 18) {" // Water Caustics (Ocean Caustic Waves with User Color)
        "   float3 p18 = wp * 0.35; float t = time * 1.35;"
        "   float2 uv1 = p18.xz * 1.8 + float2(t * 0.45, t * 0.32);"
        "   float2 uv2 = p18.xz * 2.4 - float2(t * 0.38, -t * 0.52);"
        "   float c1 = sin(uv1.x * 3.5 + uv1.y * 2.8) * cos(uv1.y * 3.2 - uv1.x * 2.4);"
        "   float c2 = sin(uv2.x * 4.2 - uv2.y * 3.6) * cos(uv2.y * 4.0 + uv2.x * 2.9);"
        "   float cau = pow(saturate(0.5 + 0.5 * (c1 + c2 * 0.85)), 2.8);"
        "   float3 waterCol = lerp(bc.rgb * 0.6, saturate(bc.rgb * 1.5), cau);"
        "   result.rgb = saturate(waterCol + cau * float3(0.4, 0.4, 0.4));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 19) {" // Deep Ocean (Aquatic Swell with User Color)
        "   float3 p19 = wp * 0.25; float t = time * 0.75;"
        "   float wave1 = sin(p19.x * 2.4 + t * 1.5 + p19.z * 1.2);"
        "   float wave2 = cos(p19.z * 2.8 - t * 1.2 + p19.y * 1.6);"
        "   float depth = saturate(0.5 + 0.5 * (wave1 * 0.6 + wave2 * 0.4));"
        "   result.rgb = saturate(lerp(bc.rgb * 0.4, saturate(bc.rgb * 1.4 + float3(0.2, 0.2, 0.2)), depth));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 20) {" // Quicksilver (Reflective Flow with User Color)
        "   float3 p20 = wp * 0.4; float t = time * 2.0;"
        "   float drip = sin(p20.y * 3.5 - t * 3.0 + sin(p20.x * 2.0 + t) * 1.2);"
        "   float blob = saturate(0.5 + 0.5 * drip);"
        "   float3 silv = lerp(bc.rgb * 0.5, saturate(bc.rgb * 1.45 + float3(0.2, 0.2, 0.2)), blob);"
        "   result.rgb = saturate(silv);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 21) {" // Ripple (Expanding Rings with User Color)
        "   float2 q = float2(wp.x, wp.z) * 0.5; float t = time * 2.6;"
        "   float d0 = length(q);"
        "   float rings = sin(d0 * 8.0 - t * 4.0) * exp(-d0 * 0.15) * 0.5 + 0.5;"
        "   result.rgb = saturate(lerp(bc.rgb * 0.5, saturate(bc.rgb * 1.5 + float3(0.3, 0.3, 0.3)), rings));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 22) {" // Oil Slick (Smooth Marbling with User Color)
        "   float3 p22 = wp * 0.35; float t = time * 1.1;"
        "   float flow = sin(p22.x * 2.0 + t) + cos(p22.y * 2.2 - t * 1.3);"
        "   float3 film = 0.5 + 0.5 * cos(flow * 2.5 + t * 1.6 + float3(0.0, 2.094, 4.188));"
        "   result.rgb = saturate(lerp(bc.rgb * 0.5, film * bc.rgb * 1.6, 0.75));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 23) {" // Metallic (Brushed Titanium with User Color)
        "   float flow = sin(wp.y * 1.2 + wp.x * 0.8 + time * 1.2) * 0.5 + 0.5;"
        "   result.rgb = saturate(lerp(bc.rgb * 0.5, saturate(bc.rgb * 1.4), flow));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 24) {" // Void Waves (Silhouette with Flowing Waves in User Color)
        "   float3 p24 = wp * 0.25; float t = time * 1.8;"
        "   float wave1 = sin(p24.y * 2.5 - t * 2.2 + sin(p24.x * 1.5 + t * 0.8) * 1.2);"
        "   float wave2 = cos(p24.y * 3.5 - t * 3.0 + p24.z * 1.2);"
        "   float band = saturate(0.5 + 0.5 * (wave1 * 0.65 + wave2 * 0.35));"
        "   band = pow(band, 2.5);"
        "   float3 voidDark = bc.rgb * 0.25;"
        "   float3 waveCol = saturate(bc.rgb * 1.6 + float3(0.2, 0.2, 0.2));"
        "   result.rgb = saturate(lerp(voidDark, waveCol, band));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 25) {" // Dark Nebula (Cosmic Clouds in User Color)
        "   float3 p25 = wp * 0.2; float t = time * 0.9;"
        "   float n1 = sin(p25.x * 1.8 + p25.y * 1.4 + t * 1.2);"
        "   float n2 = cos(p25.y * 1.8 - p25.z * 1.5 + t * 0.8);"
        "   float cloud = saturate(0.5 + 0.5 * (n1 + n2 * 0.7));"
        "   float3 cDeep = bc.rgb * 0.2;"
        "   float3 neb = saturate(bc.rgb * 1.5 + float3(0.2, 0.2, 0.2));"
        "   result.rgb = saturate(lerp(cDeep, neb, cloud));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 26) {" // Cyber Plasma (Ionized Plasma in User Color)
        "   float3 p26 = wp * 0.35; float t = time * 3.5;"
        "   float arc1 = 1.0 - smoothstep(0.0, 0.15, abs(sin(p26.y * 3.5 + t * 3.0 + sin(p26.x * 2.0) * 1.2) - 0.2));"
        "   float energy = saturate(arc1 * 1.3);"
        "   float3 darkBody = bc.rgb * 0.2;"
        "   float3 neonCol = saturate(bc.rgb * 1.6 + float3(0.3, 0.3, 0.3));"
        "   result.rgb = saturate(lerp(darkBody, neonCol, energy));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 27) {" // Energy Pulse (Forcefield Shockwaves in User Color)
        "   float pulse = frac(time * 0.75);"
        "   float shock = exp(-pow(frac(wp.y * 0.15 - pulse * 1.2) * 4.0 - 1.0, 2.0) * 4.0);"
        "   float3 shield = lerp(bc.rgb * 0.4, saturate(bc.rgb * 1.6 + float3(0.3, 0.3, 0.3)), shock);"
        "   result.rgb = saturate(shield * (0.65 + shock * 0.7));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 28) {" // Black Hole (Accretion Event Horizon in User Color)
        "   float heightWave = sin(wp.y * 1.2 + time * 3.0) * 0.5 + 0.5;"
        "   float swirl = sin(wp.y * 2.0 + wp.x * 1.5 + time * 3.0) * 0.5 + 0.5;"
        "   float3 diskCol = saturate(bc.rgb * 1.5 + float3(0.2, 0.2, 0.2));"
        "   float3 hole = bc.rgb * 0.15;"
        "   result.rgb = saturate(lerp(hole, diskCol, heightWave * 0.85));"
        "   result.a = bc.a;"
        " }"
        " else if (m == 29) {" // Cosmic Matrix (Digital Code Flow in User Color)
        "   float2 uvM = float2(wp.x + wp.z, wp.y) * 1.5;"
        "   float drop = frac(uvM.y * 0.20 - time * 1.5 + sin(uvM.x * 2.5) * 3.0);"
        "   float stream = pow(1.0 - drop, 3.5);"
        "   float3 matrixCol = saturate(bc.rgb * 1.6 + float3(0.2, 0.2, 0.2));"
        "   result.rgb = saturate(bc.rgb * 0.15 + matrixCol * stream * 1.3);"
        "   result.a = bc.a;"
        " }"
        " else if (m == 30) {" // Acrylic (Modern Frosted Glass in User Color)
        "   result.rgb = saturate(bc.rgb + float3(0.18, 0.22, 0.28));"
        "   result.a = bc.a;"
        " }"
        " return result;"
        "}";

    static const char* mask_ps_src =
        "float4 main() : SV_TARGET {"
        " return float4(1.0, 1.0, 1.0, 1.0);"
        "}";

    static const char* fullscreen_vs_src =
        "struct VSOutput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };"
        "VSOutput main(uint vertex_id : SV_VertexID) {"
        " VSOutput o;"
        " float2 pos;"
        " if (vertex_id == 0) pos = float2(-1.0, -1.0);"
        " else if (vertex_id == 1) pos = float2(-1.0, 3.0);"
        " else pos = float2(3.0, -1.0);"
        " o.pos = float4(pos, 0.0, 1.0);"
        " o.uv = float2((pos.x + 1.0) * 0.5, 1.0 - ((pos.y + 1.0) * 0.5));"
        " return o;"
        "}";

    static const char* fullscreen_fill_ps_src =
        "cbuffer FullscreenCB : register(b0) {"
        " float4 fillColor;"
        " float4 outlineColor;"
        " float2 texelSize;"
        " float2 padding;"
        "};"
        "float4 main() : SV_TARGET {"
        " return fillColor;"
        "}";

    static const char* fullscreen_outline_ps_src =
        "Texture2D maskTex : register(t0);"
        "SamplerState pointSampler : register(s0);"
        "cbuffer FullscreenCB : register(b0) {"
        " float4 fillColor;"
        " float4 outlineColor;"
        " float2 texelSize;"
        " float outlineThickness;"
        " int outlineStyle;"
        " float time;"
        " int outlineOnly;"
        " float2 padding;"
        "};"
        "struct PSInput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };"
        "float sample_mask(float2 uv) { return maskTex.SampleLevel(pointSampler, uv, 0).r; }"
        "float4 main(PSInput input) : SV_TARGET {"
        " if (sample_mask(input.uv) > 0.5) discard;"
        " int radius = clamp((int)round(outlineThickness), 1, 10);"
        " float nearest = 1e5;"
        " float2 dirs[8] = {"
        "   float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0),"
        "   float2(0.7071, 0.7071), float2(-0.7071, 0.7071),"
        "   float2(0.7071, -0.7071), float2(-0.7071, -0.7071)"
        " };"
        " [unroll] for (int k = 0; k < 8; ++k) {"
        "   [loop] for (int r = 1; r <= radius; ++r) {"
        "     float2 off = dirs[k] * (float)r * texelSize;"
        "     if (sample_mask(input.uv + off) > 0.5) {"
        "       float d = (float)r;"
        "       if (d < nearest) nearest = d;"
        "       break;"
        "     }"
        "   }"
        " }"
        " if (nearest > (float)radius + 0.5) discard;"
        " float t = saturate(nearest / (float)radius);"
        " float core = exp(-nearest * nearest * 0.35);"
        " float mid = exp(-t * t * 2.2);"
        " float tail = pow(saturate(1.0 - t), 2.8);"
        " float fall = core * 0.55 + mid * 0.40 + tail * 0.35;"
        " float3 rgb = outlineColor.rgb;"
        " float a = outlineColor.a * fall * 0.95;"
        " if (outlineStyle == 0) {" // Solid Glow
        "   rgb *= (0.90 + 0.35 * core);"
        " }"
        " else if (outlineStyle == 1) {" // Soft Fade
        "   float breath = 0.85 + 0.15 * sin(time * 1.55);"
        "   rgb *= (0.92 + 0.14 * core) * breath;"
        "   a *= breath;"
        " }"
        " else if (outlineStyle == 2) {" // Pulse Glow
        "   float wave = 0.5 + 0.5 * sin(nearest * 0.55 - time * 4.0);"
        "   float pulse = 0.55 + 0.45 * (0.5 + 0.5 * sin(time * 2.6));"
        "   a = outlineColor.a * (core * 0.50 + mid * (0.35 + 0.30 * wave) + tail * 0.40) * 0.95 * pulse;"
        "   rgb = lerp(rgb, saturate(rgb * 1.4 + 0.1), wave);"
        " }"
        " else if (outlineStyle == 3) {" // Neon Flow
        "   float2 sp = input.uv / texelSize;"
        "   float band = sin(sp.x * 0.065 + sp.y * 0.048 - time * 3.2);"
        "   float flow = 0.55 + 0.45 * (0.5 + 0.5 * band);"
        "   rgb = lerp(rgb, saturate(rgb * 1.35 + 0.15), saturate(band * 0.40 + 0.25));"
        "   a = outlineColor.a * fall * (0.55 + 0.50 * flow);"
        " }"
        " else if (outlineStyle == 4) {" // Rainbow Swirl
        "   float2 sp = input.uv / texelSize;"
        "   float ang = atan2(sp.y, sp.x);"
        "   float swirl = sin(ang * 3.0 + time * 2.5 + nearest * 0.25);"
        "   float3 rainbow = 0.5 + 0.5 * cos(time * 2.0 + ang * 2.0 + float3(0.0, 2.0, 4.0));"
        "   rgb = lerp(rgb, rainbow, 0.65) * (0.90 + 0.40 * core);"
        "   a = outlineColor.a * fall * (0.70 + 0.30 * (0.5 + 0.5 * swirl));"
        " }"
        " if (a < 0.005) discard;"
        " return float4(rgb, saturate(a));"
        "}";

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* mask_ps_blob = nullptr;
    ID3DBlob* fullscreen_vs_blob = nullptr;
    ID3DBlob* fullscreen_fill_blob = nullptr;
    ID3DBlob* fullscreen_outline_blob = nullptr;
    ID3DBlob* error_blob = nullptr;

    HRESULT hr = D3DCompile(vs_src, std::strlen(vs_src), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vs_blob, &error_blob);
    if (FAILED(hr)) {
        if (error_blob) {
            logger->log<ERR>("VS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(ps_src, std::strlen(ps_src), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &ps_blob, &error_blob);
    if (FAILED(hr)) {
        if (vs_blob) vs_blob->Release();
        if (error_blob) {
            logger->log<ERR>("PS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(mask_ps_src, std::strlen(mask_ps_src), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &mask_ps_blob, &error_blob);
    if (FAILED(hr)) {
        if (vs_blob) vs_blob->Release();
        if (ps_blob) ps_blob->Release();
        if (error_blob) {
            logger->log<ERR>("Mask PS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(fullscreen_vs_src, std::strlen(fullscreen_vs_src), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &fullscreen_vs_blob, &error_blob);
    if (FAILED(hr)) {
        if (vs_blob) vs_blob->Release();
        if (ps_blob) ps_blob->Release();
        if (mask_ps_blob) mask_ps_blob->Release();
        if (error_blob) {
            logger->log<ERR>("Fullscreen VS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(fullscreen_fill_ps_src, std::strlen(fullscreen_fill_ps_src), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &fullscreen_fill_blob, &error_blob);
    if (FAILED(hr)) {
        if (vs_blob) vs_blob->Release();
        if (ps_blob) ps_blob->Release();
        if (mask_ps_blob) mask_ps_blob->Release();
        if (fullscreen_vs_blob) fullscreen_vs_blob->Release();
        if (error_blob) {
            logger->log<ERR>("Fullscreen Fill PS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(fullscreen_outline_ps_src, std::strlen(fullscreen_outline_ps_src), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &fullscreen_outline_blob, &error_blob);
    if (FAILED(hr)) {
        if (vs_blob) vs_blob->Release();
        if (ps_blob) ps_blob->Release();
        if (mask_ps_blob) mask_ps_blob->Release();
        if (fullscreen_vs_blob) fullscreen_vs_blob->Release();
        if (fullscreen_fill_blob) fullscreen_fill_blob->Release();
        if (error_blob) {
            logger->log<ERR>("Fullscreen Outline PS compile failed: {}", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs);
    if (FAILED(hr)) return false;
    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps);
    if (FAILED(hr)) return false;
    hr = device->CreatePixelShader(mask_ps_blob->GetBufferPointer(), mask_ps_blob->GetBufferSize(), nullptr, &g_mask_ps);
    if (FAILED(hr)) return false;
    hr = device->CreateVertexShader(fullscreen_vs_blob->GetBufferPointer(), fullscreen_vs_blob->GetBufferSize(), nullptr, &g_fullscreen_vs);
    if (FAILED(hr)) return false;
    hr = device->CreatePixelShader(fullscreen_fill_blob->GetBufferPointer(), fullscreen_fill_blob->GetBufferSize(), nullptr, &g_fullscreen_fill_ps);
    if (FAILED(hr)) return false;
    hr = device->CreatePixelShader(fullscreen_outline_blob->GetBufferPointer(), fullscreen_outline_blob->GetBufferSize(), nullptr, &g_fullscreen_outline_ps);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = device->CreateInputLayout(layout, 3, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_layout);
    vs_blob->Release();
    ps_blob->Release();
    mask_ps_blob->Release();
    fullscreen_vs_blob->Release();
    fullscreen_fill_blob->Release();
    fullscreen_outline_blob->Release();
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb_desc.ByteWidth = sizeof(FrameCB);
    hr = device->CreateBuffer(&cb_desc, nullptr, &g_frame_cb);
    if (FAILED(hr)) return false;
    cb_desc.ByteWidth = sizeof(ObjectCB);
    hr = device->CreateBuffer(&cb_desc, nullptr, &g_object_cb);
    if (FAILED(hr)) return false;
    cb_desc.ByteWidth = sizeof(FullscreenCB);
    hr = device->CreateBuffer(&cb_desc, nullptr, &g_fullscreen_cb);
    if (FAILED(hr)) return false;

    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blend_desc, &g_blend);
    if (FAILED(hr)) return false;

    D3D11_BLEND_DESC opaque_blend_desc{};
    opaque_blend_desc.RenderTarget[0].BlendEnable = FALSE;
    opaque_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&opaque_blend_desc, &g_opaque_blend);
    if (FAILED(hr)) return false;

    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = 0;
    hr = device->CreateBlendState(&blend_desc, &g_no_color_blend);
    if (FAILED(hr)) return false;

    D3D11_RASTERIZER_DESC rasterizer_desc{};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_BACK;  // Hardware backface culling to eliminate all interior / inside faces
    rasterizer_desc.FrontCounterClockwise = FALSE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.SlopeScaledDepthBias = 0.0f;
    rasterizer_desc.DepthBias = 0;
    hr = device->CreateRasterizerState(&rasterizer_desc, &g_rasterizer);
    if (FAILED(hr)) return false;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(&rasterizer_desc, &g_no_cull_rasterizer);
    if (FAILED(hr)) return false;
    rasterizer_desc.FillMode = D3D11_FILL_WIREFRAME;
    hr = device->CreateRasterizerState(&rasterizer_desc, &g_wireframe_rasterizer);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC depth_desc{};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateDepthStencilState(&depth_desc, &g_depth_read);
    if (FAILED(hr)) return false;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    hr = device->CreateDepthStencilState(&depth_desc, &g_depth_write);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC stencil_desc{};
    stencil_desc.DepthEnable = TRUE;
    stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    stencil_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    stencil_desc.StencilEnable = TRUE;
    stencil_desc.StencilReadMask = 0xFF;
    stencil_desc.StencilWriteMask = 0xFF;
    stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.BackFace = stencil_desc.FrontFace;
    hr = device->CreateDepthStencilState(&stencil_desc, &g_stencil_write);
    if (FAILED(hr)) return false;

    stencil_desc.DepthEnable = FALSE;
    stencil_desc.StencilWriteMask = 0;
    stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.BackFace = stencil_desc.FrontFace;
    hr = device->CreateDepthStencilState(&stencil_desc, &g_stencil_fill);
    if (FAILED(hr)) return false;

    stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
    stencil_desc.BackFace = stencil_desc.FrontFace;
    hr = device->CreateDepthStencilState(&stencil_desc, &g_stencil_outline);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler_desc, &g_point_sampler);
    if (FAILED(hr)) return false;

    return true;
}

inline bool is_w2s_visible(const float view[16], float x, float y, float z, float margin = 0.75f) {
    float clip_w = x * view[12] + y * view[13] + z * view[14] + view[15];
    if (clip_w <= 0.1f) return false; // Behind camera
    float clip_x = x * view[0] + y * view[1] + z * view[2] + view[3];
    float clip_y = x * view[4] + y * view[5] + z * view[6] + view[7];
    float ndc_x = clip_x / clip_w;
    float ndc_y = clip_y / clip_w;
    float max_bound = 1.0f + margin;
    return (ndc_x >= -max_bound && ndc_x <= max_bound && ndc_y >= -max_bound && ndc_y <= max_bound);
}

static GpuMesh* ensure_gpu_mesh(ID3D11Device* device, uint64_t cache_key, const assetmesh::parsed_mesh& mesh) {
    if (!device || cache_key == 0 || mesh.vertices.empty() || mesh.indices.empty()) return nullptr;
    if (mesh.vertices.size() > 500000 || mesh.indices.size() > 1500000) return nullptr;

    auto it = g_gpu_meshes.find(cache_key);
    if (it != g_gpu_meshes.end()) return &it->second;

    if (g_gpu_meshes.size() > 2000) {
        for (auto& pair : g_gpu_meshes) {
            if (pair.second.vb) pair.second.vb->Release();
            if (pair.second.ib) pair.second.ib->Release();
        }
        g_gpu_meshes.clear();
    }

    std::vector<GpuVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        vertices.push_back({
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            vertex.uv.x, vertex.uv.y
        });
    }

    D3D11_BUFFER_DESC vb_desc{};
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.ByteWidth = (UINT)(vertices.size() * sizeof(GpuVertex));
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_data{};
    vb_data.pSysMem = vertices.data();

    D3D11_BUFFER_DESC ib_desc{};
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.ByteWidth = (UINT)(mesh.indices.size() * sizeof(uint32_t));
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data{};
    ib_data.pSysMem = mesh.indices.data();

    GpuMesh gpu_mesh{};
    HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &gpu_mesh.vb);
    if (FAILED(hr)) return nullptr;
    hr = device->CreateBuffer(&ib_desc, &ib_data, &gpu_mesh.ib);
    if (FAILED(hr)) {
        gpu_mesh.vb->Release();
        return nullptr;
    }

    gpu_mesh.index_count = (UINT)mesh.indices.size();
    g_gpu_meshes[cache_key] = gpu_mesh;
    return &g_gpu_meshes[cache_key];
}

static bool upload_frame_cb(ID3D11DeviceContext* context, const float view[16], const float camera_pos[3]) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(g_frame_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;
    FrameCB cb{};
    std::memcpy(cb.view0, view + 0, sizeof(float) * 4);
    std::memcpy(cb.view1, view + 4, sizeof(float) * 4);
    std::memcpy(cb.view2, view + 8, sizeof(float) * 4);
    std::memcpy(cb.view3, view + 12, sizeof(float) * 4);
    if (camera_pos && (camera_pos[0] != 0.0f || camera_pos[1] != 0.0f || camera_pos[2] != 0.0f)) {
        cb.camera_pos[0] = camera_pos[0];
        cb.camera_pos[1] = camera_pos[1];
        cb.camera_pos[2] = camera_pos[2];
        cb.camera_pos[3] = 1.0f;
    } else {
        float r00 = view[0], r01 = view[1], r02 = view[2], tx = view[3];
        float r10 = view[4], r11 = view[5], r12 = view[6], ty = view[7];
        float r20 = view[8], r21 = view[9], r22 = view[10], tz = view[11];
        cb.camera_pos[0] = -(r00 * tx + r10 * ty + r20 * tz);
        cb.camera_pos[1] = -(r01 * tx + r11 * ty + r21 * tz);
        cb.camera_pos[2] = -(r02 * tx + r12 * ty + r22 * tz);
        cb.camera_pos[3] = 1.0f;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - g_start_time).count();
    cb.time = elapsed;
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    context->Unmap(g_frame_cb, 0);
    return true;
}

static bool upload_object_cb(ID3D11DeviceContext* context, const SnapshotDraw& item, const float color[4], const float outline_color[4], int shader_type) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(g_object_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;
    ObjectCB* cb = static_cast<ObjectCB*>(mapped.pData);
    cb->rot0[0] = item.prim.rot[0]; cb->rot0[1] = item.prim.rot[1]; cb->rot0[2] = item.prim.rot[2]; cb->rot0[3] = 0.0f;
    cb->rot1[0] = item.prim.rot[3]; cb->rot1[1] = item.prim.rot[4]; cb->rot1[2] = item.prim.rot[5]; cb->rot1[3] = 0.0f;
    cb->rot2[0] = item.prim.rot[6]; cb->rot2[1] = item.prim.rot[7]; cb->rot2[2] = item.prim.rot[8]; cb->rot2[3] = 0.0f;
    cb->pos[0] = item.prim.pos.x; cb->pos[1] = item.prim.pos.y; cb->pos[2] = item.prim.pos.z; cb->pos[3] = 1.0f;
    if (item.has_scale_override) {
        // SpecialMesh semantics: native mesh coordinates x Scale, mesh pivot NOT
        // subtracted (the asset is already in its authored space).
        cb->mesh_center[0] = 0.0f;
        cb->mesh_center[1] = 0.0f;
        cb->mesh_center[2] = 0.0f;
        cb->mesh_center[3] = 0.0f;
        cb->mesh_scale[0] = item.scale_override.x;
        cb->mesh_scale[1] = item.scale_override.y;
        cb->mesh_scale[2] = item.scale_override.z;
    } else if (item.is_r6_limb) {
        // R6 CharacterMesh / Head / Limbs: authored in native limb coordinate space relative to part CFrame
        cb->mesh_center[0] = 0.0f;
        cb->mesh_center[1] = 0.0f;
        cb->mesh_center[2] = 0.0f;
        cb->mesh_center[3] = 0.0f;
        cb->mesh_scale[0] = 1.0f;
        cb->mesh_scale[1] = 1.0f;
        cb->mesh_scale[2] = 1.0f;
    } else if (item.is_accessory) {
        // Accessory / Hair MeshPart semantics: 1:1 fitted scale to part size around authored pivot
        cb->mesh_center[0] = (item.mesh && item.mesh->bounds.valid) ? item.mesh->bounds.center.x : 0.0f;
        cb->mesh_center[1] = (item.mesh && item.mesh->bounds.valid) ? item.mesh->bounds.center.y : 0.0f;
        cb->mesh_center[2] = (item.mesh && item.mesh->bounds.valid) ? item.mesh->bounds.center.z : 0.0f;
        cb->mesh_center[3] = 0.0f;
        float sx = (item.mesh && item.mesh->bounds.size.x > 0.001f) ? item.size.x / item.mesh->bounds.size.x : 1.0f;
        float sy = (item.mesh && item.mesh->bounds.size.y > 0.001f) ? item.size.y / item.mesh->bounds.size.y : 1.0f;
        float sz = (item.mesh && item.mesh->bounds.size.z > 0.001f) ? item.size.z / item.mesh->bounds.size.z : 1.0f;
        if (!std::isfinite(sx) || sx < 1e-4f) sx = 1.0f;
        if (!std::isfinite(sy) || sy < 1e-4f) sy = 1.0f;
        if (!std::isfinite(sz) || sz < 1e-4f) sz = 1.0f;
        if (sx > 200.0f) sx = 200.0f;
        if (sy > 200.0f) sy = 200.0f;
        if (sz > 200.0f) sz = 200.0f;
        cb->mesh_scale[0] = sx;
        cb->mesh_scale[1] = sy;
        cb->mesh_scale[2] = sz;
    } else {
        // MeshPart semantics (R15 limbs and modern MeshParts)
        cb->mesh_center[0] = 0.0f;
        cb->mesh_center[1] = 0.0f;
        cb->mesh_center[2] = 0.0f;
        cb->mesh_center[3] = 0.0f;
        cb->mesh_scale[0] = (item.mesh && item.mesh->bounds.size.x > 0.001f) ? item.size.x / item.mesh->bounds.size.x : 1.0f;
        cb->mesh_scale[1] = (item.mesh && item.mesh->bounds.size.y > 0.001f) ? item.size.y / item.mesh->bounds.size.y : 1.0f;
        cb->mesh_scale[2] = (item.mesh && item.mesh->bounds.size.z > 0.001f) ? item.size.z / item.mesh->bounds.size.z : 1.0f;
    }
    cb->mesh_scale[3] = 1.0f;

    std::memcpy(cb->color, color, sizeof(float) * 4);
    if (outline_color) std::memcpy(cb->outline_color, outline_color, sizeof(float) * 4);
    else std::memset(cb->outline_color, 0, sizeof(float) * 4);
    cb->shader_type = shader_type;
    context->Unmap(g_object_cb, 0);
    return true;
}

static bool upload_fullscreen_cb(ID3D11DeviceContext* context, const float fill_color[4], const float outline_color[4], float viewport_width, float viewport_height, float outline_thickness = 2.5f, int outline_style = 0, bool outline_only = false) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(g_fullscreen_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;
    FullscreenCB cb{};
    if (fill_color) std::memcpy(cb.fill_color, fill_color, sizeof(float) * 4);
    if (outline_color) std::memcpy(cb.outline_color, outline_color, sizeof(float) * 4);
    cb.texel_size[0] = viewport_width > 0.0f ? 1.0f / viewport_width : 0.0f;
    cb.texel_size[1] = viewport_height > 0.0f ? 1.0f / viewport_height : 0.0f;
    cb.outline_thickness = outline_thickness;
    cb.outline_style = outline_style;
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - g_start_time).count();
    cb.time = elapsed;
    cb.outline_only = outline_only ? 1 : 0;
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    context->Unmap(g_fullscreen_cb, 0);
    return true;
}

struct CachedAccTransform {
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
    Vec3 offset{ 0.0f, 0.0f, 0.0f };
    bool has_special = false; // part carries a SpecialMesh/FileMesh child
    bool resolved = false;
    DWORD last_scan = 0;
};
static std::unordered_map<uintptr_t, CachedAccTransform> g_acc_transform_cache;

static void build_snapshot_ptrs(const std::vector<const cache::entity_t*>& entities, const float view[16], MeshResolver resolver, bool enable_accessories, std::vector<SnapshotDraw>& out_items, size_t& out_entity_count) {
    out_items.clear();
    out_entity_count = 0;
    if (!resolver || !view) return;

    if (g_acc_transform_cache.size() > 4000) g_acc_transform_cache.clear();

    out_items.reserve(entities.size() * 16);

    for (size_t entity_index = 0; entity_index < entities.size(); ++entity_index) {
        const auto* p_entity = entities[entity_index];
        if (!p_entity) continue;
        const auto& entity = *p_entity;

        bool is_local = false;
        {
            auto local = cache::get_local_snap();
            if (local && local->instance.address != 0 && entity.instance.address == local->instance.address) {
                is_local = true;
            }
            else if (cache::local_character.address != 0 && entity.parts.count("Head") > 0) {
                auto head_it = entity.parts.find("Head");
                if (head_it != entity.parts.end() && head_it->second.address != 0) {
                    uint64_t head_parent = 0;
                    if (read_raw(head_it->second.address + Offsets::Instance::Parent, &head_parent, sizeof(head_parent))) {
                        if (head_parent == cache::local_character.address) {
                            is_local = true;
                        }
                    }
                }
            }
        }

        // Accurate W2S entity-level frustum culling: skip entire entity if behind camera or offscreen
        math::vector3 hrp_pos = entity.part_positions.torso;
        if (hrp_pos.x == 0.f && hrp_pos.y == 0.f && hrp_pos.z == 0.f) hrp_pos = entity.part_positions.head;
        if (hrp_pos.x == 0.f && hrp_pos.y == 0.f && hrp_pos.z == 0.f) hrp_pos = entity.position;
        if (!is_local && (hrp_pos.x != 0.f || hrp_pos.y != 0.f || hrp_pos.z != 0.f)) {
            if (!is_w2s_visible(view, hrp_pos.x, hrp_pos.y, hrp_pos.z, 0.75f)) continue;
        }

        static const std::unordered_set<std::string> standard_limbs = {
            "Head", "Torso", "UpperTorso", "LowerTorso", "LeftUpperArm", "LeftLowerArm", "LeftHand",
            "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg",
            "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Left Arm", "Right Arm",
            "Left Leg", "Right Leg"
        };

        math::vector3 char_root = entity.position;

        size_t initial_draw_count = out_items.size();
        std::unordered_set<uint64_t> seen_part_addresses;

        for (const auto& [part_name, part_inst] : entity.parts) {
            if (part_inst.address == 0) continue;
            if (!seen_part_addresses.insert(part_inst.address).second) continue;
            if (part_name == "HumanoidRootPart" || part_name == "CollisionCapsule" || part_name == "Hitbox" || part_name == "hitbox") continue;

            std::string lower_part_name = part_name;
            for (char& c : lower_part_name) c = (char)tolower((unsigned char)c);
            if (lower_part_name.find("root") != std::string::npos ||
                lower_part_name.find("collision") != std::string::npos ||
                lower_part_name.find("capsule") != std::string::npos ||
                lower_part_name.find("collider") != std::string::npos ||
                lower_part_name.find("hitbox") != std::string::npos ||
                lower_part_name.find("bound") != std::string::npos ||
                lower_part_name.find("shield") != std::string::npos ||
                lower_part_name.find("aura") != std::string::npos ||
                lower_part_name.find("zone") != std::string::npos ||
                lower_part_name.find("trigger") != std::string::npos) {
                continue;
            }

            bool is_standard_body = (standard_limbs.find(part_name) != standard_limbs.end()) || (part_name.rfind("pfLimb", 0) == 0);
            bool is_accessory = !is_standard_body || (part_name.rfind("Accessory_", 0) == 0);
            if (is_accessory && !enable_accessories) continue;

            uintptr_t prim_addr = 0;
            if (!read_raw(part_inst.address + Offsets::BasePart::Primitive, &prim_addr, sizeof(prim_addr)) || prim_addr == 0) continue;

            struct alignas(16) FastPrimData {
                float rot[9];
                float pos[3];
            } fast_prim{};
            if (!read_raw(prim_addr + Offsets::Primitive::Rotation, &fast_prim, sizeof(fast_prim))) continue;
            if (fast_prim.pos[0] == 0.0f && fast_prim.pos[1] == 0.0f && fast_prim.pos[2] == 0.0f) continue;

            // Accurate W2S part-level frustum culling
            if (is_local) {
                float part_w = fast_prim.pos[0] * view[12] + fast_prim.pos[1] * view[13] + fast_prim.pos[2] * view[14] + view[15];
                if (part_w <= 0.1f) continue;
            } else {
                if (!is_w2s_visible(view, fast_prim.pos[0], fast_prim.pos[1], fast_prim.pos[2], 0.75f)) continue;
            }

            if (char_root.x != 0.0f || char_root.y != 0.0f || char_root.z != 0.0f) {
                float dx = fast_prim.pos[0] - char_root.x;
                float dy = fast_prim.pos[1] - char_root.y;
                float dz = fast_prim.pos[2] - char_root.z;
                if ((dx * dx + dy * dy + dz * dz) > 2500.0f) {
                    continue; // Skip detached/dropped/ghost parts
                }
            }

            const ResolvedMeshDraw resolved = resolver(entity, part_name, part_inst.address);
            if (!resolved.cache_key || !resolved.mesh) continue;

            Vec3 sz{};
            if (!read_raw(prim_addr + Offsets::Primitive::Size, &sz, sizeof(sz))) continue;
            if (sz.x <= 0.001f || sz.y <= 0.001f || sz.z <= 0.001f) sz = { 1.0f, 1.0f, 1.0f };

            PrimitiveState prim{};
            std::memcpy(prim.rot, fast_prim.rot, sizeof(prim.rot));
            prim.pos = { fast_prim.pos[0], fast_prim.pos[1], fast_prim.pos[2] };
            Vec3 size = { sz.x, sz.y, sz.z };

            SnapshotDraw item{};
            item.is_accessory = is_accessory;
            bool is_r15_character = (entity.parts.count("UpperTorso") > 0 && entity.parts.count("LowerTorso") > 0);
            item.is_r6_limb = !is_r15_character && is_standard_body;

            // Roblox mesh transform rules, applied 1:1:
            //  * Part + SpecialMesh/FileMesh: rendered at NATIVE mesh size x
            //    SpecialMesh.Scale (+ local Offset) — the part's Size is ignored.
            //    (classic heads, R6 limbs with meshes, old-style hats)
            //  * MeshPart: mesh is FITTED to the part's Size -> size / bounds.
            //    (R15 body, modern hats)
            // The SpecialMesh walk runs for EVERY part (cached per address) and
            // only produces a scale override when a SpecialMesh actually exists.
            {
                DWORD now = GetTickCount();
                auto acc_it = g_acc_transform_cache.find(part_inst.address);
                if (acc_it == g_acc_transform_cache.end()) {
                    CachedAccTransform entry{};
                    entry.last_scan = now;
                    entry.resolved = true;
                    rbx::c_instance part_inst_obj{ part_inst.address };
                    for (const auto& child : part_inst_obj.get_children<rbx::c_instance>()) {
                        if (!child.address) continue;
                        std::string child_cls = child.get_class_name();
                        if (child_cls == "SpecialMesh" || child_cls == "FileMesh" || child_cls == "Mesh" || child_cls == "CylinderMesh" || child_cls == "BlockMesh") {
                            read_raw(child.address + Offsets::SpecialMesh::Scale, &entry.scale, sizeof(entry.scale));
                            read_raw(child.address + Offsets::SpecialMesh::Offset, &entry.offset, sizeof(entry.offset));
                            if (!std::isfinite(entry.scale.x) || entry.scale.x < 1e-4f) entry.scale.x = 1.0f;
                            if (!std::isfinite(entry.scale.y) || entry.scale.y < 1e-4f) entry.scale.y = 1.0f;
                            if (!std::isfinite(entry.scale.z) || entry.scale.z < 1e-4f) entry.scale.z = 1.0f;
                            if (entry.scale.x > 50.0f) entry.scale.x = 50.0f;
                            if (entry.scale.y > 50.0f) entry.scale.y = 50.0f;
                            if (entry.scale.z > 50.0f) entry.scale.z = 50.0f;
                            if (!std::isfinite(entry.offset.x)) entry.offset.x = 0.0f;
                            if (!std::isfinite(entry.offset.y)) entry.offset.y = 0.0f;
                            if (!std::isfinite(entry.offset.z)) entry.offset.z = 0.0f;
                            entry.has_special = true;
                            break;
                        }
                    }
                    g_acc_transform_cache[part_inst.address] = entry;
                    acc_it = g_acc_transform_cache.find(part_inst.address);
                }

                if (acc_it != g_acc_transform_cache.end() && acc_it->second.has_special) {
                    const auto& tr = acc_it->second;
                    item.has_scale_override = true;
                    item.scale_override = tr.scale;
                    prim.pos.x += prim.rot[0] * tr.offset.x + prim.rot[1] * tr.offset.y + prim.rot[2] * tr.offset.z;
                    prim.pos.y += prim.rot[3] * tr.offset.x + prim.rot[4] * tr.offset.y + prim.rot[5] * tr.offset.z;
                    prim.pos.z += prim.rot[6] * tr.offset.x + prim.rot[7] * tr.offset.y + prim.rot[8] * tr.offset.z;
                } else if (is_accessory && resolved.mesh && resolved.mesh->version == "procedural_beveled") {
                    // Skip accessory parts that have no authored mesh to avoid rendering giant bounding boxes
                    continue;
                }

                if (part_name == "Head") {
                    if (!item.has_scale_override) {
                        if (size.x > 1.7f && size.y < 1.3f && size.z < 1.3f) {
                            size = { 1.25f, 1.25f, 1.25f };
                        } else if (size.x < 0.1f || size.y < 0.1f || size.z < 0.1f) {
                            size = { 1.25f, 1.25f, 1.25f };
                        }
                    }
                }
            }

            item.part_name = part_name;
            item.cache_key = resolved.cache_key;
            item.mesh = resolved.mesh;
            item.prim = prim;
            item.size = size;
            item.entity_index = entity_index;
            out_items.push_back(item);
        }

        if (out_items.size() > initial_draw_count) {
            ++out_entity_count;
        }
    }
}

static void build_snapshot(const std::vector<cache::entity_t>& entities, const float view[16], MeshResolver resolver, bool enable_accessories, std::vector<SnapshotDraw>& out_items, size_t& out_entity_count) {
    static thread_local std::vector<const cache::entity_t*> ptrs;
    ptrs.clear();
    ptrs.reserve(entities.size());
    for (const auto& ent : entities) ptrs.push_back(&ent);
    build_snapshot_ptrs(ptrs, view, resolver, enable_accessories, out_items, out_entity_count);
}

static void setup_mesh_pipeline(ID3D11DeviceContext* context, const float view[16], const float camera_pos[3], float viewport_offset_x, float viewport_offset_y, float viewport_width, float viewport_height, ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depth_state, UINT stencil_ref, ID3D11BlendState* blend_state, ID3D11PixelShader* pixel_shader) {
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = viewport_offset_x;
    vp.TopLeftY = viewport_offset_y;
    vp.Width = viewport_width;
    vp.Height = viewport_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
    context->IASetInputLayout(g_layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_vs, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &g_frame_cb);
    context->VSSetConstantBuffers(1, 1, &g_object_cb);
    context->PSSetConstantBuffers(0, 1, &g_frame_cb);
    context->PSSetConstantBuffers(1, 1, &g_object_cb);
    context->OMSetBlendState(blend_state, nullptr, 0xffffffff);
    context->RSSetState(rasterizer);
    context->OMSetDepthStencilState(depth_state, stencil_ref);
    upload_frame_cb(context, view, camera_pos);
}

static void draw_snapshot_meshes(ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<SnapshotDraw>& items, const float color[4], const float outline_color[4], int shader_type) {
    uint64_t last_bound_key = 0;
    UINT stride = sizeof(GpuVertex);
    UINT offset = 0;

    for (size_t i = 0; i < items.size(); ++i) {
        const SnapshotDraw& item = items[i];
        if (!item.mesh) continue;
        GpuMesh* gpu_mesh = ensure_gpu_mesh(device, item.cache_key, *item.mesh);
        if (!gpu_mesh) continue;

        if (item.cache_key != last_bound_key) {
            context->IASetVertexBuffers(0, 1, &gpu_mesh->vb, &stride, &offset);
            context->IASetIndexBuffer(gpu_mesh->ib, DXGI_FORMAT_R32_UINT, 0);
            last_bound_key = item.cache_key;
        }

        if (!upload_object_cb(context, item, color, outline_color, shader_type)) continue;

        // Force solid rasterizer before every draw — game may reset state on shared D3D11 context
        context->RSSetState(g_rasterizer);
        context->DrawIndexed(gpu_mesh->index_count, 0, 0);
    }
}

static void draw_fullscreen(ID3D11DeviceContext* context, float viewport_width, float viewport_height, ID3D11DepthStencilState* depth_state, UINT stencil_ref, ID3D11PixelShader* pixel_shader, const float fill_color[4], const float outline_color[4], float outline_thickness, int outline_style, bool outline_only, ID3D11ShaderResourceView* mask_srv) {
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = viewport_width;
    vp.Height = viewport_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    if (!upload_fullscreen_cb(context, fill_color, outline_color, viewport_width, viewport_height, outline_thickness, outline_style, outline_only)) {
        return;
    }

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_fullscreen_vs, nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &g_fullscreen_cb);
    context->RSSetState(g_no_cull_rasterizer);
    context->OMSetBlendState(g_blend, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(depth_state, stencil_ref);

    ID3D11ShaderResourceView* null_srv = nullptr;
    if (mask_srv) {
        context->PSSetShaderResources(0, 1, &mask_srv);
        context->PSSetSamplers(0, 1, &g_point_sampler);
    }

    context->Draw(3, 0);

    if (mask_srv) {
        context->PSSetShaderResources(0, 1, &null_srv);
    }
}

void update_depth_bias(ID3D11Device* device, float depth_bias) {
    if (!device) return;
    if (depth_bias == g_last_depth_bias) return;
    g_last_depth_bias = depth_bias;

    // Recreate solid rasterizer with the new SlopeScaledDepthBias
    if (g_rasterizer) { g_rasterizer->Release(); g_rasterizer = nullptr; }

    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_BACK;
    desc.FrontCounterClockwise = FALSE;
    desc.DepthClipEnable = TRUE;
    desc.SlopeScaledDepthBias = depth_bias;
    desc.DepthBias = (depth_bias != 0.0f) ? static_cast<INT>(depth_bias * -100000.0f) : 0;
    desc.DepthBiasClamp = 0.0f;
    device->CreateRasterizerState(&desc, &g_rasterizer);
}

size_t render_advanced_ptrs(const std::vector<const cache::entity_t*>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_color[4], const float outline_color[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, MeshResolver resolver, float viewport_offset_x, float viewport_offset_y) {
    g_last_drawn_entities = 0;
    if (!device || !context || !resolver || entities.empty()) return 0;

    static thread_local std::vector<SnapshotDraw> snapshot;
    size_t entity_count = 0;
    build_snapshot_ptrs(entities, view, resolver, enable_accessories, snapshot, entity_count);
    if (snapshot.empty()) return 0;

    std::sort(snapshot.begin(), snapshot.end(), [](const SnapshotDraw& a, const SnapshotDraw& b) {
        return a.cache_key < b.cache_key;
    });

    if (!ensure_pipeline(device)) return 0;

    ID3D11RenderTargetView* current_rtv = nullptr;
    ID3D11DepthStencilView* current_dsv = nullptr;
    context->OMGetRenderTargets(1, &current_rtv, &current_dsv);

    UINT target_w = (UINT)(viewport_width + (std::max)(0.0f, viewport_offset_x));
    UINT target_h = (UINT)(viewport_height + (std::max)(0.0f, viewport_offset_y));
    if (current_rtv) {
        ID3D11Resource* res = nullptr;
        current_rtv->GetResource(&res);
        if (res) {
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex) {
                D3D11_TEXTURE2D_DESC td;
                tex->GetDesc(&td);
                target_w = (std::max)(target_w, td.Width);
                target_h = (std::max)(target_h, td.Height);
                tex->Release();
            }
            res->Release();
        }
    }

    bool has_depth = ensure_depth_buffer(device, target_w, target_h);
    if (has_depth && current_rtv) {
        context->ClearDepthStencilView(g_depth_stencil_view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        context->OMSetRenderTargets(1, &current_rtv, g_depth_stencil_view);
    }

    // Save the game's rasterizer state — on a hooked D3D11 context, Roblox may have set wireframe
    ID3D11RasterizerState* saved_rasterizer = nullptr;
    context->RSGetState(&saved_rasterizer);

    // Force solid fill before anything — override any wireframe state the game set
    context->RSSetState(g_rasterizer);

    // Always render solid - wireframe mode disabled
    bool is_pure_wireframe = false;

    // Pass 1 (color pass): render directly with depth WRITE (LESS_EQUAL) to eliminate z-fighting.
    if (!outline_only || !enable_outline) {
        bool is_opaque = (fill_color[3] >= 0.98f);
        setup_mesh_pipeline(
            context,
            view,
            camera_pos,
            viewport_offset_x,
            viewport_offset_y,
            viewport_width,
            viewport_height,
            g_rasterizer,
            has_depth ? g_depth_write : nullptr,
            0,
            is_opaque ? g_opaque_blend : g_blend,
            g_ps);
        draw_snapshot_meshes(device, context, snapshot, fill_color, outline_color, shader_type);
    }

    // 2. Render outline if enabled (Silhouette mode only)
    if (enable_outline && !is_pure_wireframe && outline_color) {
        if (ensure_union_mask_target(device, target_w, target_h)) {
            if (current_rtv) {
                const float clear_mask[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                context->ClearRenderTargetView(g_union_mask_rtv, clear_mask);

                context->OMSetRenderTargets(1, &g_union_mask_rtv, has_depth ? g_depth_stencil_view : nullptr);
                setup_mesh_pipeline(context, view, camera_pos, viewport_offset_x, viewport_offset_y, viewport_width, viewport_height, g_rasterizer, has_depth ? g_depth_read : nullptr, 0, nullptr, g_mask_ps);
                static const float kMaskColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                draw_snapshot_meshes(device, context, snapshot, kMaskColor, nullptr, 0);

                // Render fullscreen outline from mask texture onto current_rtv
                context->OMSetRenderTargets(1, &current_rtv, current_dsv);
                static const float kTransparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                draw_fullscreen(context, (float)target_w, (float)target_h, nullptr, 0, g_fullscreen_outline_ps, kTransparent, outline_color, outline_thickness, outline_style, outline_only, g_union_mask_srv);
            }
        }
    }

    // Restore the game's original rasterizer state
    context->RSSetState(saved_rasterizer);
    if (saved_rasterizer) saved_rasterizer->Release();

    if (current_rtv) {
        context->OMSetRenderTargets(1, &current_rtv, current_dsv);
        current_rtv->Release();
    }
    if (current_dsv) current_dsv->Release();

    g_last_drawn_entities = entity_count;
    return entity_count;
}

size_t render_advanced(const std::vector<cache::entity_t>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_color[4], const float outline_color[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, MeshResolver resolver, float viewport_offset_x, float viewport_offset_y) {
    static thread_local std::vector<const cache::entity_t*> ptrs;
    ptrs.clear();
    ptrs.reserve(entities.size());
    for (const auto& ent : entities) ptrs.push_back(&ent);
    return render_advanced_ptrs(ptrs, view, camera_pos, viewport_width, viewport_height, device, context, fill_color, outline_color, shader_type, enable_outline, outline_mode, outline_style, outline_thickness, outline_only, enable_accessories, resolver, viewport_offset_x, viewport_offset_y);
}

size_t render_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver) {
    return render_advanced(entities, view, nullptr, viewport_width, viewport_height, device, context, color, nullptr, 0, false, 0, 0, 2.5f, false, true, resolver);
}

size_t render_wireframe_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver) {
    return render_advanced(entities, view, nullptr, viewport_width, viewport_height, device, context, color, nullptr, 1, false, 0, 0, 2.5f, false, true, resolver);
}

size_t render(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4]) {
    return render_advanced(entities, view, nullptr, viewport_width, viewport_height, device, context, color, nullptr, 0, false, 0, 0, 2.5f, false, true, &default_resolver);
}

size_t get_last_drawn_entities() {
    return g_last_drawn_entities;
}

void shutdown() {
    release_resources();
}

} // namespace meshgpu
