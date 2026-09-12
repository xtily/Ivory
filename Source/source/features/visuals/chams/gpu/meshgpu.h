#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <d3d11.h>
#include <sdk/cache/core/cache.h>
#include "../mesh/assetmesh.h"

namespace meshgpu {

struct ResolvedMeshDraw {
    uint64_t cache_key = 0;
    const assetmesh::parsed_mesh* mesh = nullptr;
};

using MeshResolver = ResolvedMeshDraw(*)(const cache::entity_t& entity, const std::string& part_name, uintptr_t part_addr);

void shutdown();
void update_depth_bias(ID3D11Device* device, float depth_bias);
ResolvedMeshDraw default_resolver(const cache::entity_t& entity, const std::string& part_name, uintptr_t part_addr);
size_t render(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4]);
size_t render_advanced(const std::vector<cache::entity_t>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_color[4], const float outline_color[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, MeshResolver resolver, float viewport_offset_x = 0.0f, float viewport_offset_y = 0.0f);
size_t render_advanced_ptrs(const std::vector<const cache::entity_t*>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_color[4], const float outline_color[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, MeshResolver resolver, float viewport_offset_x = 0.0f, float viewport_offset_y = 0.0f);
size_t render_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver);
size_t render_wireframe_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver);
size_t render_union_fill_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver);
size_t render_union_outline_with_resolver(const std::vector<cache::entity_t>& entities, const float view[16], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float color[4], MeshResolver resolver);
size_t get_last_drawn_entities();

}