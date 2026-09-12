//============ Copyright ImMagic, All rights reserved ============//
//
// Purpose: 
//
//================================================================//

#include "Menu.h"

// Dear ImGui
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// Dear ImGui - Backends
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

// Dear ImGui - Addons
#include "imgui/addons/imgui_notification.h"
#include "imgui/addons/imgui_addons.h"

extern float g_AnimationSpeed;

// Dear ImGui - Misc
#include "imgui/misc/imgui_freetype.h"


#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <features/system/settings/settings.h>
#include <features/system/config/config.h>
#include <features/visuals/model/modelviewer.h>
#include <features/system/playerlist/playerlist.h>
#include <features/exploits/character/rivals_skinchanger/rivals_skinchanger.h>
#include <features/system/performance/performance.h>
#include <features/system/keybind/keybind.h>
#include <features/combat/aimbot/aimbot.h>
#include <features/combat/silent/mouse/mouse.h>
#include <features/combat/silent/raycast/raycast.h>
#include <sdk/cache/core/cache.h>
#include <ui/render/render.h>

#include "assets/nav_icons.h"
#include "assets/font_main.h"

#define PROJECT_NAME    "Ivory"

static int g_linoria_tab_index = 0;

static void DrawFramedGradient(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    ImGuiStyle& style = ImGui::GetStyle();
    const float alpha = style.Alpha;
    const ImU32 outline = IM_COL32(0, 0, 0, (int)(255 * alpha));
    const ImU32 inline_col = IM_COL32(54, 54, 54, (int)(255 * alpha));
    const ImU32 top = IM_COL32(38, 38, 38, (int)(255 * alpha));
    const ImU32 bot = IM_COL32(19, 19, 19, (int)(255 * alpha));

    dl->AddRectFilled(min, max, outline);
    dl->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), inline_col);

    ImDrawListFlags backup_flags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    dl->AddRectFilledMultiColor(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), top, top, bot, bot);
    dl->Flags = backup_flags;
}

static void draw_outlined_text(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text)
{
    ImGuiStyle& style = ImGui::GetStyle();
    const ImU32 outline = IM_COL32(0, 0, 0, (int)(255 * style.Alpha));
    dl->AddText(ImVec2(pos.x - 1, pos.y - 1), outline, text);
    dl->AddText(ImVec2(pos.x + 1, pos.y - 1), outline, text);
    dl->AddText(ImVec2(pos.x - 1, pos.y + 1), outline, text);
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), outline, text);
    dl->AddText(ImVec2(pos.x - 1, pos.y), outline, text);
    dl->AddText(ImVec2(pos.x + 1, pos.y), outline, text);
    dl->AddText(ImVec2(pos.x, pos.y - 1), outline, text);
    dl->AddText(ImVec2(pos.x, pos.y + 1), outline, text);
    dl->AddText(pos, col, text);
}

static bool DrawNavButton(
    ImDrawList* dl,
    ImVec2 pos,
    ImVec2 size,
    const char* label,
    bool open,
    int id)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##nav", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGui::PopID();

    DrawFramedGradient(dl, pos, pos + size);

    const float alpha = ImGui::GetStyle().Alpha;
    if (hovered)
        dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 2.0f), ImVec2(pos.x + size.x - 2.0f, pos.y + size.y - 2.0f), IM_COL32(255, 255, 255, (int)(13 * alpha)));
    if (active)
        dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 2.0f), ImVec2(pos.x + size.x - 2.0f, pos.y + size.y - 2.0f), IM_COL32(0, 0, 0, (int)(26 * alpha)));

    ImGuiStyle& style = ImGui::GetStyle();
    const ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Header]);
    const ImU32 text_main = IM_COL32(160, 160, 160, (int)(255 * alpha));
    const ImU32 text_misc = IM_COL32(80, 80, 80, (int)(255 * alpha));

    const ImU32 text_col = open ? accent_col : (hovered ? text_main : text_misc);
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 text_pos(
        pos.x + (size.x - text_size.x) * 0.5f,
        pos.y + (size.y - text_size.y) * 0.5f - 1.0f);
    draw_outlined_text(dl, text_pos, text_col, label);

    return clicked;
}

