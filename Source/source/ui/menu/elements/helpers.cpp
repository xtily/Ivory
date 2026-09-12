#include "../settings/functions.h"

void c_gui::push_style_color(ImGuiCol idx, ImU32 col)
{
    PushStyleColor(idx, col);
}

void c_gui::pop_style_color(int count)
{
    PopStyleColor(count);
}

void c_gui::push_style_var(ImGuiStyleVar idx, float val)
{
    PushStyleVar(idx, val);
}

void c_gui::push_style_var(ImGuiStyleVar idx, const ImVec2& val)
{
    PushStyleVar(idx, val);
}

void c_gui::pop_style_var(int count)
{
    PopStyleVar(count);
}

void c_gui::push_font(ImFont* font)
{
    ImGui::PushFont(font);
}

void c_gui::pop_font()
{
    ImGui::PopFont();
}

void c_gui::set_cursor_pos(const ImVec2& local_pos)
{
    SetCursorPos(local_pos);
}

void c_gui::begin_group()
{
    BeginGroup();
}

void c_gui::end_group()
{
    EndGroup();
}

void c_gui::begin_content()
{
    gui->push_style_var(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    gui->begin_def_child("content area", ImVec2(GetWindowWidth() - elements->content.window_padding.x * 2, GetContentRegionAvail().y - elements->content.window_padding.y * 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, elements->content.spacing);

    draw->rect_filled(GetWindowDrawList(), GetWindowPos(), GetWindowPos() + GetWindowSize(), draw->get_clr(clr->window.background_two));
    draw->rect(GetWindowDrawList(), GetWindowPos(), GetWindowPos() + GetWindowSize(), draw->get_clr(clr->window.stroke));

    gui->set_cursor_pos(elements->content.padding);
    gui->begin_group();
}

void c_gui::end_content()
{
    gui->end_group();
    gui->pop_style_var();
    gui->end_def_child();
    gui->pop_style_var();
}

void c_gui::sameline()
{
    SameLine(0.f, -1.f);
}

bool c_gui::begin_def_child(std::string_view name, const ImVec2& size_arg, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    return ImGui::BeginChild(name.data(), size_arg, child_flags, window_flags);
}

void c_gui::end_def_child()
{
    ImGui::EndChild();
}

void c_gui::set_next_window_pos(const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot)
{
    SetNextWindowPos(pos, cond, pivot);
}

void c_gui::set_next_window_size(const ImVec2& size, ImGuiCond cond)
{
    SetNextWindowSize(size, cond);
}

void c_gui::set_next_window_size_constraints(const ImVec2& size_min, const ImVec2& size_max, ImGuiSizeCallback custom_callback, void* custom_callback_data)
{
    SetNextWindowSizeConstraints(size_min, size_max, custom_callback, custom_callback_data);
}

bool c_gui::selectable_ex(const char* label, bool active, const ImVec2& size)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + size);
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered = IsItemHovered();
    bool pressed = hovered && g.IO.MouseClicked[0];
    if (pressed)
        MarkItemEdited(id);

    draw->text_clipped_outline(window->DrawList, var->font.tahoma, rect.Min - ImVec2(0, 1), rect.Max - ImVec2(0, 1), draw->get_clr(active && !hovered ? clr->accent : hovered ? clr->widgets.text_inactive : clr->widgets.text), label, NULL, NULL, ImVec2(0.5f, 0.5f));

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

bool c_gui::selectable(const char* label, bool* p_selected, const ImVec2& size)
{
    if (gui->selectable_ex(label, *p_selected, size))
    {
        *p_selected = !*p_selected;
        return true;
    }
    return false;
}