#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "colors.h"

namespace Theme {
    inline float Outline[4] = { 50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }; // #323232
    inline float TextMisc[4] = { 132.0f / 255.0f, 132.0f / 255.0f, 132.0f / 255.0f, 1.0f }; // #848484
    inline float MenuBg[4] = { 30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 1.0f }; // #1E1E1E
    inline float TabUnselected[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f }; // #141414
    inline float Inline[4] = { 50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f }; // #323232
    inline float HighContrast[4] = { 30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 1.0f }; // #1E1E1E
    inline float LowContrast[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f }; // #141414
    inline float PageBg[4] = { 30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 1.0f }; // #1E1E1E
    inline float SectionBg[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f }; // #141414
    inline float TextMain[4] = { 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 1.0f }; // #FFFFFF
    inline float Accent[4] = { 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 1.0f }; // #FFFFFF

    inline ImColor GetOutline() { return ImColor(Outline[0], Outline[1], Outline[2], Outline[3]); }
    inline ImColor GetTextMisc() { return ImColor(TextMisc[0], TextMisc[1], TextMisc[2], TextMisc[3]); }
    inline ImColor GetMenuBg() { return ImColor(MenuBg[0], MenuBg[1], MenuBg[2], MenuBg[3]); }
    inline ImColor GetTabUnselected() { return ImColor(TabUnselected[0], TabUnselected[1], TabUnselected[2], TabUnselected[3]); }
    inline ImColor GetInline() { return ImColor(Inline[0], Inline[1], Inline[2], Inline[3]); }
    inline ImColor GetHighContrast() { return ImColor(HighContrast[0], HighContrast[1], HighContrast[2], HighContrast[3]); }
    inline ImColor GetLowContrast() { return ImColor(LowContrast[0], LowContrast[1], LowContrast[2], LowContrast[3]); }
    inline ImColor GetPageBg() { return ImColor(PageBg[0], PageBg[1], PageBg[2], PageBg[3]); }
    inline ImColor GetSectionBg() { return ImColor(SectionBg[0], SectionBg[1], SectionBg[2], SectionBg[3]); }
    inline ImColor GetTextMain() { return ImColor(TextMain[0], TextMain[1], TextMain[2], TextMain[3]); }
    inline ImColor GetAccent() { return ImColor(Accent[0], Accent[1], Accent[2], Accent[3]); }

    static const char* kThemeNames[] = {
        "Ivory",
        "Pink",
        "Imgui",
        "Purple",
        "Violet",
        "Lagoon",
        "Primordial",
        "Jester",
        "Temple",
        "Nebula",
        "Abyss",
        "Neverlose",
        "Aimware",
        "Gamesense",
        "Onetap",
        "Entropy",
        "Dracula",
        "Spotify",
        "Sublime",
        "Vape",
        "Neko",
        "Corn",
        "Minecraft",
        "Monokai",
        "Obsidian",
        "RosePine",
        "Daydream 2",
        "Rosewood",
        "Nocturne",
        "Gilded",
        "Cherry",
        "Blue",
        "Purplish",
        "Assembl",
        "DavidHook",
        "Redv2",
    };

    inline int GetThemeCount() { return (int)(sizeof(kThemeNames) / sizeof(kThemeNames[0])); }
    inline const char* const* GetThemeNames() { return kThemeNames; }

    static inline ImVec4 hex(unsigned int c, float a = 1.0f)
    {
        return ImVec4(
            ((c >> 16) & 255) / 255.0f,
            ((c >> 8) & 255) / 255.0f,
            (c & 255) / 255.0f,
            a);
    }

    static inline ImVec4 rgb(int r, int g, int b, float a = 1.0f)
    {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }

    static inline void set4(float* dst, ImVec4 v)
    {
        dst[0] = v.x;
        dst[1] = v.y;
        dst[2] = v.z;
        dst[3] = v.w;
    }

    static inline ImVec4 scale_rgb(ImVec4 c, float f)
    {
        return ImVec4(
            ImMax(c.x * f, 0.0f),
            ImMax(c.y * f, 0.0f),
            ImMax(c.z * f, 0.0f),
            c.w);
    }

    struct Preset {
        ImVec4 outline, inlin, accent, high_contrast, low_contrast, text;
    };

    inline void sync_colors()
    {
        if (clr)
        {
            clr->accent = GetAccent();
            clr->window.background_one = GetPageBg();
            clr->window.background_two = GetHighContrast();
            clr->window.stroke = GetInline();
            clr->widgets.stroke_two = GetOutline();
            clr->widgets.text = GetTextMain();
            clr->widgets.text_inactive = GetTextMisc();
        }
    }

    inline void ResetDefaults()
    {
        set4(Outline, rgb(50, 50, 50));
        set4(Inline, rgb(50, 50, 50));
        set4(MenuBg, rgb(30, 30, 30));
        set4(TabUnselected, rgb(20, 20, 20));
        set4(HighContrast, rgb(30, 30, 30));
        set4(LowContrast, rgb(20, 20, 20));
        set4(PageBg, rgb(30, 30, 30));
        set4(SectionBg, rgb(20, 20, 20));
        set4(TextMain, rgb(255, 255, 255));
        set4(TextMisc, rgb(132, 132, 132));
        set4(Accent, rgb(255, 255, 255));
        sync_colors();
    }

