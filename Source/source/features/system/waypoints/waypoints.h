#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <sdk/math/math.h>
#include <imgui/imgui.h>

namespace waypoints {

struct Waypoint {
    std::string name;
    math::vector3 pos{ 0.f, 0.f, 0.f };
    uint64_t game_id = 0;
    uint64_t place_id = 0;
    std::string timestamp;
    float color[4] = { 1.0f, 0.34f, 0.50f, 1.0f };
    bool render_esp = true;
    bool show_tracer = false;
    bool show_distance = true;
    bool show_box = true;
};

struct WaypointsConfig {
    bool enabled = true;
    bool render_in_esp = true;
    bool show_all_games = false;
    bool draw_tracers = false;
    bool draw_distance = true;
    bool draw_boxes = true;
    float default_color[4] = { 1.0f, 0.34f, 0.50f, 1.0f };
    float box_size = 2.0f;
};

inline WaypointsConfig g_config;

// Directories
std::string get_waypoints_directory();
bool ensure_waypoints_directory();

// Active waypoints
std::vector<Waypoint>& get_active_waypoints();
std::mutex& get_mutex();
void add_waypoint(const std::string& name, const math::vector3& pos, uint64_t game_id = 0, uint64_t place_id = 0, const float col[4] = nullptr);
bool remove_waypoint(size_t index);
void clear_waypoints();
bool teleport_to_waypoint(size_t index);
bool teleport_to_pos(const math::vector3& pos);

// Profile persistence (JSON files in C:\Ivory\Waypoints)
std::vector<std::string> get_profile_list();
bool save_profile(const std::string& profile_name);
bool load_profile(const std::string& profile_name);
bool delete_profile(const std::string& profile_name);

// Auto-save & auto-load for current GameId/PlaceId
bool save_game_waypoints();
bool load_game_waypoints();

// ESP rendering
void render_esp(ImDrawList* draw_list, const math::matrix4& view, const math::vector2& dims, const ImVec2& screen_offset);

}
