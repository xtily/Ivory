#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace assetmesh {

struct vec3_t {
    float x;
    float y;
    float z;
};

struct vec2_t {
    float x;
    float y;
};

struct mesh_vertex {
    vec3_t position;
    vec3_t normal;
    vec2_t uv;
};

struct mesh_bounds {
    vec3_t min;
    vec3_t max;
    vec3_t center;
    vec3_t size;
    bool valid = false;
};

struct parsed_mesh {
    std::string version;
    std::vector<mesh_vertex> vertices;
    std::vector<uint32_t> indices;
    mesh_bounds bounds;
};

struct debug_stats {
    size_t cache_count;
    size_t pending_count;
    size_t queue_size;
    size_t fetch_ok;
    size_t fetch_fail;
    size_t fail_api;
    size_t fail_download;
    size_t fail_parse;
    char last_error[128];
    int last_http_status;
    uint64_t last_failed_asset_id;
    size_t avatar_cache_count;
    size_t avatar_pending_count;
    size_t retry_after_count;
};

void initialize();
void shutdown();
uint64_t get_mesh_asset_id_from_part(uintptr_t part_address);
std::string get_mesh_id_string_from_part(uintptr_t part_address);
uint64_t get_mesh_asset_id_for_part(int64_t user_id, const char* part_name, bool is_r15);
uint64_t get_default_asset_for_part(const char* part_name, bool is_r15);
void request_mesh(uint64_t asset_id);
void request_avatar_assets(int64_t user_id);
std::shared_ptr<const parsed_mesh> get_mesh(uint64_t asset_id);
std::shared_ptr<const parsed_mesh> get_mesh_for_part(const char* part_name, bool is_r15, uint64_t asset_id = 0);
debug_stats get_debug_stats();

}
