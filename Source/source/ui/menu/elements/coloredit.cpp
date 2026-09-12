#include "../settings/functions.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static float s_clipboard_color[4] = { 141.f / 255.f, 41.f / 255.f, 81.f / 255.f, 1.f };
static bool s_has_clipboard_color = false;

static void copy_color_to_clipboard(const float col[4], bool alpha)
{
    s_clipboard_color[0] = col[0];
    s_clipboard_color[1] = col[1];
    s_clipboard_color[2] = col[2];
    s_clipboard_color[3] = alpha ? col[3] : 1.0f;
    s_has_clipboard_color = true;

    char hex[32];
    if (alpha)
    {
        snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X",
            (int)std::clamp(col[0] * 255.f + 0.5f, 0.f, 255.f),
            (int)std::clamp(col[1] * 255.f + 0.5f, 0.f, 255.f),
            (int)std::clamp(col[2] * 255.f + 0.5f, 0.f, 255.f),
            (int)std::clamp(col[3] * 255.f + 0.5f, 0.f, 255.f));
    }
    else
    {
        snprintf(hex, sizeof(hex), "#%02X%02X%02X",
            (int)std::clamp(col[0] * 255.f + 0.5f, 0.f, 255.f),
            (int)std::clamp(col[1] * 255.f + 0.5f, 0.f, 255.f),
            (int)std::clamp(col[2] * 255.f + 0.5f, 0.f, 255.f));
    }
    ImGui::SetClipboardText(hex);
}

static bool paste_color_from_clipboard(float col[4], bool alpha)
{
    const char* clip = ImGui::GetClipboardText();
    if (clip && clip[0] == '#')
    {
        size_t len = strlen(clip);
        if (len == 7)
        {
            unsigned int r = 0, g = 0, b = 0;
            if (sscanf_s(clip + 1, "%02x%02x%02x", &r, &g, &b) == 3)
            {
                col[0] = r / 255.f;
                col[1] = g / 255.f;
                col[2] = b / 255.f;
                if (alpha) col[3] = 1.0f;
                return true;
            }
        }
        else if (len == 9)
        {
            unsigned int r = 0, g = 0, b = 0, a = 255;
            if (sscanf_s(clip + 1, "%02x%02x%02x%02x", &r, &g, &b, &a) == 4)
            {
                col[0] = r / 255.f;
                col[1] = g / 255.f;
                col[2] = b / 255.f;
                if (alpha) col[3] = a / 255.f;
                return true;
            }
        }
    }

    if (s_has_clipboard_color)
    {
        col[0] = s_clipboard_color[0];
        col[1] = s_clipboard_color[1];
        col[2] = s_clipboard_color[2];
        if (alpha) col[3] = s_clipboard_color[3];
        return true;
    }
    return false;
}

static void color_edit_restore_h(const float* col, float* H)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.ColorEditCurrentID != 0);
    if (g.ColorEditSavedID != g.ColorEditCurrentID || g.ColorEditSavedColor != ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0)))
        return;
    *H = g.ColorEditSavedHue;
}

static void color_edit_restore_hs(const float* col, float* H, float* S, float* V)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.ColorEditCurrentID != 0);
    if (g.ColorEditSavedID != g.ColorEditCurrentID || g.ColorEditSavedColor != ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0)))
        return;

    if (*S == 0.0f || (*H == 0.0f && g.ColorEditSavedHue == 1))
        *H = g.ColorEditSavedHue;

    if (*V == 0.0f)
        *S = g.ColorEditSavedSat;
}

bool color_button(const char* desc_id, const ImVec4& col)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(desc_id);

    const ImRect rect(window->DC.CursorPos, window->DC.CursorPos + elements->widgets.color_size);
    ItemSize(rect, 0.0f);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);

    RenderColorRectWithAlphaCheckerboard(window->DrawList, rect.Min + ImVec2(2, 2), rect.Max - ImVec2(2, 2), draw->get_clr(col, 0.f), 5, ImVec2(0, 0));
    draw->fade_rect_filled(window->DrawList, rect.Min + ImVec2(2, 2), rect.Max - ImVec2(2, 2), draw->get_clr(col), draw->get_clr({col.x - 0.2f, col.y - 0.2f, col.z - 0.2f, col.w}), fade_direction::vertically);
    draw->rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(var->window.hover_hightlight && hovered ? clr->accent : clr->widgets.stroke_two));
    draw->rect(window->DrawList, rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), draw->get_clr(clr->window.stroke));
    return pressed;
}

