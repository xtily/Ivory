#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace gui_animation
{
    struct TabAnimationState {
        int prev_tab = -1;
        int active_tab = -1;
        float progress = 1.0f;

        void update(int current_tab, float speed = 6.5f) {
            if (current_tab != prev_tab) {
                prev_tab = current_tab;
                progress = 0.0f;
                active_tab = current_tab;
            }
            if (progress < 1.0f) {
                progress += ImGui::GetIO().DeltaTime * speed;
                if (progress > 1.0f)
                    progress = 1.0f;
            }
        }

        float get_alpha() const {
            float t = 1.0f - progress;
            return ImClamp(1.0f - t * t * t, 0.05f, 1.0f);
        }

        float get_slide_offset(float max_offset = 8.0f) const {
            return (1.0f - get_alpha()) * max_offset;
        }
    };

    void DrawHeaderAccentLine(ImGuiWindow* window, ImRect window_bb, ImVec4 accent_color, bool shine_enabled, float shine_speed, float shine_width, float shine_color[4]);

    void DrawButtonRipple(ImGuiID id, const ImRect& bb, bool pressed);

    class AnimatedWindow
    {
    public:
        bool m_bOpen = false;
        bool m_bClosing = false;
        float m_flFadeAlpha = 0.0f;

        void Open();
        void Close();
        bool Begin(const char* name, ImVec2 target_pos, ImVec2 target_size, ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        void End();
    };
}
