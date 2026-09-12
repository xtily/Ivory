#include "gltfloader.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

using json = nlohmann::json;

namespace ModelViewer
{

#pragma pack(push, 1)
struct GLBHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t length;
};

struct GLBChunkHeader
{
    uint32_t length;
    uint32_t type;
};
#pragma pack(pop)

constexpr uint32_t GLB_MAGIC  = 0x46546C67;
constexpr uint32_t CHUNK_JSON = 0x4E4F534A;
constexpr uint32_t CHUNK_BIN  = 0x004E4942;

void ModelData::CalculateBoundsAndNormals()
{
    if (vertices.empty()) return;

    bounds_min = vertices[0].position;
    bounds_max = vertices[0].position;

    for (const auto& v : vertices)
    {
        bounds_min.x = (std::min)(bounds_min.x, v.position.x);
        bounds_min.y = (std::min)(bounds_min.y, v.position.y);
        bounds_min.z = (std::min)(bounds_min.z, v.position.z);

        bounds_max.x = (std::max)(bounds_max.x, v.position.x);
        bounds_max.y = (std::max)(bounds_max.y, v.position.y);
        bounds_max.z = (std::max)(bounds_max.z, v.position.z);
    }

    center.x = (bounds_min.x + bounds_max.x) * 0.5f;
    center.y = (bounds_min.y + bounds_max.y) * 0.5f;
    center.z = (bounds_min.z + bounds_max.z) * 0.5f;

    float dx = bounds_max.x - bounds_min.x;
    float dy = bounds_max.y - bounds_min.y;
    float dz = bounds_max.z - bounds_min.z;
    radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
    if (radius < 0.0001f) radius = 1.0f;

    bool has_normals = false;
    for (const auto& v : vertices)
    {
        if (v.normal.x != 0.f || v.normal.y != 0.f || v.normal.z != 0.f)
        {
            has_normals = true;
            break;
        }
    }

    if (!has_normals && !indices.empty())
    {
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

            DirectX::XMVECTOR p0 = DirectX::XMLoadFloat3(&vertices[i0].position);
            DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&vertices[i1].position);
            DirectX::XMVECTOR p2 = DirectX::XMLoadFloat3(&vertices[i2].position);

            DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p1, p0);
            DirectX::XMVECTOR e2 = DirectX::XMVectorSubtract(p2, p0);
            DirectX::XMVECTOR n = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(e1, e2));

            DirectX::XMFLOAT3 normal_val;
            DirectX::XMStoreFloat3(&normal_val, n);

            vertices[i0].normal = normal_val;
            vertices[i1].normal = normal_val;
            vertices[i2].normal = normal_val;
        }
    }
}

void ModelData::CreateDX11Textures(ID3D11Device* device)
{
    if (!device) return;

    for (auto& tex : textures)
    {
        if (tex.srv || tex.pixels.empty() || tex.width <= 0 || tex.height <= 0) continue;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = tex.width;
        desc.Height = tex.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init_data{};
        init_data.pSysMem = tex.pixels.data();
        init_data.SysMemPitch = tex.width * 4;

        ID3D11Texture2D* texture_obj = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &init_data, &texture_obj);
        if (SUCCEEDED(hr))
        {
            device->CreateShaderResourceView(texture_obj, nullptr, &tex.srv);
            texture_obj->Release();
        }
    }
}

