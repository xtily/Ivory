#include "../settings/functions.h"

ImU32 c_draw::get_clr(const ImVec4& col, float alpha)
{
    ImGuiStyle& style = GImGui->Style;
    ImVec4 c = col;
    c.w *= style.Alpha * alpha;
    return ColorConvertFloat4ToU32(c);
}

void c_draw::rect_filled_multi_color(ImDrawList* draw, const ImVec2& p_min, const ImVec2& p_max, ImU32 col_upr_left, ImU32 col_upr_right, ImU32 col_bot_right, ImU32 col_bot_left, float rounding, ImDrawFlags flags)
{
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & IM_COL32_A_MASK) == 0)
        return;

    auto fix_rect_corner_flags = [](ImDrawFlags rflags)
        {
            if ((rflags & ImDrawFlags_RoundCornersMask_) == 0)
                rflags |= ImDrawFlags_RoundCornersAll;
            return rflags;
        };

    flags = fix_rect_corner_flags(flags);
    rounding = ImMin(rounding, ImFabs(p_max.x - p_min.x) * (((flags & ImDrawFlags_RoundCornersTop) == ImDrawFlags_RoundCornersTop) || ((flags & ImDrawFlags_RoundCornersBottom) == ImDrawFlags_RoundCornersBottom) ? 0.5f : 1.0f) - 1.0f);
    rounding = ImMin(rounding, ImFabs(p_max.y - p_min.y) * (((flags & ImDrawFlags_RoundCornersLeft) == ImDrawFlags_RoundCornersLeft) || ((flags & ImDrawFlags_RoundCornersRight) == ImDrawFlags_RoundCornersRight) ? 0.5f : 1.0f) - 1.0f);

    if (rounding > 0.0f)
    {
        const int size_before = draw->VtxBuffer.Size;
        draw->AddRectFilled(p_min, p_max, IM_COL32_WHITE, rounding, flags);
        const int size_after = draw->VtxBuffer.Size;

        for (int i = size_before; i < size_after; i++)
        {
            ImDrawVert* vert = draw->VtxBuffer.Data + i;

            ImVec4 upr_left = ImGui::ColorConvertU32ToFloat4(col_upr_left);
            ImVec4 bot_left = ImGui::ColorConvertU32ToFloat4(col_bot_left);
            ImVec4 up_right = ImGui::ColorConvertU32ToFloat4(col_upr_right);
            ImVec4 bot_right = ImGui::ColorConvertU32ToFloat4(col_bot_right);

            float X = ImClamp((vert->pos.x - p_min.x) / (p_max.x - p_min.x), 0.0f, 1.0f);

            float r1 = upr_left.x + (up_right.x - upr_left.x) * X;
            float r2 = bot_left.x + (bot_right.x - bot_left.x) * X;

            float g1 = upr_left.y + (up_right.y - upr_left.y) * X;
            float g2 = bot_left.y + (bot_right.y - bot_left.y) * X;

            float b1 = upr_left.z + (up_right.z - upr_left.z) * X;
            float b2 = bot_left.z + (bot_right.z - bot_left.z) * X;

            float a1 = upr_left.w + (up_right.w - upr_left.w) * X;
            float a2 = bot_left.w + (bot_right.w - bot_left.w) * X;


            float Y = ImClamp((vert->pos.y - p_min.y) / (p_max.y - p_min.y), 0.0f, 1.0f);
            float r = r1 + (r2 - r1) * Y;
            float g = g1 + (g2 - g1) * Y;
            float b = b1 + (b2 - b1) * Y;
            float a = a1 + (a2 - a1) * Y;
            ImVec4 RGBA(r, g, b, a);

            RGBA = RGBA * ImGui::ColorConvertU32ToFloat4(vert->col);

            vert->col = ImColor(RGBA);
        }
        return;
    }

    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    draw->PrimReserve(6, 4);
    draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 1)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 2));
    draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 2)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 3));
    draw->PrimWriteVtx(p_min, uv, col_upr_left);
    draw->PrimWriteVtx(ImVec2(p_max.x, p_min.y), uv, col_upr_right);
    draw->PrimWriteVtx(p_max, uv, col_bot_right);
    draw->PrimWriteVtx(ImVec2(p_min.x, p_max.y), uv, col_bot_left);
}

void set_linear_color(ImDrawList* draw_list, int vert_start_idx, int vert_end_idx, ImVec2 gradient_p0, ImVec2 gradient_p1, ImU32 col0, ImU32 col1)
{
    ImVec2 gradient_extent = gradient_p1 - gradient_p0;
    float gradient_inv_length2 = 1.0f / ImLengthSqr(gradient_extent);
    ImDrawVert* vert_start = draw_list->VtxBuffer.Data + vert_start_idx;
    ImDrawVert* vert_end = draw_list->VtxBuffer.Data + vert_end_idx;
    const int col0_r = (int)(col0 >> IM_COL32_R_SHIFT) & 0xFF;
    const int col0_g = (int)(col0 >> IM_COL32_G_SHIFT) & 0xFF;
    const int col0_b = (int)(col0 >> IM_COL32_B_SHIFT) & 0xFF;
    const int col_delta_r = ((int)(col1 >> IM_COL32_R_SHIFT) & 0xFF) - col0_r;
    const int col_delta_g = ((int)(col1 >> IM_COL32_G_SHIFT) & 0xFF) - col0_g;
    const int col_delta_b = ((int)(col1 >> IM_COL32_B_SHIFT) & 0xFF) - col0_b;
    for (ImDrawVert* vert = vert_start; vert < vert_end; vert++)
    {
        float d = ImDot(vert->pos - gradient_p0, gradient_extent);
        float t = ImClamp(d * gradient_inv_length2, 0.0f, 1.0f);
        int r = (int)(col0_r + col_delta_r * t);
        int g = (int)(col0_g + col_delta_g * t);
        int b = (int)(col0_b + col_delta_b * t);
        vert->col = (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) | (b << IM_COL32_B_SHIFT) | (vert->col & IM_COL32_A_MASK);
    }
}

