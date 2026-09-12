#include "animations.h"

namespace gui_animation
{
    void DrawHeaderAccentLine(ImGuiWindow* window, ImRect window_bb, ImVec4 accent_color, bool shine_enabled, float shine_speed, float shine_width, float shine_color[4])
    {
        float line_y0 = window_bb.Min.y + 24.0f;
        float line_y1 = window_bb.Min.y + 27.0f;

        window->DrawList->AddRectFilled(
            ImVec2(window_bb.Min.x, line_y0),
            ImVec2(window_bb.Max.x, line_y1),
            ImGui::GetColorU32(accent_color)
        );

        if (shine_enabled)
        {
            static float s_shineX = -shine_width;
            s_shineX += shine_speed * 200.0f * ImGui::GetIO().DeltaTime;
            float bar_width = window_bb.Max.x - window_bb.Min.x;
            if (s_shineX > bar_width + shine_width)
                s_shineX = -shine_width;

            float shine_left = window_bb.Min.x + s_shineX - shine_width * 0.5f;
            float shine_right = window_bb.Min.x + s_shineX + shine_width * 0.5f;

            shine_left = ImClamp(shine_left, window_bb.Min.x, window_bb.Max.x);
            shine_right = ImClamp(shine_right, window_bb.Min.x, window_bb.Max.x);

            ImU32 col_full = ImGui::GetColorU32(ImVec4(shine_color[0], shine_color[1], shine_color[2], shine_color[3]));
            ImU32 col_clear = ImGui::GetColorU32(ImVec4(shine_color[0], shine_color[1], shine_color[2], 0.0f));

            float mid_x = (shine_left + shine_right) * 0.5f;

            window->DrawList->AddRectFilledMultiColor(
                ImVec2(shine_left, line_y0), ImVec2(mid_x, line_y1),
                col_clear, col_full, col_full, col_clear
            );
            window->DrawList->AddRectFilledMultiColor(
                ImVec2(mid_x, line_y0), ImVec2(shine_right, line_y1),
                col_full, col_clear, col_clear, col_full
            );
        }
    }

    struct ButtonAnimState { 
        ImGuiID id; 
        float time; 
        ImVec2 pos; 
    };

    void DrawButtonRipple(ImGuiID id, const ImRect& bb, bool pressed)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImGuiStyle& style = g.Style;
        static ImVector<ButtonAnimState> s_buttonAnims;

        if (pressed) {
            bool found = false;
            for (int n = 0; n < s_buttonAnims.Size; n++) {
                if (s_buttonAnims[n].id == id) {
                    s_buttonAnims[n].time = 0.0f;
                    s_buttonAnims[n].pos = g.IO.MousePos;
                    found = true;
                    break;
                }
            }
            if (!found) {
                s_buttonAnims.push_back({ id, 0.0f, g.IO.MousePos });
            }
        }

        for (int n = 0; n < s_buttonAnims.Size; n++) {
            if (s_buttonAnims[n].id == id) {
                s_buttonAnims[n].time += g.IO.DeltaTime * 4.0f;
                float progress = s_buttonAnims[n].time;
                if (progress <= 1.0f) {
                    float max_radius = ImMax(bb.GetWidth(), bb.GetHeight()) * 1.2f;
                    float radius = progress * max_radius;
                    float alpha = (1.0f - progress) * 0.35f * style.Alpha;
                    window->DrawList->PushClipRect(bb.Min, bb.Max, true);
                    window->DrawList->AddCircleFilled(s_buttonAnims[n].pos, radius, ImColor(1.0f, 1.0f, 1.0f, alpha), 32);
                    window->DrawList->PopClipRect();
                } else {
                    s_buttonAnims.erase(s_buttonAnims.Data + n);
                    n--;
                }
            }
        }
    }

    void AnimatedWindow::Open() {
        m_bOpen = true;
        m_bClosing = false;
        m_flFadeAlpha = 0.0f;
    }

    void AnimatedWindow::Close() {
        m_bClosing = true;
    }

    bool AnimatedWindow::Begin(const char* name, ImVec2 target_pos, ImVec2 target_size, ImGuiWindowFlags flags)
    {
        if (!m_bOpen && m_flFadeAlpha <= 0.0f)
            return false;

        if (m_bClosing) {
            m_flFadeAlpha -= ImGui::GetIO().DeltaTime * 5.0f;
            if (m_flFadeAlpha <= 0.0f) {
                m_flFadeAlpha = 0.0f;
                m_bOpen = false;
                m_bClosing = false;
                return false;
            }
        } else if (m_flFadeAlpha < 1.0f) {
            m_flFadeAlpha += ImGui::GetIO().DeltaTime * 5.0f;
            if (m_flFadeAlpha > 1.0f)
                m_flFadeAlpha = 1.0f;
        }

        float scale = 0.92f + (m_flFadeAlpha * 0.08f);
        ImVec2 center = ImVec2(target_pos.x + target_size.x * 0.5f, target_pos.y + target_size.y * 0.5f);
        ImVec2 anim_size = ImVec2(target_size.x * scale, target_size.y * scale);
        ImVec2 anim_pos = ImVec2(center.x - anim_size.x * 0.5f, center.y - anim_size.y * 0.5f);

        ImGui::SetNextWindowPos(anim_pos);
        ImGui::SetNextWindowSize(anim_size);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_flFadeAlpha);

        if (ImGui::Begin(name, &m_bOpen, flags))
        {
            return true;
        }

        ImGui::PopStyleVar();
        return false;
    }

    void AnimatedWindow::End()
    {
        ImGui::End();
        ImGui::PopStyleVar();
    }
}
