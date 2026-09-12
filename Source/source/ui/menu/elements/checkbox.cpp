#include "../settings/functions.h"

bool c_gui::checkbox(std::string_view label, bool* callback)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImVec2 pos = window->DC.CursorPos;
    const float width = GetContentRegionAvail().x;
    const ImRect rect(pos, pos + ImVec2(width, elements->widgets.checkbox_size.y));
    const ImRect clickable(rect.Min, rect.Min + elements->widgets.checkbox_size);
    const ImRect text(clickable.Min, clickable.Max + ImVec2(elements->widgets.padding.x + var->font.tahoma->CalcTextSizeA(var->font.tahoma->FontSize, FLT_MAX, -1.f, label.data()).x, 0));

    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(text, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    draw->fade_rect_filled(window->DrawList, clickable.Min + ImVec2(2, 2), clickable.Max - ImVec2(2, 2), draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);
    if (*callback)
        draw->fade_rect_filled(window->DrawList, clickable.Min + ImVec2(2, 2), clickable.Max - ImVec2(2, 2), draw->get_clr(clr->accent), draw->get_clr({ clr->accent.Value.x - 0.2f, clr->accent.Value.y - 0.2f, clr->accent.Value.z - 0.2f, 1.f}), fade_direction::vertically);
    draw->rect(window->DrawList, clickable.Min, clickable.Max, draw->get_clr(var->window.hover_hightlight && hovered ? clr->accent : clr->widgets.stroke_two));
    draw->rect(window->DrawList, clickable.Min + ImVec2(1, 1), clickable.Max - ImVec2(1, 1), draw->get_clr(clr->window.stroke));

    draw->text_clipped_outline(window->DrawList, var->font.tahoma, ImVec2(clickable.Max.x + elements->widgets.padding.x, rect.Min.y), rect.Max, draw->get_clr(clr->widgets.text), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    return pressed;
}

bool c_gui::checkbox(std::string_view label, bool* callback, int* key, int* mode)
{
    float y_pos = GetCursorPosY();
    bool pressed = gui->checkbox(label, callback);

    ImVec2 stored_pos = GetCursorPos();

    set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.key_size.x - GetStyle().WindowPadding.x, y_pos));
    gui->keybind((std::stringstream{} << label << "key").str().c_str(), key, mode);

    set_cursor_pos(stored_pos);
    return pressed;
}

bool c_gui::checkbox(std::string_view label, bool* callback, float col[4], bool active_alpha)
{
    float y_pos = GetCursorPosY();
    bool pressed = gui->checkbox(label, callback);

    ImVec2 stored_pos = GetCursorPos();

    set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.color_size.x - GetStyle().WindowPadding.x, y_pos));
    std::string id_str = std::string(label) + "col";
    gui->color_edit(id_str, col, active_alpha);

    set_cursor_pos(stored_pos);
    return pressed;
}

bool c_gui::checkbox(std::string_view label, bool* callback, float col1[4], float col2[4], bool active_alpha)
{
    float y_pos = GetCursorPosY();
    bool pressed = gui->checkbox(label, callback);

    ImVec2 stored_pos = GetCursorPos();

    float spacing = 4.0f;
    float w = elements->widgets.color_size.x;
    float total_w = w * 2.f + spacing;
    float start_x = GetWindowWidth() - total_w - GetStyle().WindowPadding.x;

    std::string id1 = std::string(label) + "col1";
    std::string id2 = std::string(label) + "col2";

    set_cursor_pos(ImVec2(start_x, y_pos));
    gui->color_edit(id1, col1, active_alpha);

    set_cursor_pos(ImVec2(start_x + w + spacing, y_pos));
    gui->color_edit(id2, col2, active_alpha);

    set_cursor_pos(stored_pos);
    return pressed;
}

bool c_gui::checkbox(std::string_view label, bool* callback, float col1[4], float col2[4], float col3[4], bool active_alpha)
{
    float y_pos = GetCursorPosY();
    bool pressed = gui->checkbox(label, callback);

    ImVec2 stored_pos = GetCursorPos();

    float spacing = 4.0f;
    float w = elements->widgets.color_size.x;
    float total_w = w * 3.f + spacing * 2.f;
    float start_x = GetWindowWidth() - total_w - GetStyle().WindowPadding.x;

    std::string id1 = std::string(label) + "col_high";
    std::string id2 = std::string(label) + "col_mid";
    std::string id3 = std::string(label) + "col_low";

    set_cursor_pos(ImVec2(start_x, y_pos));
    gui->color_edit(id1, col1, active_alpha);

    set_cursor_pos(ImVec2(start_x + w + spacing, y_pos));
    gui->color_edit(id2, col2, active_alpha);

    set_cursor_pos(ImVec2(start_x + (w + spacing) * 2.f, y_pos));
    gui->color_edit(id3, col3, active_alpha);

    set_cursor_pos(stored_pos);
    return pressed;
}

bool c_gui::checkbox(std::string_view label, bool* callback, int* key, int* mode, float col[4], bool active_alpha)
{
    float y_pos = GetCursorPosY();
    bool pressed = gui->checkbox(label, callback);

    ImVec2 stored_pos = GetCursorPos();

    std::string key_id = std::string(label) + "key";
    std::string col_id = std::string(label) + "col";

    set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.key_size.x - elements->widgets.color_size.x - GetStyle().ItemSpacing.x - GetStyle().WindowPadding.x, y_pos));
    gui->keybind(key_id.c_str(), key, mode);

    set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.color_size.x - GetStyle().WindowPadding.x, y_pos));
    gui->color_edit(col_id, col, active_alpha);

    set_cursor_pos(stored_pos);
    return pressed;
}