void c_draw::set_linear_color_alpha(ImDrawList* draw_list, int vert_start_idx, int vert_end_idx, ImVec2 gradient_p0, ImVec2 gradient_p1, ImU32 col0, ImU32 col1)
{
    ImVec2 gradient_extent = gradient_p1 - gradient_p0;
    float gradient_inv_length2 = 1.0f / ImLengthSqr(gradient_extent);
    ImDrawVert* vert_start = draw_list->VtxBuffer.Data + vert_start_idx;
    ImDrawVert* vert_end = draw_list->VtxBuffer.Data + vert_end_idx;
    const int col0_r = (int)(col0 >> IM_COL32_R_SHIFT) & 0xFF;
    const int col0_g = (int)(col0 >> IM_COL32_G_SHIFT) & 0xFF;
    const int col0_b = (int)(col0 >> IM_COL32_B_SHIFT) & 0xFF;
    const int col0_a = (int)(col0 >> IM_COL32_A_SHIFT) & 0xFF;
    const int col_delta_r = ((int)(col1 >> IM_COL32_R_SHIFT) & 0xFF) - col0_r;
    const int col_delta_g = ((int)(col1 >> IM_COL32_G_SHIFT) & 0xFF) - col0_g;
    const int col_delta_b = ((int)(col1 >> IM_COL32_B_SHIFT) & 0xFF) - col0_b;
    const int col_delta_a = ((int)(col1 >> IM_COL32_A_SHIFT) & 0xFF) - col0_a;
    for (ImDrawVert* vert = vert_start; vert < vert_end; vert++)
    {
        float d = ImDot(vert->pos - gradient_p0, gradient_extent);
        float t = ImClamp(d * gradient_inv_length2, 0.0f, 1.0f);
        int r = (int)(col0_r + col_delta_r * t);
        int g = (int)(col0_g + col_delta_g * t);
        int b = (int)(col0_b + col_delta_b * t);
        int a = (int)(col0_a + col_delta_a * t);
        vert->col = (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) | (b << IM_COL32_B_SHIFT) | (a << IM_COL32_A_SHIFT);
    }
}

void c_draw::fade_rect_filled(ImDrawList* draw, const ImVec2& pos_min, const ImVec2& pos_max, ImU32 col_one, ImU32 col_two, fade_direction direction, float rounding, ImDrawFlags flags)
{
    const ImVec2 fade_pos_in = (direction == fade_direction::diagonally_reversed) ? ImVec2(pos_max.x, pos_min.y) : pos_min;

    const ImVec2 fade_pos_out = (direction == fade_direction::vertically) ? ImVec2(pos_min.x, pos_max.y) :
        (direction == fade_direction::horizontally) ? ImVec2(pos_max.x, pos_min.y) :
        (direction == fade_direction::diagonally) ? pos_max :
        (direction == fade_direction::diagonally_reversed) ? ImVec2(pos_min.x, pos_max.y) : ImVec2(0, 0);

    const int vtx_buffer_start = draw->VtxBuffer.Size;
    draw->AddRectFilled(pos_min, pos_max, get_clr(ImVec4(255, 255, 255, 255)), rounding, flags);
    const int vtx_buffer_end = draw->VtxBuffer.Size;
    set_linear_color_alpha(draw, vtx_buffer_start, vtx_buffer_end, fade_pos_in, fade_pos_out, col_one, col_two);
}

void c_draw::text(ImDrawList* draw_list, const ImFont* font, float font_size, const ImVec2& pos, ImU32 col, const char* text_begin, const char* text_end, float wrap_width, const ImVec4* cpu_fine_clip_rect)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    if (text_begin == text_end || text_begin[0] == 0)
        return;
    if (text_end == NULL)
        text_end = text_begin + strlen(text_begin);

    if (font == NULL)
        font = draw_list->_Data->Font;
    if (font_size == 0.0f)
        font_size = draw_list->_Data->FontSize;

    IM_ASSERT(font->ContainerAtlas->TexID == draw_list->_CmdHeader.TexRef.GetTexID());

    ImVec4 clip_rect = draw_list->_CmdHeader.ClipRect;
    if (cpu_fine_clip_rect)
    {
        clip_rect.x = ImMax(clip_rect.x, cpu_fine_clip_rect->x);
        clip_rect.y = ImMax(clip_rect.y, cpu_fine_clip_rect->y);
        clip_rect.z = ImMin(clip_rect.z, cpu_fine_clip_rect->z);
        clip_rect.w = ImMin(clip_rect.w, cpu_fine_clip_rect->w);
    }
    const_cast<ImFont*>(font)->RenderText(draw_list, font_size, pos, col, clip_rect, text_begin, text_end, wrap_width, cpu_fine_clip_rect != NULL);
}

void c_draw::text_outline(ImDrawList* draw_list, const ImFont* font, float font_size, const ImVec2& pos, ImU32 col, const char* text_begin, const char* text_end, float wrap_width, const ImVec4* cpu_fine_clip_rect)
{
    text(draw_list, font, font_size, pos - ImVec2(1, 1), draw->get_clr({ 0, 0, 0, 1 }), text_begin, text_end, wrap_width, cpu_fine_clip_rect);
    text(draw_list, font, font_size, pos - ImVec2(1, -1), draw->get_clr({ 0, 0, 0, 1 }), text_begin, text_end, wrap_width, cpu_fine_clip_rect);
    text(draw_list, font, font_size, pos + ImVec2(1, 1), draw->get_clr({ 0, 0, 0, 1 }), text_begin, text_end, wrap_width, cpu_fine_clip_rect);
    text(draw_list, font, font_size, pos + ImVec2(1, -1), draw->get_clr({ 0, 0, 0, 1 }), text_begin, text_end, wrap_width, cpu_fine_clip_rect);
    text(draw_list, font, font_size, pos, col, text_begin, text_end, wrap_width, cpu_fine_clip_rect);
}

