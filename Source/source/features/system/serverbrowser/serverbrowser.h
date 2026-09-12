#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace serverbrowser
{
    struct server_entry_t
    {
        std::string id;
        int max_players = 0;
        int playing = 0;
        int ping = 0;
        float fps = 0.0f;
    };

    enum sort_mode_t
    {
        SORT_LOWEST_PLAYERS = 0,
        SORT_HIGHEST_PLAYERS,
        SORT_LOWEST_PING,
        SORT_HIGHEST_FPS
    };

    class c_server_browser
    {
    public:
        char input_place_id[128] = "";
        char search_filter[128] = "";
        int sort_mode = SORT_LOWEST_PLAYERS;
        bool exclude_full = false;
        bool is_loading = false;
        std::string status_message = "Ready";

        uint64_t active_place_id = 0;
        std::string active_job_id = "";

        std::vector<server_entry_t> all_servers;
        std::vector<server_entry_t> filtered_servers;

        std::string next_cursor;
        std::string prev_cursor;
        std::vector<std::string> cursor_history;
        int current_page = 1;

        std::mutex mtx;

        c_server_browser();
        void refresh_current_game();
        uint64_t get_target_place_id();
        void fetch_servers_async(uint64_t place_id, const std::string& cursor = "");
        void apply_filter();
        void join_server(uint64_t place_id, const std::string& job_id);
        void copy_job_id(const std::string& job_id);
        void copy_teleport_script(uint64_t place_id, const std::string& job_id);
        void next_page();
        void prev_page();
    };

    inline c_server_browser* browser = new c_server_browser();
}