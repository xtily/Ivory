#include <ui/menu/settings/functions.h>

bool c_gui::text_field(std::string_view label, char* buf, size_t buf_size, ImGuiInputTextFlags flags)
{
    return ImGui::InputText(label.data(), buf, buf_size, flags);
}