void c_draw::text_outline(const char* text)
{
    ImVec2 pos = GetCursorPos();

    gui->push_style_color(ImGuiCol_Text, draw->get_clr({ 0, 0, 0, 1 }));
    gui->set_cursor_pos(pos - ImVec2(1, 1));
    Text(text);
    gui->set_cursor_pos(pos - ImVec2(1, -1));
    Text(text);
    gui->set_cursor_pos(pos + ImVec2(1, 1));
    Text(text);
    gui->set_cursor_pos(pos + ImVec2(1, -1));
    Text(text);
    gui->pop_style_color();

    gui->set_cursor_pos(pos);
    Text(text);
}

void c_draw::text_clipped(ImDrawList* draw_list, ImFont* font, const ImVec2& pos_min, const ImVec2& pos_max, ImU32 color, const char* text, const char* text_display_end, const ImVec2* text_size_if_known, const ImVec2& align, const ImRect* clip_rect)
{
    PushFont(font);

    ImVec2 pos = pos_min;
    const ImVec2 text_size = text_size_if_known ? *text_size_if_known : CalcTextSize(text, text_display_end, false, 0.0f);

    const ImVec2* clip_min = clip_rect ? &clip_rect->Min : &pos_min;
    const ImVec2* clip_max = clip_rect ? &clip_rect->Max : &pos_max;
    bool need_clipping = (pos.x + text_size.x >= clip_max->x) || (pos.y + text_size.y >= clip_max->y);
    if (clip_rect) need_clipping |= (pos.x < clip_min->x) || (pos.y < clip_min->y);

    if (align.x > 0.0f) pos.x = ImMax(pos.x, pos.x + (pos_max.x - pos.x - text_size.x) * align.x);
    if (align.y > 0.0f) pos.y = ImMax(pos.y, pos.y + (pos_max.y - pos.y - text_size.y) * align.y);

    if (need_clipping)
    {
        ImVec4 fine_clip_rect(clip_min->x, clip_min->y, clip_max->x, clip_max->y);
        draw_list->AddText(NULL, 0, pos, color, text, text_display_end, 0.0f, &fine_clip_rect);
    }
    else
    {
        draw_list->AddText(NULL, 0, pos, color, text, text_display_end, 0.0f, NULL);
    }
    PopFont();
}

void c_draw::text_clipped_outline(ImDrawList* draw_list, ImFont* font, const ImVec2& pos_min, const ImVec2& pos_max, ImU32 color, const char* text, const char* text_display_end, const ImVec2* text_size_if_known, const ImVec2& align, const ImRect* clip_rect)
{
    text_clipped(draw_list, font, pos_min - ImVec2(1, 1), pos_max - ImVec2(1, 1), draw->get_clr({ 0, 0, 0, 1 }), text, text_display_end, text_size_if_known, align);
    text_clipped(draw_list, font, pos_min - ImVec2(1, -1), pos_max - ImVec2(1, -1), draw->get_clr({ 0, 0, 0, 1 }), text, text_display_end, text_size_if_known, align);
    text_clipped(draw_list, font, pos_min + ImVec2(1, 1), pos_max + ImVec2(1, 1), draw->get_clr({ 0, 0, 0, 1 }), text, text_display_end, text_size_if_known, align);
    text_clipped(draw_list, font, pos_min + ImVec2(1, -1), pos_max + ImVec2(1, -1), draw->get_clr({ 0, 0, 0, 1 }), text, text_display_end, text_size_if_known, align);
    text_clipped(draw_list, font, pos_min, pos_max, color, text, text_display_end, text_size_if_known, align);
}

void c_draw::radial_gradient(ImDrawList* draw_list, const ImVec2& center, float radius, ImU32 col_in, ImU32 col_out)
{
    if (((col_in | col_out) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
        return;

    draw_list->_PathArcToFastEx(center, radius, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
    const int count = draw_list->_Path.Size - 1;

    unsigned int vtx_base = draw_list->_VtxCurrentIdx;
    draw_list->PrimReserve(count * 3, count + 1);

    const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
    draw_list->PrimWriteVtx(center, uv, col_in);
    for (int n = 0; n < count; n++)
        draw_list->PrimWriteVtx(draw_list->_Path[n], uv, col_out);

    for (int n = 0; n < count; n++)
    {
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base));
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + n));
        draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((n + 1) % count)));
    }
    draw_list->_Path.Size = 0;
}

void c_draw::line(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;
    draw_list->PathLineTo(p1 + ImVec2(0.5f, 0.5f));
    draw_list->PathLineTo(p2 + ImVec2(0.5f, 0.5f));
    draw_list->PathStroke(col, 0, thickness);
}

void c_draw::rect(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding, ImDrawFlags flags, float thickness)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;
    if (draw_list->Flags & ImDrawListFlags_AntiAliasedLines)
        draw_list->PathRect(p_min + ImVec2(0.50f, 0.50f), p_max - ImVec2(0.50f, 0.50f), rounding, flags);
    else
        draw_list->PathRect(p_min + ImVec2(0.50f, 0.50f), p_max - ImVec2(0.49f, 0.49f), rounding, flags); // Better looking lower-right corner and rounded non-AA shapes.
    draw_list->PathStroke(col, ImDrawFlags_Closed, thickness);
}

void c_draw::rect_filled(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding, ImDrawFlags flags)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;
    if (rounding < 0.5f || (flags & ImDrawFlags_RoundCornersMask_) == ImDrawFlags_RoundCornersNone)
    {
        draw_list->PrimReserve(6, 4);
        draw_list->PrimRect(p_min, p_max, col);
    }
    else
    {
        draw_list->PathRect(p_min, p_max, rounding, flags);
        draw_list->PathFillConvex(col);
    }
}