bool LoadGLBModelFromMemory(const uint8_t* data, size_t size, ModelData& out_model, ID3D11Device* device)
{
    out_model.Clear();

    if (!data || size < sizeof(GLBHeader))
    {
        out_model.error_message = "Invalid GLB memory buffer or size too small";
        return false;
    }

    const GLBHeader* header = reinterpret_cast<const GLBHeader*>(data);
    if (header->magic != GLB_MAGIC)
    {
        out_model.error_message = "Invalid GLB header/magic";
        return false;
    }

    std::string json_str;
    std::vector<uint8_t> bin_buffer;

    size_t offset = sizeof(GLBHeader);
    size_t total_length = (std::min)(static_cast<size_t>(header->length), size);

    while (offset + sizeof(GLBChunkHeader) <= total_length)
    {
        const GLBChunkHeader* chunk_header = reinterpret_cast<const GLBChunkHeader*>(data + offset);
        offset += sizeof(GLBChunkHeader);

        if (offset + chunk_header->length > total_length) break;

        if (chunk_header->type == CHUNK_JSON)
        {
            json_str.assign(reinterpret_cast<const char*>(data + offset), chunk_header->length);
        }
        else
        {
            bin_buffer.assign(data + offset, data + offset + chunk_header->length);
        }

        offset += chunk_header->length;
    }

    if (json_str.empty())
    {
        out_model.error_message = "GLB JSON chunk not found";
        return false;
    }

    json doc;
    try
    {
        doc = json::parse(json_str);
    }
    catch (const std::exception& e)
    {
        out_model.error_message = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!doc.contains("accessors") || !doc.contains("bufferViews") || !doc.contains("meshes"))
    {
        out_model.error_message = "Missing accessors, bufferViews, or meshes in GLTF JSON";
        return false;
    }

    auto accessors = doc["accessors"];
    auto bufferViews = doc["bufferViews"];
    auto meshes = doc["meshes"];

    if (doc.contains("materials"))
    {
        for (const auto& mat_json : doc["materials"])
        {
            MaterialData mat;
            mat.name = mat_json.value("name", "Material");

            if (mat_json.contains("pbrMetallicRoughness"))
            {
                const auto& pbr = mat_json["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor"))
                {
                    const auto& col_arr = pbr["baseColorFactor"];
                    if (col_arr.size() >= 4)
                    {
                        mat.base_color_factor = DirectX::XMFLOAT4(
                            col_arr[0].get<float>(),
                            col_arr[1].get<float>(),
                            col_arr[2].get<float>(),
                            col_arr[3].get<float>()
                        );
                    }
                }

                if (pbr.contains("baseColorTexture"))
                {
                    mat.texture_index = pbr["baseColorTexture"].value("index", -1);
                }
            }

            out_model.materials.push_back(mat);
        }
    }

    if (doc.contains("images") && doc.contains("textures"))
    {
        auto images = doc["images"];
        auto textures_json = doc["textures"];

        for (const auto& tex_json : textures_json)
        {
            int source_idx = tex_json.value("source", -1);
            TextureData tex_data;

            if (source_idx >= 0 && source_idx < (int)images.size())
            {
                const auto& img_json = images[source_idx];
                int bv_idx = img_json.value("bufferView", -1);

                if (bv_idx >= 0 && bv_idx < (int)bufferViews.size())
                {
                    const auto& bv = bufferViews[bv_idx];
                    size_t bv_offset = bv.value("byteOffset", 0);
                    size_t bv_length = bv.value("byteLength", 0);

                    if (bv_offset + bv_length <= bin_buffer.size())
                    {
                        const uint8_t* img_bytes = bin_buffer.data() + bv_offset;
                        int w = 0, h = 0, channels = 0;
                        stbi_uc* decoded = stbi_load_from_memory(img_bytes, static_cast<int>(bv_length), &w, &h, &channels, 4);

                        if (decoded)
                        {
                            tex_data.width = w;
                            tex_data.height = h;
                            tex_data.pixels.assign(decoded, decoded + (w * h * 4));
                            stbi_image_free(decoded);
                        }
                    }
                }
            }

            out_model.textures.push_back(tex_data);
        }
    }

    auto get_buffer_data = [&](int accessor_idx, const uint8_t*& out_ptr, size_t& out_count, size_t& out_stride, int& out_component_type, std::string& out_type) -> bool
    {
        if (accessor_idx < 0 || accessor_idx >= (int)accessors.size()) return false;
        const auto& acc = accessors[accessor_idx];

        int bv_idx = acc.value("bufferView", -1);
        if (bv_idx < 0 || bv_idx >= (int)bufferViews.size()) return false;
        const auto& bv = bufferViews[bv_idx];

        size_t byte_offset = acc.value("byteOffset", 0) + bv.value("byteOffset", 0);
        if (byte_offset >= bin_buffer.size()) return false;

        out_ptr = bin_buffer.data() + byte_offset;
        out_count = acc.value("count", 0);
        out_component_type = acc.value("componentType", 5126);
        out_type = acc.value("type", "SCALAR");

        size_t default_component_size = 4;
        if (out_component_type == 5120 || out_component_type == 5121) default_component_size = 1;
        else if (out_component_type == 5122 || out_component_type == 5123) default_component_size = 2;
        else if (out_component_type == 5125 || out_component_type == 5126) default_component_size = 4;

        size_t num_components = 1;
        if (out_type == "VEC2") num_components = 2;
        else if (out_type == "VEC3") num_components = 3;
        else if (out_type == "VEC4") num_components = 4;

        out_stride = bv.value("byteStride", num_components * default_component_size);
        if (out_stride == 0) out_stride = num_components * default_component_size;

        return true;
    };

    struct NodeInfo {
        int mesh_idx = -1;
        DirectX::XMMATRIX transform = DirectX::XMMatrixIdentity();
    };

    std::vector<NodeInfo> node_infos;
    if (doc.contains("nodes"))
    {
        auto nodes = doc["nodes"];
        std::vector<DirectX::XMMATRIX> local_matrices(nodes.size(), DirectX::XMMatrixIdentity());

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& n = nodes[i];
            DirectX::XMMATRIX local = DirectX::XMMatrixIdentity();

            if (n.contains("matrix"))
            {
                const auto& m = n["matrix"];
                if (m.size() == 16)
                {
                    local = DirectX::XMMATRIX(
                        m[0].get<float>(), m[1].get<float>(), m[2].get<float>(), m[3].get<float>(),
                        m[4].get<float>(), m[5].get<float>(), m[6].get<float>(), m[7].get<float>(),
                        m[8].get<float>(), m[9].get<float>(), m[10].get<float>(), m[11].get<float>(),
                        m[12].get<float>(), m[13].get<float>(), m[14].get<float>(), m[15].get<float>()
                    );
                }
            }
            else
            {
                DirectX::XMMATRIX t_mat = DirectX::XMMatrixIdentity();
                DirectX::XMMATRIX r_mat = DirectX::XMMatrixIdentity();
                DirectX::XMMATRIX s_mat = DirectX::XMMatrixIdentity();

                if (n.contains("translation") && n["translation"].size() == 3)
                {
                    t_mat = DirectX::XMMatrixTranslation(
                        n["translation"][0].get<float>(),
                        n["translation"][1].get<float>(),
                        n["translation"][2].get<float>()
                    );
                }
                if (n.contains("rotation") && n["rotation"].size() == 4)
                {
                    DirectX::XMVECTOR q = DirectX::XMVectorSet(
                        n["rotation"][0].get<float>(),
                        n["rotation"][1].get<float>(),
                        n["rotation"][2].get<float>(),
                        n["rotation"][3].get<float>()
                    );
                    r_mat = DirectX::XMMatrixRotationQuaternion(q);
                }
                if (n.contains("scale") && n["scale"].size() == 3)
                {
                    s_mat = DirectX::XMMatrixScaling(
                        n["scale"][0].get<float>(),
                        n["scale"][1].get<float>(),
                        n["scale"][2].get<float>()
                    );
                }
                local = s_mat * r_mat * t_mat;
            }
            local_matrices[i] = local;
        }

        std::function<void(int, DirectX::XMMATRIX)> traverse_node = [&](int node_idx, DirectX::XMMATRIX parent_mat)
        {
            if (node_idx < 0 || node_idx >= (int)nodes.size()) return;
            const auto& n = nodes[node_idx];
            DirectX::XMMATRIX world = local_matrices[node_idx] * parent_mat;

            if (n.contains("mesh"))
            {
                NodeInfo ni;
                ni.mesh_idx = n["mesh"].get<int>();
                ni.transform = world;
                node_infos.push_back(ni);
            }

            if (n.contains("children"))
            {
                for (const auto& child_idx : n["children"])
                {
                    traverse_node(child_idx.get<int>(), world);
                }
            }
        };

        if (doc.contains("scenes") && doc.contains("scene"))
        {
            int scene_idx = doc.value("scene", 0);
            if (scene_idx >= 0 && scene_idx < (int)doc["scenes"].size())
            {
                const auto& sc = doc["scenes"][scene_idx];
                if (sc.contains("nodes"))
                {
                    for (const auto& root_node_idx : sc["nodes"])
                    {
                        traverse_node(root_node_idx.get<int>(), DirectX::XMMatrixIdentity());
                    }
                }
            }
        }
        else
        {
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                traverse_node(static_cast<int>(i), DirectX::XMMatrixIdentity());
            }
        }
    }

    if (node_infos.empty())
    {
        for (int m = 0; m < (int)meshes.size(); ++m)
        {
            NodeInfo ni;
            ni.mesh_idx = m;
            ni.transform = DirectX::XMMatrixIdentity();
            node_infos.push_back(ni);
        }
    }

    for (const auto& node_item : node_infos)
    {
        int mesh_idx = node_item.mesh_idx;
        if (mesh_idx < 0 || mesh_idx >= (int)meshes.size()) continue;
        const auto& mesh = meshes[mesh_idx];
        if (!mesh.contains("primitives")) continue;

        DirectX::XMMATRIX node_trans = node_item.transform;

        for (const auto& prim : mesh["primitives"])
        {
            if (!prim.contains("attributes")) continue;
            const auto& attrs = prim["attributes"];

            uint32_t vertex_start_idx = static_cast<uint32_t>(out_model.vertices.size());

            if (attrs.contains("POSITION"))
            {
                int pos_acc = attrs["POSITION"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(pos_acc, ptr, count, stride, comp_type, type_str) && comp_type == 5126 && type_str == "VEC3")
                {
                    size_t base_v = out_model.vertices.size();
                    out_model.vertices.resize(base_v + count);
                    for (size_t i = 0; i < count; ++i)
                    {
                        const float* pos = reinterpret_cast<const float*>(ptr + i * stride);
                        DirectX::XMVECTOR p_vec = DirectX::XMVectorSet(pos[0], pos[1], pos[2], 1.0f);
                        p_vec = DirectX::XMVector3Transform(p_vec, node_trans);

                        out_model.vertices[base_v + i].position = DirectX::XMFLOAT3(
                            DirectX::XMVectorGetX(p_vec),
                            DirectX::XMVectorGetY(p_vec),
                            DirectX::XMVectorGetZ(p_vec)
                        );
                        out_model.vertices[base_v + i].normal = DirectX::XMFLOAT3(0.f, 0.f, 0.f);
                        out_model.vertices[base_v + i].texcoord = DirectX::XMFLOAT2(0.f, 0.f);
                        out_model.vertices[base_v + i].color = DirectX::XMFLOAT4(1.f, 1.f, 1.f, 1.f);
                    }
                }
            }

            if (attrs.contains("NORMAL"))
            {
                int norm_acc = attrs["NORMAL"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(norm_acc, ptr, count, stride, comp_type, type_str) && comp_type == 5126 && type_str == "VEC3")
                {
                    for (size_t i = 0; i < count && (vertex_start_idx + i) < out_model.vertices.size(); ++i)
                    {
                        const float* norm = reinterpret_cast<const float*>(ptr + i * stride);
                        DirectX::XMVECTOR n_vec = DirectX::XMVectorSet(norm[0], norm[1], norm[2], 0.0f);
                        n_vec = DirectX::XMVector3TransformNormal(n_vec, node_trans);
                        n_vec = DirectX::XMVector3Normalize(n_vec);

                        out_model.vertices[vertex_start_idx + i].normal = DirectX::XMFLOAT3(
                            DirectX::XMVectorGetX(n_vec),
                            DirectX::XMVectorGetY(n_vec),
                            DirectX::XMVectorGetZ(n_vec)
                        );
                    }
                }
            }

            if (attrs.contains("TEXCOORD_0"))
            {
                int uv_acc = attrs["TEXCOORD_0"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(uv_acc, ptr, count, stride, comp_type, type_str) && comp_type == 5126 && type_str == "VEC2")
                {
                    for (size_t i = 0; i < count && (vertex_start_idx + i) < out_model.vertices.size(); ++i)
                    {
                        const float* uv = reinterpret_cast<const float*>(ptr + i * stride);
                        out_model.vertices[vertex_start_idx + i].texcoord = DirectX::XMFLOAT2(uv[0], uv[1]);
                    }
                }
            }

            if (attrs.contains("COLOR_0"))
            {
                int col_acc = attrs["COLOR_0"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(col_acc, ptr, count, stride, comp_type, type_str) && comp_type == 5126)
                {
                    for (size_t i = 0; i < count && (vertex_start_idx + i) < out_model.vertices.size(); ++i)
                    {
                        const float* col = reinterpret_cast<const float*>(ptr + i * stride);
                        if (type_str == "VEC3")
                            out_model.vertices[vertex_start_idx + i].color = DirectX::XMFLOAT4(col[0], col[1], col[2], 1.0f);
                        else if (type_str == "VEC4")
                            out_model.vertices[vertex_start_idx + i].color = DirectX::XMFLOAT4(col[0], col[1], col[2], col[3]);
                    }
                }
            }

            uint32_t index_offset = static_cast<uint32_t>(out_model.indices.size());
            uint32_t index_count = 0;

            if (prim.contains("indices"))
            {
                int ind_acc = prim["indices"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(ind_acc, ptr, count, stride, comp_type, type_str))
                {
                    index_count = static_cast<uint32_t>(count);
                    out_model.indices.reserve(out_model.indices.size() + count);

                    for (size_t i = 0; i < count; ++i)
                    {
                        uint32_t idx = 0;
                        if (comp_type == 5121)
                            idx = *(ptr + i * stride);
                        else if (comp_type == 5123) 
                            idx = *reinterpret_cast<const uint16_t*>(ptr + i * stride);
                        else if (comp_type == 5125)
                            idx = *reinterpret_cast<const uint32_t*>(ptr + i * stride);

                        out_model.indices.push_back(vertex_start_idx + idx);
                    }
                }
            }

            SubMesh submesh;
            submesh.name = mesh.value("name", "Mesh");
            submesh.index_offset = index_offset;
            submesh.index_count = index_count;
            submesh.material_index = prim.value("material", -1);
            out_model.submeshes.push_back(submesh);
        }
    }

    out_model.CalculateBoundsAndNormals();
    if (device)
    {
        out_model.CreateDX11Textures(device);
    }

    out_model.loaded = !out_model.vertices.empty();
    if (!out_model.loaded)
    {
        out_model.error_message = "No valid vertex data loaded from GLB model";
    }

    return out_model.loaded;
}

