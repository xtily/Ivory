#include "modelviewer.h"
#include <fstream>
#include <filesystem>
#include <ui/menu/settings/functions.h>
#include <features/system/settings/settings.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <sdk/game/game.h>
#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>
#include "3d/mainapi.hpp"

#pragma comment(lib, "d3dcompiler.lib")

namespace ModelViewer
{

ModelViewerSystem g_model_viewer;
ModelViewerSystem g_player_model_viewer;

static const char* g_shader_code = R"(
cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 LightDir;
    float4 LightColor;
    float4 ObjectColor;
    float4 AmbientLight;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Col : COLOR;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float3 Norm : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Col : COLOR;
    float3 WorldPos : TEXCOORD1;
};

PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);
    output.Norm = mul(float4(input.Norm, 0.0f), World).xyz;
    output.Tex = input.Tex;
    output.Col = input.Col;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.Tex);
    float3 norm = normalize(input.Norm);
    float3 lightDir = normalize(LightDir.xyz);
    float diff = max(dot(norm, lightDir), 0.0f);
    
    float3 ambient = AmbientLight.xyz;
    float3 diffuse = diff * LightColor.xyz;
    float3 finalColor = (ambient + diffuse) * input.Col.rgb * ObjectColor.rgb * texColor.rgb;
    
    return float4(finalColor, input.Col.a * ObjectColor.a * texColor.a);
}
)";

ModelViewerSystem::ModelViewerSystem()
{
}

ModelViewerSystem::~ModelViewerSystem()
{
    CleanupDX();
}

void ModelViewerSystem::CleanupDX()
{
    if (render_target_tex) { render_target_tex->Release(); render_target_tex = nullptr; }
    if (render_target_view) { render_target_view->Release(); render_target_view = nullptr; }
    if (shader_resource_view) { shader_resource_view->Release(); shader_resource_view = nullptr; }
    if (depth_stencil_tex) { depth_stencil_tex->Release(); depth_stencil_tex = nullptr; }
    if (depth_stencil_view) { depth_stencil_view->Release(); depth_stencil_view = nullptr; }

    if (vertex_shader) { vertex_shader->Release(); vertex_shader = nullptr; }
    if (pixel_shader) { pixel_shader->Release(); pixel_shader = nullptr; }
    if (input_layout) { input_layout->Release(); input_layout = nullptr; }
    if (vertex_buffer) { vertex_buffer->Release(); vertex_buffer = nullptr; }
    if (index_buffer) { index_buffer->Release(); index_buffer = nullptr; }
    if (constant_buffer) { constant_buffer->Release(); constant_buffer = nullptr; }
    if (raster_solid) { raster_solid->Release(); raster_solid = nullptr; }
    if (raster_wireframe) { raster_wireframe->Release(); raster_wireframe = nullptr; }
    if (depth_state) { depth_state->Release(); depth_state = nullptr; }
    if (texture_sampler) { texture_sampler->Release(); texture_sampler = nullptr; }
    if (default_white_srv) { default_white_srv->Release(); default_white_srv = nullptr; }

    initialized = false;
    rt_width = 0.f;
    rt_height = 0.f;
}

void ModelViewerSystem::ResetCamera()
{
    cam_yaw = 0.0f; // Face front towards user
    cam_pitch = 0.0f;
    cam_distance = 1.9f;
    cam_target = { 0.f, 0.f, 0.f };
}

bool ModelViewerSystem::CreateDefaultWhiteTexture(ID3D11Device* device)
{
    if (default_white_srv || !device) return true;

    uint32_t white_pixel = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = &white_pixel;
    init_data.SysMemPitch = 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init_data, &tex);
    if (FAILED(hr)) return false;

    hr = device->CreateShaderResourceView(tex, nullptr, &default_white_srv);
    tex->Release();
    return SUCCEEDED(hr);
}

bool ModelViewerSystem::CreateShaders(ID3D11Device* device)
{
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* error_blob = nullptr;

    HRESULT hr = D3DCompile(g_shader_code, strlen(g_shader_code), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vs_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob) error_blob->Release();
        return false;
    }

    hr = D3DCompile(g_shader_code, strlen(g_shader_code), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &ps_blob, &error_blob);
    if (FAILED(hr))
    {
        if (vs_blob) vs_blob->Release();
        if (error_blob) error_blob->Release();
        return false;
    }

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader);
    if (FAILED(hr))
    {
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }

    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader);
    if (FAILED(hr))
    {
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex3D, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex3D, normal),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex3D, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex3D, color), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &input_layout);
    vs_blob->Release();
    ps_blob->Release();
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.ByteWidth = sizeof(ConstantBufferData);
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&cb_desc, nullptr, &constant_buffer);
    if (FAILED(hr)) return false;

    D3D11_RASTERIZER_DESC rast_desc{};
    rast_desc.FillMode = D3D11_FILL_SOLID;
    rast_desc.CullMode = D3D11_CULL_NONE;
    rast_desc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rast_desc, &raster_solid);

    rast_desc.FillMode = D3D11_FILL_WIREFRAME;
    device->CreateRasterizerState(&rast_desc, &raster_wireframe);

    D3D11_DEPTH_STENCIL_DESC depth_desc{};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&depth_desc, &depth_state);

    D3D11_SAMPLER_DESC samp_desc{};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp_desc.MinLOD = 0;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&samp_desc, &texture_sampler);

    CreateDefaultWhiteTexture(device);

    return true;
}

bool ModelViewerSystem::CreateBuffers(ID3D11Device* device)
{
    if (vertex_buffer) { vertex_buffer->Release(); vertex_buffer = nullptr; }
    if (index_buffer) { index_buffer->Release(); index_buffer = nullptr; }

    if (model_data.vertices.empty()) return false;

    D3D11_BUFFER_DESC vb_desc{};
    vb_desc.Usage = D3D11_USAGE_DEFAULT;
    vb_desc.ByteWidth = static_cast<UINT>(sizeof(Vertex3D) * model_data.vertices.size());
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data{};
    vb_data.pSysMem = model_data.vertices.data();
    HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &vertex_buffer);
    if (FAILED(hr)) return false;

    if (!model_data.indices.empty())
    {
        D3D11_BUFFER_DESC ib_desc{};
        ib_desc.Usage = D3D11_USAGE_DEFAULT;
        ib_desc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * model_data.indices.size());
        ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ib_data{};
        ib_data.pSysMem = model_data.indices.data();
        hr = device->CreateBuffer(&ib_desc, &ib_data, &index_buffer);
        if (FAILED(hr)) return false;
    }

    return true;
}

bool ModelViewerSystem::CreateOffscreenResources(ID3D11Device* device, float width, float height)
{
    if (width <= 0.f || height <= 0.f) return false;
    if (render_target_tex && std::abs(rt_width - width) < 2.f && std::abs(rt_height - height) < 2.f)
        return true;

    if (render_target_tex) render_target_tex->Release();
    if (render_target_view) render_target_view->Release();
    if (shader_resource_view) shader_resource_view->Release();
    if (depth_stencil_tex) depth_stencil_tex->Release();
    if (depth_stencil_view) depth_stencil_view->Release();

    rt_width = width;
    rt_height = height;

    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = static_cast<UINT>(width);
    tex_desc.Height = static_cast<UINT>(height);
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &render_target_tex);
    if (FAILED(hr)) return false;

    hr = device->CreateRenderTargetView(render_target_tex, nullptr, &render_target_view);
    if (FAILED(hr)) return false;

    hr = device->CreateShaderResourceView(render_target_tex, nullptr, &shader_resource_view);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC depth_tex_desc{};
    depth_tex_desc.Width = static_cast<UINT>(width);
    depth_tex_desc.Height = static_cast<UINT>(height);
    depth_tex_desc.MipLevels = 1;
    depth_tex_desc.ArraySize = 1;
    depth_tex_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_tex_desc.SampleDesc.Count = 1;
    depth_tex_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = device->CreateTexture2D(&depth_tex_desc, nullptr, &depth_stencil_tex);
    if (FAILED(hr)) return false;

    hr = device->CreateDepthStencilView(depth_stencil_tex, nullptr, &depth_stencil_view);
    return SUCCEEDED(hr);
}

void ModelViewerSystem::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (initialized || !device) return;
    d3d_device = device;

    if (!CreateShaders(device)) return;

    // LoadModelFromMemory(device, g_default_model_bytes, g_default_model_bytes_size);
    initialized = true;
}