void c_draw::circle(ImDrawList* draw_list, const ImVec2& center, float radius, ImU32 col, int num_segments, float thickness)
{
    if ((col & IM_COL32_A_MASK) == 0 || radius < 0.5f)
        return;

    if (num_segments <= 0)
    {
        draw_list->_PathArcToFastEx(center, radius - 0.5f, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
        draw_list->_Path.Size--;
    }
    else
    {
        num_segments = ImClamp(num_segments, 3, IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MAX);

        const float a_max = (IM_PI * 2.0f) * ((float)num_segments - 1.0f) / (float)num_segments;
        draw_list->PathArcTo(center, radius - 0.5f, 0.0f, a_max, num_segments - 1);
    }

    draw_list->PathStroke(col, ImDrawFlags_Closed, thickness);
}

void c_draw::circle_filled(ImDrawList* draw_list, const ImVec2& center, float radius, ImU32 col, int num_segments)
{
    if ((col & IM_COL32_A_MASK) == 0 || radius < 0.5f)
        return;

    if (num_segments <= 0)
    {
        draw_list->_PathArcToFastEx(center, radius, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
        draw_list->_Path.Size--;
    }
    else
    {
        num_segments = ImClamp(num_segments, 3, IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MAX);

        const float a_max = (IM_PI * 2.0f) * ((float)num_segments - 1.0f) / (float)num_segments;
        draw_list->PathArcTo(center, radius, 0.0f, a_max, num_segments - 1);
    }

    draw_list->PathFillConvex(col);
}

void c_draw::triangle(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col, float thickness)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    draw_list->PathLineTo(p1);
    draw_list->PathLineTo(p2);
    draw_list->PathLineTo(p3);
    draw_list->PathStroke(col, ImDrawFlags_Closed, thickness);
}

void c_draw::triangle_filled(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    draw_list->PathLineTo(p1);
    draw_list->PathLineTo(p2);
    draw_list->PathLineTo(p3);
    draw_list->PathFillConvex(col);
}

void c_draw::image(ImDrawList* draw_list, ImTextureID user_texture_id, const ImVec2& p_min, const ImVec2& p_max, const ImVec2& uv_min, const ImVec2& uv_max, ImU32 col)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    const bool push_texture_id = user_texture_id != draw_list->_CmdHeader.TexRef.GetTexID();
    if (push_texture_id)
        draw_list->PushTextureID(user_texture_id);

    draw_list->PrimReserve(6, 4);
    draw_list->PrimRectUV(p_min, p_max, uv_min, uv_max, col);

    if (push_texture_id)
        draw_list->PopTextureID();
}

void c_draw::image_rounded(ImDrawList* draw_list, ImTextureID user_texture_id, const ImVec2& p_min, const ImVec2& p_max, const ImVec2& uv_min, const ImVec2& uv_max, ImU32 col, float rounding, ImDrawFlags flags)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    auto fix_rect_corner_flags = [](ImDrawFlags rflags)
        {
            if ((rflags & ImDrawFlags_RoundCornersMask_) == 0)
                rflags |= ImDrawFlags_RoundCornersAll;
            return rflags;
        };

    flags = fix_rect_corner_flags(flags);
    if (rounding < 0.5f || (flags & ImDrawFlags_RoundCornersMask_) == ImDrawFlags_RoundCornersNone)
    {
        draw_list->AddImage(user_texture_id, p_min, p_max, uv_min, uv_max, col);
        return;
    }

    const bool push_texture_id = user_texture_id != draw_list->_CmdHeader.TexRef.GetTexID();
    if (push_texture_id)
        draw_list->PushTextureID(user_texture_id);

    int vert_start_idx = draw_list->VtxBuffer.Size;
    draw_list->PathRect(p_min, p_max, rounding, flags);
    draw_list->PathFillConvex(col);
    int vert_end_idx = draw_list->VtxBuffer.Size;
    ImGui::ShadeVertsLinearUV(draw_list, vert_start_idx, vert_end_idx, p_min, p_max, uv_min, uv_max, true);

    if (push_texture_id)
        draw_list->PopTextureID();
}