bool LoadGLBModel(const std::string& filepath, ModelData& out_model, ID3D11Device* device)
{
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        out_model.error_message = "Failed to open file: " + filepath;
        return false;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (file_size <= 0) return false;

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size))
    {
        out_model.error_message = "Failed to read GLB file: " + filepath;
        return false;
    }

    return LoadGLBModelFromMemory(buffer.data(), buffer.size(), out_model, device);
}

bool LoadGLTFModel(const std::string& filepath, ModelData& out_model, ID3D11Device* device)
{
    out_model.Clear();

    std::ifstream file(filepath);
    if (!file.is_open())
    {
        out_model.error_message = "Failed to open glTF file: " + filepath;
        return false;
    }

    std::string gltf_dir = std::filesystem::path(filepath).parent_path().string();

    json doc;
    try
    {
        file >> doc;
    }
    catch (const std::exception& e)
    {
        out_model.error_message = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!doc.contains("accessors") || !doc.contains("bufferViews") || !doc.contains("meshes"))
    {
        out_model.error_message = "Missing accessors, bufferViews, or meshes in GLTF JSON";
        return false;
    }

    // Load external buffers (.bin)
    std::vector<std::vector<uint8_t>> buffers_data;
    if (doc.contains("buffers"))
    {
        for (const auto& buf_json : doc["buffers"])
        {
            std::vector<uint8_t> bin_buf;
            if (buf_json.contains("uri"))
            {
                std::string uri = buf_json["uri"];
                std::string bin_path = (std::filesystem::path(gltf_dir) / uri).string();
                std::ifstream bin_file(bin_path, std::ios::binary | std::ios::ate);
                if (bin_file.is_open())
                {
                    std::streamsize b_size = bin_file.tellg();
                    bin_file.seekg(0, std::ios::beg);
                    bin_buf.resize(static_cast<size_t>(b_size));
                    bin_file.read(reinterpret_cast<char*>(bin_buf.data()), b_size);
                }
            }
            buffers_data.push_back(std::move(bin_buf));
        }
    }

    auto accessors = doc["accessors"];
    auto bufferViews = doc["bufferViews"];
    auto meshes = doc["meshes"];

    // Materials
    if (doc.contains("materials"))
    {
        for (const auto& mat_json : doc["materials"])
        {
            MaterialData mat;
            mat.name = mat_json.value("name", "Material");

            if (mat_json.contains("pbrMetallicRoughness"))
            {
                const auto& pbr = mat_json["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor"))
                {
                    const auto& col_arr = pbr["baseColorFactor"];
                    if (col_arr.size() >= 4)
                    {
                        mat.base_color_factor = DirectX::XMFLOAT4(
                            col_arr[0].get<float>(),
                            col_arr[1].get<float>(),
                            col_arr[2].get<float>(),
                            col_arr[3].get<float>()
                        );
                    }
                }

                if (pbr.contains("baseColorTexture"))
                {
                    mat.texture_index = pbr["baseColorTexture"].value("index", -1);
                }
            }

            out_model.materials.push_back(mat);
        }
    }

    // Textures & Images
    if (doc.contains("images") && doc.contains("textures"))
    {
        auto images = doc["images"];
        auto textures_json = doc["textures"];

        for (const auto& tex_json : textures_json)
        {
            int source_idx = tex_json.value("source", -1);
            TextureData tex_data;

            if (source_idx >= 0 && source_idx < (int)images.size())
            {
                const auto& img_json = images[source_idx];
                if (img_json.contains("uri"))
                {
                    std::string img_uri = img_json["uri"];
                    std::string img_path = (std::filesystem::path(gltf_dir) / img_uri).string();
                    int w = 0, h = 0, channels = 0;
                    stbi_uc* decoded = stbi_load(img_path.c_str(), &w, &h, &channels, 4);
                    if (decoded)
                    {
                        tex_data.width = w;
                        tex_data.height = h;
                        tex_data.pixels.assign(decoded, decoded + (w * h * 4));
                        stbi_image_free(decoded);
                    }
                }
                else if (img_json.contains("bufferView"))
                {
                    int bv_idx = img_json.value("bufferView", -1);
                    if (bv_idx >= 0 && bv_idx < (int)bufferViews.size())
                    {
                        const auto& bv = bufferViews[bv_idx];
                        int buf_idx = bv.value("buffer", 0);
                        size_t bv_offset = bv.value("byteOffset", 0);
                        size_t bv_length = bv.value("byteLength", 0);

                        if (buf_idx < (int)buffers_data.size() && bv_offset + bv_length <= buffers_data[buf_idx].size())
                        {
                            const uint8_t* img_bytes = buffers_data[buf_idx].data() + bv_offset;
                            int w = 0, h = 0, channels = 0;
                            stbi_uc* decoded = stbi_load_from_memory(img_bytes, static_cast<int>(bv_length), &w, &h, &channels, 4);
                            if (decoded)
                            {
                                tex_data.width = w;
                                tex_data.height = h;
                                tex_data.pixels.assign(decoded, decoded + (w * h * 4));
                                stbi_image_free(decoded);
                            }
                        }
                    }
                }
            }

            out_model.textures.push_back(tex_data);
        }
    }

    auto get_buffer_data = [&](int accessor_idx, const uint8_t*& out_ptr, size_t& out_count, size_t& out_stride, int& out_component_type, std::string& out_type) -> bool
    {
        if (accessor_idx < 0 || accessor_idx >= (int)accessors.size()) return false;
        const auto& acc = accessors[accessor_idx];

        int bv_idx = acc.value("bufferView", -1);
        if (bv_idx < 0 || bv_idx >= (int)bufferViews.size()) return false;
        const auto& bv = bufferViews[bv_idx];

        int buf_idx = bv.value("buffer", 0);
        if (buf_idx < 0 || buf_idx >= (int)buffers_data.size()) return false;

        size_t byte_offset = acc.value("byteOffset", 0) + bv.value("byteOffset", 0);
        if (byte_offset >= buffers_data[buf_idx].size()) return false;

        out_ptr = buffers_data[buf_idx].data() + byte_offset;
        out_count = acc.value("count", 0);
        out_component_type = acc.value("componentType", 5126);
        out_type = acc.value("type", "SCALAR");

        size_t default_component_size = 4;
        if (out_component_type == 5120 || out_component_type == 5121) default_component_size = 1;
        else if (out_component_type == 5122 || out_component_type == 5123) default_component_size = 2;
        else if (out_component_type == 5125 || out_component_type == 5126) default_component_size = 4;

        size_t num_components = 1;
        if (out_type == "VEC2") num_components = 2;
        else if (out_type == "VEC3") num_components = 3;
        else if (out_type == "VEC4") num_components = 4;
        else if (out_type == "MAT4") num_components = 16;

        out_stride = bv.value("byteStride", default_component_size * num_components);
        return true;
    };

    // Node hierarchy traversal for glTF
    std::unordered_map<int, DirectX::XMMATRIX> mesh_world_matrices;
    std::unordered_map<int, std::vector<int>> node_children;
    std::unordered_map<int, DirectX::XMMATRIX> node_local_matrices;
    std::unordered_map<int, int> node_mesh;
    std::vector<int> all_nodes;
    std::unordered_set<int> child_node_set;

    if (doc.contains("nodes"))
    {
        int n_idx = 0;
        for (const auto& node_json : doc["nodes"])
        {
            all_nodes.push_back(n_idx);

            DirectX::XMMATRIX local = DirectX::XMMatrixIdentity();

            if (node_json.contains("matrix"))
            {
                const auto& m_arr = node_json["matrix"];
                if (m_arr.size() == 16)
                {
                    local = DirectX::XMMATRIX(
                        m_arr[0].get<float>(), m_arr[1].get<float>(), m_arr[2].get<float>(), m_arr[3].get<float>(),
                        m_arr[4].get<float>(), m_arr[5].get<float>(), m_arr[6].get<float>(), m_arr[7].get<float>(),
                        m_arr[8].get<float>(), m_arr[9].get<float>(), m_arr[10].get<float>(), m_arr[11].get<float>(),
                        m_arr[12].get<float>(), m_arr[13].get<float>(), m_arr[14].get<float>(), m_arr[15].get<float>()
                    );
                }
            }
            else
            {
                DirectX::XMVECTOR scale = DirectX::XMVectorSet(1.f, 1.f, 1.f, 0.f);
                DirectX::XMVECTOR rot_quat = DirectX::XMQuaternionIdentity();
                DirectX::XMVECTOR trans = DirectX::XMVectorZero();

                if (node_json.contains("scale"))
                {
                    const auto& s = node_json["scale"];
                    if (s.size() >= 3)
                        scale = DirectX::XMVectorSet(s[0].get<float>(), s[1].get<float>(), s[2].get<float>(), 0.f);
                }
                if (node_json.contains("rotation"))
                {
                    const auto& r = node_json["rotation"];
                    if (r.size() >= 4)
                        rot_quat = DirectX::XMVectorSet(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
                }
                if (node_json.contains("translation"))
                {
                    const auto& t = node_json["translation"];
                    if (t.size() >= 3)
                        trans = DirectX::XMVectorSet(t[0].get<float>(), t[1].get<float>(), t[2].get<float>(), 0.f);
                }

                local = DirectX::XMMatrixAffineTransformation(scale, DirectX::XMVectorZero(), rot_quat, trans);
            }

            node_local_matrices[n_idx] = local;

            if (node_json.contains("mesh"))
            {
                node_mesh[n_idx] = node_json["mesh"].get<int>();
            }

            if (node_json.contains("children"))
            {
                for (const auto& c : node_json["children"])
                {
                    int cid = c.get<int>();
                    node_children[n_idx].push_back(cid);
                    child_node_set.insert(cid);
                }
            }

            n_idx++;
        }
    }

    std::function<void(int, DirectX::XMMATRIX)> traverse_node = [&](int node_id, DirectX::XMMATRIX parent_mat)
    {
        DirectX::XMMATRIX world = DirectX::XMMatrixMultiply(node_local_matrices[node_id], parent_mat);
        if (node_mesh.find(node_id) != node_mesh.end())
        {
            mesh_world_matrices[node_mesh[node_id]] = world;
        }
        for (int child_id : node_children[node_id])
        {
            traverse_node(child_id, world);
        }
    };

    std::vector<int> root_nodes;
    if (doc.contains("scenes") && doc.contains("scene"))
    {
        int sc_idx = doc.value("scene", 0);
        if (sc_idx >= 0 && sc_idx < (int)doc["scenes"].size())
        {
            const auto& sc = doc["scenes"][sc_idx];
            if (sc.contains("nodes"))
            {
                for (const auto& n : sc["nodes"])
                    root_nodes.push_back(n.get<int>());
            }
        }
    }
    if (root_nodes.empty())
    {
        for (int nid : all_nodes)
        {
            if (child_node_set.find(nid) == child_node_set.end())
                root_nodes.push_back(nid);
        }
    }
    for (int rnode : root_nodes)
    {
        traverse_node(rnode, DirectX::XMMatrixIdentity());
    }

    // Meshes extraction
    for (int mesh_idx = 0; mesh_idx < (int)meshes.size(); ++mesh_idx)
    {
        const auto& mesh = meshes[mesh_idx];
        if (!mesh.contains("primitives")) continue;

        DirectX::XMMATRIX node_xform = DirectX::XMMatrixIdentity();
        if (mesh_world_matrices.find(mesh_idx) != mesh_world_matrices.end())
        {
            node_xform = mesh_world_matrices[mesh_idx];
        }

        for (const auto& prim : mesh["primitives"])
        {
            if (!prim.contains("attributes")) continue;
            const auto& attribs = prim["attributes"];

            if (!attribs.contains("POSITION")) continue;

            int pos_acc = attribs["POSITION"];
            const uint8_t* pos_ptr = nullptr;
            size_t pos_count = 0, pos_stride = 0;
            int pos_comp_type = 0;
            std::string pos_type;

            if (!get_buffer_data(pos_acc, pos_ptr, pos_count, pos_stride, pos_comp_type, pos_type))
                continue;

            const uint8_t* norm_ptr = nullptr;
            size_t norm_count = 0, norm_stride = 0;
            int norm_comp_type = 0;
            std::string norm_type;
            if (attribs.contains("NORMAL"))
            {
                get_buffer_data(attribs["NORMAL"], norm_ptr, norm_count, norm_stride, norm_comp_type, norm_type);
            }

            const uint8_t* uv_ptr = nullptr;
            size_t uv_count = 0, uv_stride = 0;
            int uv_comp_type = 0;
            std::string uv_type;
            if (attribs.contains("TEXCOORD_0"))
            {
                get_buffer_data(attribs["TEXCOORD_0"], uv_ptr, uv_count, uv_stride, uv_comp_type, uv_type);
            }

            const uint8_t* col_ptr = nullptr;
            size_t col_count = 0, col_stride = 0;
            int col_comp_type = 0;
            std::string col_type;
            if (attribs.contains("COLOR_0"))
            {
                get_buffer_data(attribs["COLOR_0"], col_ptr, col_count, col_stride, col_comp_type, col_type);
            }

            uint32_t vertex_start_idx = static_cast<uint32_t>(out_model.vertices.size());
            uint32_t index_offset = static_cast<uint32_t>(out_model.indices.size());
            uint32_t index_count = 0;

            out_model.vertices.reserve(out_model.vertices.size() + pos_count);

            for (size_t i = 0; i < pos_count; ++i)
            {
                Vertex3D v{};
                const float* p = reinterpret_cast<const float*>(pos_ptr + i * pos_stride);
                DirectX::XMVECTOR p_vec = DirectX::XMVectorSet(p[0], p[1], p[2], 1.0f);
                p_vec = DirectX::XMVector3Transform(p_vec, node_xform);
                DirectX::XMStoreFloat3(&v.position, p_vec);

                if (norm_ptr && i < norm_count)
                {
                    const float* n = reinterpret_cast<const float*>(norm_ptr + i * norm_stride);
                    DirectX::XMVECTOR n_vec = DirectX::XMVectorSet(n[0], n[1], n[2], 0.0f);
                    n_vec = DirectX::XMVector3TransformNormal(n_vec, node_xform);
                    n_vec = DirectX::XMVector3Normalize(n_vec);
                    DirectX::XMStoreFloat3(&v.normal, n_vec);
                }
                else
                {
                    v.normal = { 0.f, 1.f, 0.f };
                }

                if (uv_ptr && i < uv_count)
                {
                    const float* uv = reinterpret_cast<const float*>(uv_ptr + i * uv_stride);
                    v.texcoord = { uv[0], uv[1] };
                }
                else
                {
                    v.texcoord = { 0.f, 0.f };
                }

                if (col_ptr && i < col_count)
                {
                    if (col_comp_type == 5126)
                    {
                        const float* c = reinterpret_cast<const float*>(col_ptr + i * col_stride);
                        v.color = { c[0], c[1], c[2], (col_type == "VEC4") ? c[3] : 1.0f };
                    }
                    else if (col_comp_type == 5121)
                    {
                        const uint8_t* c = col_ptr + i * col_stride;
                        v.color = { c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, (col_type == "VEC4") ? (c[3] / 255.f) : 1.0f };
                    }
                    else
                    {
                        v.color = { 1.f, 1.f, 1.f, 1.f };
                    }
                }
                else
                {
                    v.color = { 1.f, 1.f, 1.f, 1.f };
                }

                out_model.vertices.push_back(v);
            }

            if (prim.contains("indices"))
            {
                int ind_acc = prim["indices"];
                const uint8_t* ptr = nullptr;
                size_t count = 0, stride = 0;
                int comp_type = 0;
                std::string type_str;

                if (get_buffer_data(ind_acc, ptr, count, stride, comp_type, type_str))
                {
                    index_count = static_cast<uint32_t>(count);
                    out_model.indices.reserve(out_model.indices.size() + count);

                    for (size_t i = 0; i < count; ++i)
                    {
                        uint32_t idx = 0;
                        if (comp_type == 5121)
                            idx = *(ptr + i * stride);
                        else if (comp_type == 5123) 
                            idx = *reinterpret_cast<const uint16_t*>(ptr + i * stride);
                        else if (comp_type == 5125)
                            idx = *reinterpret_cast<const uint32_t*>(ptr + i * stride);

                        out_model.indices.push_back(vertex_start_idx + idx);
                    }
                }
            }

            SubMesh submesh;
            submesh.name = mesh.value("name", "Mesh");
            submesh.index_offset = index_offset;
            submesh.index_count = index_count;
            submesh.material_index = prim.value("material", -1);
            out_model.submeshes.push_back(submesh);
        }
    }

    out_model.CalculateBoundsAndNormals();
    if (device)
    {
        out_model.CreateDX11Textures(device);
    }

    out_model.loaded = !out_model.vertices.empty();
    if (!out_model.loaded)
    {
        out_model.error_message = "No valid vertex data loaded from glTF model";
    }

    return out_model.loaded;
}

