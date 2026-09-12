#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace avatarmesh {

struct vec3_t { float x, y, z; };
struct vec2_t { float x, y; };

struct mesh_vertex {
    vec3_t position;
    vec3_t normal;
    vec2_t uv;
};

struct mesh_face {
    uint32_t a, b, c;
};

struct mesh_part {
    std::string name;
    std::string label;
    std::vector<mesh_vertex> vertices;
    std::vector<mesh_face> faces;
    vec3_t center;
    vec3_t size;
};

struct avatar_mesh {
    std::vector<mesh_part> parts;
    bool valid = false;
};

struct debug_stats {
    size_t cache_count;
    size_t pending_count;
    size_t queue_size;
    size_t fetch_ok;
    size_t fetch_fail;
    size_t fail_api;
    size_t fail_imageurl;
    size_t fail_meta;
    size_t fail_obj_hash;
    size_t fail_cdn;
    size_t fail_parse;
    char last_error[128];
    int last_http_status;
};

void initialize();
const avatar_mesh* get_avatar_mesh(int64_t user_id, bool is_r15);
void request_avatar_mesh(int64_t user_id, bool is_r15);
const mesh_part* find_part(const avatar_mesh* mesh, const char* roblox_name, bool is_r15);
debug_stats get_debug_stats();

}
