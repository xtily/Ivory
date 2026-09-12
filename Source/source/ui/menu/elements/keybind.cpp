#include "../settings/functions.h"

const char* keys[] =
{
    "-",
    "lmb",      
    "rmb",        
    "cn",
    "mmb",       
    "mb4",      
    "mb5",      
    "-",
    "back",
    "tab",
    "-",
    "-",
    "clr",
    "enter",
    "-",
    "-",
    "shift",
    "ctrl",
    "alt",     
    "pause",
    "caps",
    "kan",
    "-",
    "jun",
    "fin",
    "kan",
    "-",
    "esc",
    "con",
    "nco",
    "acc",
    "mad",
    "space",
    "pgu",    
    "pgd",     
    "end",
    "home",
    "left",
    "up",
    "right",
    "down",
    "sel",
    "pri",
    "exe",
    "pri",
    "ins",
    "del",
    "help",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "a",
    "b",
    "c",
    "d",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "p",
    "q",
    "r",
    "s",
    "t",
    "u",
    "v",
    "w",
    "x",
    "y",
    "z",
    "win",       
    "win",   
    "app",
    "-",
    "sle",     
    "np0",    
    "np1",     
    "np2",      
    "np3",   
    "np4",     
    "np5",  
    "np6",    
    "np7",     
    "np8",       
    "np9",     
    "mul",     
    "add",    
    "sep",     
    "sub",
    "del", 
    "div",
    "f1",
    "f2",
    "f3",
    "f4",
    "f5",
    "f6",
    "f7",
    "f8",
    "f9",
    "f10",
    "f11",
    "f12",
    "f13",
    "f14",
    "f15",
    "f16",
    "f17",
    "f18",
    "f19",
    "f20",
    "f21",
    "f22",
    "f23",
    "f24",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "num",
    "scr",
    "equ",
    "mas",
    "toy",
    "oya",
    "oya",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "shift",
    "shift",
    "ctrl",
    "ctrl",
    "alt",
    "alt"
};

bool c_gui::keybind(const char* label, int* key, int* mode)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    const ImGuiStyle& style = g.Style;

    const ImGuiID id = window->GetID(label);
    const ImRect rect(window->DC.CursorPos, window->DC.CursorPos + elements->widgets.key_size);

    ImGui::ItemSize(rect, style.FramePadding.y);
    if (!ImGui::ItemAdd(rect, id))
        return false;

    char buf_display[64] = "-";

    bool value_changed = false;
    int k = *key;

    bool hovered = ItemHoverable(rect, id, 0);

    std::string active_key = "";
    active_key += keys[*key];

    if (*key != 0 && g.ActiveId != id) {
        strcpy_s(buf_display, active_key.c_str());
    }
    else if (g.ActiveId == id) {
        strcpy_s(buf_display, "-");
    }

    draw->fade_rect_filled(window->DrawList, rect.Min + ImVec2(2, 2), rect.Max - ImVec2(2, 2), draw->get_clr(clr->window.background_two), draw->get_clr(clr->window.background_one), fade_direction::vertically);
    draw->rect(window->DrawList, rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), draw->get_clr(clr->window.stroke));
    draw->text_clipped_outline(window->DrawList, var->font.tahoma, rect.Min - ImVec2(0, 1), rect.Max - ImVec2(0, 1), draw->get_clr(clr->widgets.text), buf_display, NULL, NULL, ImVec2(0.5f, 0.5f));
    draw->rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(var->window.hover_hightlight && hovered ? clr->accent : clr->widgets.stroke_two));

    if (hovered && io.MouseClicked[0])
    {
        if (g.ActiveId != id) {
            memset(io.MouseDown, 0, sizeof(io.MouseDown));
            io.ClearInputKeys();
            *key = 0;
        }
        ImGui::SetActiveID(id, window);
        ImGui::FocusWindow(window);
    }
    else if (io.MouseClicked[0]) {
        if (g.ActiveId == id)
            ImGui::ClearActiveID();
    }

    if (g.ActiveId == id) {
        for (auto i = 0; i < 5; i++) {
            if (io.MouseDown[i]) {
                switch (i) {
                case 0:
                    k = 0x01;
                    break;
                case 1:
                    k = 0x02;
                    break;
                case 2:
                    k = 0x04;
                    break;
                case 3:
                    k = 0x05;
                    break;
                case 4:
                    k = 0x06;
                    break;
                }
                value_changed = true;
                ImGui::ClearActiveID();
            }
        }
        if (!value_changed) {
            for (auto i = 0x08; i <= 0xA5; i++) {
                if (GetAsyncKeyState(i) & 0x8000) {
                    k = i;
                    value_changed = true;
                    ImGui::ClearActiveID();
                }
            }
        }

        if (IsKeyPressed(ImGuiKey_Escape)) {
            *key = 0;
            ImGui::ClearActiveID();
        }
        else {
            *key = k;
        }
    }

    if (mode != nullptr)
    {
        const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
        bool popup_open = IsPopupOpen(popup_id, ImGuiPopupFlags_None);
        if (hovered && g.IO.MouseClicked[1] && !popup_open)
        {
            OpenPopupEx(popup_id, ImGuiPopupFlags_None);
            popup_open = true;
        }

        if (popup_open)
        {
            gui->push_style_var(ImGuiStyleVar_PopupBorderSize, 1.f);
            gui->push_style_color(ImGuiCol_Border, draw->get_clr(clr->window.stroke));
            gui->push_style_color(ImGuiCol_PopupBg, draw->get_clr(clr->window.background_one));
            gui->push_style_var(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
            gui->push_style_var(ImGuiStyleVar_WindowPadding, ImVec2(3, 3));
            gui->set_next_window_pos(ImVec2(rect.Min.x, rect.Max.y + 3));
            if (BeginPopupEx(popup_id, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_AlwaysUseWindowPadding))
            {
                if (gui->selectable_ex("Hold", 0 == *mode, ImVec2(60, 15)))
                    *mode = 0;

                if (gui->selectable_ex("Toggle", 1 == *mode, ImVec2(60, 15)))
                    *mode = 1;

                if (gui->selectable_ex("Always", 2 == *mode, ImVec2(60, 15)))
                    *mode = 2;

                EndPopup();
            }
            gui->pop_style_var(3);
            gui->pop_style_color(2);
        }
    }

    return value_changed;
}

bool c_gui::label_keybind(const char* label, int* key, int* mode)
{
    draw->text_outline(GetWindowDrawList(), var->font.tahoma, var->font.tahoma->FontSize, GetCursorScreenPos() + ImVec2(1, 0), draw->get_clr(clr->widgets.text), label);
    gui->set_cursor_pos(ImVec2(GetWindowWidth() - elements->widgets.key_size.x - GetStyle().WindowPadding.x, GetCursorPos().y));
    gui->keybind(label, key, mode);

    return true;
}