static void substracted_rect(ImDrawList* draw_list, const ImVec2& a_min, const ImVec2& a_max, const ImVec2& a_min_uv, const ImVec2& a_max_uv, ImVec2 b_min, ImVec2 b_max, ImU32 col)
{
    if (a_min.x >= a_max.x || a_min.y >= a_max.y)
        return;

    if (a_min.x >= b_min.x && a_max.x <= b_max.x && a_min.y >= b_min.y && a_max.y <= b_max.y)
        return;

    b_min = ImMax(b_min, a_min);
    b_max = ImMin(b_max, a_max);
    if (b_min.x >= b_max.x || b_min.y >= b_max.y)
    {
        draw_list->PrimReserve(6, 4);
        draw_list->PrimRectUV(a_min, a_max, a_min_uv, a_max_uv, col);
        return;
    }

    const int max_verts = 12;
    const int max_indices = 6 * 4;
    draw_list->PrimReserve(max_indices, max_verts);

    ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
    ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx idx = (ImDrawIdx)draw_list->_VtxCurrentIdx;

    vtx_write[0].pos = ImVec2(a_min.x, a_min.y); vtx_write[0].uv = ImVec2(a_min_uv.x, a_min_uv.y); vtx_write[0].col = col;
    vtx_write[1].pos = ImVec2(a_max.x, a_min.y); vtx_write[1].uv = ImVec2(a_max_uv.x, a_min_uv.y); vtx_write[1].col = col;
    vtx_write[2].pos = ImVec2(a_max.x, a_max.y); vtx_write[2].uv = ImVec2(a_max_uv.x, a_max_uv.y); vtx_write[2].col = col;
    vtx_write[3].pos = ImVec2(a_min.x, a_max.y); vtx_write[3].uv = ImVec2(a_min_uv.x, a_max_uv.y); vtx_write[3].col = col;

    const ImVec2 pos_to_uv_scale = (a_max_uv - a_min_uv) / (a_max - a_min);
    const ImVec2 pos_to_uv_offset = (a_min_uv / pos_to_uv_scale) - a_min;

#define LERP_UV(x_pos, y_pos) (ImVec2(((x_pos) + pos_to_uv_offset.x) * pos_to_uv_scale.x, ((y_pos) + pos_to_uv_offset.y) * pos_to_uv_scale.y))
    vtx_write[4].pos = ImVec2(b_min.x, b_min.y); vtx_write[4].uv = LERP_UV(b_min.x, b_min.y); vtx_write[4].col = col;
    vtx_write[5].pos = ImVec2(b_max.x, b_min.y); vtx_write[5].uv = LERP_UV(b_max.x, b_min.y); vtx_write[5].col = col;
    vtx_write[6].pos = ImVec2(b_max.x, b_max.y); vtx_write[6].uv = LERP_UV(b_max.x, b_max.y); vtx_write[6].col = col;
    vtx_write[7].pos = ImVec2(b_min.x, b_max.y); vtx_write[7].uv = LERP_UV(b_min.x, b_max.y); vtx_write[7].col = col;
    vtx_write[8].pos = ImVec2(b_min.x, a_min.y); vtx_write[8].uv = LERP_UV(b_min.x, a_min.y); vtx_write[8].col = col;
    vtx_write[9].pos = ImVec2(b_max.x, a_min.y); vtx_write[9].uv = LERP_UV(b_max.x, a_min.y); vtx_write[9].col = col;
    vtx_write[10].pos = ImVec2(b_max.x, a_max.y); vtx_write[10].uv = LERP_UV(b_max.x, a_max.y); vtx_write[10].col = col;
    vtx_write[11].pos = ImVec2(b_min.x, a_max.y); vtx_write[11].uv = LERP_UV(b_min.x, a_max.y); vtx_write[11].col = col;
#undef LERP_UV
    draw_list->_VtxWritePtr += 12;
    draw_list->_VtxCurrentIdx += 12;

    if (b_min.x > a_min.x) 
    {
        idx_write[0] = (ImDrawIdx)(idx + 0); idx_write[1] = (ImDrawIdx)(idx + 8); idx_write[2] = (ImDrawIdx)(idx + 11);
        idx_write[3] = (ImDrawIdx)(idx + 0); idx_write[4] = (ImDrawIdx)(idx + 11); idx_write[5] = (ImDrawIdx)(idx + 3);
        idx_write += 6;
    }
    if (b_min.y > a_min.y) 
    {
        idx_write[0] = (ImDrawIdx)(idx + 8); idx_write[1] = (ImDrawIdx)(idx + 9); idx_write[2] = (ImDrawIdx)(idx + 5);
        idx_write[3] = (ImDrawIdx)(idx + 8); idx_write[4] = (ImDrawIdx)(idx + 5); idx_write[5] = (ImDrawIdx)(idx + 4);
        idx_write += 6;
    }
    if (a_max.x > b_max.x) 
    {
        idx_write[0] = (ImDrawIdx)(idx + 9); idx_write[1] = (ImDrawIdx)(idx + 1); idx_write[2] = (ImDrawIdx)(idx + 2);
        idx_write[3] = (ImDrawIdx)(idx + 9); idx_write[4] = (ImDrawIdx)(idx + 2); idx_write[5] = (ImDrawIdx)(idx + 10);
        idx_write += 6;
    }
    if (a_max.y > b_max.y)
    {
        idx_write[0] = (ImDrawIdx)(idx + 7); idx_write[1] = (ImDrawIdx)(idx + 6); idx_write[2] = (ImDrawIdx)(idx + 10);
        idx_write[3] = (ImDrawIdx)(idx + 7); idx_write[4] = (ImDrawIdx)(idx + 10); idx_write[5] = (ImDrawIdx)(idx + 11);
        idx_write += 6;
    }

    const int used_indices = (int)(idx_write - draw_list->_IdxWritePtr);
    draw_list->_IdxWritePtr = idx_write;
    draw_list->PrimUnreserve(max_indices - used_indices, 0);
}

