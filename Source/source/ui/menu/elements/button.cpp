#include "../settings/functions.h"

bool c_gui::button(std::string_view label, int val)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());
    ImVec2 pos = window->DC.CursorPos;
    float full_w = GetWindowWidth() - elements->content.padding.x * 2.0f;
    float btn_w = (val > 1) ? ((full_w - style.ItemSpacing.x * (val - 1)) / (float)val) : full_w;
    const ImRect rect(pos, pos + ImVec2(btn_w, elements->widgets.dropdown_height));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held, 0);

    draw->fade_rect_filled(window->DrawList, rect.Min + ImVec2(2, 2), rect.Max - ImVec2(2, 2), draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);
    draw->rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(var->window.hover_hightlight && hovered ? clr->accent : clr->widgets.stroke_two));
    draw->rect(window->DrawList, rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), draw->get_clr(clr->window.stroke));
    const char* text_end = ImGui::FindRenderedTextEnd(label.data());
    draw->text_clipped_outline(window->DrawList, var->font.tahoma, rect.Min, rect.Max, draw->get_clr(hovered ? clr->widgets.text : clr->widgets.text_inactive), label.data(), text_end, NULL, ImVec2(0.5f, 0.5f));

    return pressed;
}