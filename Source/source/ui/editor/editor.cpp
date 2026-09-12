#include "editor.h"
#include <imgui/imgui_internal.h>
#include <ui/menu/settings/theme.h>
#include <ui/menu/settings/colors.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace editor
{
    enum token_kind { tok_plain, tok_kw, tok_str, tok_num, tok_comment, tok_ident };

    static const char* k_keywords[] = {
        "and", "break", "continue", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while", "typeof", nullptr
    };

    static const char* k_completions[] = {
        "and", "break", "continue", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while", "typeof",

        "print", "warn", "error", "assert", "pairs", "ipairs", "next", "type",
        "tostring", "tonumber", "require", "pcall", "xpcall", "select", "unpack",
        "getmetatable", "setmetatable", "rawget", "rawset", "rawequal", "rawlen",
        "tick", "time", "setreadonly", "make_writeable", "isreadonly",

        "math.abs", "math.acos", "math.asin", "math.atan", "math.atan2", "math.ceil",
        "math.clamp", "math.cos", "math.cosh", "math.deg", "math.exp", "math.floor",
        "math.fmod", "math.frexp", "math.huge", "math.ldexp", "math.log", "math.log10",
        "math.max", "math.min", "math.modf", "math.pi", "math.pow", "math.rad",
        "math.random", "math.randomseed", "math.round", "math.sign", "math.sin",
        "math.sinh", "math.sqrt", "math.tan", "math.tanh",

        "string.byte", "string.char", "string.find", "string.format", "string.gmatch",
        "string.gsub", "string.len", "string.lower", "string.match", "string.pack",
        "string.packsize", "string.rep", "string.reverse", "string.sub", "string.unpack",
        "string.upper", "string.split",

        "table.clear", "table.clone", "table.concat", "table.create", "table.find",
        "table.freeze", "table.insert", "table.isfrozen", "table.maxn", "table.move",
        "table.pack", "table.remove", "table.sort", "table.unpack",

        "task.wait", "task.spawn", "task.defer", "task.delay", "task.cancel",

        "Vector2", "Vector2.new", "Vector2.zero", "Vector2.one",
        "Vector3", "Vector3.new", "Vector3.zero", "Vector3.one", "Vector3.xAxis", "Vector3.yAxis", "Vector3.zAxis",
        "CFrame", "CFrame.new", "CFrame.Angles", "CFrame.lookAt", "CFrame.identity",
        "Color3", "Color3.new", "Color3.fromRGB", "Color3.fromHSV", "Color3.fromHex",
        "UDim", "UDim.new", "UDim2", "UDim2.new", "UDim2.fromOffset", "UDim2.fromScale",
        "Ray", "Ray.new", "RaycastParams", "RaycastParams.new",
        "Instance", "Instance.new", "TweenInfo", "TweenInfo.new",

        "Drawing", "Drawing.new", "Drawing.Fonts",
        "draw", "draw.text", "draw.text_size", "draw.line", "draw.rect", "draw.circle",
        "isrenderobj", "getrenderproperty", "setrenderproperty", "cleardrawcache",

        "game", "workspace", "Workspace", "Players", "Lighting", "ReplicatedStorage",
        "ReplicatedFirst", "ServerStorage", "ServerScriptService", "TweenService",
        "RunService", "UserInputService", "HttpService", "CoreGui", "SoundService",
        "Debris", "PathfindingService", "ContextActionService",

        "GetService", "FindFirstChild", "FindFirstChildOfClass", "FindFirstChildWhichIsA",
        "FindFirstAncestor", "FindFirstAncestorOfClass", "FindFirstAncestorWhichIsA",
        "WaitForChild", "GetChildren", "GetDescendants", "IsA", "Clone", "Destroy", "ClearAllChildren",
        "LocalPlayer", "Character", "Humanoid", "HumanoidRootPart", "Head", "Torso",
        "Position", "Size", "Name", "Parent", "ClassName", "Transparency",
        "CanCollide", "Anchored", "Health", "MaxHealth", "WalkSpeed", "JumpPower",
        "Connect", "Disconnect", "Fire", "FireServer", "InvokeServer",

        "getgenv", "getrenv", "getreg", "getgc", "getinstances", "getnilinstances",
        "getloadedmodules", "getscripts", "getsenv", "getrawmetatable", "setrawmetatable",
        "hookfunction", "replaceclosure", "islclosure", "isluau", "checkcaller",
        "fireclickdetector", "fireproximityprompt", "firetouchinterest",
        "mouse1click", "mouse1press", "mouse1release", "mouse2click", "mouse2press", "mouse2release",
        "mousemoverel", "mousemoveabs", "keyclick", "keypress", "keyrelease",
        "getclipboard", "setclipboard", "identifyexecutor", "getexecutorname",

        nullptr
    };

    static bool is_ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    static bool is_ident(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

    static int utf8_len(const char* s, size_t n)
    {
        if (!n) return 0;
        const unsigned char c = (unsigned char)s[0];
        int need = 1;
        if (c >= 0xF0) need = 4;
        else if (c >= 0xE0) need = 3;
        else if (c >= 0xC0) need = 2;
        if ((size_t)need > n) return 1;
        for (int i = 1; i < need; ++i)
            if (((unsigned char)s[i] & 0xC0) != 0x80) return 1;
        return need;
    }

    static bool is_kw(const char* s, size_t n)
    {
        for (int i = 0; k_keywords[i]; ++i)
            if (std::strncmp(k_keywords[i], s, n) == 0 && k_keywords[i][n] == 0)
                return true;
        return false;
    }

    static ImU32 col_for(token_kind k)
    {
        switch (k)
        {
        case tok_kw:      return IM_COL32(255, 118, 118, 255);
        case tok_str:     return IM_COL32(230, 175, 110, 255);
        case tok_num:     return IM_COL32(145, 215, 155, 255);
        case tok_comment: return IM_COL32(115, 120, 130, 255);
        case tok_ident:   return IM_COL32(240, 240, 240, 255);
        default:          return IM_COL32(220, 220, 220, 255);
        }
    }

    lua_editor::lua_editor()
    {
        lines.push_back("");
    }

    void lua_editor::set_text(const std::string& text)
    {
        lines.clear();
        std::string cur;
        for (char c : text)
        {
            if (c == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else if (c != '\r')
                cur.push_back(c);
        }
        lines.push_back(cur);
        if (lines.empty())
            lines.push_back("");
        cursor_row = sel_row = 0;
        cursor_col = sel_col = 0;
        scroll_y = scroll_x = 0.f;
        prefer_ac = false;
        ac_items.clear();
    }

    std::string lua_editor::get_text() const
    {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i) out.push_back('\n');
            out += lines[i];
        }
        return out;
    }

    static void clamp_pos(const std::vector<std::string>& lines, int& row, int& col)
    {
        if (lines.empty()) { row = 0; col = 0; return; }
        row = (std::clamp)(row, 0, (int)lines.size() - 1);
        col = (std::clamp)(col, 0, (int)lines[row].size());
    }

    static bool has_sel(const lua_editor& e)
    {
        return e.cursor_row != e.sel_row || e.cursor_col != e.sel_col;
    }

    static void norm_sel(const lua_editor& e, int& r0, int& c0, int& r1, int& c1)
    {
        r0 = e.sel_row; c0 = e.sel_col; r1 = e.cursor_row; c1 = e.cursor_col;
        if (r0 > r1 || (r0 == r1 && c0 > c1))
        {
            std::swap(r0, r1);
            std::swap(c0, c1);
        }
    }

    static std::string get_sel(const lua_editor& e)
    {
        if (!has_sel(e)) return {};
        int r0, c0, r1, c1;
        norm_sel(e, r0, c0, r1, c1);
        if (r0 == r1)
            return e.lines[r0].substr(c0, c1 - c0);
        std::string out = e.lines[r0].substr(c0);
        out.push_back('\n');
        for (int r = r0 + 1; r < r1; ++r)
        {
            out += e.lines[r];
            out.push_back('\n');
        }
        out += e.lines[r1].substr(0, c1);
        return out;
    }

    static void delete_sel(lua_editor& e)
    {
        if (!has_sel(e)) return;
        int r0, c0, r1, c1;
        norm_sel(e, r0, c0, r1, c1);
        if (r0 == r1)
            e.lines[r0].erase(c0, c1 - c0);
        else
        {
            std::string merged = e.lines[r0].substr(0, c0) + e.lines[r1].substr(c1);
            e.lines.erase(e.lines.begin() + r0, e.lines.begin() + r1 + 1);
            e.lines.insert(e.lines.begin() + r0, merged);
        }
        e.cursor_row = e.sel_row = r0;
        e.cursor_col = e.sel_col = c0;
    }

    static void insert_text(lua_editor& e, const std::string& text)
    {
        delete_sel(e);
        for (char c : text)
        {
            if (c == '\n')
            {
                std::string rest = e.lines[e.cursor_row].substr(e.cursor_col);
                e.lines[e.cursor_row].erase(e.cursor_col);
                e.lines.insert(e.lines.begin() + e.cursor_row + 1, rest);
                e.cursor_row++;
                e.cursor_col = 0;
            }
            else if (c != '\r')
            {
                e.lines[e.cursor_row].insert(e.lines[e.cursor_row].begin() + e.cursor_col, c);
                e.cursor_col++;
            }
        }
        e.sel_row = e.cursor_row;
        e.sel_col = e.cursor_col;
    }

    static void word_prefix(const lua_editor& e, int& start_col, std::string& prefix)
    {
        const std::string& line = e.lines[e.cursor_row];
        start_col = e.cursor_col;
        while (start_col > 0)
        {
            char c = line[start_col - 1];
            if (is_ident(c) || c == '.')
                start_col--;
            else
                break;
        }
        prefix = line.substr(start_col, e.cursor_col - start_col);
    }

    static void refresh_ac(lua_editor& e)
    {
        e.ac_items.clear();
        word_prefix(e, e.ac_start_col, e.ac_prefix);
        if (e.ac_prefix.empty())
        {
            e.prefer_ac = false;
            return;
        }
        std::string pref = e.ac_prefix;
        for (char& c : pref) c = (char)std::tolower((unsigned char)c);

        for (int i = 0; k_completions[i]; ++i)
        {
            std::string item = k_completions[i];
            std::string low = item;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (low.size() >= pref.size() && low.compare(0, pref.size(), pref) == 0 && low != pref)
                e.ac_items.push_back(item);
        }
        if (e.ac_items.empty())
            e.prefer_ac = false;
        else
        {
            e.prefer_ac = true;
            e.ac_index = (std::clamp)(e.ac_index, 0, (int)e.ac_items.size() - 1);
        }
    }

    static void apply_ac(lua_editor& e)
    {
        if (e.ac_items.empty()) return;
        const std::string& pick = e.ac_items[e.ac_index];
        e.sel_row = e.cursor_row;
        e.sel_col = e.ac_start_col;
        delete_sel(e);
        insert_text(e, pick);
        e.prefer_ac = false;
        e.ac_items.clear();
    }

    static void draw_line_tokens(ImDrawList* dl, ImVec2 pos, const std::string& line)
    {
        const char* s = line.c_str();
        size_t i = 0, n = line.size();
        float x = pos.x;
        float y = pos.y;
        while (i < n)
        {
            if (s[i] == '-' && i + 1 < n && s[i + 1] == '-')
            {
                dl->AddText(ImVec2(x, y), col_for(tok_comment), s + i);
                break;
            }
            if (s[i] == '"' || s[i] == '\'')
            {
                char q = s[i];
                size_t j = i + 1;
                while (j < n)
                {
                    if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
                    if (s[j] == q) { j++; break; }
                    j++;
                }
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(tok_str), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            if (std::isdigit((unsigned char)s[i]) || (s[i] == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1])))
            {
                size_t j = i;
                while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == 'x' || s[j] == 'X' ||
                    (s[j] >= 'a' && s[j] <= 'f') || (s[j] >= 'A' && s[j] <= 'F')))
                    j++;
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(tok_num), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            if (is_ident_start(s[i]))
            {
                size_t j = i + 1;
                while (j < n && is_ident(s[j])) j++;
                token_kind k = is_kw(s + i, j - i) ? tok_kw : tok_ident;
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(k), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            const int cl = utf8_len(s + i, n - i);
            char tmp[8]{};
            memcpy(tmp, s + i, (size_t)cl);
            dl->AddText(ImVec2(x, y), col_for(tok_plain), tmp);
            x += ImGui::CalcTextSize(tmp).x;
            i += (size_t)cl;
        }
    }

    void lua_editor::render(const char* id, ImVec2 size)
    {
        register_active_editor(this);
        ImGui::PushID(id);

        ImVec4 editor_bg = Theme::GetPageBg();
        ImVec4 editor_border = Theme::GetInline();
        ImVec4 gutter_bg = Theme::GetLowContrast();
        ImVec4 accent_col = Theme::GetAccent();
        ImVec4 text_main = Theme::GetTextMain();
        ImVec4 text_misc = Theme::GetTextMisc();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertFloat4ToU32(editor_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32(editor_border));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

        ImGui::BeginChild("##ed", size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        const float sb_w = show_scrollbar ? 6.f : 0.f;
        ImVec2 canvas_avail(avail.x - sb_w, avail.y);

        ImGui::InvisibleButton("##canvas", canvas_avail);
        bool hovered = ImGui::IsItemHovered();
        bool canvas_active = ImGui::IsItemActive();
        bool win_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked() || canvas_active)
            focused = true;
        if (!hovered && !canvas_active && ImGui::IsMouseClicked(0) && !prefer_ac && !win_hovered)
            focused = false;

        if (focused)
        {
            io.WantCaptureKeyboard = true;
            io.WantTextInput = true;
        }

        const float line_h = ImGui::GetTextLineHeightWithSpacing();

        // Calculate dynamic gutter width based on number of lines
        int max_digits = 1;
        int num_lines = (int)lines.size();
        while (num_lines >= 10) { max_digits++; num_lines /= 10; }
        if (max_digits < 2) max_digits = 2;
        char digit_sample[16] = {};
        for (int d = 0; d < max_digits; d++) digit_sample[d] = '9';
        const float gutter_w = ImGui::CalcTextSize(digit_sample).x + 12.f;
        const float pad = 8.f;

        float content_h = (float)lines.size() * line_h;
        float max_scroll = (std::max)(0.f, content_h - canvas_avail.y);
        scroll_y = (std::clamp)(scroll_y, 0.f, max_scroll);

        int vis_lines = (std::max)(1, (int)(canvas_avail.y / line_h) + 1);
        int first = (int)(scroll_y / line_h);
        first = (std::clamp)(first, 0, (std::max)(0, (int)lines.size() - 1));

        if ((hovered || win_hovered) && io.MouseWheel != 0.f && max_scroll > 0.f)
        {
            scroll_y -= io.MouseWheel * line_h * 3.f;
            scroll_y = (std::clamp)(scroll_y, 0.f, max_scroll);
            io.MouseWheel = 0.f;
        }

        // Draw gutter border using theme colors
        dl->AddLine(ImVec2(origin.x + gutter_w, origin.y), ImVec2(origin.x + gutter_w, origin.y + canvas_avail.y), ImGui::ColorConvertFloat4ToU32(editor_border), 1.0f);

        auto char_x = [&](int row, int col) -> float {
            std::string sub = lines[row].substr(0, (std::min)(col, (int)lines[row].size()));
            return origin.x + gutter_w + pad - scroll_x + ImGui::CalcTextSize(sub.c_str()).x;
        };

        // Subtle highlight on current active line
        if (focused && cursor_row >= 0 && cursor_row < (int)lines.size())
        {
            float cur_line_y = origin.y + (float)cursor_row * line_h - scroll_y;
            if (cur_line_y >= origin.y - line_h && cur_line_y <= origin.y + canvas_avail.y)
            {
                dl->AddRectFilled(ImVec2(origin.x + gutter_w + 1.f, cur_line_y), ImVec2(origin.x + canvas_avail.x, cur_line_y + line_h), ImGui::ColorConvertFloat4ToU32(ImVec4(accent_col.x, accent_col.y, accent_col.z, 0.08f)), 0.0f);
            }
        }

        if (has_sel(*this))
        {
            int r0, c0, r1, c1;
            norm_sel(*this, r0, c0, r1, c1);
            ImU32 sel_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(accent_col.x, accent_col.y, accent_col.z, 0.35f));
            for (int r = r0; r <= r1; ++r)
            {
                float y = origin.y + (float)r * line_h - scroll_y;
                if (y + line_h < origin.y || y > origin.y + canvas_avail.y) continue;
                int a = (r == r0) ? c0 : 0;
                int b = (r == r1) ? c1 : (int)lines[r].size();
                float x0 = char_x(r, a);
                float x1 = char_x(r, b);
                if (a == b) x1 = x0 + 4.f;
                dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + line_h), sel_bg, 0.0f);
            }
        }

        ImU32 line_num_col = ImGui::ColorConvertFloat4ToU32(text_misc);
        for (int i = 0; i < vis_lines + 2; ++i)
        {
            int row = first + i;
            if (row < 0 || row >= (int)lines.size()) continue;
            float y = origin.y + (float)row * line_h - scroll_y;
            if (y + line_h < origin.y || y > origin.y + canvas_avail.y) continue;

            char num[16];
            snprintf(num, sizeof(num), "%d", row + 1);
            ImVec2 ns = ImGui::CalcTextSize(num);
            float num_x = origin.x + (gutter_w - ns.x) * 0.5f;
            dl->AddText(ImVec2(num_x, y), line_num_col, num);
            draw_line_tokens(dl, ImVec2(origin.x + gutter_w + pad - scroll_x, y), lines[row]);
        }

        if (focused)
        {
            float cx = char_x(cursor_row, cursor_col);
            float cy = origin.y + (float)cursor_row * line_h - scroll_y;
            if (fmodf((float)ImGui::GetTime(), 1.f) < 0.5f)
                dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + 1.5f, cy + line_h - 2.f), ImGui::ColorConvertFloat4ToU32(text_main), 0.0f);
        }

        auto pos_at = [&](float mx, float my, int& row, int& col) {
            row = (int)((my - origin.y + scroll_y) / line_h);
            row = (std::clamp)(row, 0, (int)lines.size() - 1);
            col = 0;
            float x = origin.x + gutter_w + pad - scroll_x;
            const std::string& line = lines[row];
            while (col < (int)line.size())
            {
                const int cl = utf8_len(line.c_str() + col, line.size() - (size_t)col);
                char tmp[8]{};
                memcpy(tmp, line.c_str() + col, (size_t)cl);
                float w = ImGui::CalcTextSize(tmp).x;
                if (mx < x + w * 0.5f) break;
                x += w;
                col += cl;
            }
        };

        if (focused && hovered && ImGui::IsMouseClicked(0))
        {
            int row, col;
            pos_at(io.MousePos.x, io.MousePos.y, row, col);
            cursor_row = sel_row = row;
            cursor_col = sel_col = col;
            prefer_ac = false;
            ac_items.clear();
        }

        if (focused && ImGui::IsMouseDragging(0) && (hovered || canvas_active))
        {
            int row, col;
            pos_at(io.MousePos.x, io.MousePos.y, row, col);
            cursor_row = row;
            cursor_col = col;
        }

        if (focused)
        {
            bool shift = io.KeyShift;
            bool ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
            auto move = [&](int nr, int nc) {
                clamp_pos(lines, nr, nc);
                cursor_row = nr;
                cursor_col = nc;
                if (!shift) { sel_row = cursor_row; sel_col = cursor_col; }
            };

            bool ac_used = false;
            if (!readonly && prefer_ac && !ac_items.empty())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    ac_index = (ac_index + (int)ac_items.size() - 1) % (int)ac_items.size();
                else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    ac_index = (ac_index + 1) % (int)ac_items.size();
                else if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    apply_ac(*this);
                    ac_used = true;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    prefer_ac = false;
                    ac_items.clear();
                }
            }
            else
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                {
                    if (cursor_col > 0) move(cursor_row, cursor_col - 1);
                    else if (cursor_row > 0) move(cursor_row - 1, (int)lines[cursor_row - 1].size());
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                {
                    if (cursor_col < (int)lines[cursor_row].size()) move(cursor_row, cursor_col + 1);
                    else if (cursor_row + 1 < (int)lines.size()) move(cursor_row + 1, 0);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && cursor_row > 0)
                    move(cursor_row - 1, cursor_col);
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && cursor_row + 1 < (int)lines.size())
                    move(cursor_row + 1, cursor_col);
                if (ImGui::IsKeyPressed(ImGuiKey_Home))
                    move(cursor_row, 0);
                if (ImGui::IsKeyPressed(ImGuiKey_End))
                    move(cursor_row, (int)lines[cursor_row].size());
            }

            if (!readonly)
            {
                if (!ac_used && ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    insert_text(*this, "\n");
                    prefer_ac = false;
                    ac_items.clear();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ac_used)
                {
                    insert_text(*this, "    ");
                    prefer_ac = false;
                    ac_items.clear();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
                {
                    if (has_sel(*this))
                        delete_sel(*this);
                    else if (cursor_col > 0)
                    {
                        lines[cursor_row].erase(cursor_col - 1, 1);
                        cursor_col--;
                        sel_row = cursor_row;
                        sel_col = cursor_col;
                    }
                    else if (cursor_row > 0)
                    {
                        int prev = (int)lines[cursor_row - 1].size();
                        lines[cursor_row - 1] += lines[cursor_row];
                        lines.erase(lines.begin() + cursor_row);
                        cursor_row--;
                        cursor_col = prev;
                        sel_row = cursor_row;
                        sel_col = cursor_col;
                    }
                    refresh_ac(*this);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                {
                    if (has_sel(*this))
                        delete_sel(*this);
                    else if (cursor_col < (int)lines[cursor_row].size())
                        lines[cursor_row].erase(cursor_col, 1);
                    else if (cursor_row + 1 < (int)lines.size())
                    {
                        lines[cursor_row] += lines[cursor_row + 1];
                        lines.erase(lines.begin() + cursor_row + 1);
                    }
                }
            }

            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A))
            {
                sel_row = 0; sel_col = 0;
                cursor_row = (int)lines.size() - 1;
                cursor_col = (int)lines[cursor_row].size();
            }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
            {
                std::string s = has_sel(*this) ? get_sel(*this) : lines[cursor_row];
                ImGui::SetClipboardText(s.c_str());
            }
            if (!readonly && ctrl && ImGui::IsKeyPressed(ImGuiKey_X))
            {
                std::string s = has_sel(*this) ? get_sel(*this) : lines[cursor_row];
                ImGui::SetClipboardText(s.c_str());
                if (has_sel(*this))
                    delete_sel(*this);
                else
                {
                    lines[cursor_row].clear();
                    cursor_col = sel_col = 0;
                }
            }
            if (!readonly && ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
            {
                const char* clip = ImGui::GetClipboardText();
                if (clip) insert_text(*this, clip);
                refresh_ac(*this);
            }

            if (!readonly)
            {
                for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
                {
                    ImWchar c = io.InputQueueCharacters[n];
                    if (c == '\t') continue;
                    if (c >= 32 && c < 127)
                    {
                        char ch = (char)c;
                        insert_text(*this, std::string(1, ch));
                        refresh_ac(*this);
                    }
                }
                io.InputQueueCharacters.resize(0);
            }
        }

        if (readonly)
        {
            prefer_ac = false;
            ac_items.clear();
        }

        bool nav_key = ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsKeyPressed(ImGuiKey_End) ||
            ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_PageDown);
        if (focused && (!readonly || nav_key))
        {
            float cy = origin.y + (float)cursor_row * line_h - scroll_y;
            if (cy < origin.y)
                scroll_y = (float)cursor_row * line_h;
            if (cy + line_h > origin.y + canvas_avail.y)
                scroll_y = (float)cursor_row * line_h - canvas_avail.y + line_h;
            scroll_y = (std::clamp)(scroll_y, 0.f, max_scroll);
        }

        if (prefer_ac && !ac_items.empty() && !readonly)
        {
            float px = char_x(cursor_row, ac_start_col);
            float py = origin.y + (float)cursor_row * line_h - scroll_y + line_h + 2.f;
            float max_w = 0.f;
            for (auto& it : ac_items)
                max_w = (std::max)(max_w, ImGui::CalcTextSize(it.c_str()).x);
            int show = (std::min)(8, (int)ac_items.size());
            float ph = show * line_h + 4.f;
            float pw = max_w + 18.f;
            ImVec2 pmin(px, py);
            ImVec2 pmax(px + pw, py + ph);

            ImGuiStyle& cur_style = ImGui::GetStyle();
            ImU32 popup_bg = ImGui::GetColorU32(ImGuiCol_PopupBg);
            ImU32 popup_border = ImGui::GetColorU32(ImGuiCol_Border);
            ImU32 popup_border_shadow = ImGui::GetColorU32(ImGuiCol_BorderShadow);
            ImU32 header_col = ImGui::GetColorU32(ImGuiCol_Header);
            ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);

            // Sharp, clean flat rectangular popup (0.0f rounding)
            dl->AddRectFilled(pmin, pmax, popup_bg, 0.0f);
            if (cur_style.WindowBorderSize > 0.0f)
            {
                dl->AddRect(pmin, pmax, popup_border, 0.0f, ImDrawFlags_None, cur_style.WindowBorderSize);
                dl->AddRect(pmin - ImVec2(1.f, 1.f), pmax + ImVec2(1.f, 1.f), popup_border_shadow, 0.0f, ImDrawFlags_None, 1.0f);
            }

            int start = (std::max)(0, ac_index - show + 1);
            for (int i = 0; i < show; ++i)
            {
                int idx = start + i;
                if (idx >= (int)ac_items.size()) break;
                float item_y = pmin.y + 2.f + i * line_h;
                ImVec2 tp(pmin.x + 6.f, item_y);
                if (idx == ac_index)
                {
                    // Sharp theme accent highlight for selected item (0.0f rounding)
                    dl->AddRectFilled(ImVec2(pmin.x + 1.f, item_y), ImVec2(pmax.x - 1.f, item_y + line_h), header_col, 0.0f);
                }
                dl->AddText(tp, text_col, ac_items[idx].c_str());
            }
        }

        if (show_scrollbar && max_scroll > 0.f)
        {
            ImVec2 sb_min(origin.x + canvas_avail.x + 1.f, origin.y);
            ImVec2 sb_max(origin.x + avail.x - 1.f, origin.y + canvas_avail.y);
            dl->AddRectFilled(sb_min, sb_max, IM_COL32(18, 18, 20, 255), 0.0f);

            float track_h = sb_max.y - sb_min.y;
            float thumb_h = (std::max)(16.f, track_h * (canvas_avail.y / content_h));
            float thumb_y = sb_min.y + (track_h - thumb_h) * (scroll_y / max_scroll);
            ImVec2 th_min(sb_min.x, thumb_y);
            ImVec2 th_max(sb_max.x, thumb_y + thumb_h);

            ImGui::SetCursorScreenPos(sb_min);
            ImGui::InvisibleButton("##sb", ImVec2(sb_max.x - sb_min.x, track_h));
            bool sb_hov = ImGui::IsItemHovered();
            bool sb_held = ImGui::IsItemActive();
            if (sb_held && track_h > thumb_h)
            {
                float rel = (io.MousePos.y - sb_min.y - thumb_h * 0.5f) / (track_h - thumb_h);
                scroll_y = (std::clamp)(rel, 0.f, 1.f) * max_scroll;
                thumb_y = sb_min.y + (track_h - thumb_h) * (scroll_y / max_scroll);
                th_min.y = thumb_y;
                th_max.y = thumb_y + thumb_h;
            }

            ImU32 th_col = IM_COL32(sb_held ? 120 : (sb_hov ? 100 : 70), sb_held ? 120 : (sb_hov ? 100 : 70), sb_held ? 120 : (sb_hov ? 100 : 70), 255);
            dl->AddRectFilled(th_min, th_max, th_col, 0.0f);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    static lua_editor* g_active_editor_ptr = nullptr;

    void register_active_editor(lua_editor* ed)
    {
        g_active_editor_ptr = ed;
    }

    void set_active_editor_text(const std::string& text)
    {
        if (g_active_editor_ptr)
        {
            g_active_editor_ptr->set_text(text);
        }
    }

    std::string get_active_editor_text()
    {
        if (g_active_editor_ptr)
        {
            return g_active_editor_ptr->get_text();
        }
        return "";
    }
}
