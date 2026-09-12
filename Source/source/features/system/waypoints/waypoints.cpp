#include "waypoints.h"
#include <core/globals.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace waypoints {

static std::vector<Waypoint> g_waypoints;
static std::mutex g_waypoints_mutex;

std::string get_waypoints_directory()
{
    return "C:\\Ivory\\Waypoints";
}

bool ensure_waypoints_directory()
{
    std::string dir = get_waypoints_directory();
    try
    {
        std::filesystem::create_directories(dir);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::vector<Waypoint>& get_active_waypoints()
{
    return g_waypoints;
}

std::mutex& get_mutex()
{
    return g_waypoints_mutex;
}

static std::string get_current_timestamp_str()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
    localtime_s(&bt, &in_time_t);
    std::stringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void add_waypoint(const std::string& name, const math::vector3& pos, uint64_t game_id, uint64_t place_id, const float col[4])
{
    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    Waypoint wp;
    wp.name = name.empty() ? ("Waypoint " + std::to_string(g_waypoints.size() + 1)) : name;
    wp.pos = pos;
    wp.game_id = game_id;
    wp.place_id = place_id;
    wp.timestamp = get_current_timestamp_str();
    if (col)
    {
        wp.color[0] = col[0];
        wp.color[1] = col[1];
        wp.color[2] = col[2];
        wp.color[3] = col[3];
    }
    else
    {
        wp.color[0] = g_config.default_color[0];
        wp.color[1] = g_config.default_color[1];
        wp.color[2] = g_config.default_color[2];
        wp.color[3] = g_config.default_color[3];
    }
    wp.render_esp = true;
    wp.show_tracer = g_config.draw_tracers;
    wp.show_distance = g_config.draw_distance;
    wp.show_box = g_config.draw_boxes;

    g_waypoints.push_back(wp);
}

bool remove_waypoint(size_t index)
{
    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    if (index >= g_waypoints.size()) return false;
    g_waypoints.erase(g_waypoints.begin() + index);
    return true;
}

void clear_waypoints()
{
    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    g_waypoints.clear();
}

bool teleport_to_pos(const math::vector3& pos)
{
    cache::entity_t local_ent = cache::get_local_player();
    uintptr_t hrp_addr = local_ent.humanoid_root_part.address;
    if (!hrp_addr)
    {
        if (cache::local_character.address)
        {
            rbx::c_instance char_inst(cache::local_character.address);
            uint64_t hrp_child = char_inst.find_first_child("HumanoidRootPart");
            if (hrp_child)
                hrp_addr = hrp_child;
        }
    }
    if (!hrp_addr) return false;

    rbx::c_instance hrp_inst(hrp_addr);
    uintptr_t prim_addr = hrp_inst.get_primitive().address;
    if (!prim_addr || prim_addr < 0x10000) return false;

    math::vector3 elevated_pos = pos;
    elevated_pos.y += 1.0f; // slightly elevate to prevent getting stuck in ground

    memory->write<math::vector3>(prim_addr + Offsets::Primitive::Position, elevated_pos);
    memory->write<math::vector3>(prim_addr + Offsets::Primitive::AssemblyLinearVelocity, { 0.f, 0.f, 0.f });
    memory->write<math::vector3>(prim_addr + Offsets::Primitive::AssemblyAngularVelocity, { 0.f, 0.f, 0.f });
    return true;
}

bool teleport_to_waypoint(size_t index)
{
    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    if (index >= g_waypoints.size()) return false;
    return teleport_to_pos(g_waypoints[index].pos);
}

std::vector<std::string> get_profile_list()
{
    ensure_waypoints_directory();
    std::vector<std::string> list;
    std::string dir = get_waypoints_directory();
    try
    {
        if (std::filesystem::exists(dir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".json")
                {
                    list.push_back(entry.path().stem().string());
                }
            }
        }
    }
    catch (...) {}
    std::sort(list.begin(), list.end());
    return list;
}

bool save_profile(const std::string& profile_name)
{
    if (profile_name.empty()) return false;
    ensure_waypoints_directory();
    std::string path = get_waypoints_directory() + "\\" + profile_name + ".json";

    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    try
    {
        nlohmann::json root = nlohmann::json::object();
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& wp : g_waypoints)
        {
            nlohmann::json j_wp;
            j_wp["name"] = wp.name;
            j_wp["pos"] = { wp.pos.x, wp.pos.y, wp.pos.z };
            j_wp["game_id"] = wp.game_id;
            j_wp["place_id"] = wp.place_id;
            j_wp["timestamp"] = wp.timestamp;
            j_wp["color"] = { wp.color[0], wp.color[1], wp.color[2], wp.color[3] };
            j_wp["render_esp"] = wp.render_esp;
            j_wp["show_tracer"] = wp.show_tracer;
            j_wp["show_distance"] = wp.show_distance;
            j_wp["show_box"] = wp.show_box;
            arr.push_back(j_wp);
        }

        root["waypoints"] = arr;
        root["created_at"] = get_current_timestamp_str();

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << root.dump(4);
        f.close();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool load_profile(const std::string& profile_name)
{
    if (profile_name.empty()) return false;
    ensure_waypoints_directory();
    std::string path = get_waypoints_directory() + "\\" + profile_name + ".json";
    if (!std::filesystem::exists(path)) return false;

    std::lock_guard<std::mutex> lock(g_waypoints_mutex);
    try
    {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        nlohmann::json root;
        f >> root;
        f.close();

        if (!root.contains("waypoints") || !root["waypoints"].is_array()) return false;

        g_waypoints.clear();
        for (const auto& j_wp : root["waypoints"])
        {
            Waypoint wp;
            wp.name = j_wp.value("name", "Waypoint");
            if (j_wp.contains("pos") && j_wp["pos"].is_array() && j_wp["pos"].size() >= 3)
            {
                wp.pos.x = j_wp["pos"][0].get<float>();
                wp.pos.y = j_wp["pos"][1].get<float>();
                wp.pos.z = j_wp["pos"][2].get<float>();
            }
            wp.game_id = j_wp.value("game_id", 0ull);
            wp.place_id = j_wp.value("place_id", 0ull);
            wp.timestamp = j_wp.value("timestamp", "");
            if (j_wp.contains("color") && j_wp["color"].is_array() && j_wp["color"].size() >= 4)
            {
                wp.color[0] = j_wp["color"][0].get<float>();
                wp.color[1] = j_wp["color"][1].get<float>();
                wp.color[2] = j_wp["color"][2].get<float>();
                wp.color[3] = j_wp["color"][3].get<float>();
            }
            wp.render_esp = j_wp.value("render_esp", true);
            wp.show_tracer = j_wp.value("show_tracer", false);
            wp.show_distance = j_wp.value("show_distance", true);
            wp.show_box = j_wp.value("show_box", true);
            g_waypoints.push_back(wp);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool delete_profile(const std::string& profile_name)
{
    if (profile_name.empty()) return false;
    ensure_waypoints_directory();
    std::string path = get_waypoints_directory() + "\\" + profile_name + ".json";
    try
    {
        if (std::filesystem::exists(path))
            return std::filesystem::remove(path);
        return false;
    }
    catch (...)
    {
        return false;
    }
}

static uint64_t get_current_place_id()
{
    if (game::datamodel && game::datamodel->address >= 0x10000)
    {
        return memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::PlaceId);
    }
    return 0;
}

static uint64_t get_current_game_id()
{
    if (game::datamodel && game::datamodel->address >= 0x10000)
    {
        return memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::GameId);
    }
    return 0;
}

bool save_game_waypoints()
{
    uint64_t pid = get_current_place_id();
    if (pid == 0) pid = get_current_game_id();
    if (pid == 0) return false;
    return save_profile("place_" + std::to_string(pid));
}

bool load_game_waypoints()
{
    uint64_t pid = get_current_place_id();
    if (pid == 0) pid = get_current_game_id();
    if (pid == 0) return false;
    return load_profile("place_" + std::to_string(pid));
}

void render_esp(ImDrawList* draw_list, const math::matrix4& view, const math::vector2& dims, const ImVec2& screen_offset)
{
    if (!g_config.enabled || !g_config.render_in_esp || !draw_list) return;

    uint64_t current_place = get_current_place_id();
    uint64_t current_game = get_current_game_id();

    math::vector3 cam_pos{ 0.f, 0.f, 0.f };
    if (game::camera != 0)
    {
        rbx::c_primitive cam_prim(game::camera);
        cam_pos = cam_prim.get_position();
    }

    std::vector<Waypoint> list_copy;
    {
        std::lock_guard<std::mutex> lock(g_waypoints_mutex);
        list_copy = g_waypoints;
    }

    float tracer_start_x = screen_offset.x + dims.x * 0.5f;
    float tracer_start_y = screen_offset.y + dims.y;

    for (const auto& wp : list_copy)
    {
        if (!wp.render_esp) continue;

        if (!g_config.show_all_games && current_place != 0 && wp.place_id != 0)
        {
            if (wp.place_id != current_place && wp.game_id != current_game)
                continue;
        }

        math::vector2 spos;
        if (!game::visualengine->world_to_screen(view, dims, wp.pos, spos))
            continue;

        float sx = spos.x + screen_offset.x;
        float sy = spos.y + screen_offset.y;

        float dist = (wp.pos - cam_pos).length();

        ImU32 col = IM_COL32((int)(wp.color[0] * 255.f), (int)(wp.color[1] * 255.f), (int)(wp.color[2] * 255.f), (int)(wp.color[3] * 255.f));
        ImU32 outline_col = IM_COL32(0, 0, 0, 220);

        // Draw 3D wireframe box around waypoint
        if (wp.show_box && g_config.draw_boxes)
        {
            float half_sz = g_config.box_size * 0.5f;
            math::vector3 corners[8] = {
                { wp.pos.x - half_sz, wp.pos.y - half_sz, wp.pos.z - half_sz },
                { wp.pos.x + half_sz, wp.pos.y - half_sz, wp.pos.z - half_sz },
                { wp.pos.x + half_sz, wp.pos.y + half_sz, wp.pos.z - half_sz },
                { wp.pos.x - half_sz, wp.pos.y + half_sz, wp.pos.z - half_sz },
                { wp.pos.x - half_sz, wp.pos.y - half_sz, wp.pos.z + half_sz },
                { wp.pos.x + half_sz, wp.pos.y - half_sz, wp.pos.z + half_sz },
                { wp.pos.x + half_sz, wp.pos.y + half_sz, wp.pos.z + half_sz },
                { wp.pos.x - half_sz, wp.pos.y + half_sz, wp.pos.z + half_sz }
            };

            ImVec2 scr_corners[8];
            bool all_vis = true;
            for (int i = 0; i < 8; i++)
            {
                math::vector2 c_scr;
                if (game::visualengine->world_to_screen(view, dims, corners[i], c_scr))
                {
                    scr_corners[i] = ImVec2(c_scr.x + screen_offset.x, c_scr.y + screen_offset.y);
                }
                else
                {
                    all_vis = false;
                    break;
                }
            }

            if (all_vis)
            {
                int edges[12][2] = {
                    {0,1}, {1,2}, {2,3}, {3,0},
                    {4,5}, {5,6}, {6,7}, {7,4},
                    {0,4}, {1,5}, {2,6}, {3,7}
                };
                for (int e = 0; e < 12; e++)
                {
                    draw_list->AddLine(scr_corners[edges[e][0]], scr_corners[edges[e][1]], col, 1.5f);
                }
            }
        }
        else
        {
            // Draw diamond marker
            float r = 5.0f;
            draw_list->AddQuadFilled(
                ImVec2(sx, sy - r),
                ImVec2(sx + r, sy),
                ImVec2(sx, sy + r),
                ImVec2(sx - r, sy),
                col
            );
            draw_list->AddQuad(
                ImVec2(sx, sy - r),
                ImVec2(sx + r, sy),
                ImVec2(sx, sy + r),
                ImVec2(sx - r, sy),
                outline_col,
                1.0f
            );
        }

        // Draw Tracer line
        if (wp.show_tracer || g_config.draw_tracers)
        {
            draw_list->AddLine(ImVec2(tracer_start_x, tracer_start_y), ImVec2(sx, sy), col, 1.0f);
        }

        // Text label
        std::string label = wp.name;
        if (wp.show_distance && g_config.draw_distance)
        {
            label += " [" + std::to_string((int)dist) + "m]";
        }

        ImVec2 text_sz = ImGui::CalcTextSize(label.c_str());
        ImVec2 text_pos = ImVec2(sx - text_sz.x * 0.5f, sy + 6.0f);

        // Shadowed text
        draw_list->AddText(ImVec2(text_pos.x + 1.f, text_pos.y + 1.f), outline_col, label.c_str());
        draw_list->AddText(text_pos, col, label.c_str());
    }
}

}
