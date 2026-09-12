#include <ui/menu/settings/functions.h>

bool c_gui::begin(std::string_view name, bool* p_open, ImGuiWindowFlags flags)
{
    return ImGui::Begin(name.data(), p_open, flags);
}

void c_gui::end()
{
    ImGui::End();
}