void Menu::DrawNavigationBar()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    const float bar_w = io.DisplaySize.x;
    constexpr float bar_h = 26.0f;
    constexpr float kBtnHeight = 20.0f;
    constexpr float kBtnPadX = 6.0f;
    constexpr float kBtnSpacing = 3.0f;
    constexpr float kTextBtnGap = 12.0f;

    ImGui::SetNextWindowSize(ImVec2(bar_w, bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##TopBarOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 p0 = origin;
        const ImVec2 p1 = origin + ImVec2(bar_w, bar_h);

        DrawFramedGradient(dl, p0, p1);

        time_t t = time(nullptr);
        tm tm_info{};
        localtime_s(&tm_info, &t);
        char date_str[64];

        strftime(date_str, sizeof(date_str), "%b %d, %Y | %I:%M %p", &tm_info);
        for (char* p = date_str; *p; ++p) *p = (char)tolower((unsigned char)*p);

        const char* prefix = "ivory ";

        const ImVec2 sz_prefix = ImGui::CalcTextSize(prefix);
        const ImVec2 sz_date   = ImGui::CalcTextSize(date_str);

        const float text_y = origin.y + (bar_h - sz_prefix.y) * 0.5f - 1.0f;
        float cur_text_x = origin.x + 8.0f;

        const ImU32 text_main = IM_COL32(160, 160, 160, (int)(255 * style.Alpha));

        draw_outlined_text(dl, ImVec2(cur_text_x, text_y), text_main, prefix);
        cur_text_x += sz_prefix.x;

        draw_outlined_text(dl, ImVec2(cur_text_x, text_y), text_main, date_str);
        cur_text_x += sz_date.x;

        float btn_x = cur_text_x + kTextBtnGap;
        const float btn_y = origin.y + (bar_h - kBtnHeight) * 0.5f;

        struct NavTab {
            const char* label;
            bool* state;
        };

        NavTab nav_tabs[] = {
            { "home", &m_bShowMenu },
            { "themes", &m_bShowSettings },
            { "configs", &m_bShowConfig },
            { "preview", &m_bShowESPPreview },
            { "skins", &m_bShowRivalsSkinChanger },
            { "indicator", &m_bShowIndicator },
            { "watermark", &m_bShowWatermark },
            { "keybind list", &m_bShowKeybinds },
            { "explorer", &settings::misc::explorer_window },
            { "performance", &settings::misc::performance_window }
        };

        for (int i = 0; i < IM_ARRAYSIZE(nav_tabs); i++)
        {
            const ImVec2 label_size = ImGui::CalcTextSize(nav_tabs[i].label);
            const ImVec2 btn_size(label_size.x + kBtnPadX * 2.0f, kBtnHeight);
            const ImVec2 btn_pos(btn_x, btn_y);
            const bool open = *nav_tabs[i].state;

            if (DrawNavButton(dl, btn_pos, btn_size, nav_tabs[i].label, open, i + 900))
            {
                *nav_tabs[i].state = !(*nav_tabs[i].state);
                if (nav_tabs[i].state == &m_bShowKeybinds)
                {
                    settings::misc::keybind_list = m_bShowKeybinds;
                }
                if (nav_tabs[i].state == &settings::misc::explorer_window)
                {
                    m_bShowExplorer = settings::misc::explorer_window;
                }
            }

            btn_x += btn_size.x + kBtnSpacing;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}




void Menu::ApplyTheme(int index)
{
    ImGuiStyle& style = ImGui::GetStyle();

    auto SyncDerived = [&]() {
        style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
        style.Colors[ImGuiCol_SliderGrab] = style.Colors[ImGuiCol_Header];
        style.Colors[ImGuiCol_SliderGrabActive] = style.Colors[ImGuiCol_HeaderActive];
        style.Colors[ImGuiCol_Button] = style.Colors[ImGuiCol_FrameBg];
        style.Colors[ImGuiCol_ButtonHovered] = style.Colors[ImGuiCol_FrameBgHovered];
        style.Colors[ImGuiCol_ButtonActive] = style.Colors[ImGuiCol_FrameBgActive];
        style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_FrameBg];
        style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_FrameBgHovered];
        style.Colors[ImGuiCol_TabActive] = style.Colors[ImGuiCol_FrameBgActive];
        style.Colors[ImGuiCol_ButtonShadow] = style.Colors[ImGuiCol_FrameBgShadow];
        style.Colors[ImGuiCol_ScrollbarGrab] = style.Colors[ImGuiCol_Header];
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_Header];
        style.Colors[ImGuiCol_ScrollbarGrabActive] = style.Colors[ImGuiCol_HeaderActive];
        };

    struct Theme { ImVec4 outline, inlin, accent, high_contrast, low_contrast, text; };
    auto rgb = [](int r, int g, int b) -> ImVec4 { return ImVec4(r / 255.f, g / 255.f, b / 255.f, 1.f); };

    if (index == 0)
    {
        style.Colors[ImGuiCol_WindowBg] = ImAdd::HexToColorVec4(0x1e1e1e);
        style.Colors[ImGuiCol_ChildBg] = ImAdd::HexToColorVec4(0x141414);
        style.Colors[ImGuiCol_PopupBg] = ImAdd::HexToColorVec4(0x1c1c1c);
        style.Colors[ImGuiCol_Text] = ImAdd::HexToColorVec4(0xffffff);
        style.Colors[ImGuiCol_TextDisabled] = ImAdd::HexToColorVec4(0x848484);
        style.Colors[ImGuiCol_Border] = ImAdd::HexToColorVec4(0x323232);
        style.Colors[ImGuiCol_BorderShadow] = ImAdd::HexToColorVec4(0x000000);
        style.Colors[ImGuiCol_FrameBg] = ImAdd::HexToColorVec4(0x1e1e1e);
        style.Colors[ImGuiCol_FrameBgHovered] = ImAdd::HexToColorVec4(0x202020);
        style.Colors[ImGuiCol_FrameBgActive] = ImAdd::HexToColorVec4(0x1c1c1c);
        style.Colors[ImGuiCol_FrameBgShadow] = ImAdd::HexToColorVec4(0x000000, 0.3f);
        style.Colors[ImGuiCol_Header] = ImAdd::HexToColorVec4(0xffffff);
        style.Colors[ImGuiCol_HeaderHovered] = ImAdd::HexToColorVec4(0xffffff);
        style.Colors[ImGuiCol_HeaderActive] = ImAdd::HexToColorVec4(0xffffff);
        SyncDerived();
        return;
    }

    static const Theme themes[] = {
        // 0  Purple (og default)
        { rgb(0,0,0),    rgb(30,30,30),  {131 / 255.f,131 / 255.f,219 / 255.f,1}, {13 / 255.f,10 / 255.f,23 / 255.f,1},  {13 / 255.f,10 / 255.f,23 / 255.f,1},  rgb(180,180,180) },
        { rgb(0,0,0),    rgb(50,50,50),  rgb(103,89,179),   rgb(22,22,31),   rgb(25,25,37),   rgb(255,255,255) },
        // 2  Lagoon
        { rgb(0,0,0),    rgb(44,54,90),  rgb(41,92,168),    rgb(32,35,51),   rgb(38,43,60),   rgb(255,255,255) },
        // 3  Primordial
        { rgb(0,0,0),    rgb(67,67,67),  rgb(194,155,165),  rgb(31,31,31),   rgb(21,21,21),   rgb(255,255,255) },
        // 4  Jester
        { rgb(0,0,0),    rgb(55,55,55),  rgb(219,68,103),   rgb(28,28,28),   rgb(36,36,36),   rgb(255,255,255) },
        // 5  Temple
        { rgb(10,10,10), rgb(45,45,45),  {220 / 255.f,142 / 255.f,240 / 255.f,1}, rgb(28,28,28),   rgb(28,28,28),   rgb(180,180,180) },
        // 6  Nebula
        { rgb(13,1,6),   rgb(32,8,18),   {150 / 255.f,42 / 255.f,85 / 255.f,1},   rgb(24,5,13),    rgb(18,4,10),    rgb(180,180,180) },
        // 7  Abyss
        { rgb(10,10,10), rgb(45,45,45),  rgb(140,135,180),  rgb(30,30,30),   rgb(20,20,20),   rgb(255,255,255) },
        // 8  Fatality
        { rgb(15,15,40), rgb(50,40,80),  rgb(240,15,80),    rgb(35,25,70),   rgb(25,20,50),   rgb(200,200,255) },
        // 9  Neverlose
        { rgb(0,0,5),    rgb(10,30,40),  rgb(0,180,240),    rgb(0,15,30),    rgb(5,5,20),     rgb(255,255,255) },
        // 10 Aimware
        { rgb(0,0,5),    rgb(55,55,55),  rgb(200,40,40),    rgb(43,43,43),   rgb(25,25,25),   rgb(232,232,232) },
        // 11 Youtube
        { rgb(0,0,0),    rgb(57,57,57),  rgb(255,0,0),      rgb(35,35,35),   rgb(15,15,15),   rgb(241,241,241) },
        // 12 Gamesense
        { rgb(0,0,0),    rgb(40,40,40),  rgb(167,217,77),   rgb(23,23,23),   rgb(12,12,12),   rgb(255,255,255) },
        // 13 Onetap
        { rgb(0,0,0),    rgb(78,81,88),  rgb(221,168,93),   rgb(44,48,55),   rgb(31,33,37),   rgb(214,217,224) },
        // 14 Entropy
        { rgb(10,10,10), rgb(76,74,82),  rgb(129,187,233),  rgb(61,58,67),   rgb(48,47,55),   rgb(220,220,220) },
        // 15 Interwebz
        { rgb(26,26,26), rgb(64,54,79),  rgb(201,101,75),   rgb(41,31,56),   rgb(31,22,43),   rgb(252,252,252) },
        // 16 Dracula
        { rgb(32,33,38), rgb(60,56,77),  rgb(154,129,179),  rgb(42,44,56),   rgb(37,39,48),   rgb(220,220,220) },
        // 17 Spotify
        { rgb(10,10,10), rgb(41,41,41),  rgb(30,215,96),    rgb(24,24,24),   rgb(18,18,18),   rgb(208,208,208) },
        // 18 Sublime
        { rgb(0,0,0),    rgb(72,73,72),  rgb(255,152,0),    rgb(50,51,45),   rgb(40,41,35),   rgb(232,255,255) },
        // 19 Vape
        { rgb(10,10,10), rgb(54,54,54),  rgb(38,134,106),   rgb(31,31,31),   rgb(26,26,26),   rgb(220,220,220) },
        // 20 Neko
        { rgb(0,0,0),    rgb(45,45,45),  rgb(210,31,106),   rgb(23,23,23),   rgb(19,19,19),   rgb(255,255,255) },
        // 21 Corn
        { rgb(0,0,0),    rgb(51,51,51),  rgb(255,144,0),    rgb(37,37,37),   rgb(25,25,25),   rgb(220,220,220) },
        // 22 Minecraft
        { rgb(0,0,0),    rgb(51,51,51),  rgb(39,206,64),    rgb(51,51,51),   rgb(38,38,38),   rgb(255,255,255) },
        // 23 Nord
        { rgb(46,52,64), rgb(59,66,82),  rgb(136,192,208),  rgb(67,76,94),   rgb(54,60,74),   rgb(236,239,244) },
        // 24 Monokai
        { rgb(20,20,20), rgb(39,40,34),  rgb(249,38,114),   rgb(50,50,50),   rgb(30,30,30),   rgb(248,248,242) },
        // 25 Obsidian
        { rgb(5,5,5),    rgb(28,28,28),  rgb(120,130,150),  rgb(38,38,38),   rgb(18,18,18),   rgb(210,210,210) },
        // 26 RosePine
        { rgb(25,23,36), rgb(31,29,46),  rgb(235,188,186),  rgb(40,38,58),   rgb(28,26,40),   rgb(224,222,244) },
    };

    if (index < 1 || index - 1 >= (int)(sizeof(themes) / sizeof(themes[0]))) return;
    const Theme& t = themes[index - 1];

    style.Colors[ImGuiCol_BorderShadow] = t.outline;
    style.Colors[ImGuiCol_Border] = t.inlin;
    style.Colors[ImGuiCol_Header] = t.accent;
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(ImMin(t.accent.x * 1.2f, 1.f), ImMin(t.accent.y * 1.2f, 1.f), ImMin(t.accent.z * 1.2f, 1.f), 1.f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(t.accent.x * 0.75f, t.accent.y * 0.75f, t.accent.z * 0.75f, 1.f);
    style.Colors[ImGuiCol_WindowBg] = t.high_contrast;
    style.Colors[ImGuiCol_FrameBg] = t.high_contrast;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(ImMin(t.high_contrast.x + 0.03f, 1.f), ImMin(t.high_contrast.y + 0.03f, 1.f), ImMin(t.high_contrast.z + 0.03f, 1.f), 1.f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(ImMax(t.high_contrast.x - 0.02f, 0.f), ImMax(t.high_contrast.y - 0.02f, 0.f), ImMax(t.high_contrast.z - 0.02f, 0.f), 1.f);
    style.Colors[ImGuiCol_ChildBg] = t.low_contrast;
    style.Colors[ImGuiCol_PopupBg] = t.low_contrast;
    style.Colors[ImGuiCol_Text] = t.text;
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(t.text.x * 0.55f, t.text.y * 0.55f, t.text.z * 0.55f, 1.f);
    style.Colors[ImGuiCol_FrameBgShadow] = ImVec4(0, 0, 0, 0.3f);

    SyncDerived();
}

bool Menu::Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
    bool result = true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.IniFilename = nullptr;   // Disable INI File

    // Setup Dear ImGui default style
    ImGui::StyleColorsDark();

    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.PopupRounding = 0;
    style.GrabRounding = 0;
    style.ScrollbarRounding = 0;

    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;

    style.WindowPadding = ImVec2(6, 6);
    style.ChildPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(5.0f, 4.0f);
    style.CellPadding = ImVec2(1.5f, 1.5f); // checkbox padding
    style.ItemSpacing = ImVec2(4, 4);
    style.ItemInnerSpacing = ImVec2(4, 3);
    style.WindowMinSize = ImVec2(0, 0);

    style.ScrollbarSize = 6.0f;

    style.Colors[ImGuiCol_WindowBg] = ImAdd::HexToColorVec4(0x1e1e1e);
    style.Colors[ImGuiCol_ChildBg] = ImAdd::HexToColorVec4(0x141414);
    style.Colors[ImGuiCol_PopupBg] = ImAdd::HexToColorVec4(0x1c1c1c);

    style.Colors[ImGuiCol_Text] = ImAdd::HexToColorVec4(0xffffff);
    style.Colors[ImGuiCol_TextDisabled] = ImAdd::HexToColorVec4(0x848484); // idk if this is accurate, coz in the ss there was no disabled text

    style.Colors[ImGuiCol_Border] = ImAdd::HexToColorVec4(0x323232);
    style.Colors[ImGuiCol_BorderShadow] = ImAdd::HexToColorVec4(0x000000);
    style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];

    style.Colors[ImGuiCol_Header] = ImAdd::HexToColorVec4(0x3264ff);
    style.Colors[ImGuiCol_HeaderHovered] = ImAdd::HexToColorVec4(0x567fff);
    style.Colors[ImGuiCol_HeaderActive] = ImAdd::HexToColorVec4(0x28418d);

    style.Colors[ImGuiCol_SliderGrab] = style.Colors[ImGuiCol_Header];
    style.Colors[ImGuiCol_SliderGrabActive] = style.Colors[ImGuiCol_HeaderActive];

    style.Colors[ImGuiCol_FrameBg] = ImAdd::HexToColorVec4(0x1e1e1e);
    style.Colors[ImGuiCol_FrameBgHovered] = ImAdd::HexToColorVec4(0x202020);
    style.Colors[ImGuiCol_FrameBgActive] = ImAdd::HexToColorVec4(0x1c1c1c);

    style.Colors[ImGuiCol_Button] = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_ButtonHovered] = style.Colors[ImGuiCol_FrameBgHovered];
    style.Colors[ImGuiCol_ButtonActive] = style.Colors[ImGuiCol_FrameBgActive];

    style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_FrameBgHovered];
    style.Colors[ImGuiCol_TabActive] = style.Colors[ImGuiCol_FrameBgActive];

    style.Colors[ImGuiCol_FrameBgShadow] = ImAdd::HexToColorVec4(0x000000, 0.3f);
    style.Colors[ImGuiCol_ButtonShadow] = style.Colors[ImGuiCol_FrameBgShadow];

    // Setup Font
    ImFontConfig font_cfg_main;
    font_cfg_main.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;
    font_cfg_main.GlyphOffset = ImVec2(0, 1);
    font_cfg_main.SizePixels = 12.0f;
    font_cfg_main.GlyphExtraAdvanceX = 1.0f;
    if (!io.Fonts->AddFontFromMemoryTTF(tahoma_hex, sizeof(tahoma_hex), 13.f, &font_cfg_main, io.Fonts->GetGlyphRangesCyrillic()))
        io.Fonts->AddFontDefault(&font_cfg_main); // fallback if verdana not found
    ImFontConfig font_cfg_icons;
    font_cfg_icons.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
    font_cfg_icons.GlyphOffset = ImVec2(0, 0);
    font_cfg_icons.SizePixels = 16.0f;
    font_cfg_icons.FontDataOwnedByAtlas = false;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    m_pIconFont = io.Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 16.0f, &font_cfg_icons, icon_ranges);
    // Initialize Dear ImGui - WIN32
    result = ImGui_ImplWin32_Init(hWnd);
    if (!result) return false;

    // Initialize Dear ImGui - DX11
    result = ImGui_ImplDX11_Init(pDevice, pDeviceContext);
    if (!result) return false;

    m_bInitialized = true;

    return true;
}

void Menu::DrawAll(bool menu_open)
{
    static bool s_last_menu_open = false;
    if (menu_open && !s_last_menu_open)
    {
        m_bShowMenu = true;
    }
    s_last_menu_open = menu_open;

    if (menu_open)
    {
        DrawNavigationBar();

        if (m_bShowMenu)
            DrawMenu();

        DrawESPPreview();

        if (m_bShowSettings)
            DrawSettings();

        if (m_bShowConfig)
            DrawConfig();

        if (m_bShowRivalsSkinChanger)
            exploits::rivals_skinchanger::render_window(&m_bShowRivalsSkinChanger);

        if (settings::misc::performance_window)
            performance::render_window(&settings::misc::performance_window);
    }

    if (m_bShowIndicator)
        DrawIndicator();

    if (m_bShowKeybinds || settings::misc::keybind_list)
        DrawKeybinds();

    if (m_bShowWatermark)
        DrawWatermark();

    ImNotify::Update();
    ImNotify::Render();
}

void Menu::Render()
{
    DrawAll(true);
}

void Menu::DrawWatermark()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    char watermark_text[128];
    snprintf(watermark_text, sizeof(watermark_text), "Ivory | linoria v2 | %d fps", (int)io.Framerate);

    const float wm_width = ImGui::CalcTextSize(watermark_text).x + style.ChildPadding.x * 2.0f;
    const float wm_height = ImGui::GetFontSize() + style.ChildPadding.y * 2.0f + style.ChildBorderSize * 5.0f;

    ImGui::SetNextWindowSize(ImVec2(wm_width, wm_height), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - wm_width - 8.0f, 32.0f), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool main_window = ImGui::Begin("ImMagic - Watermark", (bool*)0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar(2);

    if (main_window)
    {
        if (ImAdd::BeginChild("watermark", ImGui::GetWindowSize()))
        {
            ImVec2 curr_pos = ImGui::GetCursorScreenPos();
            ImAdd::RenderText(curr_pos, watermark_text, 0, true, true);
        }
        ImAdd::EndChild();
    }
    ImGui::End();
}