bool ModelViewerSystem::LoadModelFromMemory(ID3D11Device* device, const uint8_t* data, size_t size)
{
    model_data.Clear();

    bool ok = LoadGLBModelFromMemory(data, size, model_data, device);
    if (ok && device)
    {
        CreateBuffers(device);
        ResetCamera();
    }
    return ok;
}

bool ModelViewerSystem::LoadOBJModelFromCache(const ::c_obj_model& obj_model, const std::string& user_id, ID3D11Device* device) {
    model_data.Clear();

    if (!obj_model.valid) {
        model_data.error_message = "OBJ model invalid";
        return false;
    }

    std::unordered_map<uint64_t, uint32_t> unique_vertices;
    std::unordered_map<std::string, int> material_map;
    std::unordered_map<int, int> texture_index_map;

    std::ofstream log("C:\\Users\\8\\AppData\\Local\\Roblox\\avatar_debug.log", std::ios::app);
    log << "[INFO] LoadOBJModelFromCache started for user " << user_id << ", device: " << device << "\n";
    log << "[INFO] obj_model.materials count: " << obj_model.materials.size() << "\n";

    for (const auto& [mat_name, obj_mat] : obj_model.materials) {
        MaterialData mat;
        mat.name = mat_name;
        mat.base_color_factor = DirectX::XMFLOAT4(obj_mat.diffuse[0], obj_mat.diffuse[1], obj_mat.diffuse[2], 1.0f);

        int orig_tex_idx = obj_mat.texture_index;
        log << "[INFO] Material: " << mat_name << ", diffuse: " << obj_mat.diffuse[0] << "," << obj_mat.diffuse[1] << "," << obj_mat.diffuse[2] << ", orig_tex_idx: " << orig_tex_idx << "\n";
        if (orig_tex_idx >= 0) {
            auto it = texture_index_map.find(orig_tex_idx);
            if (it != texture_index_map.end()) {
                mat.texture_index = it->second;
                log << "[INFO] Reused texture index: " << mat.texture_index << "\n";
            }
            else {
                auto* decoded = ::c_texture_cache::get().get_texture(user_id, orig_tex_idx);
                if (decoded && decoded->ready.load()) {
                    TextureData tex;
                    tex.width = decoded->width;
                    tex.height = decoded->height;
                    tex.pixels = decoded->pixels;
                    int new_tex_idx = static_cast<int>(model_data.textures.size());
                    model_data.textures.push_back(tex);
                    texture_index_map[orig_tex_idx] = new_tex_idx;
                    mat.texture_index = new_tex_idx;
                    log << "[INFO] Loaded texture: index: " << new_tex_idx << ", size: " << tex.width << "x" << tex.height << ", pixels count: " << tex.pixels.size() << "\n";
                }
                else {
                    mat.texture_index = -1;
                    log << "[WARNING] Decoded texture not ready or null! decoded: " << decoded << "\n";
                }
            }
        }
        else {
            mat.texture_index = -1;
        }

        int mat_idx = static_cast<int>(model_data.materials.size());
        model_data.materials.push_back(mat);
        material_map[mat_name] = mat_idx;
    }

    std::unordered_map<int, std::vector<const ::c_obj_face*>> faces_by_material;
    for (const auto& face : obj_model.faces) {
        int mat_idx = -1;
        auto it = material_map.find(face.material_name);
        if (it != material_map.end()) {
            mat_idx = it->second;
        }
        faces_by_material[mat_idx].push_back(&face);
    }

    for (const auto& [mat_idx, faces] : faces_by_material) {
        SubMesh submesh;
        submesh.material_index = mat_idx;
        submesh.name = (mat_idx >= 0) ? model_data.materials[mat_idx].name : "Default";
        submesh.index_offset = static_cast<uint32_t>(model_data.indices.size());

        uint32_t start_index_count = static_cast<uint32_t>(model_data.indices.size());

        for (const auto* face_ptr : faces) {
            const auto& face = *face_ptr;
            if (face.vertex_indices.size() < 3) continue;

            for (size_t i = 1; i + 1 < face.vertex_indices.size(); ++i) {
                int indices_to_add[3] = { 0, static_cast<int>(i), static_cast<int>(i + 1) };
                for (int corner : indices_to_add) {
                    int v_idx = face.vertex_indices[corner];
                    int vt_idx = -1;
                    if (corner < static_cast<int>(face.texcoord_indices.size())) {
                        vt_idx = face.texcoord_indices[corner];
                    }

                    uint64_t key = (static_cast<uint64_t>(v_idx) << 32) | (static_cast<uint32_t>(vt_idx) & 0xFFFFFFFF);
                    auto vit = unique_vertices.find(key);
                    uint32_t final_idx;
                    if (vit != unique_vertices.end()) {
                        final_idx = vit->second;
                    }
                    else {
                        Vertex3D vert{};
                        vert.position.x = obj_model.vertices[v_idx].x;
                        vert.position.y = obj_model.vertices[v_idx].y;
                        vert.position.z = obj_model.vertices[v_idx].z;

                        vert.normal.x = face.normal[0];
                        vert.normal.y = face.normal[1];
                        vert.normal.z = face.normal[2];

                        if (vt_idx >= 0 && vt_idx < static_cast<int>(obj_model.tex_coords.size())) {
                            vert.texcoord.x = obj_model.tex_coords[vt_idx].u;
                            vert.texcoord.y = 1.0f - obj_model.tex_coords[vt_idx].v;
                        }
                        else {
                            vert.texcoord.x = 0.0f;
                            vert.texcoord.y = 0.0f;
                        }

                        vert.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

                        final_idx = static_cast<uint32_t>(model_data.vertices.size());
                        model_data.vertices.push_back(vert);
                        unique_vertices[key] = final_idx;
                    }
                    model_data.indices.push_back(final_idx);
                }
            }
        }
        submesh.index_count = static_cast<uint32_t>(model_data.indices.size()) - start_index_count;
        if (submesh.index_count > 0) {
            model_data.submeshes.push_back(submesh);
        }
    }

    model_data.CalculateBoundsAndNormals();
    if (device) {
        model_data.CreateDX11Textures(device);
        CreateBuffers(device);
    }
    model_data.loaded = !model_data.vertices.empty();
    return model_data.loaded;
}

bool ModelViewerSystem::LoadModelFile(ID3D11Device* device, const std::string& path)
{
    current_file_path = path;
    strncpy_s(path_buffer, path.c_str(), sizeof(path_buffer) - 1);

    bool ok = LoadModelAny(path, model_data, device);
    if (ok && device)
    {
        CreateBuffers(device);
        ResetCamera();
    }
    return ok;
}