static int clip_polygon_shape(ImVec2* src_points, int num_src_points, ImVec2* dest_points, int allocated_dest_points, ImVec2 clip_rect_min, ImVec2 clip_rect_max)
{
    if (clip_rect_max.x <= clip_rect_min.x || clip_rect_max.y <= clip_rect_min.y)
        return 0;

    if (num_src_points < 3)
        return 0;

    ImU8* outflags[2];
    outflags[0] = (ImU8*)alloca(2 * allocated_dest_points * sizeof(ImU8));
    outflags[1] = outflags[0] + allocated_dest_points;

    ImU8 outflags_anded = 0xFF;
    ImU8 outflags_ored = 0;
    for (int point_idx = 0; point_idx < num_src_points; point_idx++)
    {
        const ImVec2 pos = src_points[point_idx];
        const ImU8 point_outflags = (pos.x < clip_rect_min.x ? 1 : 0) | (pos.x > clip_rect_max.x ? 2 : 0) | (pos.y < clip_rect_min.y ? 4 : 0) | (pos.y > clip_rect_max.y ? 8 : 0);
        outflags[0][point_idx] = point_outflags;
        outflags_anded &= point_outflags;
        outflags_ored |= point_outflags;
    }
    if (outflags_anded != 0)
        return 0;

    if (outflags_ored == 0)
    {
        if (allocated_dest_points < num_src_points)
            return -1;

        memcpy(dest_points, src_points, num_src_points * sizeof(ImVec2));
        return num_src_points;
    }

    ImVec2* clip_buf[2];
    clip_buf[0] = (ImVec2*)alloca(2 * allocated_dest_points * sizeof(ImVec2));
    clip_buf[1] = clip_buf[0] + allocated_dest_points;

    memcpy(clip_buf[0], src_points, num_src_points * sizeof(ImVec2));
    int clip_buf_size = num_src_points;

    int read_buffer_idx = 0;

    for (int clip_plane = 0; clip_plane < 4; clip_plane++)
    {
        const int clip_plane_bit = 1 << clip_plane;
        if ((outflags_ored & clip_plane_bit) == 0)
            continue;

        ImVec2* read_vert = &clip_buf[read_buffer_idx][0];
        ImVec2* write_vert = &clip_buf[1 - read_buffer_idx][0];
        ImVec2* write_vert_end = write_vert + allocated_dest_points;
        ImU8* read_outflags = &outflags[read_buffer_idx][0];
        ImU8* write_outflags = &outflags[1 - read_buffer_idx][0];

        ImVec2* last_vert = &read_vert[clip_buf_size - 1];
        ImU8 last_outflags = read_outflags[clip_buf_size - 1];

        for (int vert = 0; vert < clip_buf_size; vert++)
        {
            ImU8 current_outflags = *(read_outflags++);
            bool out = (current_outflags & clip_plane_bit) != 0;
            if (((current_outflags ^ last_outflags) & clip_plane_bit) == 0)
            {
                if (!out)
                {
                    if (write_vert >= write_vert_end)
                        return -1;
                    *(write_vert++) = *read_vert;
                    *(write_outflags++) = current_outflags;
                }
            }
            else
            {
                float t = 0.0f;
                ImVec2 pos0 = *last_vert;
                ImVec2 pos1 = *read_vert;
                ImVec2 intersect_pos;
                switch (clip_plane)
                {
                case 0: t = (clip_rect_min.x - pos0.x) / (pos1.x - pos0.x); intersect_pos = ImVec2(clip_rect_min.x, pos0.y + ((pos1.y - pos0.y) * t)); break;
                case 1: t = (clip_rect_max.x - pos0.x) / (pos1.x - pos0.x); intersect_pos = ImVec2(clip_rect_max.x, pos0.y + ((pos1.y - pos0.y) * t)); break;
                case 2: t = (clip_rect_min.y - pos0.y) / (pos1.y - pos0.y); intersect_pos = ImVec2(pos0.x + ((pos1.x - pos0.x) * t), clip_rect_min.y); break;
                case 3: t = (clip_rect_max.y - pos0.y) / (pos1.y - pos0.y); intersect_pos = ImVec2(pos0.x + ((pos1.x - pos0.x) * t), clip_rect_max.y); break;
                }

                if (write_vert >= write_vert_end)
                    return -1;

                *(write_vert++) = intersect_pos;
                *(write_outflags++) = (intersect_pos.x < clip_rect_min.x ? 1 : 0) | (intersect_pos.x > clip_rect_max.x ? 2 : 0) | (intersect_pos.y < clip_rect_min.y ? 4 : 0) | (intersect_pos.y > clip_rect_max.y ? 8 : 0);

                if (!out)
                {
                    if (write_vert >= write_vert_end)
                        return -1;
                    *(write_vert++) = *read_vert;
                    *(write_outflags++) = current_outflags;
                }

                last_outflags = current_outflags;
            }

            last_vert = read_vert;
            read_vert++;
        }

        clip_buf_size = (int)(write_vert - &clip_buf[1 - read_buffer_idx][0]);
        read_buffer_idx = 1 - read_buffer_idx;
    }

    if (clip_buf_size < 3)
        return 0;

    int num_out_verts = 0;
    ImVec2 last_vert = clip_buf[read_buffer_idx][clip_buf_size - 1];
    for (int i = 0; i < clip_buf_size; i++)
    {
        ImVec2 vert = clip_buf[read_buffer_idx][i];
        if (ImLengthSqr(vert - last_vert) <= 0.00001f)
            continue;
        dest_points[num_out_verts++] = vert;
        last_vert = vert;
    }

    return (num_out_verts > 2) ? num_out_verts : 0;
}

