#include "imgui_notification.h"
#include <cstdarg>
#include <cstdio>

void ImNotify::Print(NotifyLevel type, const char* text, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, text);
    vsnprintf(buffer, sizeof(buffer), text, args);
    va_end(args);

    mNotifications.emplace_back(buffer);
    mNotificationsType.push_back(type);
    mNotificationTimers.push_back(fWaitTime);
}

// Helper
ImVec4 HexToColorVec4(unsigned int hex_color, float alpha)
{
    ImVec4 color;
    color.x = ((hex_color >> 16) & 0xFF) / 255.0f;
    color.y = ((hex_color >> 8) & 0xFF) / 255.0f;
    color.z = (hex_color & 0xFF) / 255.0f;
    color.w = alpha;
    return color;
}

void ImNotify::Update()
{
    if (!mNotifications.empty())
    {
        for (size_t i = 0; i < mNotificationTimers.size(); i++)
        {
            mNotificationTimers[i] -= ImGui::GetIO().DeltaTime;
            if (mNotificationTimers[i] <= 0.0f)
            {
                mNotifications.erase(mNotifications.begin() + i);
                mNotificationsType.erase(mNotificationsType.begin() + i);
                mNotificationTimers.erase(mNotificationTimers.begin() + i);
                i--;
            }
        }
    }
}

void ImNotify::Render()
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiIO& io = g.IO;

    float windowHeight = ImGui::GetFontSize() + style.WindowPadding.y * 2.0f;
    float spacing = style.DisplayWindowPadding.y;
    int count = (int)mNotifications.size();

    for (int i = 0; i < count; i++)
    {
        const auto& notification = mNotifications[i];
        NotifyLevel type = mNotificationsType[i];
        ImVec4 mainColor;

        switch (type)
        {
        case NotifyLevel::Info:    mainColor = HexToColorVec4(0x40A2ED, 1.f); break;
        case NotifyLevel::Success: mainColor = HexToColorVec4(0x15961e, 1.f); break;
        case NotifyLevel::Warn:    mainColor = HexToColorVec4(0xED9B40, 1.f); break;
        case NotifyLevel::Error:   mainColor = HexToColorVec4(0xD64550, 1.f); break;
        default:                   mainColor = HexToColorVec4(0xd4d4d4, 1.f); break;
        }

        float delta = ImClamp(mNotificationTimers[i] / fWaitTime, 0.0f, 1.0f);

        ImVec2 notification_size = ImVec2(
            ImGui::CalcTextSize(notification.c_str()).x + style.WindowPadding.x * 2.0f,
            windowHeight
        );

        // Stack upward from bottom center, newest at bottom
        int stackIndex = count - 1 - i;
        float posX = style.DisplayWindowPadding.x;
        float posY = io.DisplaySize.y
                     - style.DisplayWindowPadding.y
                     - windowHeight
                     - stackIndex * (windowHeight + spacing);

        ImGui::SetNextWindowPos(ImVec2(posX, posY));
        ImGui::SetNextWindowSize(notification_size);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool notification_window = ImGui::Begin(
            ("Notification" + std::to_string(i)).c_str(), nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground
        );
        ImGui::PopStyleVar(2);

        if (notification_window)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ImRect bb = window->Rect();
            ImDrawList* dl = window->DrawList;
            float bsz = ImMax(style.WindowBorderSize, 1.0f);
            ImU32 col = ImGui::GetColorU32(mainColor);

            dl->PushClipRectFullScreen();

            // Background — same as child panels (ChildBg fill + double border)
            dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_ChildBg));
            // Outer border (BorderShadow = black) first, inner (Border = grey) on top
            dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Border), 0.0f, ImDrawFlags_None, bsz);
            dl->AddRect(bb.Min + ImVec2(bsz, bsz), bb.Max - ImVec2(bsz, bsz), ImGui::GetColorU32(ImGuiCol_BorderShadow), 0.0f, ImDrawFlags_None, bsz);

            // Left accent line — full height, always visible
            dl->AddLine(
                ImVec2(bb.Min.x + bsz * 0.5f, bb.Min.y),
                ImVec2(bb.Min.x + bsz * 0.5f, bb.Max.y),
                col, bsz * 2.0f
            );

            // Bottom progress line — shrinks right to left as timer runs out
            float lineRight = bb.Min.x + (bb.Max.x - bb.Min.x) * delta;
            if (lineRight > bb.Min.x)
            {
                dl->AddLine(
                    ImVec2(bb.Min.x, bb.Max.y - bsz * 0.5f),
                    ImVec2(lineRight, bb.Max.y - bsz * 0.5f),
                    col, bsz * 2.0f
                );
            }

            dl->PopClipRect();

            // Text
            ImAdd::RenderText(
                bb.Min + ImVec2(style.WindowPadding.x, ImTrunc((bb.GetHeight() - ImGui::GetFontSize()) / 2.0f)),
                notification.c_str(), nullptr, false, true
            );
        }
        ImGui::End();
    }
}
