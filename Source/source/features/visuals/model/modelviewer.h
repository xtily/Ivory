#pragma once
#include "gltf/gltfloader.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <imgui/imgui.h>
#include <string>

#include "default/defaultmodel.h"
#include "custom/tung_tung/tung_tung_sahur_model.h"

struct c_obj_model; // Forward declaration

namespace ModelViewer
{

struct ConstantBufferData
{
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
    DirectX::XMFLOAT4 light_dir;
    DirectX::XMFLOAT4 light_color;
    DirectX::XMFLOAT4 object_color;
    DirectX::XMFLOAT4 ambient_light;
};

class ModelViewerSystem
{
public:
    ModelViewerSystem();
    ~ModelViewerSystem();

    void Init(ID3D11Device* device, ID3D11DeviceContext* context);
    void RenderWindow(ID3D11Device* device, ID3D11DeviceContext* context, ImVec2 main_menu_pos = ImVec2(-1, -1), ImVec2 main_menu_size = ImVec2(-1, -1), bool main_menu_open = true);
    bool LoadModelFile(ID3D11Device* device, const std::string& path);
    bool LoadModelFromMemory(ID3D11Device* device, const uint8_t* data, size_t size);
    bool LoadOBJModelFromCache(const ::c_obj_model& obj_model, const std::string& user_id, ID3D11Device* device);
    void ResetCamera();
    void RefreshAvatar();
    std::uint64_t GetLocalPlayerUserId();
    void CleanupDX();
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shader_resource_view; }

    ModelData model_data;
    std::string current_file_path = "C:\\Users\\0x5\\Desktop\\model\\model.glb";

    int selected_model_type = 1; // 0 = Embedded, 1 = Roblox Avatar
    int current_model_source = 0; // 0 = Embedded, 1 = Roblox Avatar
    char avatar_user_id[32] = "8685595779";
    std::string loaded_avatar_id = "";
    float cam_fov = 45.0f;

    ID3D11Device* d3d_device = nullptr;

    bool auto_rotate = false;
    float rotate_speed = 0.5f;
    int render_mode = 0; // 0 = Solid, 1 = Wireframe, 2 = Solid + Wireframe
    float bg_color[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f }; // Matches menu_color_child #141414
    float object_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float light_intensity = 1.2f;
    float light_dir[3] = { 0.5f, 1.0f, 0.75f };

    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;
    float cam_distance = 1.9f;
    DirectX::XMFLOAT3 cam_target = { 0.f, 0.f, 0.f };
    void Render3D(ID3D11DeviceContext* context, float width, float height);
    void RenderESPOverlay(ImVec2 img_min, ImVec2 canvas_size);
    bool is_dragging_healthbar = false;
    ImVec2 healthbar_drag_start_pos = ImVec2(0, 0);
    ImVec2 healthbar_drag_start_offset = ImVec2(0, 0);
    int selected_preview_condition = 0; // 0=None, 1=Visible, 2=Invisible, 3=Enemy, 4=Teammate, 5=Target
    bool CreateOffscreenResources(ID3D11Device* device, float width, float height);

private:
    bool CreateShaders(ID3D11Device* device);
    bool CreateBuffers(ID3D11Device* device);
    bool CreateDefaultWhiteTexture(ID3D11Device* device);

    ID3D11Texture2D* render_target_tex = nullptr;
    ID3D11RenderTargetView* render_target_view = nullptr;
    ID3D11ShaderResourceView* shader_resource_view = nullptr;
    ID3D11Texture2D* depth_stencil_tex = nullptr;
    ID3D11DepthStencilView* depth_stencil_view = nullptr;

    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11RasterizerState* raster_solid = nullptr;
    ID3D11RasterizerState* raster_wireframe = nullptr;
    ID3D11DepthStencilState* depth_state = nullptr;
    ID3D11SamplerState* texture_sampler = nullptr;
    ID3D11ShaderResourceView* default_white_srv = nullptr;

    float rt_width = 0.f;
    float rt_height = 0.f;
    bool initialized = false;
    char path_buffer[260] = "C:\\Users\\0x5\\Desktop\\model\\model.glb";
};

extern ModelViewerSystem g_model_viewer;
extern ModelViewerSystem g_player_model_viewer;

}
