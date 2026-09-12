#include "notifications.h"
#include <external/imgui/addons/imgui_notification.h>

namespace notifications
{
    void add(const std::string& text, float duration)
    {
        ImNotify::Print(NotifyLevel::Info, "%s", text.c_str());
    }

    void add_welcome()
    {
    }

    void add_hit(const std::string& player_name, float damage)
    {
    }

    void add_kill(const std::string& player_name, const std::string& weapon)
    {
    }

    void render()
    {
    }
}
