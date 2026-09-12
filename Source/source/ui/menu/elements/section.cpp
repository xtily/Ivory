#include "../settings/functions.h"

bool c_gui::section(std::string_view icon, bool* callback)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID((std::stringstream{} << icon << "section").str().c_str());

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + elements->section.size);
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);

    if (pressed)
        *callback = !*callback;

    draw->fade_rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->window.background_one), draw->get_clr(clr->window.background_two), fade_direction::vertically);
    draw->rect(window->DrawList, rect.Min, rect.Max, var->window.hover_hightlight && hovered ? draw->get_clr(clr->accent) : draw->get_clr(clr->window.stroke));
    if (icon == "SVR")
    {
        ImVec2 center = ImVec2((rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
        ImU32 col = draw->get_clr(clr->accent, *callback ? 1.f : 0.5f);
        
        float w = 14.f, h = 4.f, step = 5.5f;
        for (int row = -1; row <= 1; row++)
        {
            float y = center.y + row * step;
            window->DrawList->AddRect(ImVec2(center.x - w * 0.5f, y - h * 0.5f), ImVec2(center.x + w * 0.5f, y + h * 0.5f), col, 0.0f, 0, 1.0f);
            window->DrawList->AddLine(ImVec2(center.x - w * 0.5f + 3.0f, y), ImVec2(center.x - w * 0.5f + 7.0f, y), col, 1.0f);
            window->DrawList->AddCircleFilled(ImVec2(center.x + w * 0.5f - 2.5f, y), 0.8f, col);
        }
    }
    else if (icon == "NPC")
    {
        ImVec2 center = ImVec2((rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
        ImU32 col = draw->get_clr(clr->accent, *callback ? 1.f : 0.5f);
        
        // Clean, simple, minimalist NPC/User silhouette icon
        ImVec2 head_pos = center - ImVec2(0.0f, 3.5f);
        window->DrawList->AddCircle(head_pos, 3.2f, col, 16, 1.3f);
        
        // Shoulders / torso arch
        window->DrawList->PathClear();
        window->DrawList->PathArcTo(center + ImVec2(0.0f, 6.0f), 5.5f, IM_PI + 0.35f, IM_PI * 2.0f - 0.35f, 24);
        window->DrawList->PathStroke(col, 0, 1.3f);
    }
    else
    {
        ImFont* font_to_use = (icon.length() > 1 || icon == "{/}") ? var->font.tahoma : var->font.icons[0];
        draw->text_clipped(window->DrawList, font_to_use, rect.Min, rect.Max, draw->get_clr(clr->accent, *callback ? 1.f : 0.5f), icon.data(), NULL, NULL, ImVec2(0.5f, 0.5f));
    }

    gui->sameline();

    return pressed;
}

bool c_gui::sub_section(std::string_view label, int section_id, int& section_variable, float count)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());
    const bool selected = section_id == section_variable;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + ImVec2((GetWindowWidth() - elements->content.window_padding.x * 2 - g.Style.ItemSpacing.x * (count - 1)) / count, elements->section.height));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);

    if (pressed)
        section_variable = section_id;

    if (selected || hovered)
        draw->fade_rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->window.background_one), draw->get_clr(clr->window.background_two), fade_direction::vertically);
    else
        draw->fade_rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);

    draw->rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->window.stroke));

    if (selected)
        draw->line(GetForegroundDrawList(), ImVec2(rect.Min.x + 1, rect.Max.y - 1), rect.Max - ImVec2(1, 1), draw->get_clr(clr->window.background_two));

    draw->text_clipped_outline(window->DrawList, var->font.tahoma, rect.Min, rect.Max, selected ? draw->get_clr(clr->accent) : draw->get_clr(clr->widgets.text_inactive), label.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    gui->sameline();

    return pressed;
}