bool color_picker(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImDrawList* draw_list = window->DrawList;
    ImGuiStyle& style = g.Style;
    ImGuiIO& io = g.IO;

    const float width = CalcItemWidth();
    const bool is_readonly = ((g.NextItemData.ItemFlags | g.CurrentItemFlags) & ImGuiItemFlags_ReadOnly) != 0;
    g.NextItemData.ClearFlags();

    PushID(label);
    const bool set_current_color_edit_id = (g.ColorEditCurrentID == 0);
    if (set_current_color_edit_id)
        g.ColorEditCurrentID = window->IDStack.back();
    BeginGroup();

    if (!(flags & ImGuiColorEditFlags_NoSidePreview))
        flags |= ImGuiColorEditFlags_NoSmallPreview;

    if (!(flags & ImGuiColorEditFlags_NoOptions))
        ColorPickerOptionsPopup(col, flags);

    if (!(flags & ImGuiColorEditFlags_PickerMask_))
        flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_PickerMask_;
    if (!(flags & ImGuiColorEditFlags_InputMask_))
        flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_InputMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_InputMask_;
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_PickerMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));
    if (!(flags & ImGuiColorEditFlags_NoOptions))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_AlphaBar);

    int components = (flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4;
    bool alpha_bar = (flags & ImGuiColorEditFlags_AlphaBar) && !(flags & ImGuiColorEditFlags_NoAlpha);
    ImVec2 picker_pos = window->DC.CursorPos;
    float square_sz = 0;
    float bars_width = 13;
    float sv_picker_size = 150;
    float bar0_pos_x = picker_pos.x + sv_picker_size + style.ItemInnerSpacing.x;
    float bar1_pos_x = bar0_pos_x + bars_width + style.ItemInnerSpacing.x;
    float bars_triangles_half_sz = IM_TRUNC(bars_width * 0.20f);

    float backup_initial_col[4];
    memcpy(backup_initial_col, col, components * sizeof(float));

    float wheel_thickness = sv_picker_size * 0.08f;
    float wheel_r_outer = sv_picker_size * 0.50f;
    float wheel_r_inner = wheel_r_outer - wheel_thickness;
    ImVec2 wheel_center(picker_pos.x + (sv_picker_size + bars_width) * 0.5f, picker_pos.y + sv_picker_size * 0.5f);

    float triangle_r = wheel_r_inner - (int)(sv_picker_size * 0.027f);
    ImVec2 triangle_pa = ImVec2(triangle_r, 0.0f);
    ImVec2 triangle_pb = ImVec2(triangle_r * -0.5f, triangle_r * -0.866025f);
    ImVec2 triangle_pc = ImVec2(triangle_r * -0.5f, triangle_r * +0.866025f);

    float H = col[0], S = col[1], V = col[2];
    float R = col[0], G = col[1], B = col[2];
    if (flags & ImGuiColorEditFlags_InputRGB)
    {
        ColorConvertRGBtoHSV(R, G, B, H, S, V);
        color_edit_restore_hs(col, &H, &S, &V);
    }
    else if (flags & ImGuiColorEditFlags_InputHSV)
    {
        ColorConvertHSVtoRGB(H, S, V, R, G, B);
    }

    bool value_changed = false, value_changed_h = false, value_changed_sv = false;

    InvisibleButton("sv", ImVec2(sv_picker_size, sv_picker_size));
    if (IsItemActive() && !is_readonly)
    {
        S = ImSaturate((io.MousePos.x - picker_pos.x) / (sv_picker_size - 1));
        V = 1.0f - ImSaturate((io.MousePos.y - picker_pos.y) / (sv_picker_size - 1));
        color_edit_restore_h(col, &H);
        value_changed = value_changed_sv = true;
    }

    SetCursorScreenPos(ImVec2(bar0_pos_x, picker_pos.y));
    InvisibleButton("hue", ImVec2(bars_width, sv_picker_size));
    if (IsItemActive() && !is_readonly)
    {
        H = ImSaturate((io.MousePos.y - picker_pos.y) / (sv_picker_size - 1));
        value_changed = value_changed_h = true;
    }

    if (alpha_bar)
    {
        SetCursorScreenPos(ImVec2(bar1_pos_x, picker_pos.y));
        InvisibleButton("alpha", ImVec2(bars_width, sv_picker_size));
        if (IsItemActive())
        {
            col[3] = 1.0f - ImSaturate((io.MousePos.y - picker_pos.y) / (sv_picker_size - 1));
            value_changed = true;
        }
    }

    if (value_changed_h || value_changed_sv)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);
            g.ColorEditSavedHue = H;
            g.ColorEditSavedSat = S;
            g.ColorEditSavedID = g.ColorEditCurrentID;
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0));
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            col[0] = H;
            col[1] = S;
            col[2] = V;
        }
    }

    bool value_changed_fix_hue_wrap = false;

    if (value_changed_fix_hue_wrap && (flags & ImGuiColorEditFlags_InputRGB))
    {
        float new_H, new_S, new_V;
        ColorConvertRGBtoHSV(col[0], col[1], col[2], new_H, new_S, new_V);
        if (new_H <= 0 && H > 0)
        {
            if (new_V <= 0 && V != new_V)
                ColorConvertHSVtoRGB(H, S, new_V <= 0 ? V * 0.5f : new_V, col[0], col[1], col[2]);
            else if (new_S <= 0)
                ColorConvertHSVtoRGB(H, new_S <= 0 ? S * 0.5f : new_S, new_V, col[0], col[1], col[2]);
        }
    }

    if (value_changed)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            R = col[0];
            G = col[1];
            B = col[2];
            ColorConvertRGBtoHSV(R, G, B, H, S, V);
            color_edit_restore_hs(col, &H, &S, &V);
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            H = col[0];
            S = col[1];
            V = col[2];
            ColorConvertHSVtoRGB(H, S, V, R, G, B);
        }
    }

    const int style_alpha8 = IM_F32_TO_INT8_SAT(style.Alpha);
    const ImU32 col_black = IM_COL32(0, 0, 0, style_alpha8);
    const ImU32 col_white = IM_COL32(255, 255, 255, style_alpha8);
    const ImU32 col_midgrey = IM_COL32(128, 128, 128, style_alpha8);
    const ImU32 col_hues[6 + 1] = { IM_COL32(255,0,0,style_alpha8), IM_COL32(255,255,0,style_alpha8), IM_COL32(0,255,0,style_alpha8), IM_COL32(0,255,255,style_alpha8), IM_COL32(0,0,255,style_alpha8), IM_COL32(255,0,255,style_alpha8), IM_COL32(255,0,0,style_alpha8) };

    ImVec4 hue_color_f(1, 1, 1, style.Alpha); ColorConvertHSVtoRGB(H, 1, 1, hue_color_f.x, hue_color_f.y, hue_color_f.z);
    ImU32 hue_color32 = ColorConvertFloat4ToU32(hue_color_f);
    ImU32 user_col32_striped_of_alpha = ColorConvertFloat4ToU32(ImVec4(R, G, B, style.Alpha)); // Important: this is still including the main rendering/style alpha!!

    ImVec2 sv_cursor_pos;

    draw_list->AddRectFilledMultiColor(picker_pos, picker_pos + ImVec2(sv_picker_size, sv_picker_size), col_white, hue_color32, hue_color32, col_white);
    draw_list->AddRectFilledMultiColor(picker_pos, picker_pos + ImVec2(sv_picker_size, sv_picker_size), 0, 0, col_black, col_black);
    RenderFrameBorder(picker_pos, picker_pos + ImVec2(sv_picker_size, sv_picker_size), 0.0f);
    sv_cursor_pos.x = ImClamp(IM_ROUND(picker_pos.x + ImSaturate(S) * sv_picker_size), picker_pos.x + 2, picker_pos.x + sv_picker_size - 2); // Sneakily prevent the circle to stick out too much
    sv_cursor_pos.y = ImClamp(IM_ROUND(picker_pos.y + ImSaturate(1 - V) * sv_picker_size), picker_pos.y + 2, picker_pos.y + sv_picker_size - 2);

    for (int i = 0; i < 6; ++i)
        draw_list->AddRectFilledMultiColor(ImVec2(bar0_pos_x, picker_pos.y + i * (sv_picker_size / 6)), ImVec2(bar0_pos_x + bars_width, picker_pos.y + (i + 1) * (sv_picker_size / 6)), col_hues[i], col_hues[i], col_hues[i + 1], col_hues[i + 1]);
    float bar0_line_y = IM_ROUND(picker_pos.y + H * (sv_picker_size - 10));
    RenderFrameBorder(ImVec2(bar0_pos_x, picker_pos.y), ImVec2(bar0_pos_x + bars_width, picker_pos.y + sv_picker_size), 0.0f);

    draw->rect(window->DrawList, ImVec2(bar0_pos_x - 1, bar0_line_y), ImVec2(bar0_pos_x + bars_width + 1, bar0_line_y + 10), draw->get_clr(clr->widgets.text), 0, 0, 1);

    draw_list->AddCircleFilled(sv_cursor_pos, 4, user_col32_striped_of_alpha, 30);
    draw_list->AddCircle(sv_cursor_pos, 4 + 1, col_midgrey, 30);
    draw_list->AddCircle(sv_cursor_pos, 4, col_white, 30);

    if (alpha_bar)
    {
        float alpha = ImSaturate(col[3]);
        ImRect bar1_bb(bar1_pos_x, picker_pos.y, bar1_pos_x + bars_width, picker_pos.y + sv_picker_size);
        draw_list->AddRectFilledMultiColor(bar1_bb.Min, bar1_bb.Max, user_col32_striped_of_alpha, user_col32_striped_of_alpha, col_black, col_black);
        float bar1_line_y = IM_ROUND(picker_pos.y + (1.0f - alpha) * (sv_picker_size - 10));
        RenderFrameBorder(bar1_bb.Min, bar1_bb.Max, 0.0f);
        draw->rect(window->DrawList, ImVec2(bar1_pos_x - 1, bar1_line_y), ImVec2(bar1_pos_x + bars_width + 1, bar1_line_y + 10), draw->get_clr(clr->widgets.text), 0, 0, 1);
    }

    EndGroup();

    if (value_changed && memcmp(backup_initial_col, col, components * sizeof(float)) == 0)
        value_changed = false;
    if (value_changed && g.LastItemData.ID != 0)
        MarkItemEdited(g.LastItemData.ID);

    if (set_current_color_edit_id)
        g.ColorEditCurrentID = 0;
    PopID();

    return value_changed;
}