static void substracted_rect(ImDrawList* draw_list, const ImVec2& a_min, const ImVec2& a_max, const ImVec2& a_min_uv, const ImVec2& a_max_uv, ImVec2* b_points, int num_b_points, ImU32 col)
{
    if (a_min.x >= a_max.x || a_min.y >= a_max.y)
        return;

    const int max_clipped_points = num_b_points + 4;
    ImVec2* clipped_b_points = (ImVec2*)alloca(max_clipped_points * sizeof(ImVec2));
    const int num_clipped_points = clip_polygon_shape(b_points, num_b_points, clipped_b_points, max_clipped_points, a_min, a_max);
    IM_ASSERT(num_clipped_points >= 0);

    b_points = clipped_b_points;
    num_b_points = num_clipped_points;

    if (num_clipped_points == 0)
    {
        draw_list->PrimReserve(6, 4);
        draw_list->PrimRectUV(a_min, a_max, a_min_uv, a_max_uv, col);
    }
    else
    {
        const int max_verts = num_b_points + 4;
        const int max_indices = (num_b_points * 3) + (4 * 3);
        draw_list->PrimReserve(max_indices, max_verts);

        ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
        ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
        ImDrawIdx inner_idx = (ImDrawIdx)draw_list->_VtxCurrentIdx;

        const ImVec2 pos_to_uv_scale = (a_max_uv - a_min_uv) / (a_max - a_min);
        const ImVec2 pos_to_uv_offset = (a_min_uv / pos_to_uv_scale) - a_min;

#define LERP_UV(x_pos, y_pos) (ImVec2(((x_pos) + pos_to_uv_offset.x) * pos_to_uv_scale.x, ((y_pos) + pos_to_uv_offset.y) * pos_to_uv_scale.y))
        for (int i = 0; i < num_b_points; i++)
        {
            vtx_write[i].pos = b_points[i];
            vtx_write[i].uv = LERP_UV(b_points[i].x, b_points[i].y);
            vtx_write[i].col = col;
        }
#undef LERP_UV

        vtx_write += num_b_points;

        ImDrawIdx outer_idx = (ImDrawIdx)(inner_idx + num_b_points);

        ImVec2 outer_verts[4];
        outer_verts[0] = ImVec2(a_min.x, a_min.y);
        outer_verts[1] = ImVec2(a_max.x, a_min.y);
        outer_verts[2] = ImVec2(a_max.x, a_max.y);
        outer_verts[3] = ImVec2(a_min.x, a_max.y);

        vtx_write[0].pos = outer_verts[0]; vtx_write[0].uv = ImVec2(a_min_uv.x, a_min_uv.y); vtx_write[0].col = col;
        vtx_write[1].pos = outer_verts[1]; vtx_write[1].uv = ImVec2(a_max_uv.x, a_min_uv.y); vtx_write[1].col = col;
        vtx_write[2].pos = outer_verts[2]; vtx_write[2].uv = ImVec2(a_max_uv.x, a_max_uv.y); vtx_write[2].col = col;
        vtx_write[3].pos = outer_verts[3]; vtx_write[3].uv = ImVec2(a_min_uv.x, a_max_uv.y); vtx_write[3].col = col;

        draw_list->_VtxCurrentIdx += num_b_points + 4;
        draw_list->_VtxWritePtr += num_b_points + 4;

        ImVec2 last_inner_vert = b_points[num_b_points - 1];
        int last_inner_vert_idx = num_b_points - 1;
        int last_outer_vert_idx = -1;
        int first_outer_vert_idx = -1;

#define IS_DEGENERATE(a, b, c) (ImFabs((((a).x * ((b).y - (c).y)) + ((b).x * ((c).y - (a).y)) + ((c).x * ((a).y - (b).y)))) < (0.1f * 2.0f))

        int outer_vertex_winding = (((b_points[0].x * (b_points[1].y - b_points[2].y)) + (b_points[1].x * (b_points[2].y - b_points[0].y)) + (b_points[2].x * (b_points[0].y - b_points[1].y))) < 0.0f) ? -1 : 1;
        for (int inner_vert_idx = 0; inner_vert_idx < num_b_points; inner_vert_idx++)
        {
            ImVec2 current_inner_vert = b_points[inner_vert_idx];

            ImVec2 normal(current_inner_vert.y - last_inner_vert.y, -(current_inner_vert.x - last_inner_vert.x));

            int outer_vert_idx = (ImFabs(normal.x) > ImFabs(normal.y)) ? ((normal.x >= 0.0f) ? ((normal.y > 0.0f) ? 2 : 1) : ((normal.y > 0.0f) ? 3 : 0)) : ((normal.y >= 0.0f) ? ((normal.x > 0.0f) ? 2 : 3) : ((normal.x > 0.0f) ? 1 : 0));
            ImVec2 outer_vert = outer_verts[outer_vert_idx];

            if (!IS_DEGENERATE(last_inner_vert, current_inner_vert, outer_vert))
            {
                idx_write[0] = (ImDrawIdx)(inner_idx + last_inner_vert_idx);
                idx_write[1] = (ImDrawIdx)(inner_idx + inner_vert_idx);
                idx_write[2] = (ImDrawIdx)(outer_idx + outer_vert_idx);
                idx_write += 3;
            }

            if (first_outer_vert_idx == -1)
            {
                first_outer_vert_idx = outer_vert_idx;
                last_outer_vert_idx = outer_vert_idx;
            }

            while (outer_vert_idx != last_outer_vert_idx)
            {
                int next_outer_vert_idx = (last_outer_vert_idx + outer_vertex_winding) & 3;
                if (!IS_DEGENERATE(outer_verts[last_outer_vert_idx], outer_verts[next_outer_vert_idx], last_inner_vert))
                {
                    idx_write[0] = (ImDrawIdx)(outer_idx + last_outer_vert_idx);
                    idx_write[1] = (ImDrawIdx)(outer_idx + next_outer_vert_idx);
                    idx_write[2] = (ImDrawIdx)(inner_idx + last_inner_vert_idx);
                    idx_write += 3;
                }
                last_outer_vert_idx = next_outer_vert_idx;
            }

            last_inner_vert = current_inner_vert;
            last_inner_vert_idx = inner_vert_idx;
        }

        if (first_outer_vert_idx != -1)
        {
            while (first_outer_vert_idx != last_outer_vert_idx)
            {
                int next_outer_vert_idx = (last_outer_vert_idx + outer_vertex_winding) & 3;
                if (!IS_DEGENERATE(outer_verts[last_outer_vert_idx], outer_verts[next_outer_vert_idx], last_inner_vert))
                {
                    idx_write[0] = (ImDrawIdx)(outer_idx + last_outer_vert_idx);
                    idx_write[1] = (ImDrawIdx)(outer_idx + next_outer_vert_idx);
                    idx_write[2] = (ImDrawIdx)(inner_idx + last_inner_vert_idx);
                    idx_write += 3;
                }
                last_outer_vert_idx = next_outer_vert_idx;
            }
        }
#undef IS_DEGENERATE

        int used_indices = (int)(idx_write - draw_list->_IdxWritePtr);
        draw_list->_IdxWritePtr = idx_write;
        draw_list->PrimUnreserve(max_indices - used_indices, 0);
    }
}

void c_draw::shadow_rect(ImDrawList* draw_list, const ImVec2& obj_min, const ImVec2& obj_max, ImU32 shadow_col, float shadow_thickness, const ImVec2& shadow_offset, ImDrawFlags flags, float obj_rounding) {}

void c_draw::shadow_convex_poly(ImDrawList* draw_list, const ImVec2* points, int points_count, ImU32 shadow_col, float shadow_thickness, const ImVec2& shadow_offset, ImDrawFlags flags) {}

void c_draw::shadow_circle(ImDrawList* draw_list, const ImVec2& obj_center, float obj_radius, ImU32 shadow_col, float shadow_thickness, const ImVec2& shadow_offset, ImDrawFlags flags, int num_segments) {}

void c_draw::shadow_ngon(ImDrawList* draw_list, const ImVec2& obj_center, float obj_radius, ImU32 shadow_col, float shadow_thickness, const ImVec2& shadow_offset, ImDrawFlags flags, int num_segments) {}

int rotation_start_index;
void c_draw::rotate_start()
{
    rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

ImVec2 rotate_center()
{
    ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);

    const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);

    return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
}

