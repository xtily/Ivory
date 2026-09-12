#pragma once

#include <vector>
#include <string>

#include "../imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "../imgui_internal.h"

#include "imgui_addons.h"

enum class NotifyLevel
{
    Success,
    Info,
    Warn,
    Error,
    None,
};

class ImNotify
{
public:
    static void Print(NotifyLevel type/*, ImTextureID icon*/, const char* text, ...);
    static void Update();
    static void Render();

    inline static float fWaitTime = 5.0f;

private:
    inline static std::vector<std::string> mNotifications;
    inline static std::vector<NotifyLevel> mNotificationsType;
    inline static std::vector<float> mNotificationTimers;
    inline static float mNotificationTimer = 0.0f;
};
