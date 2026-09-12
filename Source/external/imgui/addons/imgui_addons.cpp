    //============ Copyright KiwiHax, All rights reserved ============//
    //
    //  Purpose: 
    //
    //================================================================//
    #define _CRT_SECURE_NO_WARNINGS
    #include <windows.h>
    #include "../imgui_internal.h"
    #include "imgui_addons.h"

    #include <map>
    #include <unordered_map>
    #include <string>
    #include <algorithm>

    float g_AnimationSpeed = 0.07f;
    float g_TabHeight      = 0.0f;
    bool  g_TabFillSpace   = true;

    using namespace ImGui;

    ImVec4 ImAdd::HexToColorVec4(unsigned int hex_color, float alpha)
    {
        ImVec4 color;

        color.x = ((hex_color >> 16) & 0xFF) / 255.0f;
        color.y = ((hex_color >> 8) & 0xFF) / 255.0f;
        color.z = (hex_color & 0xFF) / 255.0f;
        color.w = alpha;

        return color;
    }

    float ImAdd::GetColorPickerWidth()
    {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        return g.FontSize * 2.0f + style.CellPadding.y * 4.0f;
    }

    static std::string GetVKName(int key)
    {
        if (key <= 0 || key >= 256) return "-";
        char kname[256];
        switch (key)
        {
        case VK_LBUTTON: return "LMB";
        case VK_RBUTTON: return "RMB";
        case VK_MBUTTON: return "MMB";
        case VK_XBUTTON1: return "X1";
        case VK_XBUTTON2: return "X2";
        case VK_SHIFT:   return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU:    return "Alt";
        case VK_SPACE:   return "Space";
        case VK_TAB:     return "Tab";
        case VK_RETURN:  return "Enter";
        case VK_CAPITAL: return "Caps";
        case VK_ESCAPE:  return "Esc";
        default:
            if (key >= 'A' && key <= 'Z') return std::string(1, (char)key);
            if (key >= '0' && key <= '9') return std::string(1, (char)key);
            if (key >= VK_F1 && key <= VK_F12) return "F" + std::to_string(key - VK_F1 + 1);
            if (GetKeyNameTextA(MapVirtualKeyA(key, MAPVK_VK_TO_VSC) << 16, kname, sizeof(kname))) return kname;
            return std::to_string(key);
        }
    }

    float ImAdd::CalcKeyBindWidth(int key)
    {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        std::string display = (key > 0 && key < 256)
            ? "[" + GetVKName(key) + "]"
            : "Unbinded";

        float text_w = CalcTextSize(display.c_str()).x + style.FramePadding.x * 2.0f;
        return (std::max)(28.0f, text_w);
    }

    float ImAdd::CalcKeyBindWidth(ImGuiKey key)
    {
        return CalcKeyBindWidth((int)key);
    }

    void ImAdd::SeparatorText(const char* label, float thickness)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;
        
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(ImVec2(-0.1f, g.FontSize), label_size.x, g.FontSize);

        const ImRect total_bb(pos, pos + size);
        ItemSize(total_bb);
        if (!ItemAdd(total_bb, id)) {
            return;
        }

        window->DrawList->AddText(pos, GetColorU32(ImGuiCol_TextDisabled), label);

        if (thickness > 0)
            window->DrawList->AddLine(pos + ImVec2(label_size.x + style.ItemInnerSpacing.x, size.y / 2), pos + ImVec2(size.x, size.y / 2), GetColorU32(ImGuiCol_Separator), thickness);
    }

    void ImAdd::VSeparator(float margin, float thickness)
    {
        if (thickness <= 0)
            return;

        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(ImVec2(thickness, -0.1f), thickness, thickness);

        const ImRect bb(pos, pos + size);
        const ImRect bb_rect(pos + ImVec2(0, margin), pos + size - ImVec2(0, margin));

        ItemSize(ImVec2(thickness, 0.0f));
        if (!ItemAdd(bb, 0))
            return;

        window->DrawList->AddRectFilled(bb_rect.Min, bb_rect.Max, GetColorU32(ImGuiCol_Separator));
    }

    bool ImAdd::SelectableLabel(const char* label, bool selected, bool centered, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, label_size.x, label_size.y);

        const ImRect total_bb(pos, pos + size);
        ItemSize(size);
        if (!ItemAdd(total_bb, id))
            return false;

        // Behaviors
        bool hovered, held;
        bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

        // Colors
        ImVec4 colLabel = GetStyleColorVec4(hovered || selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

        ImVec4 colLineMain = GetStyleColorVec4(ImGuiCol_SliderGrab);
        ImVec4 colLineNull = colLineMain;
        colLineNull.w = 0.0f;

        ImVec4 colLine = selected ? colLineMain : colLineNull;

        // Animations
        struct stColors_State {
            ImColor Label;
            ImColor Line;
        };

        static std::map<ImGuiID, stColors_State> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, stColors_State() });
            it_anim = anim.find(id);

            it_anim->second.Label = colLabel;
            it_anim->second.Line = colLine;
        }

        it_anim->second.Label.Value = ImLerp(it_anim->second.Label.Value, colLabel, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);
        it_anim->second.Line.Value = ImLerp(it_anim->second.Line.Value, colLine, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);

        RenderNavCursor(total_bb, id);

        window->DrawList->AddText(pos + ImTrunc(ImVec2(centered ? (size.x / 2 - label_size.x / 2) : 0.0f, size.y / 2 - label_size.y / 2)), it_anim->second.Label, label);

        return pressed;
    }

    bool ImAdd::CheckBox(const char* label, bool* v)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        const float square_sz = GetFontSize() + style.CellPadding.y * 2.0f;

        const ImVec2 pos = window->DC.CursorPos;
        const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + style.CellPadding.y * 2.0f));

        ItemSize(total_bb, style.CellPadding.y);
        if (!ItemAdd(total_bb, id))
        {
            return false;
        }

        bool hovered, held, checked = false;
        bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

        if (v)
        {
            checked = *v;
            if (pressed) *v = !*v;
        }

        const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));

        // Colors
        ImU32 colFrame = GetColorU32(checked ? ImGuiCol_Header : (held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

        RenderNavHighlight(total_bb, id);
        RenderFrame(check_bb.Min, check_bb.Max, colFrame, false);
        window->DrawList->AddRectFilledMultiColor(check_bb.Min, check_bb.Max, 
            GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow, 0.0f),
            GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow), GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow));
        RenderFrameBorder(check_bb.Min, check_bb.Max);

        if (label_size.x > 0.0f)
        {
            const ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.CellPadding.y);

            //PushStyleColor(ImGuiCol_Text, style.Colors[checked ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
            RenderText(label_pos, label, NULL, true, true);
            //PopStyleColor();
        }

        return pressed;
    }

    bool ImAdd::Button(const char* label, const ImVec2& size_arg, ImDrawFlags draw_flags)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

        const ImRect total_bb(pos, pos + size);
        ItemSize(size);
        if (!ItemAdd(total_bb, id))
            return false;

        // Behaviors
        bool hovered, held;
        bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

        // Colors
        ImU32 colFrame = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

        RenderNavCursor(total_bb, id);
        RenderFrame(total_bb.Min, total_bb.Max, colFrame);
        window->DrawList->AddRectFilledMultiColor(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow), GetColorU32(ImGuiCol_ButtonShadow));
        RenderText(pos + ImTrunc((size - label_size) / 2) + ImVec2(1.0f, 0.0f), label, NULL, true, true);

        return pressed;
    }

    bool ImAdd::ButtonAccent(const char* label, const ImVec2& size_arg, ImDrawFlags draw_flags)
    {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_Header]);
        PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_HeaderHovered]);
        PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_HeaderActive]);
        PushStyleColor(ImGuiCol_ButtonShadow, style.Colors[ImGuiCol_FrameBgShadow]);

        bool result = Button(label, size_arg, draw_flags);

        PopStyleColor(4);

        return result;
    }

    bool ImAdd::ComboMulti(const char* label, std::vector<std::pair<bool, const char*>>* _items)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;

        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, nullptr, true);

        const float width = CalcItemWidth();
        const float height = GetFrameHeight();
        const float label_offset_y = (label_size.x > 0.0f)
            ? (g.FontSize + style.ItemInnerSpacing.y)
            : 0.0f;

        auto& items = *_items;
        const int items_count = static_cast<int>(items.size());

        std::string preview_item;
        //preview_item.reserve(64);

        for (int i = 0; i < items_count; ++i)
        {
            if (items[i].first)
                preview_item += "," + std::string(items[i].second); 
        }

        preview_item = preview_item.empty()
            ? "*nothing is selected*"
            : preview_item.substr(1); 

        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 size = ImVec2(width, height + label_offset_y);

        const ImRect total_bb(pos, pos + size);
        const ImRect frame_bb(
            ImVec2(pos.x, pos.y + label_offset_y),
            pos + ImVec2(size.x, size.y)
        );

        ItemSize(size);
        if (!ItemAdd(total_bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);

        const std::string popup_id = std::string(label) + "::combo_popup";
        if (pressed)
            OpenPopup(popup_id.c_str());

        PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);

        if (BeginPopupEx(GetID(popup_id.c_str()),
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
        {
            SetWindowPos(frame_bb.Min + ImVec2(0.0f, height + style.FramePadding.y), ImGuiCond_Always);
            SetWindowSize(
                ImVec2(
                    frame_bb.GetWidth(),
                    g.FontSize * items_count + style.FramePadding.y * (items_count + 1)
                ),
                ImGuiCond_Always
            );

            RenderFrameBorder(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), true);

            PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.FramePadding.y));

            for (int i = 0; i < items_count; ++i)
            {
                if (ImAdd::SelectableLabel(
                    items[i].second,
                    items[i].first,
                    false,
                    ImVec2(GetContentRegionAvail().x, g.FontSize)))
                {
                    items[i].first = !items[i].first;
                }
            }

            PopStyleVar();

            EndPopup();
        }

        PopStyleVar();

        const ImU32 colFrame = GetColorU32((hovered && held) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

        RenderNavCursor(frame_bb, id);

        window->DrawList->AddRectFilled(
            frame_bb.Min,
            frame_bb.Max,
            colFrame,
            style.FrameRounding
        );

        window->DrawList->AddRectFilledMultiColorRounded(
            frame_bb.Min,
            frame_bb.Max,
            GetColorU32(ImGuiCol_FrameBgShadow, 0.0f),
            GetColorU32(ImGuiCol_FrameBgShadow, 0.0f),
            GetColorU32(ImGuiCol_FrameBgShadow),
            GetColorU32(ImGuiCol_FrameBgShadow),
            style.FrameRounding
        );

        if (style.FrameBorderSize > 0.0f)
            RenderFrameBorder(frame_bb.Min, frame_bb.Max);

        RenderText(ImVec2(frame_bb.Min.x, total_bb.Min.y), label, nullptr, true, true);

        const ImVec2 preview_size = CalcTextSize(preview_item.c_str(), nullptr, false);

        if (preview_size.x + style.FramePadding.x >
            (frame_bb.GetWidth() - style.FramePadding.x * 3.0f + g.FontSize))
        {
            const float max_width =
                frame_bb.GetWidth() - style.FramePadding.x * 3.0f - g.FontSize;

            const char* ellipsis = ".";
            const float ellipsis_width = CalcTextSize(ellipsis).x;

            std::string trimmed;
            trimmed.reserve(preview_item.size());

            for (char c : preview_item)
            {
                trimmed.push_back(c);

                if (CalcTextSize(trimmed.c_str()).x + ellipsis_width > max_width)
                {
                    trimmed.pop_back();
                    break;
                }
            }

            preview_item = trimmed + ellipsis;
        }

        ImAdd::RenderText(frame_bb.Min + style.FramePadding, preview_item.c_str(), nullptr, false, true);

        RenderArrow(
            window->DrawList,
            frame_bb.Min + ImVec2(frame_bb.GetWidth() - g.FontSize - style.FramePadding.x,
                style.FramePadding.y),
            GetColorU32(ImGuiCol_Text),
            g.FontSize,
            ImGuiDir_Down
        );

        return pressed;
    }

    bool ImAdd::Combo(const char* label, int* selected_index, std::vector<const char*> items)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);
        const float square_sz = g.FontSize + style.FramePadding.y * 2.0f;

        const float width = CalcItemWidth();
        const float height = GetFrameHeight();
        const float label_offset_y = ((label_size.x > 0.0f) ? (g.FontSize + style.ItemInnerSpacing.y) : 0.0f);

        int items_count = items.size();

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = ImVec2(width, height + label_offset_y);

        const ImRect total_bb(pos, pos + size);
        const ImRect frame_bb(ImVec2(pos.x, pos.y + label_offset_y), pos + size);
        ItemSize(size);
        if (!ItemAdd(total_bb, id))
            return false;


        bool hovered, held;
        bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);

        std::string popup_str_id = std::string(std::string(label) + "::combo_popup");

        if (pressed)
        {
            OpenPopup(popup_str_id.c_str());
        }

        PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.FramePadding.y));
        if (BeginPopupEx(GetID(popup_str_id.c_str()), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
        {
            SetWindowPos(frame_bb.Min + ImVec2(0, height + style.FramePadding.y), ImGuiCond_Always);
            SetWindowSize(ImVec2(frame_bb.GetWidth(), ImGui::GetFontSize() * items_count + style.FramePadding.y * (items_count + 1)), ImGuiCond_Always);

            RenderFrameBorder(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), true);

            for (int i = 0; i < items_count; i++)
            {
                if (ImAdd::SelectableLabel(items[i], i == *selected_index, false, ImVec2(GetContentRegionAvail().x, GetFontSize())))
                {
                    *selected_index = i;
                    CloseCurrentPopup();
                }
            }

            EndPopup();
        }
        PopStyleVar(2);

        ImVec4 colFrame = GetStyleColorVec4((hovered && held) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

        struct stColors_State {
            ImColor Frame;
        };

        static std::map<ImGuiID, stColors_State> anim;
        auto it_anim = anim.find(id);

        if (it_anim == anim.end())
        {
            anim.insert({ id, stColors_State() });
            it_anim = anim.find(id);

            it_anim->second.Frame = colFrame;
        }

        it_anim->second.Frame.Value = ImLerp(it_anim->second.Frame.Value, colFrame, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);

        RenderNavCursor(frame_bb, id);

        window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, it_anim->second.Frame, style.FrameRounding);
        window->DrawList->AddRectFilledMultiColorRounded(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow), style.FrameRounding);

        if (style.FrameBorderSize > 0)
        {
            RenderFrameBorder(frame_bb.Min, frame_bb.Max);
        }

        std::string preview_item;
        if (*selected_index > items.size()) {
            preview_item = "*unknown item*";
        }
        else
        {
            preview_item = items[*selected_index];
        }

        ImVec2 label_pos = ImVec2(frame_bb.Min.x, total_bb.Min.y);
        RenderText(label_pos, label, nullptr, true, true);

        ImAdd::RenderText(frame_bb.Min + style.FramePadding, preview_item.c_str(), nullptr, false, true);

        RenderArrow(window->DrawList, frame_bb.Min + ImVec2(frame_bb.GetWidth() - GetFontSize() - style.FramePadding.x, style.FramePadding.y), GetColorU32(ImGuiCol_Text), ImGui::GetFontSize(), ImGuiDir_Down);

        return pressed;
    }

    bool ImAdd::ColorButton(const char* desc_id, const ImVec4& col, const ImVec2& size_arg, bool has_alpha)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(desc_id);

        ImVec2 pos = window->DC.CursorPos;
        const float square_sz = g.FontSize + style.CellPadding.y * 2.0f;
        const ImVec2 size(CalcItemSize(size_arg, square_sz, square_sz));
        const ImRect bb(pos, pos + size);

        ItemSize(bb);
        if (!ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held);

        ImVec4 col_rgb = col;
        ImVec4 col_rgb_without_alpha(col_rgb.x, col_rgb.y, col_rgb.z, 1.0f);

        ImVec4 col_source = has_alpha ? col_rgb : col_rgb_without_alpha;

        if (col_source.w < 1.0f && has_alpha)
            RenderColorRectWithAlphaCheckerboard(window->DrawList, bb.Min, bb.Max, GetColorU32(col_source), 3.0f, ImVec2(0, 0));
        else
            window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColorU32(col_source));

        if (style.FrameBorderSize > 0.0f)
            RenderFrameBorder(bb.Min, bb.Max);

        window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow));

        RenderNavHighlight(bb, id);

        return pressed;
    }

    bool ImAdd::ColorEdit4(const char* label, float col[4])
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        const ImVec4 col_v4(col[0], col[1], col[2], col[3]);
        const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip;

        BeginGroup();
        if (label_size.x > 0.0f)
        {
            Text(label);
            SameLine(GetContentRegionAvail().x - GetColorPickerWidth());
        }

        bool pressed = ColorButton(label, col_v4, ImVec2(GetColorPickerWidth(), g.FontSize + style.CellPadding.y * 2.0f));
        EndGroup();

        if (pressed)
        {
            OpenPopup(label);
        }

        if (BeginPopup(label))
        {
            PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
            PushStyleVar(ImGuiStyleVar_ItemSpacing, style.FramePadding);
            PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ColorPicker4(label, col, flags);
            PopStyleVar(3);

            // Hex editor
            // Build hex buf from current color
            struct HexState { char buf[10]; bool editing; };
            static std::unordered_map<ImGuiID, HexState> hex_states;
            ImGuiID hex_id = GetID("##hex_input");

            auto& hs = hex_states[hex_id];

            // Sync hex buf from color when not actively editing
            if (!hs.editing)
            {
                int r = (int)(col[0] * 255.0f + 0.5f);
                int gr = (int)(col[1] * 255.0f + 0.5f);
                int b = (int)(col[2] * 255.0f + 0.5f);
                int a = (int)(col[3] * 255.0f + 0.5f);
                snprintf(hs.buf, sizeof(hs.buf), "#%02X%02X%02X%02X", r, gr, b, a);
            }

            PushStyleVar(ImGuiStyleVar_FramePadding, style.FramePadding);
            SetNextItemWidth(GetContentRegionAvail().x);

            ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll;

            if (InputText("##hex", hs.buf, sizeof(hs.buf), input_flags))
            {
                // Parse hex → color dynamically as user types (skip leading '#')
                const char* hex_str = hs.buf[0] == '#' ? hs.buf + 1 : hs.buf;
                size_t len = strlen(hex_str);
                unsigned int hex_val = 0;
                if (sscanf(hex_str, "%X", &hex_val) == 1)
                {
                    if (len <= 6)
                    {
                        col[0] = ((hex_val >> 16) & 0xFF) / 255.0f;
                        col[1] = ((hex_val >> 8)  & 0xFF) / 255.0f;
                        col[2] = ((hex_val)        & 0xFF) / 255.0f;
                    }
                    else
                    {
                        col[0] = ((hex_val >> 24) & 0xFF) / 255.0f;
                        col[1] = ((hex_val >> 16) & 0xFF) / 255.0f;
                        col[2] = ((hex_val >> 8)  & 0xFF) / 255.0f;
                        col[3] = ((hex_val)        & 0xFF) / 255.0f;
                    }
                }
            }

            hs.editing = IsItemActive();

            PopStyleVar();

            EndPopup();
        }
    }


    bool ImAdd::KeyBind(const char* str_id, int* k, int* type, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        ImGuiIO& io = g.IO;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(str_id);

        ImVec2 pos = window->DC.CursorPos;

        char buf_display[64] = "Unbinded";
        bool is_selecting = (g.ActiveId == id);

        if (is_selecting)
        {
            strcpy_s(buf_display, sizeof(buf_display), "[]");
        }
        else if (k && *k > 0 && *k < 256)
        {
            std::string kname = GetVKName(*k);
            snprintf(buf_display, sizeof(buf_display), "[%s]", kname.c_str());
        }

        ImVec2 buf_display_size = ImGui::CalcTextSize(buf_display, NULL, true);
        float item_w = CalcKeyBindWidth(k ? *k : 0);
        if (size_arg.x > 0.0f) item_w = size_arg.x;
        ImVec2 size = CalcItemSize(size_arg, item_w, g.FontSize + style.FramePadding.y * 2.0f - 3.0f);
        ImRect frame_bb(pos, pos + size);
        ImRect total_bb(pos, frame_bb.Max);

        ItemSize(total_bb);
        if (!ItemAdd(total_bb, id))
            return false;

        const bool hovered = ItemHoverable(frame_bb, id, 0);
        if (hovered)
        {
            SetHoveredID(id);
            g.MouseCursor = ImGuiMouseCursor_Hand;
        }

        const bool user_clicked  = hovered && IsMouseClicked(ImGuiMouseButton_Left);
        const bool popup_trigger = IsMouseHoveringRect(frame_bb.Min, frame_bb.Max) && IsMouseClicked(ImGuiMouseButton_Right);

        std::string popup_str_id = std::string(str_id) + "::keybind_popup";

        if (type && popup_trigger)
        {
            OpenPopup(popup_str_id.c_str());
        }

        const float popup_width = CalcTextSize("Always").x + style.FramePadding.x * 2.0f + 16.0f;

        PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_BorderShadow]);
        PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));
        PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);

        if (BeginPopupEx(GetID(popup_str_id.c_str()), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
        {
            SetWindowPos(ImVec2(frame_bb.Max.x - popup_width, frame_bb.Max.y + style.FramePadding.y), ImGuiCond_Always);
            SetWindowSize(ImVec2(popup_width, g.FontSize * 3 + style.WindowPadding.y * 4.0f + 10.0f), ImGuiCond_Always);

            PushStyleVar(ImGuiStyleVar_ItemSpacing, style.WindowPadding);

            if (ImAdd::SelectableLabel("Hold", *type == 1, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) { *type = 1; CloseCurrentPopup(); }
            if (ImAdd::SelectableLabel("Toggle", *type == 0, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) { *type = 0; CloseCurrentPopup(); }
            if (ImAdd::SelectableLabel("Always", *type == 2, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) { *type = 2; CloseCurrentPopup(); }

            PopStyleVar();
            EndPopup();
        }

        PopStyleVar();
        PopStyleColor(2);

        bool value_changed = false;

        if (popup_trigger)
        {
            // handled
        }
        else if (user_clicked)
        {
            if (g.ActiveId == id)
            {
                ClearActiveID();
            }
            else
            {
                SetActiveID(id, window);
                FocusWindow(window);
            }
        }
        else if (IsMouseClicked(ImGuiMouseButton_Left) && g.ActiveId == id && !hovered)
        {
            ClearActiveID();
        }

        // Handle key capture
        if (g.ActiveId == id && !user_clicked)
        {
            // Check mouse buttons (RMB, MMB, X1, X2, or LMB if clicked on item)
            for (int i = 0; i < 5; i++)
            {
                if (IsMouseClicked(i))
                {
                    int vk = 0;
                    if (i == 0) vk = VK_LBUTTON;
                    else if (i == 1) vk = VK_RBUTTON;
                    else if (i == 2) vk = VK_MBUTTON;
                    else if (i == 3) vk = VK_XBUTTON1;
                    else if (i == 4) vk = VK_XBUTTON2;

                    if (k) *k = vk;
                    value_changed = true;
                    ClearActiveID();
                    break;
                }
            }

            // Check keyboard keys
            if (!value_changed && g.ActiveId == id)
            {
                for (int vk = 1; vk < 256; vk++)
                {
                    if (vk == VK_LBUTTON) continue;

                    if (GetAsyncKeyState(vk) & 0x8000)
                    {
                        if (vk == VK_ESCAPE)
                        {
                            if (k) *k = 0;
                        }
                        else
                        {
                            if (k) *k = vk;
                        }
                        value_changed = true;
                        ClearActiveID();
                        break;
                    }
                }
            }
        }

        // Render
        ImGui::RenderNavHighlight(total_bb, id);

        ImU32 colFrame = GetColorU32(is_selecting ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, colFrame, style.FrameRounding);
        window->DrawList->AddRectFilledMultiColor(frame_bb.Min, frame_bb.Max,
            GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow, 0.0f),
            GetColorU32(ImGuiCol_ButtonShadow), GetColorU32(ImGuiCol_ButtonShadow));
        RenderFrameBorder(frame_bb.Min, frame_bb.Max);

        RenderText(pos + ImVec2((size.x - buf_display_size.x) / 2.0f, (size.y - buf_display_size.y) / 2.0f), buf_display, 0, true, true);

        return value_changed;
    }

    bool ImAdd::KeyBind(const char* str_id, ImGuiKey* k, ImGuiKeyType* type, const ImVec2& size_arg)
    {
        int vk = k ? (int)*k : 0;
        int t = type ? (int)*type : 0;
        bool res = KeyBind(str_id, &vk, type ? &t : nullptr, size_arg);
        if (res)
        {
            if (k) *k = (ImGuiKey)vk;
            if (type) *type = (ImGuiKeyType)t;
        }
        return res;
    }

    bool ImAdd::Tab(const char* label, bool selected, const ImVec2& size_arg)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

        const ImRect bb(pos, pos + size);
        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held);

        // Colors
        const ImU32 col = GetColorU32(selected ? ImGuiCol_Tab : hovered && held ? ImGuiCol_TabActive : hovered ? ImGuiCol_TabHovered : ImGuiCol_ChildBg);

        // Render
        RenderNavCursor(bb, id);
        RenderFrame(bb.Min, bb.Max, col, false);
        window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max,
            GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow, 0.0f),
            GetColorU32(ImGuiCol_ButtonShadow), GetColorU32(ImGuiCol_ButtonShadow));

        if (!selected)
        {
            window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y - style.ChildBorderSize), ImVec2(bb.Max.x, bb.Max.y - style.ChildBorderSize), GetColorU32(ImGuiCol_BorderShadow), style.ChildBorderSize);
        }

        RenderText(pos + ImTrunc((size - label_size) / 2) - ImVec2(0.0f, style.ChildBorderSize), label, NULL, true, true);

        return pressed;
    }

    void ImAdd::ScrollBar(const char* str_id, ImGuiWindow* window, const ImVec2& size_arg)
    {
        if (!window || window->SkipItems)
            return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(str_id);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, GetFrameHeight(), CalcItemWidth());
        const ImRect total_bb(pos, pos + size);
        ItemSize(size);
        if (!ItemAdd(total_bb, id))
            return;

        bool hovered, held;
        bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

        // Scroll metrics
        float visible_height = size.y;
        float total_height = window->ContentSize.y;
        float scroll_max = ImMax(window->ScrollMax.y, 0.0f);
        float scroll_y = window->Scroll.y;

        float scroll_height = (total_height > 0.0f)
            ? (visible_height / total_height) * visible_height
            : visible_height;

        scroll_height = ImClamp(scroll_height, 15.0f, visible_height);

        float scroll_top = (scroll_max > 0.0f)
            ? (scroll_y / scroll_max) * (visible_height - scroll_height)
            : 0.0f;

        // Handle drag-to-scroll
        if (held && scroll_max > 0.0f)
        {
            float mouse_delta = g.IO.MouseDelta.y;
            float scrollable_range = visible_height - scroll_height;
            if (scrollable_range > 0.0f)
            {
                float ratio = scroll_max / scrollable_range;
                window->Scroll.y = ImClamp(window->Scroll.y + mouse_delta * ratio, 0.0f, scroll_max);
            }
        }

        // Handle mouse wheel scrolling
        bool hovered_window = IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (!held && (hovered || hovered_window) && scroll_max > 0.0f && !IsKeyDown(ImGuiKey_LeftShift))
        {
            // Disable native ImGui scrolling
            window->Flags |= ImGuiWindowFlags_NoScrollWithMouse;

            const float wheel_speed = 40.0f; // tweak to taste
            window->Scroll.y = ImClamp(
                window->Scroll.y - g.IO.MouseWheel * wheel_speed,
                0.0f,
                scroll_max
            );
        }

        // Colors
        ImU32 colGrab = GetColorU32((hovered && held) ? ImGuiCol_ScrollbarGrabActive : hovered ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab);

        // Draw background and grab
        window->DrawList->AddRectFilled(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_ScrollbarBg), style.ScrollbarRounding);
        window->DrawList->AddRectFilled(
            ImVec2(total_bb.Min.x, total_bb.Min.y + scroll_top),
            ImVec2(total_bb.Max.x, total_bb.Min.y + scroll_top + scroll_height),
            colGrab,
            style.ScrollbarRounding
        );

        RenderNavCursor(total_bb, id);
    }

    bool ImAdd::BeginChild(const char* str_id, std::vector<const char*> tabs, int* selected_tab_index_callback, const ImVec2& size_arg)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* parent_window = g.CurrentWindow;
        //if (parent_window->SkipItems)
        //    return;

        const ImGuiID id = parent_window->GetID(str_id);
        const ImGuiStyle& style = g.Style;

        std::string str_id_tabs         = std::string(str_id) + "##child##tabs";
        std::string str_id_scrollbar    = std::string(str_id) + "##child##scrollbar";

        PushStyleVar(ImGuiStyleVar_WindowPadding, style.ChildPadding);
        bool result = ImGui::BeginChild(str_id, size_arg, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        PopStyleVar();

        //if (result)
        {
            ImGuiWindow*    window  = GetCurrentWindow();
            ImVec2          cur_pos = window->DC.CursorPos;
            ImVec2          pos     = window->Pos;
            ImVec2          size    = window->Size;
            ImRect		    window_bb(pos, pos + size);
            bool            has_scroll = window->ScrollMax.y > 0;

            bool has_tabs = tabs.size() > 0;
            float tabs_height = g_TabHeight > 0.0f ? g_TabHeight : GetFrameHeight();

            if (has_tabs)
            {
                SetCursorScreenPos(pos + ImVec2(0.0f, style.ChildBorderSize * 5.0f));
                PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.ChildBorderSize * 2.0f, 0.0f));
                if (ImGui::BeginChild(str_id_tabs.c_str(), ImVec2(size.x, tabs_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
                {
                    ImRect tabs_bb = ImGui::GetCurrentWindow()->Rect();
                    tabs_bb.Min.x += style.ChildBorderSize * 2.0f;
                    tabs_bb.Max.x -= style.ChildBorderSize * 2.0f;
                    tabs_bb.Max.y -= style.ChildBorderSize;

                    RenderFrame(tabs_bb.Min, tabs_bb.Max, GetColorU32(ImGuiCol_Tab), false, false);

                    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
                    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

                    // Pre-compute per-tab width when fill mode is on:
                    // available = tabs_bb width, minus (n-1) separators of ChildBorderSize each
                    float fill_tab_w = 0.0f;
                    if (g_TabFillSpace)
                    {
                        float avail    = tabs_bb.GetWidth();
                        float sep_cost = (tabs.size() - 1) * style.ChildBorderSize;
                        fill_tab_w     = ImFloor((avail - sep_cost) / (float)tabs.size());
                    }

                    for (int i = 0; i < tabs.size(); i++)
                    {
                        bool selected = false;

                        static std::map<ImGuiID, int> anim;
                        auto it_anim = anim.find(id);

                        if (it_anim == anim.end())
                        {
                            anim.insert({ id, int() });
                            it_anim = anim.find(id);

                            it_anim->second = 0;
                        }

                        if (selected_tab_index_callback)
                        {
                            selected = (*selected_tab_index_callback == i);
                        }
                        else
                        {
                            selected = (it_anim->second == i);
                        }

                        float tab_w = g_TabFillSpace ? fill_tab_w : 0.0f;
                        if (Tab(tabs[i], selected, ImVec2(tab_w, tabs_height)))
                        {
                            if (selected_tab_index_callback)
                            {
                                *selected_tab_index_callback = i;
                            }
                            else
                            {
                                it_anim->second = i;
                            }
                        }

                        if (i < tabs.size())
                        {
                            SameLine();
                            PushStyleColor(ImGuiCol_Separator, style.Colors[ImGuiCol_BorderShadow]);
                            VSeparator(0.0f, style.ChildBorderSize);
                            PopStyleColor();
                            SameLine();
                        }
                    }

                    PopStyleVar(2);
                }
                ImGui::EndChild();
                PopStyleVar();
            }

            if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoBackground)
            {
                window->DrawList->AddRectFilled(pos, pos + size, GetColorU32(ImGuiCol_ChildBg));

                if (style.ChildBorderSize > 0.0f)
                {
                    // Window outer border
                    RenderFrameBorder(window_bb.Min, window_bb.Max, true);

                    // Window top decoration
                    window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 2.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize * 2.0f, window_bb.Min.y + style.ChildBorderSize * 2.0f), ImGui::GetColorU32(ImGuiCol_Header), style.ChildBorderSize);
                    window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 3.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize * 2.0f, window_bb.Min.y + style.ChildBorderSize * 3.0f), ImGui::GetColorU32(ImGuiCol_HeaderActive), style.ChildBorderSize);
                    window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 4.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize * 2.0f, window_bb.Min.y + style.ChildBorderSize * 4.0f), ImGui::GetColorU32(ImGuiCol_BorderShadow), style.ChildBorderSize);

                    if (has_tabs)
                    {
                        window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize * 2.0f, tabs_height + style.ChildBorderSize * 4.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize * 2.0f, window_bb.Min.y + tabs_height + style.ChildBorderSize * 4.0f), ImGui::GetColorU32(ImGuiCol_BorderShadow), style.ChildBorderSize);
                    }
                }
            }

            float scroll_offset_y = style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : style.ChildBorderSize);

            if (has_scroll)
            {
                SetCursorScreenPos(pos + ImVec2(size.x - style.ScrollbarSize - style.ChildPadding.x, scroll_offset_y + style.ChildPadding.y));
                ImAdd::ScrollBar(str_id_scrollbar.c_str(), window, ImVec2(style.ScrollbarSize, size.y - scroll_offset_y - style.ChildPadding.y * 2.0f));
            }

            SetCursorScreenPos(cur_pos + ImVec2(0.0f, style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : 0.0f)));

            if (has_scroll)
            {
                window->ContentRegionRect.Max.x -= style.ScrollbarSize + style.ChildPadding.x;
            }

            window->DrawList->PushClipRect(window_bb.Min + ImVec2(style.ChildBorderSize, style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : style.ChildBorderSize)), window_bb.Max - ImVec2(style.ChildBorderSize, has_tabs ? 0.0f : style.ChildBorderSize), true);
        }

        PushItemWidth(GetContentRegionAvail().x);

        return result;
    }

    bool ImAdd::BeginChild(const char* str_id, std::vector<const char*> tabs, const ImVec2& size_arg)
    {
        return ImAdd::BeginChild(str_id, tabs, (int*)NULL, size_arg);
    }

    bool ImAdd::BeginChild(const char* str_id, const ImVec2& size_arg)
    {
        return ImAdd::BeginChild(str_id, {}, (int*)NULL, size_arg);
    }

    void ImAdd::EndChild()
    {
        PopItemWidth();

        ImGuiWindow* window = GetCurrentWindow();

        window->DrawList->PopClipRect();

        ImGui::EndChild();
    }

    bool ImAdd::SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        const float btn_size  = g.FontSize;
        const float btn_gap   = style.ItemInnerSpacing.x;
        const float full_width = CalcItemWidth();
        const float slider_width = full_width - (btn_size + btn_gap) * 2.0f;

        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 label_size = CalcTextSize(label, NULL, true);
        const bool has_label = label_size.x > 0;
        const float frame_pos_y = has_label ? (g.FontSize + style.ItemInnerSpacing.y) : 0.0f;
        const float frame_height = g.FontSize;

        const ImRect frame_bb(pos + ImVec2(0, frame_pos_y), pos + ImVec2(slider_width, frame_pos_y + frame_height));
        const ImRect total_bb(pos, frame_bb.Max);

        ItemSize(total_bb);
        if (!ItemAdd(total_bb, id, &frame_bb, 0))
            return false;

        if (format == NULL)
            format = DataTypeGetInfo(data_type)->PrintFmt;

        const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool held = g.ActiveId == id;
        const bool make_active = (clicked || g.NavActivateId == id);

        if (make_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }

        ImU32 colFrame = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImU32 colLine  = GetColorU32(held ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab);

        ImRect grab_bb;
        bool value_changed = SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, 0, &grab_bb);
        if (value_changed)
            MarkItemEdited(id);

        float relative_value = 0.0f;
        if (data_type == ImGuiDataType_Float)
        {
            float val = *(float*)p_data, mn = *(float*)p_min, mx = *(float*)p_max;
            relative_value = (val - mn) / (mx - mn);
        }
        else if (data_type == ImGuiDataType_S32)
        {
            int val = *(int*)p_data, mn = *(int*)p_min, mx = *(int*)p_max;
            relative_value = (float)(val - mn) / (float)(mx - mn);
        }
        relative_value = ImClamp(relative_value, 0.0f, 1.0f);

        window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, colFrame, style.FrameRounding);
        ImVec2 fill_end = ImTrunc(ImVec2(frame_bb.Min.x + relative_value * frame_bb.GetWidth(), frame_bb.Max.y));
        ImRect slider_fill(ImTrunc(frame_bb.Min), fill_end);
        if (slider_fill.Max.x > slider_fill.Min.x + style.FrameRounding)
            window->DrawList->AddRectFilled(slider_fill.Min, slider_fill.Max, colLine, style.FrameRounding);
        window->DrawList->AddRectFilledMultiColorRounded(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow), style.FrameRounding);
        if (style.FrameBorderSize > 0.0f)
            RenderFrameBorder(frame_bb.Min, frame_bb.Max);

        char value_buf[64];
        DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
        if (has_label)
            RenderText(total_bb.Min, label, NULL, true, true);
        ImVec2 val_size = CalcTextSize(value_buf);
        RenderText(frame_bb.Min + ImTrunc((frame_bb.GetSize() - val_size) / 2), value_buf, NULL, true, true);

        // − and + buttons via SameLine after the slider item
        float btn_y = frame_bb.Min.y;
        ImVec2 btn_sz(btn_size, frame_height);

        // manually place cursor for minus button
        SetCursorScreenPos(ImVec2(frame_bb.Max.x + btn_gap + 1.0f, btn_y));
        PushID(id);

        if (ImAdd::Button("-", btn_sz))
        {
            if (data_type == ImGuiDataType_Float)
            {
                float& v = *(float*)p_data;
                v = ImClamp(v - 1.0f, *(float*)p_min, *(float*)p_max);
            }
            else if (data_type == ImGuiDataType_S32)
            {
                int& v = *(int*)p_data;
                v = ImClamp(v - 1, *(int*)p_min, *(int*)p_max);
            }
            value_changed = true;
        }

        SetCursorScreenPos(ImVec2(frame_bb.Max.x + btn_gap + 1.0f + btn_size + btn_gap, btn_y));

        if (ImAdd::Button("+", btn_sz))
        {
            if (data_type == ImGuiDataType_Float)
            {
                float& v = *(float*)p_data;
                v = ImClamp(v + 1.0f, *(float*)p_min, *(float*)p_max);
            }
            else if (data_type == ImGuiDataType_S32)
            {
                int& v = *(int*)p_data;
                v = ImClamp(v + 1, *(int*)p_min, *(int*)p_max);
            }
            value_changed = true;
        }

        PopID();

        // restore cursor past the full row
        SetCursorScreenPos(ImVec2(pos.x, frame_bb.Max.y + style.ItemSpacing.y));

        return value_changed;
    }

    bool ImAdd::SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format)
    {
        return ImAdd::SliderScalar(label, ImGuiDataType_Float, v, &v_min, &v_max, format);
    }

    bool ImAdd::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format)
    {
        return ImAdd::SliderScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, format);
    }

    void ImAdd::RenderArrow(ImDrawList* draw_list, ImVec2 pos, ImU32 col, float sz, ImGuiDir dir)
    {
        if (dir < 0 || dir >= ImGuiDir_COUNT)
            return;

        draw_list->PathClear();

        float half  = sz * 0.5f;
        float pad_x = sz * 0.25f;
        float pad_y = sz * 0.385f;

        switch (dir)
        {
        case ImGuiDir_Down:
            draw_list->PathLineTo(pos + ImVec2(pad_x, pad_y));
            draw_list->PathLineTo(pos + ImVec2(half, sz - pad_y));
            draw_list->PathLineTo(pos + ImVec2(sz - pad_x, pad_y));
            break;

        case ImGuiDir_Up:
            draw_list->PathLineTo(pos + ImVec2(pad_x, sz - pad_y));
            draw_list->PathLineTo(pos + ImVec2(half, pad_y));
            draw_list->PathLineTo(pos + ImVec2(sz - pad_x, sz - pad_y));
            break;

        case ImGuiDir_Right:
            draw_list->PathLineTo(pos + ImVec2(pad_y, pad_x));
            draw_list->PathLineTo(pos + ImVec2(sz - pad_y, half));
            draw_list->PathLineTo(pos + ImVec2(pad_y, sz - pad_x));
            break;

        case ImGuiDir_Left:
            draw_list->PathLineTo(pos + ImVec2(sz - pad_y, pad_x));
            draw_list->PathLineTo(pos + ImVec2(pad_y, half));
            draw_list->PathLineTo(pos + ImVec2(sz - pad_y, sz - pad_x));
            break;
        }

        draw_list->PathFillConvex(col);
    }

    void ImAdd::RenderText(ImVec2 pos, const char* text, const char* text_end, bool hide_text_after_hash, bool has_outlines)
    {
        ImGuiContext& g = *GImGui;

        pos.y -= 1.0f; // nudge all widget text up 1px for Verdana

        if (has_outlines)
        {
            PushStyleColor(ImGuiCol_Text, g.Style.Colors[ImGuiCol_BorderShadow]);

            ImGui::RenderText(pos + ImVec2(0, 1), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(0, -1), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(1, 0), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(-1, 0), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(1, 1), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(-1, -1), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(1, -1), text, text_end, hide_text_after_hash);
            ImGui::RenderText(pos + ImVec2(-1, 1), text, text_end, hide_text_after_hash);

            PopStyleColor();
        }

        ImGui::RenderText(pos, text, text_end, hide_text_after_hash);
    }

    void ImAdd::RenderFrame(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool borders, bool shadows, bool inverted, float rounding)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        window->DrawList->AddRectFilled(p_min, p_max, fill_col, rounding);
        const float border_size = g.Style.FrameBorderSize;
        if (borders && border_size > 0.0f)
        {
            RenderFrameBorder(p_min, p_max, inverted, shadows, rounding);
        }
    }

    void ImAdd::RenderFrameBorder(ImVec2 p_min, ImVec2 p_max, bool inverted, bool shadow, float rounding)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        const float border_size = g.Style.FrameBorderSize;
        if (border_size > 0.0f)
        {
            window->DrawList->AddRect(p_min, p_max, GetColorU32(inverted ? ImGuiCol_Border : ImGuiCol_BorderShadow), rounding, 0, border_size);
            window->DrawList->AddRect(p_min + ImVec2(1, 1), p_max - ImVec2(1, 1), GetColorU32(inverted ? ImGuiCol_BorderShadow : ImGuiCol_Border), rounding, 0, border_size);
        }
    }