void Menu::DrawKeybinds()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    struct bind_spec_t {
        const char* name;
        bool enabled;
        int vk;
        int mode;
        const char* id;
    };

    const bind_spec_t specs[] = {
        { "Aimbot",        settings::aimbot::enabled,                 settings::aimbot::keybind,                                 settings::aimbot::activation_mode,                        "aimbot" },
        { "Triggerbot",    settings::triggerbot::enabled,             settings::triggerbot::keybind,                             settings::triggerbot::activation_mode,                    "triggerbot" },
        { "Silent Aim",    settings::silentaim::enabled,              settings::silentaim::keybind,                              settings::silentaim::activation_mode,                     "silentaim" },
        { "360 Mode",      settings::raycast_silentaim::mode_360,     settings::raycast_silentaim::mode_360_keybind,             settings::raycast_silentaim::mode_360_activation_mode,    "raycast_360" },
        { "Magic Bullet",  settings::raycast_silentaim::magic_bullet, settings::raycast_silentaim::magic_bullet_keybind,         settings::raycast_silentaim::magic_bullet_activation_mode,"magic_bullet" },
        { "Speedhack",     settings::movement::speedhack::enabled,    settings::movement::speedhack::keybind,                    settings::movement::speedhack::activation_mode,           "speedhack" },
        { "Flyhack",       settings::movement::flyhack::enabled,      settings::movement::flyhack::keybind,                      settings::movement::flyhack::activation_mode,             "flyhack" },
        { "Bhop",          settings::movement::bhop::enabled,         settings::movement::bhop::keybind,                        settings::movement::bhop::activation_mode,                "bhop" },
        { "Spin 360",      settings::movement::spin360::enabled,      settings::movement::spin360::keybind,                     1,                                                        "spin360" },
        { "Freeze Player", settings::freezeplayer::enabled,           settings::freezeplayer::keybind,                           settings::freezeplayer::activation_mode,                  "freezeplayer" },
        { "Spam TP",       settings::spamtp::enabled,                 settings::spamtp::keybind,                                 settings::spamtp::activation_mode,                        "spamtp" },
        { "NPC System",    settings::misc::npc_system_window,         settings::misc::npc_system_keybind,                        settings::misc::npc_system_keybind_mode,                  "npc_system" },
        { "Wallslide",     settings::movement::wallslide::enabled,    settings::movement::wallslide::keybind,                    settings::movement::wallslide::activation_mode,           "wallslide" },
        { "Pixelsurf",     settings::movement::pixelsurf::enabled,    settings::movement::pixelsurf::keybind,                    settings::movement::pixelsurf::activation_mode,           "pixelsurf" },
        { "Void-Hide",     settings::movement::voidhide::enabled,     settings::movement::voidhide::keybind,                     settings::movement::voidhide::activation_mode,            "voidhide" }
    };

    struct active_bind_t {
        std::string name;
        std::string mode_str;
    };

    std::vector<active_bind_t> active_binds;
    for (const auto& sp : specs)
    {
        if (sp.enabled && keybind::is_key_active(sp.vk, sp.mode, sp.id))
        {
            const char* mstr = (sp.mode == 0) ? "[toggle]" : (sp.mode == 1) ? "[hold]" : "[always]";
            active_binds.push_back({ sp.name, mstr });
        }
    }

    const float kb_width = 175.0f;
    const float header_h = ImGui::GetFontSize() + style.ChildPadding.y * 2.0f + style.ChildBorderSize * 5.0f;
    const float row_h = ImGui::GetFontSize() + 4.0f;
    const int count = (int)active_binds.size();
    const float kb_height = header_h + (count == 0 ? row_h : count * row_h) + style.ChildPadding.y;

    const ImVec2 default_pos = ImVec2(io.DisplaySize.x - kb_width - 8.0f, 70.0f);
    const ImVec2 default_size = ImVec2(kb_width, kb_height);

    static bool s_was_open = false;
    bool is_opening = ((m_bShowKeybinds || settings::misc::keybind_list) && !s_was_open);
    s_was_open = (m_bShowKeybinds || settings::misc::keybind_list);

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool main_window = ImGui::Begin("ImMagic - Keybinds", (bool*)0,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar(2);

    if (main_window)
    {
        if (ImAdd::BeginChild("keybinds_box", ImGui::GetWindowSize()))
        {
            ImVec2 curr_pos = ImGui::GetCursorScreenPos();
            const char* title = "Keybinds";
            ImVec2 title_sz = ImGui::CalcTextSize(title);
            ImVec2 title_pos(curr_pos.x + (kb_width - style.ChildPadding.x * 2.0f - title_sz.x) * 0.5f, curr_pos.y);
            ImAdd::RenderText(title_pos, title, 0, true, true);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            float line_y = curr_pos.y + title_sz.y + 4.0f;
            dl->AddLine(ImVec2(curr_pos.x, line_y), ImVec2(curr_pos.x + kb_width - style.ChildPadding.x * 2.0f, line_y),
                ImGui::GetColorU32(ImGuiCol_BorderShadow));

            float cur_y = line_y + 4.0f;
            if (active_binds.empty())
            {
                const char* empty_txt = "No active keybinds";
                ImAdd::RenderText(ImVec2(curr_pos.x, cur_y), empty_txt, 0, false, true);
            }
            else
            {
                for (const auto& b : active_binds)
                {
                    ImAdd::RenderText(ImVec2(curr_pos.x, cur_y), b.name.c_str(), 0, true, true);
                    ImVec2 mode_sz = ImGui::CalcTextSize(b.mode_str.c_str());
                    float mode_x = curr_pos.x + kb_width - style.ChildPadding.x * 2.0f - mode_sz.x;
                    ImAdd::RenderText(ImVec2(mode_x, cur_y), b.mode_str.c_str(), 0, false, true);
                    cur_y += row_h;
                }
            }
        }
        ImAdd::EndChild();
    }
    ImGui::End();
}