void c_draw::rotate_end(float rad, ImVec2 center)
{
    if (center.x == 0 && center.y == 0) center = rotate_center();

    float s = sin(rad), c = cos(rad);
    center = ImRotate(center, s, c) - center;

    auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

float c_draw::deg_to_rad(float deg)
{
    return deg / 57.3f;
}

void c_draw::push_clip_rect(ImDrawList* draw_list, const ImVec2& clip_rect_min, const ImVec2& clip_rect_max, bool intersect_with_current_clip_rect)
{
    ImVec4 cr(clip_rect_min.x, clip_rect_min.y, clip_rect_max.x, clip_rect_max.y);
    if (intersect_with_current_clip_rect)
    {
        ImVec4 current = draw_list->_CmdHeader.ClipRect;
        if (cr.x < current.x) cr.x = current.x;
        if (cr.y < current.y) cr.y = current.y;
        if (cr.z > current.z) cr.z = current.z;
        if (cr.w > current.w) cr.w = current.w;
    }
    cr.z = ImMax(cr.x, cr.z);
    cr.w = ImMax(cr.y, cr.w);

    draw_list->_ClipRectStack.push_back(cr);
    draw_list->_CmdHeader.ClipRect = cr;
    draw_list->_OnChangedClipRect();
}

void c_draw::pop_clip_rect(ImDrawList* draw_list)
{
    draw_list->_ClipRectStack.pop_back();
    draw_list->_CmdHeader.ClipRect = (draw_list->_ClipRectStack.Size == 0) ? draw_list->_Data->ClipRectFullscreen : draw_list->_ClipRectStack.Data[draw_list->_ClipRectStack.Size - 1];
    draw_list->_OnChangedClipRect();
}

void c_draw::window_decorations()
{
    draw->rect(GetBackgroundDrawList(), GetWindowPos() - ImVec2(1, 1), GetWindowPos() + GetWindowSize() + ImVec2(1, 1), draw->get_clr({0, 0, 0, var->window.window_glow ? 0.5f : 0.0f}));

    if (var->window.window_gradients)
        draw->fade_rect_filled(GetWindowDrawList(), GetWindowPos() + ImVec2(0, var->window.titlebar), GetWindowPos() + GetWindowSize(), draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);
    else
        draw->rect_filled(GetWindowDrawList(), GetWindowPos() + ImVec2(0, var->window.titlebar), GetWindowPos() + GetWindowSize(), draw->get_clr(clr->window.background_one));

    if (var->window.window_gradients)
        draw->fade_rect_filled(GetWindowDrawList(), GetWindowPos(), GetWindowPos() + ImVec2(GetWindowWidth(), var->window.titlebar), draw->get_clr(clr->window.background_one), draw->get_clr(clr->window.background_two), fade_direction::vertically);
    else
        draw->rect_filled(GetWindowDrawList(), GetWindowPos(), GetWindowPos() + ImVec2(GetWindowWidth(), var->window.titlebar), draw->get_clr(clr->window.background_one));

    draw->rect(GetWindowDrawList(), GetWindowPos(), GetWindowPos() + GetWindowSize(), draw->get_clr(clr->accent));
    draw->rect_filled(GetWindowDrawList(), GetWindowPos() + ImVec2(6, var->window.titlebar), GetWindowPos() + GetWindowSize() - ImVec2(6, 6), draw->get_clr(clr->window.background_one));
    draw->rect(GetWindowDrawList(), GetWindowPos() + ImVec2(6, var->window.titlebar), GetWindowPos() + GetWindowSize() - ImVec2(6, 6), draw->get_clr(clr->window.stroke));
    
    ImGuiWindow* w = GetCurrentWindow();
    const char* win_name = w->Name;
    const char* title_end = ImGui::FindRenderedTextEnd(win_name);
    std::string clean_title(win_name, title_end);
    draw->text_outline(GetWindowDrawList(), var->font.tahoma, var->font.tahoma->FontSize, GetWindowPos() + ImVec2(7, 4), draw->get_clr(clr->accent), clean_title.c_str());

    // Titlebar Close 'x' button for all windows
    ImVec2 wp = GetWindowPos();
    ImVec2 ws = GetWindowSize();
    float th = var->window.titlebar;

    ImRect cr(wp + ImVec2(ws.x - 22, (th - 14) * 0.5f), wp + ImVec2(ws.x - 8, (th + 14) * 0.5f));
    ImGuiID cid = w->GetID("titlebar_close");
    bool hovered = false, held = false;
    if (ItemAdd(cr, cid, NULL, ImGuiItemFlags_NoNav))
    {
        bool pressed = ButtonBehavior(cr, cid, &hovered, &held, ImGuiButtonFlags_FlattenChildren | ImGuiButtonFlags_NoNavFocus);
        if (hovered) SetMouseCursor(ImGuiMouseCursor_Hand);

        ImU32 color = draw->get_clr(hovered ? clr->accent : clr->widgets.text);

        ImVec2 text_size = var->font.tahoma->CalcTextSizeA(var->font.tahoma->FontSize, FLT_MAX, 0.0f, "x");
        ImVec2 text_pos = cr.Min + (cr.GetSize() - text_size) * 0.5f;
        text_pos.y -= 1.0f;

        draw->text_outline(GetWindowDrawList(), var->font.tahoma, var->font.tahoma->FontSize, text_pos, color, "x");

        if (pressed)
        {
            if (strcmp(win_name, "Home") == 0) var->gui.current_section[0] = false;
            else if (strcmp(win_name, "Players") == 0) var->gui.current_section[1] = false;
            else if (strcmp(win_name, "Viewer") == 0) var->gui.current_section[2] = false;
            else if (strcmp(win_name, "Explorer") == 0) var->gui.current_section[3] = false;
            else if (strcmp(win_name, "Code") == 0) var->gui.current_section[4] = false;
            else if (strcmp(win_name, "Style") == 0) var->gui.current_section[5] = false;
            else if (strcmp(win_name, "Configs") == 0) var->gui.current_section[8] = false;
            else if (strcmp(win_name, "Waypoints") == 0) var->gui.current_section[7] = false;
            else if (strcmp(win_name, "Server Browser") == 0) var->gui.current_section[6] = false;
            else if (strcmp(win_name, "NPC Manager") == 0) var->gui.current_section[9] = false;
        }
    }
}