    static inline void apply_menu_colors(const Preset& t)
    {
        set4(Outline, t.outline);
        set4(Inline, t.inlin);
        set4(Accent, t.accent);
        set4(HighContrast, t.high_contrast);
        set4(LowContrast, t.low_contrast);
        set4(TextMain, t.text);
        // Ensure text_inactive has a minimum readability threshold across all themes
        float misc_r = ImMax(t.text.x * 0.70f, 0.55f);
        float misc_g = ImMax(t.text.y * 0.70f, 0.55f);
        float misc_b = ImMax(t.text.z * 0.70f, 0.55f);
        set4(TextMisc, ImVec4(misc_r, misc_g, misc_b, 1.0f));
        set4(MenuBg, scale_rgb(t.low_contrast, 0.95f));
        set4(SectionBg, scale_rgb(t.low_contrast, 0.95f));
        set4(TabUnselected, scale_rgb(t.low_contrast, 0.8f));
        set4(PageBg, scale_rgb(t.high_contrast, 0.95f));
    }

    inline void ApplyTheme(int index)
    {
        if (index < 0 || index >= GetThemeCount())
            return;

        if (index == 0)
        {
            ResetDefaults();
            return;
        }

        if (index == 1)
        {
            set4(Outline, hex(0x000000));
            set4(Inline, hex(0x202020));
            set4(Accent, hex(0xFF9DB4));
            set4(HighContrast, hex(0x111010));
            set4(LowContrast, hex(0x0B0B0B));
            set4(PageBg, hex(0x0B0B0B));
            set4(MenuBg, hex(0x0B0B0B));
            set4(SectionBg, hex(0x0B0B0B));
            set4(TabUnselected, hex(0x0B0B0B));
            set4(TextMain, hex(0xFFFFFF));
            set4(TextMisc, hex(0x8C8C8C));
            sync_colors();
            return;
        }

        if (index == 2)
        {
            Preset t = {
                hex(0x000000),
                hex(0x323232),
                hex(0x3264ff),
                hex(0x1e1e1e),
                hex(0x141414),
                hex(0xffffff),
            };
            apply_menu_colors(t);
            set4(MenuBg, hex(0x141414));
            set4(SectionBg, hex(0x141414));
            set4(TabUnselected, hex(0x101010));
            set4(PageBg, hex(0x1e1e1e));
            set4(TextMisc, hex(0x848484));
            sync_colors();
            return;
        }

        static const Preset presets[] = {
            { rgb(0, 0, 0), rgb(30, 30, 30), rgb(131, 131, 219), rgb(13, 10, 23), rgb(13, 10, 23), rgb(180, 180, 180) },
            { rgb(0, 0, 0), rgb(50, 50, 50), rgb(103, 89, 179), rgb(22, 22, 31), rgb(25, 25, 37), rgb(255, 255, 255) },
            { rgb(0, 0, 0), rgb(44, 54, 90), rgb(41, 92, 168), rgb(32, 35, 51), rgb(38, 43, 60), rgb(255, 255, 255) },
            { rgb(0, 0, 0), rgb(67, 67, 67), rgb(194, 155, 165), rgb(31, 31, 31), rgb(21, 21, 21), rgb(255, 255, 255) },
            { rgb(0, 0, 0), rgb(55, 55, 55), rgb(219, 68, 103), rgb(28, 28, 28), rgb(36, 36, 36), rgb(255, 255, 255) },
            { rgb(10, 10, 10), rgb(45, 45, 45), rgb(220, 142, 240), rgb(28, 28, 28), rgb(28, 28, 28), rgb(180, 180, 180) },
            { rgb(13, 1, 6), rgb(32, 8, 18), rgb(150, 42, 85), rgb(24, 5, 13), rgb(18, 4, 10), rgb(180, 180, 180) },
            { rgb(10, 10, 10), rgb(45, 45, 45), rgb(140, 135, 180), rgb(30, 30, 30), rgb(20, 20, 20), rgb(255, 255, 255) },
            { rgb(0, 0, 5), rgb(10, 30, 40), rgb(0, 180, 240), rgb(0, 15, 30), rgb(5, 5, 20), rgb(255, 255, 255) },
            { rgb(0, 0, 5), rgb(55, 55, 55), rgb(200, 40, 40), rgb(43, 43, 43), rgb(25, 25, 25), rgb(232, 232, 232) },
            { rgb(0, 0, 0), rgb(40, 40, 40), rgb(167, 217, 77), rgb(23, 23, 23), rgb(12, 12, 12), rgb(255, 255, 255) },
            { rgb(0, 0, 0), rgb(78, 81, 88), rgb(221, 168, 93), rgb(44, 48, 55), rgb(31, 33, 37), rgb(214, 217, 224) },
            { rgb(10, 10, 10), rgb(76, 74, 82), rgb(129, 187, 233), rgb(61, 58, 67), rgb(48, 47, 55), rgb(220, 220, 220) },
            { rgb(32, 33, 38), rgb(60, 56, 77), rgb(154, 129, 179), rgb(42, 44, 56), rgb(37, 39, 48), rgb(220, 220, 220) },
            { rgb(10, 10, 10), rgb(41, 41, 41), rgb(30, 215, 96), rgb(24, 24, 24), rgb(18, 18, 18), rgb(208, 208, 208) },
            { rgb(0, 0, 0), rgb(72, 73, 72), rgb(255, 152, 0), rgb(50, 51, 45), rgb(40, 41, 35), rgb(232, 255, 255) },
            { rgb(10, 10, 10), rgb(54, 54, 54), rgb(38, 134, 106), rgb(31, 31, 31), rgb(26, 26, 26), rgb(220, 220, 220) },
            { rgb(0, 0, 0), rgb(45, 45, 45), rgb(210, 31, 106), rgb(23, 23, 23), rgb(19, 19, 19), rgb(255, 255, 255) },
            { rgb(0, 0, 0), rgb(51, 51, 51), rgb(255, 144, 0), rgb(37, 37, 37), rgb(25, 25, 25), rgb(220, 220, 220) },
            { rgb(0, 0, 0), rgb(51, 51, 51), rgb(39, 206, 64), rgb(51, 51, 51), rgb(38, 38, 38), rgb(255, 255, 255) },
            { rgb(20, 20, 20), rgb(39, 40, 34), rgb(249, 38, 114), rgb(50, 50, 50), rgb(30, 30, 30), rgb(248, 248, 242) },
            { rgb(5, 5, 5), rgb(28, 28, 28), rgb(120, 130, 150), rgb(38, 38, 38), rgb(18, 18, 18), rgb(210, 210, 210) },
            { rgb(25, 23, 36), rgb(31, 29, 46), rgb(235, 188, 186), rgb(40, 38, 58), rgb(28, 26, 40), rgb(224, 222, 244) },
            { rgb(12, 12, 12), rgb(85, 85, 85), rgb(51, 153, 255), rgb(58, 58, 58), rgb(37, 37, 37), rgb(255, 255, 255) },
            { rgb(15, 10, 8), rgb(90, 65, 58), rgb(255, 179, 179), rgb(82, 58, 50), rgb(61, 43, 38), rgb(252, 228, 228) },
            { rgb(12, 15, 22), rgb(55, 65, 85), rgb(250, 204, 21), rgb(43, 52, 70), rgb(28, 35, 49), rgb(230, 232, 240) },
            { rgb(10, 10, 10), rgb(55, 55, 55), rgb(196, 165, 90), rgb(40, 40, 40), rgb(22, 22, 22), rgb(180, 180, 180) },
            { hex(0x0D0106), hex(0x200812), hex(0x962A55), hex(0x18050D), hex(0x16040C), hex(0xB4B4B4) },
            { rgb(0, 0, 0), rgb(28, 41, 64), rgb(42, 122, 222), rgb(3, 1, 18), rgb(0, 2, 23), rgb(210, 210, 210) },
            { rgb(32, 32, 38), rgb(60, 55, 75), rgb(155, 125, 175), rgb(36, 36, 48), rgb(42, 42, 56), rgb(180, 180, 180) },
            { rgb(0, 0, 0), rgb(43, 48, 64), rgb(139, 152, 199), rgb(10, 11, 16), rgb(25, 28, 37), rgb(221, 234, 246) },
            { rgb(10, 10, 13), rgb(30, 30, 30), rgb(152, 122, 173), rgb(14, 15, 14), rgb(25, 25, 25), rgb(254, 255, 254) },
            { rgb(39, 19, 27), rgb(39, 19, 27), rgb(141, 41, 81), rgb(39, 19, 27), rgb(26, 10, 19), rgb(200, 200, 200) }
        };

        const int preset_index = index - 3;
        if (preset_index < 0 || preset_index >= (int)(sizeof(presets) / sizeof(presets[0])))
            return;

        if (index == 35) // Redv2 (matches drain theme_index 11)
        {
            set4(Outline, rgb(39, 19, 27));
            set4(Inline, rgb(39, 19, 27));
            set4(Accent, rgb(141, 41, 81));
            set4(HighContrast, rgb(39, 19, 27));
            set4(LowContrast, rgb(26, 10, 19));
            set4(PageBg, rgb(26, 10, 19));
            set4(MenuBg, rgb(26, 10, 19));
            set4(SectionBg, rgb(26, 10, 19));
            set4(TabUnselected, rgb(26, 10, 19));
            set4(TextMain, rgb(200, 200, 200));
            set4(TextMisc, rgb(136, 136, 136));
            sync_colors();
            return;
        }

        const Preset& t = presets[preset_index];
        apply_menu_colors(t);
        sync_colors();
    }
}
