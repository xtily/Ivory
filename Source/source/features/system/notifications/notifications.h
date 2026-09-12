#pragma once
#include <string>

namespace notifications
{
    void add(const std::string& text, float duration = -1.0f);
    void add_welcome();
    void add_hit(const std::string& player_name, float damage);
    void add_kill(const std::string& player_name, const std::string& weapon = "");
    void render();
}
