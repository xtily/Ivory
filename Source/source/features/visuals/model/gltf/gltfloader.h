#pragma once
#include <string>
#include <vector>
#include <d3d11.h>
#include <DirectXMath.h>

namespace ModelViewer
{

struct Vertex3D
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texcoord;
    DirectX::XMFLOAT4 color;
};

struct MaterialData
{
    std::string name;
    DirectX::XMFLOAT4 base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
    int texture_index = -1;
};

struct TextureData
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA 32-bit
    ID3D11ShaderResourceView* srv = nullptr;
};

struct SubMesh
{
    std::string name;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    int material_index = -1;
};

class ModelData
{
public:
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
    std::vector<MaterialData> materials;
    std::vector<TextureData> textures;

    DirectX::XMFLOAT3 bounds_min = { 0.f, 0.f, 0.f };
    DirectX::XMFLOAT3 bounds_max = { 0.f, 0.f, 0.f };
    DirectX::XMFLOAT3 center = { 0.f, 0.f, 0.f };
    float radius = 1.0f;

    bool loaded = false;
    std::string error_message;

    void Clear()
    {
        vertices.clear();
        indices.clear();
        submeshes.clear();
        materials.clear();

        for (auto& tex : textures)
        {
            if (tex.srv) { tex.srv->Release(); tex.srv = nullptr; }
        }
        textures.clear();

        bounds_min = { 0.f, 0.f, 0.f };
        bounds_max = { 0.f, 0.f, 0.f };
        center = { 0.f, 0.f, 0.f };
        radius = 1.0f;
        loaded = false;
        error_message.clear();
    }

    void CalculateBoundsAndNormals();
    void CreateDX11Textures(ID3D11Device* device);
};

bool LoadGLBModel(const std::string& filepath, ModelData& out_model, ID3D11Device* device = nullptr);
bool LoadGLBModelFromMemory(const uint8_t* data, size_t size, ModelData& out_model, ID3D11Device* device = nullptr);
bool LoadGLTFModel(const std::string& filepath, ModelData& out_model, ID3D11Device* device = nullptr);
bool LoadModelAny(const std::string& filepath, ModelData& out_model, ID3D11Device* device = nullptr);
bool ConvertAndSaveToResources(const std::string& source_path, const std::string& save_name, std::string& out_saved_path, std::string& out_error);

}
