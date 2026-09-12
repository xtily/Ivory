#pragma once
#include <sdk/cache/core/cache.h>
#include "../gpu/meshgpu.h"

namespace memorymesh {
    void start();
    void render(const std::vector<cache::entity_t>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_col[4], const float outline_col[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, float viewport_offset_x = 0.0f, float viewport_offset_y = 0.0f);
    void render_ptrs(const std::vector<const cache::entity_t*>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_col[4], const float outline_col[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, float viewport_offset_x = 0.0f, float viewport_offset_y = 0.0f);
    void shutdown();
    meshgpu::ResolvedMeshDraw resolve_live_mesh_gpu(const cache::entity_t& entity, const std::string& part_name, uintptr_t part_address);
}