void Menu::DrawMenu()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    const ImVec2 default_pos = ImVec2(8.0f, 32.0f);
    const ImVec2 default_size = ImVec2(504.0f, 604.0f);

    static bool s_was_open = false;
    bool is_opening = (m_bShowMenu && !s_was_open);
    s_was_open = m_bShowMenu;

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool main_window = ImGui::Begin("ImMagic - Menu", (bool*)0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    if (main_window)
    {
        ImRect window_bb(ImGui::GetCurrentWindow()->Rect());

        if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoBackground)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();

            window->DrawList->AddRectFilled(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));

            if (style.WindowBorderSize > 0.0f)
            {
                // Window outer border
                window->DrawList->AddRect(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
                window->DrawList->AddRect(window_bb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), window_bb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
            }

            ImAdd::RenderText(window_bb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), PROJECT_NAME, NULL, false, true);
        }

        int& tab_index = g_linoria_tab_index;

        ImGui::SetCursorScreenPos(window_bb.Min + ImVec2(style.WindowPadding.x, ImGui::GetFontSize() + style.FramePadding.y * 2.0f));

        if (ImGui::BeginChild("body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoBackground)
            {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                ImRect bb = window->Rect();

                ImAdd::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_ChildBg), true, true, true);
            }

            if (tab_index > 2) tab_index = 0;
            if (ImAdd::BeginChild("tabs-result", { "Aimbot", "Visuals", "Exploits" }, &tab_index))
            {
                if (tab_index == 0)
                {
                    float group_width = ImTrunc((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) / 2);

                    static const char* target_parts[] = { "Head", "Torso", "HumanoidRootPart", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Closest Part", "Nearest" };
                    static const char* aim_modes[] = { "Mouse", "Camera" };
                    static const char* silentaim_methods[] = { "Raycast", "Mouse", "Viewport", "Camera" };

                    static int aimbot_subtab = 0;
                    static int silent_subtab = 0;

                    // --- Left Column: Aimbot ---
                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("aimbot_group", { "Main", "Field" }, &aimbot_subtab, ImVec2(group_width, 0.0f)))
                    {
                        if (aimbot_subtab == 0)
                        {
                            ImAdd::CheckBox("Enabled##aimbot", &settings::aimbot::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::aimbot::keybind));
                            ImAdd::KeyBind("key##aimbot", &settings::aimbot::keybind, &settings::aimbot::activation_mode);

                            std::vector<const char*> aim_modes_vec(aim_modes, aim_modes + IM_ARRAYSIZE(aim_modes));
                            ImAdd::Combo("Aim Mode", &settings::aimbot::mode, aim_modes_vec);

                            std::vector<const char*> target_parts_vec(target_parts, target_parts + IM_ARRAYSIZE(target_parts));
                            ImAdd::Combo("Target Part", &settings::aimbot::target_part, target_parts_vec);
                            ImAdd::Combo("Air Target Part", &settings::aimbot::air_part, target_parts_vec);

                            if (settings::aimbot::mode == 0)
                            {
                                ImAdd::SliderFloat("Sensitivity", &settings::aimbot::mouse_sensitivity, 0.1f, 5.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Smoothing", &settings::aimbot::smoothing);
                            if (settings::aimbot::smoothing)
                            {
                                ImAdd::SliderFloat("Smoothing X", &settings::aimbot::smoothingx, 1.0f, 100.0f, "%.1f");
                                ImAdd::SliderFloat("Smoothing Y", &settings::aimbot::smoothingy, 1.0f, 100.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Prediction##aimbot", &settings::aimbot::enable_prediction);
                            if (settings::aimbot::enable_prediction)
                            {
                                ImAdd::SliderFloat("Prediction X##aimbot", &settings::aimbot::prediction_x, 0.0f, 100.0f, "%.1f");
                                ImAdd::SliderFloat("Prediction Y##aimbot", &settings::aimbot::prediction_y, 0.0f, 100.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Team Check##aimbot", &settings::aimbot::teamcheck);
                            ImAdd::CheckBox("Knock Check##aimbot", &settings::aimbot::knock_check);
                            ImAdd::CheckBox("Sticky Target##aimbot", &settings::aimbot::sticky_aim);
                            ImAdd::CheckBox("Disable On Kill##aimbot", &settings::aimbot::disable_on_kill);
                        }
                        else if (aimbot_subtab == 1)
                        {
                            ImAdd::SliderFloat("FOV##aimbot", &settings::aimbot::fov, 0.0f, 600.0f, "%.0f");
                            ImAdd::CheckBox("Dynamic FOV##aimbot", &settings::aimbot::dynamic_fov);

                            ImAdd::CheckBox("Draw FOV##aimbot", &settings::aimbot::draw_fov);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth() * 2.0f - style.ItemSpacing.x);
                            ImAdd::ColorEdit4("##aimbot_fov_col", settings::aimbot::fov_circle_colour);
                            ImGui::SameLine();
                            ImAdd::ColorEdit4("##aimbot_fov_outline", settings::aimbot::fov_outline_colour);

                            ImAdd::CheckBox("Fill FOV##aimbot", &settings::aimbot::fill_fov);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##aimbot_fov_fill", settings::aimbot::fov_fill_colour);

                            ImAdd::CheckBox("Ring FOV##aimbot", &settings::aimbot::ring_fov_enabled);
                            if (settings::aimbot::ring_fov_enabled)
                            {
                                ImAdd::SliderFloat("Ring Inner##aimbot", &settings::aimbot::ring_fov_inner, 0.0f, 300.0f, "%.0f");
                                ImAdd::SliderFloat("Ring Outer##aimbot", &settings::aimbot::ring_fov_outer, 0.0f, 600.0f, "%.0f");
                            }
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    // --- Right Column: Silent Aim & Triggerbot ---
                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("silentaim_group", { "Main", "Field", "Safety" }, &silent_subtab, ImVec2(group_width, 310)))
                    {
                        if (silent_subtab == 0)
                        {
                            ImAdd::CheckBox("Enabled##silent", &settings::silentaim::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::silentaim::keybind));
                            ImAdd::KeyBind("key##silent", &settings::silentaim::keybind, &settings::silentaim::activation_mode);

                            std::vector<const char*> silent_methods_vec(silentaim_methods, silentaim_methods + IM_ARRAYSIZE(silentaim_methods));
                            ImAdd::Combo("Method##silent", &settings::silentaim::method, silent_methods_vec);

                            std::vector<const char*> target_parts_vec(target_parts, target_parts + IM_ARRAYSIZE(target_parts));
                            ImAdd::Combo("Target Bone##silent", &settings::silentaim::target_part, target_parts_vec);

                            ImAdd::CheckBox("Use Aimbot Target##silent", &settings::silentaim::use_aimbot_target);

                            ImAdd::CheckBox("Snapline##silent", &settings::silentaim::snapline);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##silent_snapline_col", settings::silentaim::snapline_colour);

                            ImAdd::CheckBox("Prediction##silent", &settings::silentaim::enable_prediction);
                            if (settings::silentaim::enable_prediction)
                            {
                                ImAdd::SliderFloat("Pred X##silent", &settings::silentaim::prediction_x, 0.0f, 100.0f, "%.1f");
                                ImAdd::SliderFloat("Pred Y##silent", &settings::silentaim::prediction_y, 0.0f, 100.0f, "%.1f");
                            }

                            if (settings::silentaim::method == 0)
                            {
                                ImAdd::CheckBox("Wallbang##silent", &settings::raycast_silentaim::magic_bullet);
                            }
                        }
                        else if (silent_subtab == 1)
                        {
                            if (ImAdd::CheckBox("360 Mode##silent", &settings::silentaim::mode_360))
                            {
                                settings::raycast_silentaim::mode_360 = settings::silentaim::mode_360;
                            }

                            if (ImAdd::SliderFloat("Fov##silent", &settings::silentaim::fov, 0.0f, 1000.0f, "%.0f"))
                            {
                                settings::raycast_silentaim::fov = settings::silentaim::fov;
                            }

                            if (ImAdd::CheckBox("Use Fov##silent", &settings::silentaim::use_fov))
                            {
                                settings::raycast_silentaim::use_fov = settings::silentaim::use_fov;
                            }

                            if (ImAdd::CheckBox("Draw Fov##silent", &settings::silentaim::draw_fov))
                            {
                                settings::raycast_silentaim::draw_fov = settings::silentaim::draw_fov;
                            }
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth() * 2.0f - style.ItemSpacing.x);
                            if (ImAdd::ColorEdit4("##silent_fov_col", settings::silentaim::fov_circle_colour))
                            {
                                for (int c = 0; c < 4; c++) settings::raycast_silentaim::fov_circle_colour[c] = settings::silentaim::fov_circle_colour[c];
                            }
                            ImGui::SameLine();
                            if (ImAdd::ColorEdit4("##silent_fov_outline", settings::silentaim::fov_outline_colour))
                            {
                                for (int c = 0; c < 4; c++) settings::raycast_silentaim::fov_outline_colour[c] = settings::silentaim::fov_outline_colour[c];
                            }

                            if (ImAdd::CheckBox("Fill Fov##silent", &settings::silentaim::fill_fov))
                            {
                                settings::raycast_silentaim::fill_fov = settings::silentaim::fill_fov;
                            }
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            if (ImAdd::ColorEdit4("##silent_fov_fill", settings::silentaim::fov_fill_colour))
                            {
                                for (int c = 0; c < 4; c++) settings::raycast_silentaim::fov_fill_colour[c] = settings::silentaim::fov_fill_colour[c];
                            }
                        }
                        else if (silent_subtab == 2)
                        {
                            ImAdd::CheckBox("Team Check##silent", &settings::silentaim::teamcheck);
                            ImAdd::CheckBox("Gun Check##silent", &settings::silentaim::guncheck);
                            ImAdd::CheckBox("Knock Check##silent", &settings::silentaim::knock_check);
                            ImAdd::CheckBox("Forcefield Check##silent", &settings::silentaim::forcefield_check);
                            ImAdd::CheckBox("Katana Check##silent", &settings::silentaim::katana_check);
                            ImAdd::CheckBox("Spectate Check##silent", &settings::silentaim::shoot_spectating);

                            ImAdd::CheckBox("Min Health Check##silent", &settings::silentaim::health_check_enabled);
                            if (settings::silentaim::health_check_enabled)
                            {
                                ImAdd::SliderFloat("Min Health##silent", &settings::silentaim::min_health, 0.0f, 100.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Sticky Aim##silent", &settings::silentaim::sticky_aim);
                            ImAdd::CheckBox("Auto Switch Target##silent", &settings::silentaim::auto_switch);
                            ImAdd::CheckBox("Spoof Mouse(Use With Mouse)##silent", &settings::silentaim::spoof_mouse);
                        }
                    }
                    ImAdd::EndChild();

                    if (ImAdd::BeginChild("triggerbot_group", { "Triggerbot" }, ImVec2(group_width, 0.0f)))
                    {
                        ImAdd::CheckBox("Enabled##trig", &settings::triggerbot::enabled);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::triggerbot::keybind));
                        ImAdd::KeyBind("key##trig", &settings::triggerbot::keybind, &settings::triggerbot::activation_mode);

                        std::vector<const char*> target_parts_vec(target_parts, target_parts + IM_ARRAYSIZE(target_parts));
                        ImAdd::Combo("Target Part##trig", &settings::triggerbot::target_part, target_parts_vec);

                        ImAdd::SliderFloat("Delay##trig", &settings::triggerbot::delay_ms, 0.0f, 500.0f, "%.0fms");
                        ImAdd::SliderFloat("Cooldown##trig", &settings::triggerbot::cooldown_ms, 0.0f, 1000.0f, "%.0fms");
                        ImAdd::CheckBox("Team Check##trig", &settings::triggerbot::teamcheck);
                        ImAdd::CheckBox("Wall Check##trig", &settings::triggerbot::wallcheck);
                        ImAdd::CheckBox("Knock Check##trig", &settings::triggerbot::knock_check);
                        ImAdd::CheckBox("Gun Check##trig", &settings::triggerbot::guncheck);
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                }
                else if (tab_index == 1)
                {
                    float group_width = ImTrunc((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) / 2);

                    static int target_team = 0;
                    static int chams_subtab = 0;
                    static int world_subtab = 0;

                    settings::visuals::priority_settings_t* cur = (target_team == 0) ? &settings::visuals::hostile : ((target_team == 1) ? &settings::visuals::friendly : &settings::visuals::neutral);

                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("esp_players_group", { "Enemy", "Team", "Local" }, &target_team, ImVec2(group_width, 0.0f)))
                    {
                        ImAdd::CheckBox("Enabled##esp", &settings::visuals::enabled);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::visuals::keybind));
                        ImAdd::KeyBind("key##esp", &settings::visuals::keybind, &settings::visuals::activation_mode);

                        ImAdd::CheckBox("Box##esp", &cur->box);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_box_col", cur->colour);
                        if (cur->box)
                        {
                            static const char* box_types[] = { "2D", "Corner", "3D" };
                            std::vector<const char*> box_types_vec(box_types, box_types + IM_ARRAYSIZE(box_types));
                            ImAdd::Combo("Box Type##esp", &cur->box_type, box_types_vec);

                            ImAdd::CheckBox("Box Fill##esp", &cur->box_fill);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##esp_box_fill_col", cur->box_fill_colour);
                        }

                        ImAdd::CheckBox("Name##esp", &cur->username);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_name_col", cur->username_colour);
                        if (cur->username)
                        {
                            static const char* name_modes[] = { "Display Name", "Username", "Both" };
                            std::vector<const char*> name_modes_vec(name_modes, name_modes + IM_ARRAYSIZE(name_modes));
                            ImAdd::Combo("Name Mode##esp", &cur->username_type, name_modes_vec);

                            static const char* name_positions[] = { "Top", "Bottom", "Left", "Right" };
                            std::vector<const char*> name_positions_vec(name_positions, name_positions + IM_ARRAYSIZE(name_positions));
                            ImAdd::Combo("Position##name_esp", &cur->username_position, name_positions_vec);
                        }

                        ImAdd::CheckBox("Distance##esp", &cur->distance);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_dist_col", cur->distance_colour);

                        ImAdd::CheckBox("Held Tool##esp", &cur->tool);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_tool_col", cur->tool_colour);

                        ImAdd::CheckBox("Health Bar##esp", &cur->healthbar);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth() * 3.0f - style.ItemSpacing.x * 2.0f);
                        ImAdd::ColorEdit4("##esp_hp_col_high", cur->healthbar_colour);
                        ImGui::SameLine();
                        ImAdd::ColorEdit4("##esp_hp_col_mid", cur->healthbar_colour_mid);
                        ImGui::SameLine();
                        ImAdd::ColorEdit4("##esp_hp_col_low", cur->healthbar_colour_low);
                        if (cur->healthbar)
                        {
                            static const char* hp_styles[] = { "Left", "Right", "Top", "Bottom" };
                            std::vector<const char*> hp_styles_vec(hp_styles, hp_styles + IM_ARRAYSIZE(hp_styles));
                            ImAdd::Combo("Position##hp_esp", &cur->healthbar_style, hp_styles_vec);

                            ImAdd::CheckBox("Health Text##esp", &cur->healthbar_text);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##esp_hp_text_col", cur->healthbar_text_colour);
                        }

                        ImAdd::CheckBox("Tracers##esp", &cur->tracers);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_tracer_col", cur->tracers_colour);
                        if (cur->tracers)
                        {
                            static const char* tracer_origins[] = { "Bottom", "Top", "Center", "Mouse" };
                            std::vector<const char*> tracer_origins_vec(tracer_origins, tracer_origins + IM_ARRAYSIZE(tracer_origins));
                            ImAdd::Combo("Origin##tracer_esp", &cur->tracers_origin, tracer_origins_vec);
                        }

                        ImAdd::CheckBox("Skeleton##esp", &cur->skeleton);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth() * 2.0f - style.ItemSpacing.x);
                        ImAdd::ColorEdit4("##esp_skel_col", cur->skeleton_colour);
                        ImGui::SameLine();
                        ImAdd::ColorEdit4("##esp_skel_outline", cur->skeleton_outline_colour);

                        ImAdd::CheckBox("Head Dot##esp", &cur->head_dot);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_head_col", cur->head_dot_colour);
                        if (cur->head_dot)
                        {
                            static const char* head_types[] = { "Dot", "Hexagon" };
                            std::vector<const char*> head_types_vec(head_types, head_types + IM_ARRAYSIZE(head_types));
                            ImAdd::Combo("Head Type##esp", &cur->head_type, head_types_vec);
                            ImAdd::SliderFloat("Dot Size##esp", &cur->head_dot_size, 1.0f, 10.0f, "%.1f");
                        }

                        ImAdd::CheckBox("Offscreen Arrows##esp", &cur->offscreen_arrows);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                        ImAdd::ColorEdit4("##esp_arrow_col", cur->offscreen_arrows_colour);
                        if (cur->offscreen_arrows)
                        {
                            ImAdd::SliderFloat("Radius##arrow_esp", &cur->offscreen_arrows_radius, 50.0f, 400.0f, "%.0f");
                            ImAdd::SliderFloat("Size##arrow_esp", &cur->offscreen_arrows_size, 5.0f, 30.0f, "%.0f");
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("chams_group", { "Chams", "Other" }, &chams_subtab, ImVec2(group_width, 280.0f)))
                    {
                        if (chams_subtab == 0)
                        {
                            ImAdd::CheckBox("Chams##esp", &cur->chams);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth() * 2.0f - style.ItemSpacing.x);
                            ImAdd::ColorEdit4("##chams_col", cur->chams_colour);
                            ImGui::SameLine();
                            ImAdd::ColorEdit4("##chams_outline_col", cur->chams_outline_colour);

                            static const char* chams_types[] = { "Clipper", "Memory", "Engine" };
                            std::vector<const char*> chams_types_vec(chams_types, chams_types + IM_ARRAYSIZE(chams_types));

                            if (cur->chams_type != 2)
                            {
                                static const char* mesh_shader_names[] = {
                                    "Flat", "Chrome", "Rainbow", "Pearl", "Glossy", "Holographic", "Fade", "Wireframe",
                                    "Glass", "Copper Ropes", "Liquid Metal", "Soft Glass", "Ice", "Ghost Pulse", "Aurora",
                                    "Bubble", "Jelly", "Mercury", "Water Caustics", "Deep Ocean", "Quicksilver", "Ripple",
                                    "Oil Slick", "Metallic", "Void Waves", "Dark Nebula", "Cyber Plasma", "Energy Pulse",
                                    "Black Hole", "Cosmic Matrix", "Acrylic"
                                };
                                std::vector<const char*> mesh_shaders_vec(mesh_shader_names, mesh_shader_names + IM_ARRAYSIZE(mesh_shader_names));

                                ImAdd::Combo("Type##chams", &cur->chams_type, chams_types_vec);
                                ImAdd::Combo("Shader##chams", &cur->mesh_shader, mesh_shaders_vec);
                                ImAdd::SliderFloat("Depth Bias##chams", &cur->mesh_depth_bias, -5.0f, 5.0f, "%.2f");
                                ImAdd::SliderFloat("Thickness##chams", &settings::visuals::chams_thickness, 0.0f, 5.0f, "%.1f");

                                ImAdd::CheckBox("Accessories##chams", &cur->mesh_accessories);
                                ImAdd::CheckBox("Health Based##chams", &cur->chams_health_based);
                                ImAdd::CheckBox("Chams Flash##chams", &cur->chams_flash);
                                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                                ImAdd::ColorEdit4("##chams_flash_col", cur->chams_flash_colour);
                                if (cur->chams_flash)
                                {
                                    ImAdd::SliderFloat("Flash Speed##chams", &cur->chams_flash_speed, 0.1f, 10.0f, "%.1fx");
                                }
                                ImAdd::CheckBox("Chams Fade##chams", &cur->chams_fade);
                                if (cur->chams_fade)
                                {
                                    ImAdd::SliderFloat("Fade Speed##chams", &cur->chams_fade_speed, 0.1f, 10.0f, "%.1fx");
                                }

                                ImAdd::CheckBox("Outline##chams", &cur->mesh_outline);
                                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                                ImAdd::ColorEdit4("##chams_outline_col2", cur->chams_outline_colour);
                                if (cur->mesh_outline)
                                {
                                    if (cur->chams_type == 1)
                                    {
                                        static const char* outline_modes[] = { "Silhouette", "Wireframe" };
                                        std::vector<const char*> outline_modes_vec(outline_modes, outline_modes + IM_ARRAYSIZE(outline_modes));
                                        ImAdd::Combo("Outline Mode##chams", &cur->mesh_outline_mode, outline_modes_vec);

                                        static const char* outline_styles[] = { "Solid Glow", "Soft Fade", "Pulse Glow" };
                                        std::vector<const char*> outline_styles_vec(outline_styles, outline_styles + IM_ARRAYSIZE(outline_styles));
                                        ImAdd::Combo("Outline Style##chams", &cur->mesh_outline_style, outline_styles_vec);
                                    }
                                    ImAdd::SliderFloat("Outline Thickness##chams", &cur->mesh_outline_thickness, 0.5f, 6.0f, "%.1f");
                                }

                            }
                            else
                            {
                                static const char* engine_styles[] = { "Wireframe Normal", "Wireframe Textured", "Character Wireframe", "Character Mesh" };
                                std::vector<const char*> engine_styles_vec(engine_styles, engine_styles + IM_ARRAYSIZE(engine_styles));
                                ImAdd::Combo("Engine Style##chams", &settings::visuals::engine_chams_style, engine_styles_vec);

                                static const char* engine_modes[] = { "Glow", "Invisible", "Always On Top" };
                                std::vector<const char*> engine_modes_vec(engine_modes, engine_modes + IM_ARRAYSIZE(engine_modes));
                                ImAdd::Combo("Engine Mode##chams", &settings::visuals::engine_chams_mode, engine_modes_vec);

                                static const char* engine_colors[] = {
                                    "White", "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta", "Orange",
                                    "Purple", "Pink", "Lime", "Aqua", "Gold", "Violet", "Teal", "Crimson",
                                    "Navy", "Emerald", "Coral", "Silver"
                                };
                                std::vector<const char*> engine_colors_vec(engine_colors, engine_colors + IM_ARRAYSIZE(engine_colors));
                                ImAdd::Combo("Engine Color##chams", &settings::visuals::engine_chams_color_index, engine_colors_vec);
                            }
                        }
                        else if (chams_subtab == 1)
                        {
                            ImAdd::CheckBox("View Angles##fx", &cur->view_angle_lines);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##view_angle_col", cur->view_angle_lines_colour);
                            if (cur->view_angle_lines)
                            {
                                ImAdd::SliderFloat("Length##view_angle", &cur->view_angle_lines_length, 1.0f, 50.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Movement Trails##fx", &cur->movement_trails);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##trails_col", cur->movement_trails_colour);

                            ImAdd::CheckBox("Sound ESP##fx", &cur->sound_esp);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##sound_esp_col", cur->sound_esp_colour);

                            ImAdd::CheckBox("Footprints##fx", &cur->footprints);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##footprints_col", cur->footprints_colour);

                            ImAdd::CheckBox("Hit Impact##fx", &settings::visuals::chams_hit_impact);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##hit_impact_col", settings::visuals::chams_hit_impact_colour);
                        }
                    }
                    ImAdd::EndChild();

                    if (ImAdd::BeginChild("world_group", { "Filters", "World" }, &world_subtab, ImVec2(group_width, 0.0f)))
                    {
                        if (world_subtab == 0)
                        {
                            ImAdd::CheckBox("Team Check##vis", &settings::visuals::teamcheck);
                            ImAdd::CheckBox("Visible Check##vis", &settings::visuals::visible_check);
                            ImAdd::CheckBox("Knock Check##vis", &settings::visuals::knock_check);
                            ImAdd::CheckBox("Alive Check##vis", &settings::visuals::alive_check);
                            ImAdd::CheckBox("Ragdoll Check##vis", &settings::visuals::ragdoll_check);
                            ImAdd::CheckBox("Distance Limit##vis", &settings::visuals::distance_check);
                            if (settings::visuals::distance_check)
                            {
                                ImAdd::SliderFloat("Max Dist##vis", &settings::visuals::max_distance, 50.0f, 5000.0f, "%.0f studs");
                            }
                            ImAdd::CheckBox("Enemy Highlight##vis", &settings::visuals::enemy_highlight);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##enemy_hl_col", settings::visuals::enemy_highlight_colour);
                            ImAdd::CheckBox("Friendly Highlight##vis", &settings::visuals::friendly_highlight);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::GetColorPickerWidth());
                            ImAdd::ColorEdit4("##friendly_hl_col", settings::visuals::friendly_highlight_colour);
                        }
                        else if (world_subtab == 1)
                        {
                            ImAdd::CheckBox("Custom FOV##world", &settings::movement::fov_changer::enabled);
                            if (settings::movement::fov_changer::enabled)
                            {
                                ImAdd::SliderFloat("FOV##fovchanger", &settings::movement::fov_changer::fov_value, 30.0f, 140.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Third Person##world", &settings::movement::third_person::enabled);
                            if (settings::movement::third_person::enabled)
                            {
                                ImAdd::SliderFloat("Distance##tp", &settings::movement::third_person::distance, 5.0f, 50.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Preview Window##world", &settings::misc::model_viewer_window);
                            if (settings::misc::model_viewer_window)
                            {
                                static const char* preview_models[] = { "Arsenal", "Client", "Tung Tung Tung Sahur", "Custom" };
                                std::vector<const char*> preview_models_vec(preview_models, preview_models + IM_ARRAYSIZE(preview_models));
                                if (ImAdd::Combo("Model##world", &settings::misc::preview_model, preview_models_vec))
                                {
                                    ModelViewer::g_model_viewer.RefreshAvatar();
                                }
                            }
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                }
                else if (tab_index == 2)
                {
                    float group_width = ImTrunc((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) / 2);

                    static int movement_subtab = 0;
                    static int utility_subtab = 0;
                    static int misc_exp_subtab = 0;

                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("movement_group", { "Movement", "Other" }, &movement_subtab, ImVec2(group_width, 0.0f)))
                    {
                        if (movement_subtab == 0)
                        {
                            ImAdd::CheckBox("Speed##exp", &settings::movement::speedhack::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::speedhack::keybind));
                            ImAdd::KeyBind("key##speed", &settings::movement::speedhack::keybind, &settings::movement::speedhack::activation_mode);
                            if (settings::movement::speedhack::enabled)
                            {
                                static const char* speed_modes[] = { "Velocity", "WalkSpeed", "CFrame" };
                                std::vector<const char*> speed_modes_vec(speed_modes, speed_modes + IM_ARRAYSIZE(speed_modes));
                                ImAdd::Combo("Mode##speed", &settings::movement::speedhack::mode, speed_modes_vec);
                                ImAdd::SliderFloat("Speed##speed", &settings::movement::speedhack::speed, 16.0f, 500.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Flight##exp", &settings::movement::flyhack::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::flyhack::keybind));
                            ImAdd::KeyBind("key##fly", &settings::movement::flyhack::keybind, &settings::movement::flyhack::activation_mode);
                            if (settings::movement::flyhack::enabled)
                            {
                                static const char* fly_modes[] = { "Velocity", "CFrame" };
                                std::vector<const char*> fly_modes_vec(fly_modes, fly_modes + IM_ARRAYSIZE(fly_modes));
                                ImAdd::Combo("Mode##fly", &settings::movement::flyhack::mode, fly_modes_vec);
                                ImAdd::SliderFloat("Speed##fly", &settings::movement::flyhack::speed, 10.0f, 500.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Noclip##exp", &settings::movement::noclip::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::noclip::keybind));
                            ImAdd::KeyBind("key##noclip", &settings::movement::noclip::keybind, &settings::movement::noclip::activation_mode);

                            ImAdd::CheckBox("B-Hop##exp", &settings::movement::bhop::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::bhop::keybind));
                            ImAdd::KeyBind("key##bhop", &settings::movement::bhop::keybind, &settings::movement::bhop::activation_mode);
                            if (settings::movement::bhop::enabled)
                            {
                                ImAdd::SliderFloat("Speed##bhop", &settings::movement::bhop::speed, 10.0f, 300.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Spiderman##exp", &settings::movement::spiderman::enabled);
                            ImAdd::CheckBox("No Fall Damage##exp", &settings::movement::no_fall_damage::enabled);
                        }
                        else if (movement_subtab == 1)
                        {
                            ImAdd::CheckBox("Spinbot##exp", &settings::movement::spinbot::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::spinbot::keybind));
                            ImAdd::KeyBind("key##spin", &settings::movement::spinbot::keybind, &settings::movement::spinbot::activation_mode);
                            if (settings::movement::spinbot::enabled)
                            {
                                ImAdd::SliderFloat("Speed##spin", &settings::movement::spinbot::speed, 10.0f, 500.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Spin 360##exp", &settings::movement::spin360::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::spin360::keybind));
                            ImAdd::KeyBind("key##spin360", &settings::movement::spin360::keybind);

                            ImAdd::CheckBox("Wallslide##exp", &settings::movement::wallslide::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::wallslide::keybind));
                            ImAdd::KeyBind("key##wallslide", &settings::movement::wallslide::keybind, &settings::movement::wallslide::activation_mode);
                            if (settings::movement::wallslide::enabled)
                            {
                                ImAdd::SliderFloat("Speed##wallslide", &settings::movement::wallslide::speed, 5.0f, 50.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Pixel surf##exp", &settings::movement::pixelsurf::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::pixelsurf::keybind));
                            ImAdd::KeyBind("key##pixelsurf", &settings::movement::pixelsurf::keybind, &settings::movement::pixelsurf::activation_mode);

                            ImAdd::CheckBox("Void Hide##exp", &settings::movement::voidhide::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::voidhide::keybind));
                            ImAdd::KeyBind("key##voidhide", &settings::movement::voidhide::keybind, &settings::movement::voidhide::activation_mode);

                            ImAdd::CheckBox("Wallbug##exp", &settings::movement::wallbug::enabled);
                            if (settings::movement::wallbug::enabled)
                            {
                                ImAdd::SliderFloat("Height##wallbug", &settings::movement::wallbug::height, 5.0f, 100.0f, "%.0f");
                            }

                            ImAdd::CheckBox("Hip Height##exp", &settings::movement::hipheight::enabled);
                            if (settings::movement::hipheight::enabled)
                            {
                                ImAdd::SliderFloat("Height##hipheight", &settings::movement::hipheight::value, -5.0f, 20.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Gravity##exp", &settings::movement::gravity::enabled);
                            if (settings::movement::gravity::enabled)
                            {
                                ImAdd::SliderFloat("Value##gravity", &settings::movement::gravity::value, 0.0f, 400.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Tickrate##exp", &settings::movement::tickrate::enabled);
                            if (settings::movement::tickrate::enabled)
                            {
                                ImAdd::SliderFloat("Rate##tickrate", &settings::movement::tickrate::value, 60.0f, 500.0f, "%.0f");
                            }
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("utility_group", { "Utility", "Character" }, &utility_subtab, ImVec2(group_width, 280.0f)))
                    {
                        if (utility_subtab == 0)
                        {
                            ImAdd::CheckBox("Freecam##exp", &settings::movement::freecam::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::freecam::keybind));
                            ImAdd::KeyBind("key##freecam", &settings::movement::freecam::keybind, &settings::movement::freecam::activation_mode);
                            if (settings::movement::freecam::enabled)
                            {
                                ImAdd::SliderFloat("Speed##freecam", &settings::movement::freecam::speed, 0.1f, 10.0f, "%.1f");
                                ImAdd::SliderFloat("Sensitivity##freecam", &settings::movement::freecam::sensitivity, 0.001f, 0.01f, "%.4f");
                                ImAdd::CheckBox("Azerty##freecam", &settings::movement::freecam::azerty);
                                ImAdd::CheckBox("Lock Char##freecam", &settings::movement::freecam::lock_character);
                            }

                            ImAdd::CheckBox("Freeze Player##exp", &settings::freezeplayer::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::freezeplayer::keybind));
                            ImAdd::KeyBind("key##freeze", &settings::freezeplayer::keybind, &settings::freezeplayer::activation_mode);

                            ImAdd::CheckBox("BTools##exp", &settings::btools::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::btools::keybind));
                            ImAdd::KeyBind("key##btools", &settings::btools::keybind, &settings::btools::activation_mode);
                            if (settings::btools::enabled)
                            {
                                static const char* btools_types[] = { "Hammer", "Grab", "Clone" };
                                std::vector<const char*> btools_types_vec(btools_types, btools_types + IM_ARRAYSIZE(btools_types));
                                ImAdd::Combo("Tool Type##btools", &settings::btools::tool_type, btools_types_vec);
                            }

                            ImAdd::CheckBox("Anchor##exp", &settings::movement::anchor::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::anchor::keybind));
                            ImAdd::KeyBind("key##anchor", &settings::movement::anchor::keybind, &settings::movement::anchor::activation_mode);

                            ImAdd::CheckBox("Ideal Peek##exp", &settings::movement::idealpeek::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::idealpeek::keybind));
                            ImAdd::KeyBind("key##idealpeek", &settings::movement::idealpeek::keybind, &settings::movement::idealpeek::activation_mode);

                            ImAdd::CheckBox("Anti-AFK##exp", &settings::movement::antiafk::enabled);
                        }
                        else if (utility_subtab == 1)
                        {
                            ImAdd::CheckBox("Anti-Aim##exp", &settings::movement::antiaim::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::antiaim::keybind));
                            ImAdd::KeyBind("key##antiaim", &settings::movement::antiaim::keybind, &settings::movement::antiaim::activation_mode);
                            if (settings::movement::antiaim::enabled)
                            {
                                static const char* pitch_modes[] = { "None", "Down", "Up", "Custom" };
                                std::vector<const char*> pitch_modes_vec(pitch_modes, pitch_modes + IM_ARRAYSIZE(pitch_modes));
                                ImAdd::Combo("Pitch##antiaim", &settings::movement::antiaim::pitch_base, pitch_modes_vec);

                                static const char* yaw_modes[] = { "None", "Backwards", "Spin", "Custom" };
                                std::vector<const char*> yaw_modes_vec(yaw_modes, yaw_modes + IM_ARRAYSIZE(yaw_modes));
                                ImAdd::Combo("Yaw##antiaim", &settings::movement::antiaim::yaw_base, yaw_modes_vec);

                                if (settings::movement::antiaim::yaw_base == 2)
                                {
                                    ImAdd::SliderFloat("Spin Speed##antiaim", &settings::movement::antiaim::spin_speed, 1.0f, 200.0f, "%.0f");
                                }
                                ImAdd::CheckBox("Yaw Jitter##antiaim", &settings::movement::antiaim::yaw_jitter);
                                ImAdd::CheckBox("Upside Down##antiaim", &settings::movement::antiaim::upside_down);
                            }

                            ImAdd::CheckBox("Desync##exp", &settings::movement::desync::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::movement::desync::keybind));
                            ImAdd::KeyBind("key##desync", &settings::movement::desync::keybind, &settings::movement::desync::activation_mode);

                            ImAdd::CheckBox("Long Neck##exp", &settings::movement::longneck::enabled);
                            if (settings::movement::longneck::enabled)
                            {
                                ImAdd::SliderFloat("Length##longneck", &settings::movement::longneck::length, 1.0f, 20.0f, "%.1f");
                            }

                            ImAdd::CheckBox("Sit##exp", &settings::movement::sit::enabled);
                            ImAdd::CheckBox("Platform Stand##exp", &settings::movement::platformstand::enabled);
                        }
                    }
                    ImAdd::EndChild();

                    if (ImAdd::BeginChild("misc_exp_group", { "Combat", "Visual Mod" }, &misc_exp_subtab, ImVec2(group_width, 0.0f)))
                    {
                        if (misc_exp_subtab == 0)
                        {
                            ImAdd::CheckBox("Hitbox Expander##cexp", &settings::hitboxexpander::enabled);
                            if (settings::hitboxexpander::enabled)
                            {
                                ImAdd::SliderFloat("Size X##cexp", &settings::hitboxexpander::size_x, 1.0f, 50.0f, "%.1f");
                                ImAdd::SliderFloat("Size Y##cexp", &settings::hitboxexpander::size_y, 1.0f, 50.0f, "%.1f");
                                ImAdd::SliderFloat("Size Z##cexp", &settings::hitboxexpander::size_z, 1.0f, 50.0f, "%.1f");
                                ImAdd::CheckBox("Team Check##cexp", &settings::hitboxexpander::teamcheck);
                                ImAdd::CheckBox("Knock Check##cexp", &settings::hitboxexpander::knock_check);
                            }

                            ImAdd::CheckBox("Spam TP##cexp", &settings::spamtp::enabled);
                            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImAdd::CalcKeyBindWidth(settings::spamtp::keybind));
                            ImAdd::KeyBind("key##spamtp", &settings::spamtp::keybind, &settings::spamtp::activation_mode);
                            if (settings::spamtp::enabled)
                            {
                                ImAdd::SliderFloat("Interval##spamtp", &settings::spamtp::interval_ms, 10.0f, 500.0f, "%.0fms");
                            }

                            ImAdd::CheckBox("Autoshoot##cexp", &settings::autoshoot::enabled);
                            if (settings::autoshoot::enabled)
                            {
                                ImAdd::SliderFloat("Fire Rate##autoshoot", &settings::autoshoot::fire_interval_ms, 10.0f, 500.0f, "%.0fms");
                            }
                        }
                        else if (misc_exp_subtab == 1)
                        {
                            ImAdd::CheckBox("Material Changer##mod", &settings::materialchanger::enabled);
                            if (settings::materialchanger::enabled)
                            {
                                static const char* mat_names[] = {
                                    "ForceField", "Neon", "Glass", "Plastic", "SmoothPlastic",
                                    "Foil", "Ice", "Metal", "CorrodedMetal", "DiamondPlate",
                                    "Wood", "WoodPlanks", "Marble", "Slate", "Concrete",
                                    "Granite", "Brick", "Pebble", "Cobblestone", "Sand", "Fabric"
                                };
                                std::vector<const char*> mat_names_vec(mat_names, mat_names + IM_ARRAYSIZE(mat_names));
                                ImAdd::Combo("Material##mod", &settings::materialchanger::material_index, mat_names_vec);
                                ImAdd::CheckBox("Accessories##mod", &settings::materialchanger::affect_accessories);
                            }
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                }
            }
            ImAdd::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void Menu::Shutdown()
{
    if (!m_bInitialized) return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Menu::InvalidateDeviceObjects()
{
    if (!m_bInitialized) return;

    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Menu::CreateDeviceObjects()
{
    if (!m_bInitialized) return;

    ImGui_ImplDX11_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool Menu::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!m_bInitialized) return false;

#if 0
    if (uMsg == WM_KEYDOWN && wParam == VK_DELETE)
    {
        return true;
    }
#endif

    return ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
}

void Menu::DrawESPPreview()
{
    static float alpha       = 0.0f;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO&    io    = ImGui::GetIO();

    alpha += ((m_bShowESPPreview ? 1.0f : 0.0f) - alpha) * 0.15f;
    if (alpha < 0.01f) { alpha = 0.0f; return; }
    if (alpha > 0.99f)   alpha = 1.0f;

    const ImVec2 default_pos = ImVec2(520.0f, 32.0f);
    const ImVec2 default_size = ImVec2(240.0f, 340.0f);

    static bool s_was_open = false;
    bool is_opening = (m_bShowESPPreview && !s_was_open);
    s_was_open = m_bShowESPPreview;

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool win_open = ImGui::Begin("ImMagic - ESP Preview", &m_bShowESPPreview,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar(2);

    if (win_open)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImRect wbb(win->Pos, win->Pos + win->Size);

        win->DrawList->AddRectFilled(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));
        if (style.WindowBorderSize > 0.0f)
        {
            win->DrawList->AddRect(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
            win->DrawList->AddRect(wbb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), wbb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
        }

        ImAdd::RenderText(wbb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), "Esp Preview", nullptr, false, true);

        const float title_h = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        ImVec2 x_sz  = ImGui::CalcTextSize("O");
        ImVec2 x_pos = { wbb.Max.x - x_sz.x - style.FramePadding.x * 2.0f - style.WindowBorderSize, wbb.Min.y + style.WindowBorderSize };
        ImRect x_bb(x_pos, { x_pos.x + x_sz.x + style.FramePadding.x * 2.0f, x_pos.y + title_h });
        if (ImGui::IsMouseHoveringRect(x_bb.Min, x_bb.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_bShowESPPreview = false;
        ImAdd::RenderText({ x_pos.x + style.FramePadding.x, x_pos.y + (title_h - x_sz.y) * 0.5f }, "O", nullptr, false, true);

        ImGui::SetCursorScreenPos(wbb.Min + ImVec2(style.WindowPadding.x, title_h));
        if (ImGui::BeginChild("esp_body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            ImGuiWindow* body = ImGui::GetCurrentWindow();
            ImAdd::RenderFrame(body->Rect().Min, body->Rect().Max, ImGui::GetColorU32(ImGuiCol_ChildBg), true, true, true);

            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            if (canvas_size.x > 10.f && canvas_size.y > 10.f && ::render && ::render->detail && ::render->detail->device && ::render->detail->device_context)
            {
                if (ModelViewer::g_model_viewer.CreateOffscreenResources(::render->detail->device, canvas_size.x, canvas_size.y))
                {
                    ModelViewer::g_model_viewer.Render3D(::render->detail->device_context, canvas_size.x, canvas_size.y);
                    ID3D11ShaderResourceView* srv = ModelViewer::g_model_viewer.GetShaderResourceView();
                    if (srv)
                    {
                        ImVec2 img_pos = ImGui::GetCursorScreenPos();
                        ImGui::Image(reinterpret_cast<ImTextureID>(srv), canvas_size);
                        ModelViewer::g_model_viewer.RenderESPOverlay(img_pos, canvas_size);
                        if (ImGui::IsItemHovered())
                        {
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                ModelViewer::g_model_viewer.RefreshAvatar();
                            }
                            if (io.MouseWheel != 0.0f)
                            {
                                ModelViewer::g_model_viewer.cam_distance -= io.MouseWheel * 0.3f;
                                ModelViewer::g_model_viewer.cam_distance = std::clamp(ModelViewer::g_model_viewer.cam_distance, 1.2f, 20.0f);
                            }
                            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ModelViewer::g_model_viewer.is_dragging_healthbar)
                            {
                                ModelViewer::g_model_viewer.cam_yaw += io.MouseDelta.x * 0.005f;
                                ModelViewer::g_model_viewer.cam_pitch += io.MouseDelta.y * 0.005f;
                                ModelViewer::g_model_viewer.cam_pitch = std::clamp(ModelViewer::g_model_viewer.cam_pitch, -1.55f, 1.55f);
                            }
                            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                            {
                                float pan_speed = ModelViewer::g_model_viewer.cam_distance * 0.002f;
                                ModelViewer::g_model_viewer.cam_target.x -= io.MouseDelta.x * pan_speed * cosf(ModelViewer::g_model_viewer.cam_yaw);
                                ModelViewer::g_model_viewer.cam_target.z += io.MouseDelta.x * pan_speed * sinf(ModelViewer::g_model_viewer.cam_yaw);
                                ModelViewer::g_model_viewer.cam_target.y += io.MouseDelta.y * pan_speed;
                            }
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void Menu::DrawIndicator()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO&    io    = ImGui::GetIO();

    const ImVec2 win_size = { 332.0f, 177.0f };
    const ImVec2 default_pos = ImVec2(8.0f, io.DisplaySize.y - win_size.y - 8.0f);

    static bool s_was_open = false;
    bool is_opening = (m_bShowIndicator && !s_was_open);
    s_was_open = m_bShowIndicator;

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool open = ImGui::Begin("ImMagic - Indicator", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar(2);

    if (!open) { ImGui::End(); return; }

    ImRect wbb(ImGui::GetCurrentWindow()->Rect());
    ImGuiWindow* win = ImGui::GetCurrentWindow();

    win->DrawList->AddRectFilled(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));
    win->DrawList->AddRect(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
    win->DrawList->AddRect(wbb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), wbb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);

    ImAdd::RenderText(wbb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), "Indicator", nullptr, false, true);

    const float title_h = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
    ImGui::SetCursorScreenPos(wbb.Min + ImVec2(style.WindowPadding.x, title_h));

    if (ImGui::BeginChild("indicator_body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
    {
        ImGuiWindow* body = ImGui::GetCurrentWindow();
        ImRect bb = body->Rect();
        ImAdd::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_ChildBg), true, true, true);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddLine(bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 2.0f),
            ImVec2(bb.Max.x - style.ChildBorderSize * 2.0f, bb.Min.y + style.ChildBorderSize * 2.0f),
            ImGui::GetColorU32(ImGuiCol_Header), style.ChildBorderSize);
        dl->AddLine(bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 3.0f),
            ImVec2(bb.Max.x - style.ChildBorderSize * 2.0f, bb.Min.y + style.ChildBorderSize * 3.0f),
            ImGui::GetColorU32(ImGuiCol_HeaderActive), style.ChildBorderSize);
        dl->AddLine(bb.Min + ImVec2(style.ChildBorderSize * 2.0f, style.ChildBorderSize * 4.0f),
            ImVec2(bb.Max.x - style.ChildBorderSize * 2.0f, bb.Min.y + style.ChildBorderSize * 4.0f),
            ImGui::GetColorU32(ImGuiCol_BorderShadow), style.ChildBorderSize);

        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.0f, style.ChildBorderSize * 3.0f));

        ImAdd::RenderText(ImGui::GetCursorScreenPos(), "Info", nullptr, false, true);
        ImGui::Dummy(ImVec2(bb.GetWidth(), ImGui::GetFontSize()));

        float avail_h = ImGui::GetContentRegionAvail().y;

        ImVec2 av_pos = ImGui::GetCursorScreenPos();
        float  av_sz  = avail_h;
        dl->AddRectFilledMultiColor(av_pos, av_pos + ImVec2(av_sz, av_sz),
            ImGui::GetColorU32(ImGuiCol_WindowBg), ImGui::GetColorU32(ImGuiCol_WindowBg),
            ImGui::GetColorU32(ImGuiCol_ChildBg),  ImGui::GetColorU32(ImGuiCol_ChildBg));
        dl->AddRect(av_pos + ImVec2(1,1), av_pos + ImVec2(av_sz-1, av_sz-1), ImGui::GetColorU32(ImGuiCol_Border));
        const char* ph = "?";
        ImVec2 phsz = ImGui::CalcTextSize(ph);
        ImAdd::RenderText(av_pos + (ImVec2(av_sz, av_sz) - phsz) * 0.5f, ph, nullptr, false, true);
        ImGui::Dummy(ImVec2(av_sz, av_sz));

        ImGui::SameLine(0.0f, style.ItemSpacing.x);

        cache::entity_t target{};
        bool has_target = false;

        cache::entity_t aim_target = aimbot::get_player();
        cache::entity_t silent_target = silentaim::get_target();
        if (aim_target.instance.address != 0)
        {
            target = aim_target;
            has_target = true;
        }
        else if (silent_target.instance.address != 0)
        {
            target = silent_target;
            has_target = true;
        }
        else if (raycast_silentaim::target_address != 0)
        {
            std::lock_guard<std::mutex> lock(cache::mtx);
            for (const auto& entity : cache::players)
            {
                if (entity.instance.address == raycast_silentaim::target_address)
                {
                    target = entity;
                    has_target = true;
                    break;
                }
            }
        }

        if (!has_target)
        {
            std::lock_guard<std::mutex> lock(cache::mtx);
            if (!cache::players.empty())
            {
                float best_dist = 999999.0f;
                int best_idx = -1;
                for (size_t i = 0; i < cache::players.size(); ++i)
                {
                    const auto& entity = cache::players[i];
                    if (!entity.instance.address) continue;
                    if (entity.humanoid_root_part.address != 0)
                    {
                        rbx::c_primitive prim = entity.humanoid_root_part.get_primitive();
                        if (prim.address != 0)
                        {
                            math::vector3 pos = prim.get_position();
                            if (cache::get_local_player().humanoid_root_part.address != 0)
                            {
                                rbx::c_primitive local_prim = cache::get_local_player().humanoid_root_part.get_primitive();
                                if (local_prim.address != 0)
                                {
                                    float dist = (pos - local_prim.get_position()).length();
                                    if (dist < best_dist)
                                    {
                                        best_dist = dist;
                                        best_idx = static_cast<int>(i);
                                    }
                                }
                            }
                            else
                            {
                                best_idx = 0;
                                break;
                            }
                        }
                    }
                }
                if (best_idx >= 0 && best_idx < static_cast<int>(cache::players.size()))
                {
                    target = cache::players[best_idx];
                    has_target = true;
                }
            }
        }

        std::string name_str = has_target ? (target.name.empty() ? "None" : target.name) : "None";
        std::string display_str = has_target ? (target.display_name.empty() ? name_str : target.display_name) : "None";
        std::string tool_str = has_target ? (target.tool_name.empty() ? "None" : target.tool_name) : "None";

        char dist_buf[32] = "0m";
        if (has_target && target.humanoid_root_part.address != 0)
        {
            rbx::c_primitive target_prim = target.humanoid_root_part.get_primitive();
            rbx::c_primitive local_prim = cache::get_local_player().humanoid_root_part.get_primitive();
            if (target_prim.address != 0 && local_prim.address != 0)
            {
                float dist_studs = (target_prim.get_position() - local_prim.get_position()).length();
                snprintf(dist_buf, sizeof(dist_buf), "%.0fm", dist_studs / 3.57f);
            }
        }

        int target_health = 0;
        if (has_target)
        {
            float h = target.health;
            float max_h = target.max_health > 0.0f ? target.max_health : 100.0f;
            target_health = static_cast<int>(std::clamp((h / max_h) * 100.0f, 0.0f, 100.0f));
        }

        ImGui::BeginGroup();
        auto row = [&](const char* key, const char* val) {
            ImGui::TextDisabled(key);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(val).x);
            ImGui::TextUnformatted(val);
        };
        row("Name:",         name_str.c_str());
        row("Display Name:", display_str.c_str());
        row("Tool:",         tool_str.c_str());
        row("Distance:",     dist_buf);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImAdd::SliderInt("##health", &target_health, 0, 100, "%d");
        ImGui::PopItemWidth();
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    ImGui::End();
}

void Menu::DrawSettings()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO&    io    = ImGui::GetIO();

    const ImVec2 default_pos = ImVec2(768.0f, 32.0f);
    const ImVec2 default_size = ImVec2(504.0f, 420.0f);

    static bool s_was_open = false;
    bool is_opening = (m_bShowSettings && !s_was_open);
    s_was_open = m_bShowSettings;

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool main_window = ImGui::Begin("ImMagic - Settings", (bool*)0,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    if (main_window)
    {
        ImRect window_bb(ImGui::GetCurrentWindow()->Rect());
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        window->DrawList->AddRectFilled(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));

        if (style.WindowBorderSize > 0.0f)
        {
            window->DrawList->AddRect(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
            window->DrawList->AddRect(window_bb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), window_bb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
        }

        ImAdd::RenderText(window_bb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), "Settings", NULL, false, true);

        const float title_h = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        ImVec2 x_sz  = ImGui::CalcTextSize("X");
        ImVec2 x_pos = { window_bb.Max.x - x_sz.x - style.FramePadding.x * 2.0f - style.WindowBorderSize, window_bb.Min.y + style.WindowBorderSize };
        ImRect x_bb(x_pos, { x_pos.x + x_sz.x + style.FramePadding.x * 2.0f, x_pos.y + title_h });
        if (ImGui::IsMouseHoveringRect(x_bb.Min, x_bb.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_bShowSettings = false;
        ImAdd::RenderText({ x_pos.x + style.FramePadding.x, x_pos.y + (title_h - x_sz.y) * 0.5f }, "X", nullptr, false, true);

        ImGui::SetCursorScreenPos(window_bb.Min + ImVec2(style.WindowPadding.x, title_h));

        if (ImGui::BeginChild("settings_body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            ImGuiWindow* body = ImGui::GetCurrentWindow();
            ImRect bb = body->Rect();
            ImAdd::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_ChildBg), true, true, true);

            static int appearance_tab = 0;
            static int theme_index = 0;
            static int prev_theme_index = -1;

            if (theme_index != prev_theme_index)
            {
                ApplyTheme(theme_index);
                prev_theme_index = theme_index;
            }

            float group_width = ImTrunc((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) / 2);

            ImGui::BeginGroup();

            if (ImAdd::BeginChild("appearance_left", { "Theme", "Colors", "Preview" }, &appearance_tab, ImVec2(group_width, 0.0f)))
            {
                if (appearance_tab == 0)
                {
                    ImAdd::Combo("Theme", &theme_index, {
                        "Default", "Purple", "Tokyo", "Lagoon", "Primordial", "Jester",
                        "Temple", "Nebula", "Abyss", "Fatality", "Neverlose",
                        "Aimware", "Youtube", "Gamesense", "Onetap", "Entropy",
                        "Interwebz", "Dracula", "Spotify", "Sublime", "Vape",
                        "Neko", "Corn", "Minecraft", "Nord", "Monokai",
                        "Obsidian", "RosePine"
                        });
                }
                else if (appearance_tab == 1)
                {
                    ImAdd::ColorEdit4("Shadow", (float*)&style.Colors[ImGuiCol_FrameBgShadow]);
                    ImAdd::ColorEdit4("Border", (float*)&style.Colors[ImGuiCol_Border]);
                    ImAdd::ColorEdit4("Border Shadow", (float*)&style.Colors[ImGuiCol_BorderShadow]);
                    ImAdd::ColorEdit4("Text", (float*)&style.Colors[ImGuiCol_Text]);
                    ImAdd::ColorEdit4("Text Disabled", (float*)&style.Colors[ImGuiCol_TextDisabled]);
                    ImAdd::ColorEdit4("Window Background", (float*)&style.Colors[ImGuiCol_WindowBg]);
                    ImAdd::ColorEdit4("Child Background", (float*)&style.Colors[ImGuiCol_ChildBg]);
                    ImAdd::ColorEdit4("Popup Background", (float*)&style.Colors[ImGuiCol_PopupBg]);
                    ImAdd::ColorEdit4("Frame", (float*)&style.Colors[ImGuiCol_FrameBg]);
                    ImAdd::ColorEdit4("Frame Hovered", (float*)&style.Colors[ImGuiCol_FrameBgHovered]);
                    ImAdd::ColorEdit4("Frame Active", (float*)&style.Colors[ImGuiCol_FrameBgActive]);
                    ImAdd::ColorEdit4("Accent", (float*)&style.Colors[ImGuiCol_Header]);
                    ImAdd::ColorEdit4("Accent Hovered", (float*)&style.Colors[ImGuiCol_HeaderHovered]);
                    ImAdd::ColorEdit4("Accent Active", (float*)&style.Colors[ImGuiCol_HeaderActive]);

                    style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
                    style.Colors[ImGuiCol_SliderGrab] = style.Colors[ImGuiCol_Header];
                    style.Colors[ImGuiCol_SliderGrabActive] = style.Colors[ImGuiCol_HeaderActive];
                    style.Colors[ImGuiCol_Button] = style.Colors[ImGuiCol_FrameBg];
                    style.Colors[ImGuiCol_ButtonHovered] = style.Colors[ImGuiCol_FrameBgHovered];
                    style.Colors[ImGuiCol_ButtonActive] = style.Colors[ImGuiCol_FrameBgActive];
                    style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_FrameBg];
                    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_FrameBgHovered];
                    style.Colors[ImGuiCol_TabActive] = style.Colors[ImGuiCol_FrameBgActive];
                    style.Colors[ImGuiCol_ButtonShadow] = style.Colors[ImGuiCol_FrameBgShadow];

                    ImGui::Spacing();
                    if (ImAdd::Button("Extract Colors", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                    {
                        auto to_hex = [](const ImVec4& c) -> std::string {
                            char buf[16];
                            sprintf_s(buf, "#%02X%02X%02X%02X", (int)(c.x * 255.f + 0.5f), (int)(c.y * 255.f + 0.5f), (int)(c.z * 255.f + 0.5f), (int)(c.w * 255.f + 0.5f));
                            return std::string(buf);
                        };

                        std::string out = "//Extracted Colors n";
                        struct ColEntry { const char* name; ImVec4 col; };
                        ColEntry entries[] = {
                            { "Shadow", style.Colors[ImGuiCol_FrameBgShadow] },
                            { "Border", style.Colors[ImGuiCol_Border] },
                            { "Border Shadow", style.Colors[ImGuiCol_BorderShadow] },
                            { "Text", style.Colors[ImGuiCol_Text] },
                            { "Text Disabled", style.Colors[ImGuiCol_TextDisabled] },
                            { "Window Background", style.Colors[ImGuiCol_WindowBg] },
                            { "Child Background", style.Colors[ImGuiCol_ChildBg] },
                            { "Popup Background", style.Colors[ImGuiCol_PopupBg] },
                            { "Frame", style.Colors[ImGuiCol_FrameBg] },
                            { "Frame Hovered", style.Colors[ImGuiCol_FrameBgHovered] },
                            { "Frame Active", style.Colors[ImGuiCol_FrameBgActive] },
                            { "Accent", style.Colors[ImGuiCol_Header] },
                            { "Accent Hovered", style.Colors[ImGuiCol_HeaderHovered] },
                            { "Accent Active", style.Colors[ImGuiCol_HeaderActive] },
                        };

                        for (const auto& e : entries)
                        {
                            char line[160];
                            sprintf_s(line, "%-18s: %s | rgba(%d, %d, %d, %.2f) | { %.3ff, %.3ff, %.3ff, %.3ff }\n",
                                e.name, to_hex(e.col).c_str(),
                                (int)(e.col.x * 255.f + 0.5f), (int)(e.col.y * 255.f + 0.5f), (int)(e.col.z * 255.f + 0.5f), e.col.w,
                                e.col.x, e.col.y, e.col.z, e.col.w);
                            out += line;
                        }

                        ImGui::SetClipboardText(out.c_str());

                        if (OpenClipboard(nullptr))
                        {
                            EmptyClipboard();
                            HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, out.size() + 1);
                            if (hGlob)
                            {
                                void* p = GlobalLock(hGlob);
                                if (p)
                                {
                                    memcpy(p, out.c_str(), out.size() + 1);
                                    GlobalUnlock(hGlob);
                                    SetClipboardData(CF_TEXT, hGlob);
                                }
                            }
                            CloseClipboard();
                        }

                        ImNotify::Print(NotifyLevel::Success, "Extracted colors copied to clipboard!");
                    }
                }
                else if (appearance_tab == 2)
                {
                    static const char* preview_models[] = { "Arsenal", "Client", "Tung Tung Tung Sahur" };
                    std::vector<const char*> preview_models_vec(preview_models, preview_models + IM_ARRAYSIZE(preview_models));
                    if (ImAdd::Combo("Preview Model", &settings::misc::preview_model, preview_models_vec))
                    {
                        ModelViewer::g_model_viewer.RefreshAvatar();
                    }

                    ImAdd::CheckBox("Auto Rotate Preview", &ModelViewer::g_model_viewer.auto_rotate);
                    if (ModelViewer::g_model_viewer.auto_rotate)
                    {
                        ImAdd::SliderFloat("Rotation Speed", &ModelViewer::g_model_viewer.rotate_speed, 0.1f, 10.0f, "%.1f");
                    }
                }
            }
            ImAdd::EndChild();

            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();

            float right_child_h = ImTrunc((ImGui::GetContentRegionAvail().y - style.ItemSpacing.y) / 2);

            if (ImAdd::BeginChild("appearance_right_top", ImVec2(0.0f, right_child_h)))
            {
                ImGui::TextDisabled("Config");
                ImGui::Spacing();

                static float anim_speed = 0.07f;
                static float notify_time = 3.0f;

                ImAdd::SliderFloat("Animation Speed (notification)", &anim_speed, 0.01f, 0.5f, "%.2f");
                g_AnimationSpeed = anim_speed;

                ImAdd::SliderFloat("Notification Time(notification)", &notify_time, 1.0f, 10.0f, "%.1fs");
                ImNotify::fWaitTime = notify_time;

                ImAdd::SliderFloat("Tab Height", &g_TabHeight, 0.0f, 40.0f, "%.0f");
                ImAdd::CheckBox("Fill Tab Space", &g_TabFillSpace);
            }
            ImAdd::EndChild();

            if (ImAdd::BeginChild("appearance_right_bottom", ImVec2(0.0f, 0.0f)))
            {
                ImGui::TextDisabled("Display");
                ImGui::Spacing();

                ImAdd::CheckBox("V-Sync##settings", &settings::vsync);
                ImAdd::CheckBox("Show Watermark##settings", &m_bShowWatermark);
            }
            ImAdd::EndChild();

            ImGui::EndGroup();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void Menu::DrawConfig()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    const ImVec2 default_pos = ImVec2(768.0f, 458.0f);
    const ImVec2 default_size = ImVec2(504.0f, 410.0f);

    static bool s_was_open = false;
    bool is_opening = (m_bShowConfig && !s_was_open);
    s_was_open = m_bShowConfig;

    if (is_opening) {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool main_window = ImGui::Begin("ImMagic - Config Manager", &m_bShowConfig, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    if (main_window)
    {
        ImRect window_bb(ImGui::GetCurrentWindow()->Rect());

        if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoBackground)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();

            window->DrawList->AddRectFilled(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));

            if (style.WindowBorderSize > 0.0f)
            {
                window->DrawList->AddRect(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
                window->DrawList->AddRect(window_bb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), window_bb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
            }

            ImAdd::RenderText(window_bb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), "Config Manager", NULL, false, true);
        }

        static int config_subtab = 0;
        float header_h = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        ImGui::SetCursorScreenPos(window_bb.Min + ImVec2(style.WindowPadding.x, header_h));

        if (ImGui::BeginChild("body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            static std::vector<config::config_info_t> cfg_list = config::get_config_list();
            static int selected_cfg = -1;
            static char new_cfg_name[64] = "";

            std::string current_autoload = config::get_autoload();
            static int config_subtab = 0;
            if (ImAdd::BeginChild("config_subtabs", { "Configs", "Auto-Load" }, &config_subtab, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            {
                if (config_subtab == 0)
                {
                    if (ImAdd::BeginChild("config_list_container", ImVec2(220.0f, 0.0f)))
                    {
                        ImGui::TextDisabled("Available Configs");
                        ImGui::Spacing();

                        if (ImGui::BeginListBox("##configs_box", ImVec2(-FLT_MIN, -FLT_MIN)))
                        {
                            for (int i = 0; i < (int)cfg_list.size(); i++)
                            {
                                bool is_selected = (selected_cfg == i);
                                bool is_auto = (cfg_list[i].name == current_autoload);

                                std::string display_name = cfg_list[i].name;
                                if (is_auto) display_name += " [Auto]";

                                if (ImGui::Selectable(display_name.c_str(), is_selected))
                                {
                                    selected_cfg = i;
                                }

                                if (is_selected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndListBox();
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::SameLine();
                    ImGui::BeginGroup();

                    if (ImAdd::BeginChild("config_actions", ImVec2(0.0f, 0.0f)))
                    {
                        ImGui::TextDisabled("Management");
                        ImGui::Spacing();

                        ImGui::InputTextWithHint("##new_cfg", "Config Name", new_cfg_name, sizeof(new_cfg_name));
                        if (ImAdd::Button("Create Config", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (strlen(new_cfg_name) > 0)
                            {
                                if (config::save_config(new_cfg_name))
                                {
                                    cfg_list = config::get_config_list();
                                    ImNotify::Print(NotifyLevel::Success, "Config created successfully!");
                                    new_cfg_name[0] = '\0';
                                }
                                else
                                {
                                    ImNotify::Print(NotifyLevel::Error, "Failed to create config!");
                                }
                            }
                        }

                        bool has_selection = (selected_cfg >= 0 && selected_cfg < (int)cfg_list.size());
                        std::string sel_name = has_selection ? cfg_list[selected_cfg].name : "";

                        if (!has_selection) ImGui::BeginDisabled();

                        if (ImAdd::Button("Save Selected", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (config::save_config(sel_name))
                            {
                                ImNotify::Print(NotifyLevel::Success, "Config saved!");
                            }
                            else
                            {
                                ImNotify::Print(NotifyLevel::Error, "Failed to save config!");
                            }
                        }

                        if (ImAdd::Button("Load Selected", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (config::load_config(sel_name))
                            {
                                ImNotify::Print(NotifyLevel::Success, "Config loaded!");
                            }
                            else
                            {
                                ImNotify::Print(NotifyLevel::Error, "Failed to load config!");
                            }
                        }

                        if (ImAdd::Button("Delete Config", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (config::delete_config(sel_name))
                            {
                                cfg_list = config::get_config_list();
                                selected_cfg = -1;
                                ImNotify::Print(NotifyLevel::Success, "Config deleted!");
                            }
                            else
                            {
                                ImNotify::Print(NotifyLevel::Error, "Failed to delete config!");
                            }
                        }

                        if (!has_selection) ImGui::EndDisabled();


                        if (ImAdd::Button("Refresh List", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            cfg_list = config::get_config_list();
                            ImNotify::Print(NotifyLevel::Info, "Config list refreshed!");
                        }

                        if (ImAdd::Button("Export to Clipboard", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (config::export_config_to_clipboard())
                            {
                                ImNotify::Print(NotifyLevel::Success, "Config exported to clipboard!");
                            }
                        }

                        if (ImAdd::Button("Import from Clipboard", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                        {
                            if (config::import_config_from_clipboard())
                            {
                                cfg_list = config::get_config_list();
                                ImNotify::Print(NotifyLevel::Success, "Config imported from clipboard!");
                            }
                            else
                            {
                                ImNotify::Print(NotifyLevel::Error, "Failed to import config!");
                            }
                        }
                    }
                    ImAdd::EndChild();

                    ImGui::EndGroup();
                }
                else if (config_subtab == 1)
                {
                    if (ImAdd::BeginChild("autoload_settings", ImVec2(0.0f, 0.0f)))
                    {
                        ImGui::TextDisabled("Auto Load Configuration");
                        ImGui::Spacing();

                        bool has_autoload = !current_autoload.empty();
                        ImGui::Text("Current : %s", has_autoload ? current_autoload.c_str() : "None");
                        ImGui::Spacing();

                        bool has_selection = (selected_cfg >= 0 && selected_cfg < (int)cfg_list.size());
                        std::string sel_name = has_selection ? cfg_list[selected_cfg].name : "";

                        if (has_selection)
                        {
                            bool is_current_auto = (sel_name == current_autoload);
                            bool auto_check = is_current_auto;
                            if (ImAdd::CheckBox(("Set '" + sel_name + "' as Auto-Load").c_str(), &auto_check))
                            {
                                if (auto_check)
                                {
                                    config::set_autoload(sel_name);
                                    ImNotify::Print(NotifyLevel::Info, "Auto-load config set!");
                                }
                                else
                                {
                                    config::clear_autoload();
                                    ImNotify::Print(NotifyLevel::Info, "Auto-load config cleared!");
                                }
                            }
                        }
                        else

                        if (has_autoload)
                        {
                            if (ImAdd::Button("Clear Auto Load Config", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                            {
                                config::clear_autoload();
                                ImNotify::Print(NotifyLevel::Info, "Auto load cleared!");
                            }
                        }
                    }
                    ImAdd::EndChild();
                }
            }
            ImAdd::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}