void ModelViewerSystem::Render3D(ID3D11DeviceContext* context, float width, float height)
{
    if (!context) return;

    if (!d3d_device) {
        context->GetDevice(&d3d_device);
        if (d3d_device) {
            d3d_device->Release();
        }
    }

    if (d3d_device) {
        int desired_model_type = (this == &g_player_model_viewer) ? 1 : settings::misc::preview_model;

        if (desired_model_type == 1) {
            if (current_model_source != 1) {
                loaded_avatar_id.clear();
            }
            if (this == &g_model_viewer) {
                std::uint64_t local_uid = GetLocalPlayerUserId();
                if (local_uid != 0) {
                    sprintf_s(avatar_user_id, sizeof(avatar_user_id), "%llu", local_uid);
                }
            }
            if (loaded_avatar_id != avatar_user_id) {
                auto* data = ::c_avatar_3d_api::get().request_data(avatar_user_id);
                if (data && data->ready) {
                    bool all_textures_ready = true;
                    for (size_t i = 0; i < data->texture_data.size(); ++i) {
                        auto* decoded = ::c_avatar_3d_api::get().get_decoded_texture(avatar_user_id, static_cast<int>(i));
                        if (!decoded || !decoded->ready.load()) {
                            all_textures_ready = false;
                            break;
                        }
                    }
                    if (!data->face_texture_hash.empty()) {
                        auto* decoded_face = ::c_avatar_3d_api::get().get_decoded_face_texture(avatar_user_id);
                        if (!decoded_face || !decoded_face->ready.load()) {
                            all_textures_ready = false;
                        }
                    }

                    if (all_textures_ready) {
                        std::ofstream log("C:\\Users\\8\\AppData\\Local\\Roblox\\avatar_debug.log", std::ios::app);
                        log << "[INFO] All textures ready, loading model for " << avatar_user_id << "\n";
                        ::c_obj_model obj_model;
                        bool parsed_obj = ::c_avatar_3d_api::get().parse_obj_model(data->obj_data, obj_model);
                        log << "[INFO] parse_obj_model returned: " << parsed_obj << "\n";
                        if (parsed_obj) {
                            ::c_avatar_3d_api::get().parse_mtl_data(data->mtl_data, obj_model, data->texture_hashes);
                            log << "[INFO] parse_mtl_data finished\n";
                            bool loaded_cache = LoadOBJModelFromCache(obj_model, avatar_user_id, d3d_device);
                            log << "[INFO] LoadOBJModelFromCache returned: " << loaded_cache << "\n";
                            if (loaded_cache) {
                                loaded_avatar_id = avatar_user_id;
                                current_model_source = 1;
                                ResetCamera();
                                cam_yaw = 3.14159265f; // Roblox OBJ avatar faces camera at pi
                                log << "[INFO] Reset camera to default centered position facing front\n";
                            }
                        }
                    }
                }
            }
        }
        else if (desired_model_type == 2) {
            // Tung Tung Tung Sahur model
            if (current_model_source != 2) {
                LoadModelFromMemory(d3d_device, g_tung_tung_tung_sahur_bytes, g_tung_tung_tung_sahur_bytes_size);
                loaded_avatar_id.clear();
                current_model_source = 2;
                cam_fov = 45.0f;
                ResetCamera();
                cam_yaw = 0.0f; // Face front towards user
            }
        }
        else if (desired_model_type == 3) {
            // Custom model from Resources or path
            std::string custom_path = settings::misc::custom_model_path;
            if (current_model_source != 3 || current_file_path != custom_path) {
                if (!custom_path.empty() && std::filesystem::exists(custom_path)) {
                    LoadModelFile(d3d_device, custom_path);
                    current_file_path = custom_path;
                }
                else {
                    // Fallback to searching Resources directory
                    std::string res_dir = "C:\\Ivory\\Resources";
                    bool loaded_any = false;
                    if (std::filesystem::exists(res_dir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(res_dir)) {
                            if (entry.is_regular_file()) {
                                std::string ext = entry.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == ".glb" || ext == ".gltf") {
                                    LoadModelFile(d3d_device, entry.path().string());
                                    current_file_path = entry.path().string();
                                    strncpy_s(settings::misc::custom_model_path, entry.path().string().c_str(), sizeof(settings::misc::custom_model_path) - 1);
                                    loaded_any = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!loaded_any) {
                        LoadModelFromMemory(d3d_device, g_tung_tung_tung_sahur_bytes, g_tung_tung_tung_sahur_bytes_size);
                    }
                }
                loaded_avatar_id.clear();
                current_model_source = 3;
                cam_fov = 45.0f;
                ResetCamera();
                cam_yaw = 0.0f;
            }
        }
        else {
            // Arsenal / Default model (0)
            if (current_model_source != 0) {
                LoadModelFromMemory(d3d_device, g_default_model_bytes, sizeof(g_default_model_bytes));
                loaded_avatar_id.clear();
                current_model_source = 0;
                cam_fov = 45.0f;
                ResetCamera();
                cam_yaw = 0.0f; // Face front towards user
            }
        }

        if (model_data.vertices.empty()) {
            if (desired_model_type == 1) {
                // Keep waiting for avatar to finish loading without loading Arsenal model
            }
            else if (desired_model_type == 2) {
                LoadModelFromMemory(d3d_device, g_tung_tung_tung_sahur_bytes, g_tung_tung_tung_sahur_bytes_size);
                current_model_source = 2;
            }
            else if (desired_model_type == 3 && strlen(settings::misc::custom_model_path) > 0 && std::filesystem::exists(settings::misc::custom_model_path)) {
                LoadModelFile(d3d_device, settings::misc::custom_model_path);
                current_model_source = 3;
            }
            else {
                LoadModelFromMemory(d3d_device, g_default_model_bytes, sizeof(g_default_model_bytes));
                current_model_source = 0;
            }
        }
    }

    if (!render_target_view || !depth_stencil_view || !vertex_buffer) return;

    ImVec4 child_bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    float clear_col[4] = {
        child_bg.x,
        child_bg.y,
        child_bg.z,
        0.0f
    };

    context->ClearRenderTargetView(render_target_view, clear_col);
    context->ClearDepthStencilView(depth_stencil_view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp{};
    vp.Width = width;
    vp.Height = height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    context->OMSetRenderTargets(1, &render_target_view, depth_stencil_view);
    context->OMSetDepthStencilState(depth_state, 0);

    if (auto_rotate && !is_dragging_healthbar)
    {
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt > 0.0001f && dt < 0.1f)
                cam_yaw += dt * rotate_speed;
            else
                cam_yaw += 0.016f * rotate_speed;
        }
    }

    DirectX::XMVECTOR target = DirectX::XMVectorSet(cam_target.x, cam_target.y, cam_target.z, 0.0f);
    
    float cos_p = cosf(cam_pitch);
    float sin_p = sinf(cam_pitch);
    float cos_y = cosf(cam_yaw);
    float sin_y = sinf(cam_yaw);

    float eye_x = cam_target.x + cam_distance * cos_p * sin_y;
    float eye_y = cam_target.y + cam_distance * sin_p;
    float eye_z = cam_target.z + cam_distance * cos_p * cos_y;

    DirectX::XMVECTOR eye = DirectX::XMVectorSet(eye_x, eye_y, eye_z, 0.0f);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view_mat = DirectX::XMMatrixLookAtLH(eye, target, up);
    float aspect = (height > 0.f) ? (width / height) : 1.0f;
    DirectX::XMMATRIX proj_mat = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(cam_fov), aspect, 0.01f, 100.0f);

    float scale = (model_data.radius > 0.0001f) ? (0.85f / model_data.radius) : 1.0f;
    DirectX::XMMATRIX trans_center = DirectX::XMMatrixTranslation(-model_data.center.x, -model_data.center.y, -model_data.center.z);
    DirectX::XMMATRIX scale_mat = DirectX::XMMatrixScaling(scale, scale, scale);
    DirectX::XMMATRIX world_mat = trans_center * scale_mat;

    ConstantBufferData cb_data{};
    cb_data.world = DirectX::XMMatrixTranspose(world_mat);
    cb_data.view = DirectX::XMMatrixTranspose(view_mat);
    cb_data.projection = DirectX::XMMatrixTranspose(proj_mat);

    DirectX::XMVECTOR light_v = DirectX::XMVector3Normalize(DirectX::XMVectorSet(light_dir[0], light_dir[1], light_dir[2], 0.0f));
    DirectX::XMStoreFloat4(&cb_data.light_dir, light_v);
    cb_data.light_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, light_intensity);
    cb_data.ambient_light = DirectX::XMFLOAT4(0.35f, 0.35f, 0.35f, 1.0f);

    UINT stride = sizeof(Vertex3D);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    if (index_buffer)
        context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(input_layout);
    context->VSSetShader(vertex_shader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constant_buffer);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constant_buffer);

    if (texture_sampler)
    {
        context->PSSetSamplers(0, 1, &texture_sampler);
    }

    auto draw_submeshes_pass = [&](ID3D11RasterizerState* r_state, bool override_color = false, DirectX::XMFLOAT4 override_col = {1,1,1,1}) {
        context->RSSetState(r_state);

        if (model_data.submeshes.empty())
        {
            cb_data.object_color = override_color ? override_col : DirectX::XMFLOAT4(object_tint[0], object_tint[1], object_tint[2], object_tint[3]);
            context->UpdateSubresource(constant_buffer, 0, nullptr, &cb_data, 0, 0);

            ID3D11ShaderResourceView* bind_srv = default_white_srv;
            context->PSSetShaderResources(0, 1, &bind_srv);

            if (index_buffer && !model_data.indices.empty())
                context->DrawIndexed(static_cast<UINT>(model_data.indices.size()), 0, 0);
            else
                context->Draw(static_cast<UINT>(model_data.vertices.size()), 0);
        }
        else
        {
            for (const auto& submesh : model_data.submeshes)
            {
                DirectX::XMFLOAT4 mat_color = { object_tint[0], object_tint[1], object_tint[2], object_tint[3] };
                ID3D11ShaderResourceView* mat_srv = default_white_srv;

                if (submesh.material_index >= 0 && submesh.material_index < (int)model_data.materials.size())
                {
                    const auto& mat = model_data.materials[submesh.material_index];
                    mat_color.x *= mat.base_color_factor.x;
                    mat_color.y *= mat.base_color_factor.y;
                    mat_color.z *= mat.base_color_factor.z;
                    mat_color.w *= mat.base_color_factor.w;

                    if (mat.texture_index >= 0 && mat.texture_index < (int)model_data.textures.size())
                    {
                        if (model_data.textures[mat.texture_index].srv)
                        {
                            mat_srv = model_data.textures[mat.texture_index].srv;
                        }
                    }
                }

                cb_data.object_color = override_color ? override_col : mat_color;
                context->UpdateSubresource(constant_buffer, 0, nullptr, &cb_data, 0, 0);

                context->PSSetShaderResources(0, 1, &mat_srv);

                if (index_buffer && submesh.index_count > 0)
                {
                    context->DrawIndexed(submesh.index_count, submesh.index_offset, 0);
                }
            }
        }
    };

    // Always draw solid pass only - no wireframe overlay
    draw_submeshes_pass(raster_solid);
}