bool LoadModelAny(const std::string& filepath, ModelData& out_model, ID3D11Device* device)
{
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".gltf")
    {
        return LoadGLTFModel(filepath, out_model, device);
    }
    else
    {
        return LoadGLBModel(filepath, out_model, device);
    }
}

bool ConvertAndSaveToResources(const std::string& source_path, const std::string& save_name, std::string& out_saved_path, std::string& out_error)
{
    try
    {
        std::filesystem::path src(source_path);
        if (!std::filesystem::exists(src))
        {
            out_error = "File does not exist: " + source_path;
            return false;
        }

        std::string res_dir = "C:\\Ivory\\Resources";
        std::filesystem::create_directories(res_dir);

        std::string filename = save_name.empty() ? src.filename().string() : save_name;
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".glb" && ext != ".gltf")
        {
            filename += ext;
        }

        std::filesystem::path target_file = std::filesystem::path(res_dir) / filename;
        out_saved_path = target_file.string();

        if (ext == ".gltf")
        {
            // For glTF, also copy referenced textures and .bin buffers into Resources alongside it
            std::filesystem::path parent = src.parent_path();
            for (const auto& entry : std::filesystem::recursive_directory_iterator(parent))
            {
                if (entry.is_regular_file())
                {
                    std::filesystem::path rel = std::filesystem::relative(entry.path(), parent);
                    std::filesystem::path dst = std::filesystem::path(res_dir) / rel;
                    std::filesystem::create_directories(dst.parent_path());
                    std::filesystem::copy_file(entry.path(), dst, std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        else
        {
            std::filesystem::copy_file(src, target_file, std::filesystem::copy_options::overwrite_existing);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        out_error = e.what();
        return false;
    }
}

}
