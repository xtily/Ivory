#pragma once

#include <imgui/imgui.h>
#include <string>
#include <vector>

namespace editor
{
    struct lua_editor
    {
        std::vector<std::string> lines;
        int cursor_row = 0;
        int cursor_col = 0;
        int sel_row = 0;
        int sel_col = 0;
        float scroll_y = 0.f;
        float scroll_x = 0.f;
        bool focused = false;
        bool readonly = false;
        bool show_scrollbar = true;
        bool prefer_ac = false;
        int ac_index = 0;
        std::vector<std::string> ac_items;
        std::string ac_prefix;
        int ac_start_col = 0;

        lua_editor();
        void set_text(const std::string& text);
        std::string get_text() const;
        void render(const char* id, ImVec2 size);
    };

    void register_active_editor(lua_editor* ed);
    void set_active_editor_text(const std::string& text);
    std::string get_active_editor_text();
}