void ModelViewerSystem::RenderWindow(ID3D11Device* device, ID3D11DeviceContext* context, ImVec2 main_menu_pos, ImVec2 main_menu_size, bool main_menu_open)
{
    if (!settings::misc::model_viewer_window || !main_menu_open) return;

    if (!initialized && device && context)
    {
        Init(device, context);
    }

    float target_w = 340.0f;
    float target_h = (main_menu_size.y > 100.0f) ? main_menu_size.y : orok_config.menu_size[1];

    ImVec2 real_menu_pos = main_menu_pos;
    ImVec2 real_menu_size = main_menu_size;
    ImGuiWindow* menu_win = ImGui::FindWindowByName("Ivory ");
    if (menu_win)
    {
        real_menu_pos  = menu_win->Pos;
        real_menu_size = menu_win->Size;
        target_h = (real_menu_size.y > 100.0f) ? real_menu_size.y : orok_config.menu_size[1];
    }

    ImVec2 sticky_pos;
    if (real_menu_pos.x >= 10.0f && real_menu_size.x >= 200.0f)
    {
        float sticky_y = (std::max)(real_menu_pos.y, 35.0f);
        sticky_pos = ImVec2(real_menu_pos.x + real_menu_size.x + 8.0f, sticky_y);
    }
    else
    {
        sticky_pos = ImVec2(700.0f, 100.0f);
    }

    ImGui::SetNextWindowPos(sticky_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(target_w, target_h), ImGuiCond_Always);

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 old_win_bg = style.Colors[ImGuiCol_WindowBg];
    ImVec4 old_child_bg = style.Colors[ImGuiCol_ChildBg];
    ImVec4 old_border = style.Colors[ImGuiCol_Border];

    style.Colors[ImGuiCol_WindowBg] = ImVec4(orok_config.menu_color_secondary[0], orok_config.menu_color_secondary[1], orok_config.menu_color_secondary[2], orok_config.menu_color_secondary[3]);
    style.Colors[ImGuiCol_ChildBg]  = ImVec4(orok_config.menu_color_child[0], orok_config.menu_color_child[1], orok_config.menu_color_child[2], orok_config.menu_color_child[3]);
    style.Colors[ImGuiCol_Border]   = ImVec4(orok_config.menu_color_border[0], orok_config.menu_color_border[1], orok_config.menu_color_border[2], orok_config.menu_color_border[3]);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("Ivory_model_viewer", nullptr, flags))
    {
        style.Colors[ImGuiCol_WindowBg] = old_win_bg;
        style.Colors[ImGuiCol_ChildBg]  = old_child_bg;
        style.Colors[ImGuiCol_Border]   = old_border;
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    ImU32 theme_accent = IM_COL32(
        static_cast<int>(orok_config.menu_color[0] * 255.f),
        static_cast<int>(orok_config.menu_color[1] * 255.f),
        static_cast<int>(orok_config.menu_color[2] * 255.f),
        255
    );
    ImU32 border_col = IM_COL32(
        static_cast<int>(orok_config.menu_color_border[0] * 255.f),
        static_cast<int>(orok_config.menu_color_border[1] * 255.f),
        static_cast<int>(orok_config.menu_color_border[2] * 255.f),
        255
    );
    ImU32 text_col = IM_COL32(
        static_cast<int>(orok_config.menu_color_text[0] * 255.f),
        static_cast<int>(orok_config.menu_color_text[1] * 255.f),
        static_cast<int>(orok_config.menu_color_text[2] * 255.f),
        255
    );

    ImVec2 p_min = win_pos;
    ImVec2 p_max = ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y);
    dl->AddLine(p_min, ImVec2(p_max.x, p_min.y), theme_accent, 1.0f);
    dl->AddRect(ImVec2(p_min.x + 1.0f, p_min.y + 1.0f), ImVec2(p_max.x - 1.0f, p_max.y - 1.0f), border_col, 0.0f, 0, 1.0f);
    dl->AddRect(p_min, p_max, IM_COL32(12, 12, 12, 255), 0.0f, 0, 1.0f);

    float header_height = 28.0f;

    ImGui::SetCursorPos(ImVec2(10.0f, header_height + 4.0f));
    ImVec2 viewport_child_size = ImVec2(win_size.x - 20.0f, win_size.y - header_height - 14.0f);

    ImGui::BeginChild("##ModelViewerChild", viewport_child_size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x > 10.f && canvas_size.y > 10.f)
        {
            if (!model_data.loaded)
            {
                LoadModelFromMemory(device, g_default_model_bytes, g_default_model_bytes_size);
            }

            if (CreateOffscreenResources(device, canvas_size.x, canvas_size.y))
            {
                if (auto_rotate)
                {
                    cam_yaw += ImGui::GetIO().DeltaTime * rotate_speed;
                }

                Render3D(context, canvas_size.x, canvas_size.y);

                ImGui::Image(reinterpret_cast<ImTextureID>(shader_resource_view), canvas_size);
                ImVec2 img_min = ImGui::GetItemRectMin();
                ImVec2 img_max = ImGui::GetItemRectMax();

                if (ImGui::IsItemHovered())
                {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.MouseWheel != 0.0f)
                    {
                        cam_distance -= io.MouseWheel * 0.3f;
                        cam_distance = std::clamp(cam_distance, 0.2f, 20.0f);
                    }

                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    {
                        cam_yaw += io.MouseDelta.x * 0.005f;
                        cam_pitch += io.MouseDelta.y * 0.005f;
                        cam_pitch = std::clamp(cam_pitch, -1.55f, 1.55f);
                    }
                    else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                    {
                        float pan_speed = cam_distance * 0.002f;
                        cam_target.x -= io.MouseDelta.x * pan_speed * cosf(cam_yaw);
                        cam_target.z += io.MouseDelta.x * pan_speed * sinf(cam_yaw);
                        cam_target.y += io.MouseDelta.y * pan_speed;
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    style.Colors[ImGuiCol_WindowBg] = old_win_bg;
    style.Colors[ImGuiCol_ChildBg]  = old_child_bg;
    style.Colors[ImGuiCol_Border]   = old_border;

    ImGui::End();
}

std::uint64_t ModelViewerSystem::GetLocalPlayerUserId() {
    if (!game::datamodel || game::datamodel->address == 0) return 0;

    uint64_t players_addr = game::datamodel->find_first_child_by_class("Players");
    if (!players_addr) {
        players_addr = game::datamodel->find_first_child("Players");
    }
    if (!players_addr) return 0;

    uint64_t local_player_addr = memory->read<uint64_t>(players_addr + Offsets::Player::LocalPlayer);
    if (!local_player_addr) return 0;

    rbx::c_player local_player(local_player_addr);
    return local_player.get_user_id();
}

void ModelViewerSystem::RefreshAvatar() {
    std::uint64_t local_uid = GetLocalPlayerUserId();
    if (local_uid != 0) {
        ::c_avatar_3d_api::get().clear_user(std::to_string(local_uid));
        loaded_avatar_id.clear();
    }
    else {
        ::c_avatar_3d_api::get().clear_user(avatar_user_id);
        loaded_avatar_id.clear();
    }
}

void ModelViewerSystem::RenderESPOverlay(ImVec2 img_min, ImVec2 canvas_size) {
    if (model_data.vertices.empty()) return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!draw_list) return;

    // View & Projection math using model space -> screen space projections
    DirectX::XMVECTOR target = DirectX::XMVectorSet(cam_target.x, cam_target.y, cam_target.z, 0.0f);
    float cos_p = cosf(cam_pitch);
    float sin_p = sinf(cam_pitch);
    float cos_y = cosf(cam_yaw);
    float sin_y = sinf(cam_yaw);

    float eye_x = cam_target.x + cam_distance * cos_p * sin_y;
    float eye_y = cam_target.y + cam_distance * sin_p;
    float eye_z = cam_target.z + cam_distance * cos_p * cos_y;

    DirectX::XMVECTOR eye = DirectX::XMVectorSet(eye_x, eye_y, eye_z, 0.0f);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view_mat = DirectX::XMMatrixLookAtLH(eye, target, up);
    float aspect = (canvas_size.y > 0.f) ? (canvas_size.x / canvas_size.y) : 1.0f;
    DirectX::XMMATRIX proj_mat = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(cam_fov), aspect, 0.01f, 100.0f);

    float scale = (model_data.radius > 0.0001f) ? (0.85f / model_data.radius) : 1.0f;
    DirectX::XMMATRIX trans_center = DirectX::XMMatrixTranslation(-model_data.center.x, -model_data.center.y, -model_data.center.z);
    DirectX::XMMATRIX scale_mat = DirectX::XMMatrixScaling(scale, scale, scale);
    DirectX::XMMATRIX world_mat = trans_center * scale_mat;
    DirectX::XMMATRIX wvp = world_mat * view_mat * proj_mat;

    auto project_point = [&](const DirectX::XMFLOAT3& pos3d, ImVec2& out_screen) -> bool {
        DirectX::XMVECTOR pos = DirectX::XMVectorSet(pos3d.x, pos3d.y, pos3d.z, 1.0f);
        DirectX::XMVECTOR proj = DirectX::XMVector4Transform(pos, wvp);

        float w = DirectX::XMVectorGetW(proj);
        if (w < 0.0001f) return false;

        float x = DirectX::XMVectorGetX(proj) / w;
        float y = DirectX::XMVectorGetY(proj) / w;

        out_screen.x = img_min.x + (x + 1.0f) * 0.5f * canvas_size.x;
        out_screen.y = img_min.y + (1.0f - y) * 0.5f * canvas_size.y;
        return true;
    };

    // Calculate bounding box on the screen
    float min_x = 999999.0f, max_x = -999999.0f;
    float min_y = 999999.0f, max_y = -999999.0f;
    bool any_projected = false;

    // Sample vertices to estimate bounding box
    for (size_t i = 0; i < model_data.vertices.size(); i += 8) {
        ImVec2 screen_pt;
        if (project_point(model_data.vertices[i].position, screen_pt)) {
            min_x = (std::min)(min_x, screen_pt.x);
            max_x = (std::max)(max_x, screen_pt.x);
            min_y = (std::min)(min_y, screen_pt.y);
            max_y = (std::max)(max_y, screen_pt.y);
            any_projected = true;
        }
    }

    if (model_data.vertices.empty() || (settings::misc::preview_model == 1 && loaded_avatar_id != avatar_user_id)) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const char* loading_text = "Loading...";
        ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 1.2f, FLT_MAX, 0.0f, loading_text);
        ImVec2 center_pos(img_min.x + (canvas_size.x - text_sz.x) * 0.5f, img_min.y + (canvas_size.y - text_sz.y) * 0.5f);
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(center_pos.x - 1.f, center_pos.y), IM_COL32(0, 0, 0, 255), loading_text);
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(center_pos.x + 1.f, center_pos.y), IM_COL32(0, 0, 0, 255), loading_text);
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(center_pos.x, center_pos.y - 1.f), IM_COL32(0, 0, 0, 255), loading_text);
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(center_pos.x, center_pos.y + 1.f), IM_COL32(0, 0, 0, 255), loading_text);
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, center_pos, IM_COL32(255, 255, 255, 220), loading_text);
        return;
    }

    if (!any_projected) return;

    // Access active hostile/friendly configuration settings dynamically
    const auto& s = (selected_preview_condition == 4) ? settings::visuals::friendly : settings::visuals::hostile;

    // Setup colors
    ImU32 box_col_top = IM_COL32(static_cast<int>(s.colour[0] * 255.f), static_cast<int>(s.colour[1] * 255.f), static_cast<int>(s.colour[2] * 255.f), 255);
    ImU32 box_col_mid = IM_COL32(static_cast<int>(s.box_colour_mid[0] * 255.f), static_cast<int>(s.box_colour_mid[1] * 255.f), static_cast<int>(s.box_colour_mid[2] * 255.f), 255);
    ImU32 box_col_low = IM_COL32(static_cast<int>(s.box_colour_low[0] * 255.f), static_cast<int>(s.box_colour_low[1] * 255.f), static_cast<int>(s.box_colour_low[2] * 255.f), 255);

    ImU32 esp_color = box_col_top;
    if (s.box_gradient && s.box_gradient_type == 2)
    {
        float time = static_cast<float>(ImGui::GetTime());
        float speed = (s.box_pulse_speed > 0.01f) ? s.box_pulse_speed : 2.0f;
        float phase = fmodf(time * speed, 2.0f);
        float t = phase < 1.0f ? phase : (2.0f - phase);
        float r, g, b, a;
        if (t < 0.5f) {
            float factor = t * 2.0f;
            r = s.colour[0] * (1.f - factor) + s.box_colour_mid[0] * factor;
            g = s.colour[1] * (1.f - factor) + s.box_colour_mid[1] * factor;
            b = s.colour[2] * (1.f - factor) + s.box_colour_mid[2] * factor;
            a = s.colour[3] * (1.f - factor) + s.box_colour_mid[3] * factor;
        } else {
            float factor = (t - 0.5f) * 2.0f;
            r = s.box_colour_mid[0] * (1.f - factor) + s.box_colour_low[0] * factor;
            g = s.box_colour_mid[1] * (1.f - factor) + s.box_colour_low[1] * factor;
            b = s.box_colour_mid[2] * (1.f - factor) + s.box_colour_low[2] * factor;
            a = s.box_colour_mid[3] * (1.f - factor) + s.box_colour_low[3] * factor;
        }
        esp_color = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), 255);
    }

    const char* text_label = "Player";
    if (selected_preview_condition == 1) text_label = "Visible";
    else if (selected_preview_condition == 2) text_label = "Invisible";
    else if (selected_preview_condition == 3) text_label = "Hostile Enemy";
    else if (selected_preview_condition == 4) text_label = "Teammate";
    else if (selected_preview_condition == 5) text_label = "Aimbot Target";

    // Draw Box Fill
    if (s.box && s.box_fill) {
        ImU32 fill_col_top = IM_COL32(s.box_fill_colour[0] * 255.f, s.box_fill_colour[1] * 255.f, s.box_fill_colour[2] * 255.f, s.box_fill_colour[3] * 255.f);
        ImU32 fill_col_mid = IM_COL32(s.box_fill_colour_mid[0] * 255.f, s.box_fill_colour_mid[1] * 255.f, s.box_fill_colour_mid[2] * 255.f, s.box_fill_colour_mid[3] * 255.f);
        ImU32 fill_col_low = IM_COL32(s.box_fill_colour_low[0] * 255.f, s.box_fill_colour_low[1] * 255.f, s.box_fill_colour_low[2] * 255.f, s.box_fill_colour_low[3] * 255.f);

        if (s.box_fill_gradient) {
            if (s.box_fill_gradient_type == 0) {
                float mid_y = (min_y + max_y) * 0.5f;
                draw_list->AddRectFilledMultiColor(ImVec2(min_x, min_y), ImVec2(max_x, mid_y), fill_col_top, fill_col_top, fill_col_mid, fill_col_mid);
                draw_list->AddRectFilledMultiColor(ImVec2(min_x, mid_y), ImVec2(max_x, max_y), fill_col_mid, fill_col_mid, fill_col_low, fill_col_low);
            }
            else if (s.box_fill_gradient_type == 1) {
                float mid_x = (min_x + max_x) * 0.5f;
                draw_list->AddRectFilledMultiColor(ImVec2(min_x, min_y), ImVec2(mid_x, max_y), fill_col_top, fill_col_mid, fill_col_mid, fill_col_top);
                draw_list->AddRectFilledMultiColor(ImVec2(mid_x, min_y), ImVec2(max_x, max_y), fill_col_mid, fill_col_low, fill_col_low, fill_col_mid);
            }
        }
        else {
            draw_list->AddRectFilled(ImVec2(min_x, min_y), ImVec2(max_x, max_y), fill_col_top);
        }
    }

    // Draw Box
    if (s.box) {
        if (s.box_type == 0) { // Corner Box
            float width = max_x - min_x;
            float height = max_y - min_y;
            float stroke = (std::max)(1.0f, settings::visuals::box_thickness);
            float len = std::clamp((std::min)(width, height) * 0.25f, stroke * 2.0f, (std::min)(width, height) * 0.5f);
            
            bool enable_outline = settings::visuals::render_outlines[1];
            ImU32 outline_col = IM_COL32(0, 0, 0, 255);

            auto draw_bar = [&](float x0, float y0, float x1, float y1) {
                if (enable_outline) {
                    if (s.box_inline) {
                        float ix0 = (x0 == min_x) ? x0 + stroke : x0;
                        float ix1 = (x1 == max_x) ? x1 - stroke : x1;
                        float iy0 = (y0 == min_y) ? y0 + stroke : y0;
                        float iy1 = (y1 == max_y) ? y1 - stroke : y1;
                        draw_list->AddRectFilled(ImVec2(ix0, iy0), ImVec2(ix1, iy1), outline_col);
                    }
                    draw_list->AddRectFilled(ImVec2(x0 - 1.0f, y0 - 1.0f), ImVec2(x1 + 1.0f, y1 + 1.0f), outline_col);
                }
                draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), esp_color);
            };

            // Top-Left
            draw_bar(min_x, min_y, min_x + len, min_y + stroke);
            draw_bar(min_x, min_y + stroke, min_x + stroke, min_y + len);
            // Top-Right
            draw_bar(max_x - len, min_y, max_x, min_y + stroke);
            draw_bar(max_x - stroke, min_y + stroke, max_x, min_y + len);
            // Bottom-Left
            draw_bar(min_x, max_y - stroke, min_x + len, max_y);
            draw_bar(min_x, max_y - len, min_x + stroke, max_y - stroke);
            // Bottom-Right
            draw_bar(max_x - len, max_y - stroke, max_x, max_y);
            draw_bar(max_x - stroke, max_y - len, max_x, max_y - stroke);
        }
        else if (s.box_type == 1) { // 2D Full Box
            float stroke = settings::visuals::box_thickness;
            bool enable_outline = settings::visuals::render_outlines[1];
            ImU32 outline_col = IM_COL32(15, 15, 15, 255);

            if (enable_outline) {
                if (s.box_inline) {
                    draw_list->AddRect(ImVec2(min_x, min_y), ImVec2(max_x, max_y), outline_col, 0.0f, 0, 1.0f);
                }
                draw_list->AddRect(ImVec2(min_x - 1.0f, min_y - 1.0f), ImVec2(max_x + 1.0f, max_y + 1.0f), outline_col, 0.0f, 0, 1.0f);
            }

            ImVec2 inner_min(min_x, min_y);
            ImVec2 inner_max(max_x, max_y);

            if (s.box_gradient) {
                if (s.box_gradient_type == 0) {
                    int vtx0 = draw_list->VtxBuffer.Size;
                    draw_list->AddRect(inner_min, inner_max, box_col_top, 0.0f, 0, stroke);
                    int vtx1 = draw_list->VtxBuffer.Size;
                    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw_list, vtx0, vtx1, inner_min, ImVec2(inner_min.x, inner_max.y), box_col_top, box_col_low);
                }
                else if (s.box_gradient_type == 1) {
                    int vtx0 = draw_list->VtxBuffer.Size;
                    draw_list->AddRect(inner_min, inner_max, box_col_top, 0.0f, 0, stroke);
                    int vtx1 = draw_list->VtxBuffer.Size;
                    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw_list, vtx0, vtx1, inner_min, ImVec2(inner_max.x, inner_min.y), box_col_top, box_col_low);
                }
                else {
                    draw_list->AddRect(inner_min, inner_max, esp_color, 0.0f, 0, stroke);
                }
            }
            else {
                draw_list->AddRect(inner_min, inner_max, esp_color, 0.0f, 0, stroke);
            }
        }
    }

    // Draw Skeleton (Draw connection lines of the model)
    // Disabled wireframe line rendering over model mesh
    if (false && s.skeleton && model_data.indices.size() > 2) {
        ImU32 skel_col = IM_COL32(s.skeleton_colour[0] * 255.f, s.skeleton_colour[1] * 255.f, s.skeleton_colour[2] * 255.f, 255);
        for (size_t i = 0; i < model_data.indices.size() - 2; i += 18) {
            ImVec2 p1, p2;
            if (project_point(model_data.vertices[model_data.indices[i]].position, p1) &&
                project_point(model_data.vertices[model_data.indices[i+1]].position, p2)) {
                draw_list->AddLine(p1, p2, skel_col, 1.2f);
            }
        }
    }

    // Draw Head Dot
    if (s.head_dot) {
        ImU32 head_col = IM_COL32(s.head_dot_colour[0] * 255.f, s.head_dot_colour[1] * 255.f, s.head_dot_colour[2] * 255.f, 255);
        DirectX::XMFLOAT3 head_pos3d = model_data.center;
        head_pos3d.y += model_data.radius * 0.7f; // Top of the model
        ImVec2 head_scr;
        if (project_point(head_pos3d, head_scr)) {
            draw_list->AddCircleFilled(head_scr, s.head_dot_size, head_col);
        }
    }

    // Calculate top and bottom spacing dynamically to prevent overlapping
    float top_text_offset = min_y - 3.f;
    float bottom_text_offset = max_y + 3.f;

    if (s.healthbar && !s.ignore_full)
    {
        float bar_h = settings::visuals::healthbar_thickness + s.healthbar_padding + 3.f;
        if (s.healthbar_style == 3) // Healthbar at Top
        {
            top_text_offset -= (bar_h + 2.f);
        }
        else if (s.healthbar_style == 2) // Healthbar at Bottom
        {
            bottom_text_offset += (bar_h + 2.f);
        }
    }

    // Draw Username
    if (s.username) {
        std::string formatted_label = text_label;
        if (s.username_type == 0) {
            formatted_label = "Ivory";
        } else if (s.username_type == 1) {
            formatted_label = "Ivory";
        } else if (s.username_type == 2) {
            formatted_label = "Ivory (@Ivory)";
        }

        if (s.name_transform == 1) {
            for (char& c : formatted_label) c = (char)toupper(c);
        } else if (s.name_transform == 2) {
            for (char& c : formatted_label) c = (char)tolower(c);
        }

        ImU32 name_col = IM_COL32(static_cast<int>(s.username_colour[0] * 255.f), static_cast<int>(s.username_colour[1] * 255.f), static_cast<int>(s.username_colour[2] * 255.f), 255);
        ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, formatted_label.c_str());

        ImVec2 pos;
        if (s.username_position == 1) { // Bottom
            pos = ImVec2((min_x + max_x) * 0.5f - text_sz.x * 0.5f, bottom_text_offset);
            bottom_text_offset += (text_sz.y + 2.f);
        } else if (s.username_position == 2) { // Left
            pos = ImVec2(min_x - text_sz.x - 4.f, min_y);
        } else if (s.username_position == 3) { // Right
            pos = ImVec2(max_x + 4.f, min_y);
        } else { // 0 = Top
            pos = ImVec2((min_x + max_x) * 0.5f - text_sz.x * 0.5f, top_text_offset - text_sz.y);
            top_text_offset -= (text_sz.y + 2.f);
        }

        ImU32 final_name_col = name_col;
        if (s.username_gradient)
        {
            float cur_time = static_cast<float>(ImGui::GetTime());
            if (s.username_gradient_type == 1) // Rainbow
            {
                float hue = std::fmodf(cur_time * (s.username_gradient_speed * 0.2f), 1.0f);
                float r, g, b;
                ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.0f, r, g, b);
                final_name_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), 255);
            }
            else // Wave or Left to Right
            {
                float wave_t = std::sinf(cur_time * s.username_gradient_speed) * 0.5f + 0.5f;
                float wr = s.username_colour[0] + (s.username_colour_two[0] - s.username_colour[0]) * wave_t;
                float wg = s.username_colour[1] + (s.username_colour_two[1] - s.username_colour[1]) * wave_t;
                float wb = s.username_colour[2] + (s.username_colour_two[2] - s.username_colour[2]) * wave_t;
                final_name_col = IM_COL32(static_cast<int>(wr * 255.f), static_cast<int>(wg * 255.f), static_cast<int>(wb * 255.f), 255);
            }
        }

        if (settings::visuals::render_outlines[2])
        {
            draw_list->AddText(ImVec2(pos.x - 1.f, pos.y), IM_COL32(0,0,0,255), formatted_label.c_str());
            draw_list->AddText(ImVec2(pos.x + 1.f, pos.y), IM_COL32(0,0,0,255), formatted_label.c_str());
            draw_list->AddText(ImVec2(pos.x, pos.y - 1.f), IM_COL32(0,0,0,255), formatted_label.c_str());
            draw_list->AddText(ImVec2(pos.x, pos.y + 1.f), IM_COL32(0,0,0,255), formatted_label.c_str());
        }
        draw_list->AddText(pos, final_name_col, formatted_label.c_str());
    }

    // Draw Distance
    if (s.distance) {
        ImU32 dist_col = IM_COL32(static_cast<int>(s.distance_colour[0] * 255.f), static_cast<int>(s.distance_colour[1] * 255.f), static_cast<int>(s.distance_colour[2] * 255.f), 255);
        const char* dist_text = "15m";
        ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, dist_text);
        ImVec2 pos((min_x + max_x) * 0.5f - text_sz.x * 0.5f, bottom_text_offset);
        bottom_text_offset += (text_sz.y + 2.f);
        if (settings::visuals::render_outlines[2])
        {
            draw_list->AddText(ImVec2(pos.x - 1.f, pos.y), IM_COL32(0,0,0,255), dist_text);
            draw_list->AddText(ImVec2(pos.x + 1.f, pos.y), IM_COL32(0,0,0,255), dist_text);
            draw_list->AddText(ImVec2(pos.x, pos.y - 1.f), IM_COL32(0,0,0,255), dist_text);
            draw_list->AddText(ImVec2(pos.x, pos.y + 1.f), IM_COL32(0,0,0,255), dist_text);
        }
        draw_list->AddText(pos, dist_col, dist_text);
    }

    // Draw Health bar
    if (s.healthbar && !(s.ignore_full)) {
        float time_sec = static_cast<float>(ImGui::GetTime());
        // Triangle wave cycling between 1.0 (100 HP) and 0.0 (0 HP) every 3 seconds
        float period = 3.0f;
        float cycle = fmodf(time_sec, period * 2.0f);
        float target_hp_percent = (cycle < period) ? (1.0f - (cycle / period)) : ((cycle - period) / period);

        static float s_preview_anim_hp = 1.0f;
        float current_anim = target_hp_percent;
        if (s.healthbar_lerp)
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt <= 0.0f) dt = 0.016f;
            s_preview_anim_hp += (target_hp_percent - s_preview_anim_hp) * (dt * 14.0f < 1.0f ? dt * 14.0f : 1.0f);
            if (std::abs(s_preview_anim_hp - target_hp_percent) < 0.001f) s_preview_anim_hp = target_hp_percent;
            current_anim = s_preview_anim_hp;
        }
        else
        {
            current_anim = target_hp_percent;
        }
        current_anim = std::clamp(current_anim, 0.0f, 1.0f);

        // Access mutable settings for dragging
        auto& mut_s = (selected_preview_condition == 4) ? settings::visuals::friendly : settings::visuals::hostile;

        float bar_thickness = settings::visuals::healthbar_thickness;
        float bar_spacing = s.healthbar_padding;

        ImVec2 bg_min, bg_max;
        ImVec2 outline_min, outline_max;
        ImVec2 fill_min, fill_max;

        float box_left = min_x;
        float box_right = max_x;
        float box_top = min_y;
        float box_bottom = max_y;
        float box_width = max_x - min_x;
        float box_height = max_y - min_y;

        float off_x = s.healthbar_offset_x;
        float off_y = s.healthbar_offset_y;

        int style = s.healthbar_style;
        if (style == 0) // Left
        {
            float health_bar_height = box_height + 2.f;
            bg_min = ImVec2(box_left - bar_spacing - bar_thickness + off_x, box_top - 1.f + off_y);
            bg_max = ImVec2(box_left - bar_spacing + off_x, box_bottom + 1.f + off_y);

            float health_fill_height = health_bar_height * current_anim;
            fill_min = ImVec2(box_left - bar_spacing - bar_thickness + off_x, box_bottom + 1.f - health_fill_height + off_y);
            fill_max = ImVec2(box_left - bar_spacing + off_x, box_bottom + 1.f + off_y);
        }
        else if (style == 1) // Right
        {
            float health_bar_height = box_height + 2.f;
            bg_min = ImVec2(box_right + bar_spacing + off_x, box_top - 1.f + off_y);
            bg_max = ImVec2(box_right + bar_spacing + bar_thickness + off_x, box_bottom + 1.f + off_y);

            float health_fill_height = health_bar_height * current_anim;
            fill_min = ImVec2(box_right + bar_spacing + off_x, box_bottom + 1.f - health_fill_height + off_y);
            fill_max = ImVec2(box_right + bar_spacing + bar_thickness + off_x, box_bottom + 1.f + off_y);
        }
        else if (style == 2) // Bottom
        {
            float health_bar_width = box_width + 2.f;
            bg_min = ImVec2(box_left - 1.f + off_x, box_bottom + bar_spacing + off_y);
            bg_max = ImVec2(box_right + 1.f + off_x, box_bottom + bar_spacing + bar_thickness + off_y);

            float health_fill_width = health_bar_width * current_anim;
            fill_min = ImVec2(box_left - 1.f + off_x, box_bottom + bar_spacing + off_y);
            fill_max = ImVec2(box_left - 1.f + health_fill_width + off_x, box_bottom + bar_spacing + bar_thickness + off_y);
        }
        else // Top (3)
        {
            float health_bar_width = box_width + 2.f;
            bg_min = ImVec2(box_left - 1.f + off_x, box_top - bar_spacing - bar_thickness + off_y);
            bg_max = ImVec2(box_right + 1.f + off_x, box_top - bar_spacing + off_y);

            float health_fill_width = health_bar_width * current_anim;
            fill_min = ImVec2(box_left - 1.f + off_x, box_top - bar_spacing - bar_thickness + off_y);
            fill_max = ImVec2(box_left - 1.f + health_fill_width + off_x, box_top - bar_spacing + off_y);
        }

        // Handle interactive drag & hold to reposition healthbar
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        float hit_padding = 8.0f;
        bool mouse_in_bar = (mouse_pos.x >= (bg_min.x - hit_padding) && mouse_pos.x <= (bg_max.x + hit_padding) &&
                             mouse_pos.y >= (bg_min.y - hit_padding) && mouse_pos.y <= (bg_max.y + hit_padding));

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_bar)
        {
            is_dragging_healthbar = true;
            healthbar_drag_start_pos = mouse_pos;
            healthbar_drag_start_offset = ImVec2(mut_s.healthbar_offset_x, mut_s.healthbar_offset_y);
        }

        if (is_dragging_healthbar)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // Calculate cursor position relative to box center
                float box_center_x = (box_left + box_right) * 0.5f;
                float box_center_y = (box_top + box_bottom) * 0.5f;
                float rel_x = mouse_pos.x - box_center_x;
                float rel_y = mouse_pos.y - box_center_y;

                // Auto-detect best side based on drag direction
                float norm_x = rel_x / ((box_width > 1.f) ? (box_width * 0.5f) : 1.f);
                float norm_y = rel_y / ((box_height > 1.f) ? (box_height * 0.5f) : 1.f);

                if (std::abs(norm_x) > std::abs(norm_y))
                {
                    // Left or Right side
                    if (rel_x < 0.0f)
                    {
                        mut_s.healthbar_style = 0; // Left
                        mut_s.healthbar_offset_x = std::clamp(mouse_pos.x - (box_left - bar_spacing - bar_thickness), -60.f, 40.f);
                        mut_s.healthbar_offset_y = std::clamp(mouse_pos.y - box_center_y, -box_height * 0.4f, box_height * 0.4f);
                    }
                    else
                    {
                        mut_s.healthbar_style = 1; // Right
                        mut_s.healthbar_offset_x = std::clamp(mouse_pos.x - (box_right + bar_spacing), -40.f, 60.f);
                        mut_s.healthbar_offset_y = std::clamp(mouse_pos.y - box_center_y, -box_height * 0.4f, box_height * 0.4f);
                    }
                }
                else
                {
                    // Top or Bottom side
                    if (rel_y < 0.0f)
                    {
                        mut_s.healthbar_style = 3; // Top
                        mut_s.healthbar_offset_x = std::clamp(mouse_pos.x - box_center_x, -box_width * 0.4f, box_width * 0.4f);
                        mut_s.healthbar_offset_y = std::clamp(mouse_pos.y - (box_top - bar_spacing - bar_thickness), -40.f, 40.f);
                    }
                    else
                    {
                        mut_s.healthbar_style = 2; // Bottom
                        mut_s.healthbar_offset_x = std::clamp(mouse_pos.x - box_center_x, -box_width * 0.4f, box_width * 0.4f);
                        mut_s.healthbar_offset_y = std::clamp(mouse_pos.y - (box_bottom + bar_spacing), -40.f, 40.f);
                    }
                }
            }
            else
            {
                is_dragging_healthbar = false;
            }
        }

        if (s.bar_fold)
        {
            bg_min = fill_min;
            bg_max = fill_max;
        }

        outline_min = ImVec2(bg_min.x - 1.f, bg_min.y - 1.f);
        outline_max = ImVec2(bg_max.x + 1.f, bg_max.y + 1.f);

        // Highlight healthbar when hovered or being dragged
        if (mouse_in_bar || is_dragging_healthbar)
        {
            draw_list->AddRect(ImVec2(outline_min.x - 2.f, outline_min.y - 2.f), ImVec2(outline_max.x + 2.f, outline_max.y + 2.f), IM_COL32(255, 255, 255, 120), 0.0f, 0, 1.0f);
        }

        if (settings::visuals::bar_fill)
        {
            ImU32 bg_fill = IM_COL32(settings::visuals::bar_fill_colour[0] * 255.f, settings::visuals::bar_fill_colour[1] * 255.f, settings::visuals::bar_fill_colour[2] * 255.f, settings::visuals::bar_fill_colour[3] * 255.f);
            draw_list->AddRectFilled(bg_min, bg_max, bg_fill);
        }

        if (settings::visuals::render_outlines[3]) // index 3 = Health Bar
        {
            draw_list->AddRectFilled(outline_min, outline_max, IM_COL32(0, 0, 0, 215));
        }

        if (current_anim > 0.f)
        {
            ImU32 col_top = IM_COL32(s.healthbar_colour[0] * 255.f, s.healthbar_colour[1] * 255.f, s.healthbar_colour[2] * 255.f, 255);
            ImU32 col_mid = IM_COL32(s.healthbar_colour_mid[0] * 255.f, s.healthbar_colour_mid[1] * 255.f, s.healthbar_colour_mid[2] * 255.f, 255);
            ImU32 col_low = IM_COL32(s.healthbar_colour_low[0] * 255.f, s.healthbar_colour_low[1] * 255.f, s.healthbar_colour_low[2] * 255.f, 255);

            if (s.healthbar_gradient_type == 0)
            {
                float mid_y = (fill_min.y + fill_max.y) * 0.5f;
                draw_list->AddRectFilledMultiColor(fill_min, ImVec2(fill_max.x, mid_y), col_top, col_top, col_mid, col_mid);
                draw_list->AddRectFilledMultiColor(ImVec2(fill_min.x, mid_y), fill_max, col_mid, col_mid, col_low, col_low);
            }
            else if (s.healthbar_gradient_type == 1)
            {
                float mid_x = (fill_min.x + fill_max.x) * 0.5f;
                draw_list->AddRectFilledMultiColor(fill_min, ImVec2(mid_x, fill_max.y), col_top, col_mid, col_mid, col_top);
                draw_list->AddRectFilledMultiColor(ImVec2(mid_x, fill_min.y), fill_max, col_mid, col_low, col_low, col_mid);
            }
            else if (s.healthbar_gradient_type == 2)
            {
                float time = static_cast<float>(ImGui::GetTime());
                float speed = (s.healthbar_pulse_speed > 0.01f) ? s.healthbar_pulse_speed : 2.5f;
                float phase = fmodf(time * speed, 2.0f);
                float t = phase < 1.0f ? phase : (2.0f - phase);
                float r, g, b, a;
                if (t < 0.5f) {
                    float factor = t * 2.0f;
                    r = s.healthbar_colour[0] * (1.f - factor) + s.healthbar_colour_mid[0] * factor;
                    g = s.healthbar_colour[1] * (1.f - factor) + s.healthbar_colour_mid[1] * factor;
                    b = s.healthbar_colour[2] * (1.f - factor) + s.healthbar_colour_mid[2] * factor;
                    a = s.healthbar_colour[3] * (1.f - factor) + s.healthbar_colour_mid[3] * factor;
                } else {
                    float factor = (t - 0.5f) * 2.0f;
                    r = s.healthbar_colour_mid[0] * (1.f - factor) + s.healthbar_colour_low[0] * factor;
                    g = s.healthbar_colour_mid[1] * (1.f - factor) + s.healthbar_colour_low[1] * factor;
                    b = s.healthbar_colour_mid[2] * (1.f - factor) + s.healthbar_colour_low[2] * factor;
                    a = s.healthbar_colour_mid[3] * (1.f - factor) + s.healthbar_colour_low[3] * factor;
                }
                ImU32 pulse_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), 255);
                draw_list->AddRectFilled(fill_min, fill_max, pulse_col);
            }
        }

        if (s.healthbar_text && current_anim > 0.f)
        {
            int hp_val = static_cast<int>(std::round(current_anim * 100.0f));
            std::string hp_str = std::to_string(hp_val);
            float hp_font_size = ImGui::GetFontSize();
            ImVec2 hp_size = ImGui::GetFont()->CalcTextSizeA(hp_font_size, FLT_MAX, 0.0f, hp_str.c_str());
            ImVec2 hp_pos{};

            if (style == 0) // Left
            {
                hp_pos.x = fill_min.x - hp_size.x - 2.f;
                hp_pos.y = s.value_follow ? (fill_min.y - hp_size.y * 0.5f) : (bg_min.y + 2.f);
            }
            else if (style == 1) // Right
            {
                hp_pos.x = fill_max.x + 2.f;
                hp_pos.y = s.value_follow ? (fill_min.y - hp_size.y * 0.5f) : (bg_min.y + 2.f);
            }
            else if (style == 2) // Bottom
            {
                hp_pos.x = s.value_follow ? (fill_max.x - hp_size.x * 0.5f) : (bg_max.x + 2.f);
                hp_pos.y = fill_max.y + 2.f;
            }
            else // Top
            {
                hp_pos.x = s.value_follow ? (fill_max.x - hp_size.x * 0.5f) : (bg_max.x + 2.f);
                hp_pos.y = fill_min.y - hp_size.y - 2.f;
            }

            ImU32 text_col = IM_COL32(s.healthbar_text_colour[0] * 255.f, s.healthbar_text_colour[1] * 255.f, s.healthbar_text_colour[2] * 255.f, 255);
            
            // Render text with simple outline
            if (settings::visuals::render_outlines[4]) // Health Text Outline
            {
                draw_list->AddText(ImVec2(hp_pos.x - 1.0f, hp_pos.y), IM_COL32(0,0,0,255), hp_str.c_str());
                draw_list->AddText(ImVec2(hp_pos.x + 1.0f, hp_pos.y), IM_COL32(0,0,0,255), hp_str.c_str());
                draw_list->AddText(ImVec2(hp_pos.x, hp_pos.y - 1.0f), IM_COL32(0,0,0,255), hp_str.c_str());
                draw_list->AddText(ImVec2(hp_pos.x, hp_pos.y + 1.0f), IM_COL32(0,0,0,255), hp_str.c_str());
            }
            draw_list->AddText(hp_pos, text_col, hp_str.c_str());
        }
    }
}

}