bool c_gui::color_edit(std::string_view label, float col[4], bool active_alpha)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const float square_sz = GetFrameHeight();
    const char* label_display_end = FindRenderedTextEnd(label.data());
    float w_full = CalcItemWidth();
    g.NextItemData.ClearFlags();
    ImGuiColorEditFlags flags = 0;

    BeginGroup();
    PushID(label.data());
    const bool set_current_color_edit_id = (g.ColorEditCurrentID == 0);
    if (set_current_color_edit_id)
        g.ColorEditCurrentID = window->IDStack.back();

    const ImGuiColorEditFlags flags_untouched = 0;
    if (flags & ImGuiColorEditFlags_NoInputs)
        flags = (flags & (~ImGuiColorEditFlags_DisplayMask_)) | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoOptions;

    if (!(flags & ImGuiColorEditFlags_NoOptions))
        ColorEditOptionsPopup(col, flags);

    if (!(flags & ImGuiColorEditFlags_DisplayMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DisplayMask_);
    if (!(flags & ImGuiColorEditFlags_DataTypeMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DataTypeMask_);
    if (!(flags & ImGuiColorEditFlags_PickerMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_);
    if (!(flags & ImGuiColorEditFlags_InputMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_InputMask_);
    flags |= (g.ColorEditOptions & ~(ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_DisplayMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));

    const bool alpha = (flags & ImGuiColorEditFlags_NoAlpha) == 0;
    const bool hdr = (flags & ImGuiColorEditFlags_HDR) != 0;
    const int components = alpha ? 4 : 3;
    const float w_button = (flags & ImGuiColorEditFlags_NoSmallPreview) ? 0.0f : (square_sz + style.ItemInnerSpacing.x);
    const float w_inputs = ImMax(w_full - w_button, 1.0f);
    w_full = w_inputs + w_button;

    float f[4] = { col[0], col[1], col[2], alpha ? col[3] : 1.0f };
    if ((flags & ImGuiColorEditFlags_InputHSV) && (flags & ImGuiColorEditFlags_DisplayRGB))
        ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
    else if ((flags & ImGuiColorEditFlags_InputRGB) && (flags & ImGuiColorEditFlags_DisplayHSV))
    {
        ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);
        color_edit_restore_hs(col, &f[0], &f[1], &f[2]);
    }
    int i[4] = { IM_F32_TO_INT8_UNBOUND(f[0]), IM_F32_TO_INT8_UNBOUND(f[1]), IM_F32_TO_INT8_UNBOUND(f[2]), IM_F32_TO_INT8_UNBOUND(f[3]) };

    bool value_changed = false;
    bool value_changed_as_float = false;

    const ImVec2 pos = window->DC.CursorPos;
    const float inputs_offset_x = (style.ColorButtonPosition == ImGuiDir_Left) ? w_button : 0.0f;
    window->DC.CursorPos.x = pos.x + inputs_offset_x;

    ImGuiWindow* picker_active_window = NULL;

    gui->push_style_var(ImGuiStyleVar_PopupBorderSize, 1.f);
    gui->push_style_color(ImGuiCol_Border, draw->get_clr(clr->window.stroke));
    gui->push_style_color(ImGuiCol_PopupBg, draw->get_clr(clr->window.background_one));
    if (!(flags & ImGuiColorEditFlags_NoSmallPreview))
    {
        const float button_offset_x = ((flags & ImGuiColorEditFlags_NoInputs) || (style.ColorButtonPosition == ImGuiDir_Left)) ? 0.0f : w_inputs + style.ItemInnerSpacing.x;

        const ImVec4 col_v4(col[0], col[1], col[2], alpha ? col[3] : 1.0f);
        if (color_button("##ColorButton", col_v4))
        {
            if (!(flags & ImGuiColorEditFlags_NoPicker))
            {
                g.ColorPickerRef = col_v4;
                OpenPopup("picker");
                SetNextWindowPos(g.LastItemData.Rect.GetBL() + ImVec2(0.0f, style.ItemSpacing.y));
            }
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            OpenPopup("ColorContextMenu");
            SetNextWindowPos(g.LastItemData.Rect.GetBL() + ImVec2(0.0f, style.ItemSpacing.y));
        }

        if (BeginPopup("ColorContextMenu"))
        {
            if (ImGui::MenuItem("Copy Color"))
            {
                copy_color_to_clipboard(col, alpha);
            }
            if (ImGui::MenuItem("Paste Color"))
            {
                if (paste_color_from_clipboard(col, alpha))
                    value_changed = true;
            }
            EndPopup();
        }

        if (BeginPopup("picker"))
        {
            if (g.CurrentWindow->BeginCount == 1)
            {
                picker_active_window = g.CurrentWindow;
                ImGuiColorEditFlags picker_flags_to_forward = ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_ | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_AlphaBar;
                ImGuiColorEditFlags picker_flags = (flags_untouched & picker_flags_to_forward) | ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf;
                value_changed |= color_picker("##picker", col, picker_flags | (active_alpha ? ImGuiColorEditFlags_AlphaBar : 0), &g.ColorPickerRef.x);

                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    OpenPopup("ColorContextMenuInPicker");
                }

                if (BeginPopup("ColorContextMenuInPicker"))
                {
                    if (ImGui::MenuItem("Copy Color"))
                    {
                        copy_color_to_clipboard(col, alpha);
                    }
                    if (ImGui::MenuItem("Paste Color"))
                    {
                        if (paste_color_from_clipboard(col, alpha))
                            value_changed = true;
                    }
                    EndPopup();
                }
            }
            EndPopup();
        }
    }
    gui->pop_style_var();
    gui->pop_style_color(2);

    if (value_changed && picker_active_window == NULL)
    {
        if (!value_changed_as_float)
            for (int n = 0; n < 4; n++)
                f[n] = i[n] / 255.0f;
        if ((flags & ImGuiColorEditFlags_DisplayHSV) && (flags & ImGuiColorEditFlags_InputRGB))
        {
            g.ColorEditSavedHue = f[0];
            g.ColorEditSavedSat = f[1];
            ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
            g.ColorEditSavedID = g.ColorEditCurrentID;
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(f[0], f[1], f[2], 0));
        }
        if ((flags & ImGuiColorEditFlags_DisplayRGB) && (flags & ImGuiColorEditFlags_InputHSV))
            ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);

        col[0] = f[0];
        col[1] = f[1];
        col[2] = f[2];
        if (alpha)
            col[3] = f[3];
    }

    if (set_current_color_edit_id)
        g.ColorEditCurrentID = 0;
    PopID();
    EndGroup();

    if (picker_active_window && g.ActiveId != 0 && g.ActiveIdWindow == picker_active_window)
        g.LastItemData.ID = g.ActiveId;

    if (value_changed && g.LastItemData.ID != 0)
        MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}

bool c_gui::label_color_edit(std::string_view label, float col[4], bool active_alpha)
{
    draw->text_outline(GetWindowDrawList(), var->font.tahoma, var->font.tahoma->FontSize, GetCursorScreenPos() + ImVec2(1, 0), draw->get_clr(clr->widgets.text), label.data());
    gui->set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.color_size.x - GetStyle().WindowPadding.x, GetCursorPos().y));
    gui->color_edit(label, col, active_alpha);

    return true;
}