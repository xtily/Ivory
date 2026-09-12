#include "../settings/functions.h"
#include <unordered_map>
#include <vector>
#include <string>

struct subtab {
    std::string name;
    bool open = false;
};

static std::unordered_map<ImGuiID, int> child_subtab_map;
static std::unordered_map<ImGuiID, std::vector<subtab>> subtab_list_map;

int c_gui::get_child_subtab(ImGuiID id) {
    return child_subtab_map[id];
}

void set_child_subtab(ImGuiID id, int v) {
    child_subtab_map[id] = v;
}

void set_child_subtabs(ImGuiID id, const std::vector<std::string>& names) {
    auto& tabs = subtab_list_map[id];
    if (tabs.size() != names.size()) {
        tabs.clear();
        for (auto& n : names)
            tabs.push_back({ n, false });
        if (!tabs.empty())
            tabs[0].open = true;
    }
}

void draw_child_subtabs(ImGuiWindow* parent_window, ImGuiID id, const ImVec2& size_arg) {
    auto& tabs = subtab_list_map[id];
    if (tabs.empty()) return;

    int active = c_gui::get_child_subtab(id);
    if (active < 0 || active >= static_cast<int>(tabs.size()))
        active = 0;

    int count = static_cast<int>(tabs.size());
    float tab_h = var->window.titlebar + 4.f;

    float tab_w = size_arg.x / count;
    ImVec2 base = parent_window->DC.CursorPos;

    for (int i = 0; i < count; ++i) {
        ImVec2 p_min = base + ImVec2(tab_w * i, 0.f);
        ImVec2 p_max = p_min + ImVec2(tab_w, tab_h);

        if (IsMouseHoveringRect(p_min, p_max, false) && (IsMouseClicked(0) || IsMouseReleased(0))) {
            active = i;
        }

        bool selected = (i == active);
        bool hovered = IsMouseHoveringRect(p_min, p_max, false);

        if (selected || hovered)
            draw->fade_rect_filled(parent_window->DrawList, p_min, p_max, draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);
        else
            draw->fade_rect_filled(parent_window->DrawList, p_min, p_max, draw->get_clr(clr->window.background_one), draw->get_clr(clr->window.background_two), fade_direction::vertically);

        draw->rect(parent_window->DrawList, p_min, p_max, draw->get_clr(clr->window.stroke), 0.f);

        if (selected)
             draw->line(parent_window->DrawList, ImVec2(p_min.x + 1, p_max.y - 1), p_max - ImVec2(1, 1), draw->get_clr(clr->window.background_one));

        draw->text_clipped_outline(parent_window->DrawList, var->font.tahoma, p_min, p_max, selected ? draw->get_clr(clr->accent) : draw->get_clr(clr->widgets.text_inactive), tabs[i].name.c_str(), NULL, NULL, ImVec2(0.5f, 0.5f));
        tabs[i].open = selected;
    }

    draw->line(parent_window->DrawList, base + ImVec2(2, 2), base + ImVec2(size_arg.x - 2, 2), draw->get_clr(clr->accent));

    set_child_subtab(id, active);
}

void c_gui::begin_multi_subtab(std::string_view name, int x, int y, int tab_count, const ImVec2& size, const std::vector<std::string>& tab_names) {
    ImGuiID id = GetCurrentWindow()->GetID(name.data());
    set_child_subtabs(id, tab_names);

    gui->push_style_var(ImGuiStyleVar_WindowPadding, elements->widgets.padding);
    ImGui::BeginChild(name.data(), size, ImGuiChildFlags_Border, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove);
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, elements->widgets.spacing);
}

bool c_gui::is_subtab_open(std::string_view name, int tab_index) {
    ImGuiID id = GetCurrentWindow()->GetID(name.data());
    auto& tabs = subtab_list_map[id];
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs.size())) return false;
    return tabs[tab_index].open;
}
void c_gui::begin_child(std::string_view name, int x, int y, const ImVec2& size)
{
    gui->push_style_var(ImGuiStyleVar_WindowPadding, elements->widgets.padding);
    ImGui::BeginChild(name.data(), size, ImGuiChildFlags_Border, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove);
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, elements->widgets.spacing);
}

void c_gui::end_child()
{
    gui->pop_style_var(2);
    ImGui::EndChild();
}