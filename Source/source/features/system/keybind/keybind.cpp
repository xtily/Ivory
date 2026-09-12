#include "keybind.h"
#include <imgui/imgui.h>
#include <imgui/addons/imgui_addons.h>
#include <ui/render/render.h>
#include <sdk/game/game.h>
#include <features/exploits/utility/typingcheck/typingcheck.h>
#include <features/system/settings/settings.h>
#include <ui/menu/settings/functions.h>
#include <map>
#include <string>
#include <mutex>

bool keybind::is_key_active(int vk, int mode, const char* id)
{
	if (mode == 2)
		return true;

	if (vk <= 0)
		return true;

	HWND foreground_window = GetForegroundWindow();
	HWND roblox_window = game::get_roblox_window();
	HWND overlay_window = (render && render->detail) ? render->detail->window : NULL;

	if (roblox_window != NULL && foreground_window != overlay_window && foreground_window != roblox_window)
	{
		return false;
	}

	if (settings::typingcheck::enabled && check::textchatopen)
	{
		return false;
	}

	bool menu_open = g_menu.IsMenuOpened();
	bool is_pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;

	if (mode == 1)
	{
		if (menu_open)
			return false;
		return is_pressed;
	}

	else if (mode == 0)
	{
		struct toggle_state_t {
			bool state = false;
			bool was_pressed = false;
		};
		// Shared between the render thread and feature worker threads — must be guarded.
		static std::map<std::string, toggle_state_t> toggle_map;
		static std::mutex toggle_mtx;

		std::string key_id = (id && strlen(id) > 0) ? id : std::to_string(vk);

		std::lock_guard<std::mutex> guard(toggle_mtx);
		auto& st = toggle_map[key_id];

		if (menu_open)
		{
			if (!is_pressed)
				st.was_pressed = false;
			return st.state;
		}

		if (is_pressed && !st.was_pressed)
		{
			st.state = !st.state;
			st.was_pressed = true;
		}
		else if (!is_pressed)
		{
			st.was_pressed = false;
		}

		return st.state;
	}

	return false;
}

bool keybind::is_active(keybind_t& kb)
{
	return is_key_active(kb.key, static_cast<int>(kb.mode), nullptr);
}

std::string keybind::get_key_name(int vk_code)
{
	if (vk_code == 0)
	{
		return "None";
	}

	if (vk_code >= 'A' && vk_code <= 'Z')
	{
		return std::string(1, static_cast<char>(vk_code));
	}

	if (vk_code >= '0' && vk_code <= '9')
	{
		return std::string(1, static_cast<char>(vk_code));
	}

	switch (vk_code)
	{
	case VK_LBUTTON: return "LMB";
	case VK_RBUTTON: return "RMB";
	case VK_MBUTTON: return "MMB";
	case VK_XBUTTON1: return "X1";
	case VK_XBUTTON2: return "X2";
	case VK_BACK: return "Backspace";
	case VK_TAB: return "Tab";
	case VK_RETURN: return "Enter";
	case VK_SHIFT: return "Shift";
	case VK_CONTROL: return "Ctrl";
	case VK_MENU: return "Alt";
	case VK_PAUSE: return "Pause";
	case VK_CAPITAL: return "Caps";
	case VK_ESCAPE: return "Esc";
	case VK_SPACE: return "Space";
	case VK_PRIOR: return "Page Up";
	case VK_NEXT: return "Page Down";
	case VK_END: return "End";
	case VK_HOME: return "Home";
	case VK_LEFT: return "Left";
	case VK_UP: return "Up";
	case VK_RIGHT: return "Right";
	case VK_DOWN: return "Down";
	case VK_INSERT: return "Insert";
	case VK_DELETE: return "Delete";
	case VK_LWIN: return "LWin";
	case VK_RWIN: return "RWin";
	case VK_NUMPAD0: return "Numpad 0";
	case VK_NUMPAD1: return "Numpad 1";
	case VK_NUMPAD2: return "Numpad 2";
	case VK_NUMPAD3: return "Numpad 3";
	case VK_NUMPAD4: return "Numpad 4";
	case VK_NUMPAD5: return "Numpad 5";
	case VK_NUMPAD6: return "Numpad 6";
	case VK_NUMPAD7: return "Numpad 7";
	case VK_NUMPAD8: return "Numpad 8";
	case VK_NUMPAD9: return "Numpad 9";
	case VK_MULTIPLY: return "Numpad *";
	case VK_ADD: return "Numpad +";
	case VK_SUBTRACT: return "Numpad -";
	case VK_DECIMAL: return "Numpad .";
	case VK_DIVIDE: return "Numpad /";
	case VK_F1: return "F1";
	case VK_F2: return "F2";
	case VK_F3: return "F3";
	case VK_F4: return "F4";
	case VK_F5: return "F5";
	case VK_F6: return "F6";
	case VK_F7: return "F7";
	case VK_F8: return "F8";
	case VK_F9: return "F9";
	case VK_F10: return "F10";
	case VK_F11: return "F11";
	case VK_F12: return "F12";
	default:
	{
		char key_name[256];
		if (GetKeyNameTextA(MapVirtualKeyA(vk_code, MAPVK_VK_TO_VSC) << 16, key_name, sizeof(key_name)))
		{
			return std::string(key_name);
		}
		return "Key " + std::to_string(vk_code);
	}
	}
}

bool keybind::keybind_selector(int& key, activation_mode& mode, const char* unique_id)
{
	bool changed = false;
	std::string key_name = get_key_name(key);

	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);

	std::string popup_id;
	std::string mode_popup_id;

	if (unique_id)
	{
		popup_id = std::string("keybind_popup_") + unique_id;
		mode_popup_id = std::string("keybind_mode_popup_") + unique_id;
	}
	else
	{
		popup_id = "keybind_popup_" + std::to_string(reinterpret_cast<std::uintptr_t>(&key));
		mode_popup_id = "keybind_mode_popup_" + std::to_string(reinterpret_cast<std::uintptr_t>(&key));
	}

	if (unique_id)
	{
		ImGui::PushID(unique_id);
	}
	else
	{
		ImGui::PushID(&key);
	}

	if (ImGui::Button(key_name.c_str(), ImVec2(70, 0)))
	{
		ImGui::OpenPopup(popup_id.c_str());
	}

	if (ImGui::IsItemClicked(1))
	{
		ImGui::OpenPopup(mode_popup_id.c_str());
	}

	static std::map<std::string, int> popup_frame_count;

	if (ImGui::BeginPopup(popup_id.c_str()))
	{
		ImGui::Text("Press any key...");
		ImGui::Separator();

		std::string popup_key = popup_id;
		if (popup_frame_count.find(popup_key) == popup_frame_count.end())
		{
			popup_frame_count[popup_key] = 0;
		}
		popup_frame_count[popup_key]++;

		if (popup_frame_count[popup_key] > 2)
		{
		for (int i = 1; i < 256; i++)
		{
			if (GetAsyncKeyState(i) & 0x8000)
			{
				key = i;
				changed = true;
				ImGui::CloseCurrentPopup();
					popup_frame_count.erase(popup_key);
				break;
				}
			}
		}

		if (ImGui::Button("Clear"))
		{
			key = 0;
			changed = true;
			ImGui::CloseCurrentPopup();
			popup_frame_count.erase(popup_key);
		}

		ImGui::EndPopup();
	}
	else
	{
		std::string popup_key = popup_id;
		popup_frame_count.erase(popup_key);
	}

	if (ImGui::BeginPopup(mode_popup_id.c_str()))
	{
		if (ImGui::Selectable("Toggle", mode == activation_mode::toggle))
		{
			mode = activation_mode::toggle;
			changed = true;
		}
		if (ImGui::Selectable("Hold", mode == activation_mode::hold))
		{
			mode = activation_mode::hold;
			changed = true;
		}
		if (ImGui::Selectable("Always", mode == activation_mode::always))
		{
			mode = activation_mode::always;
			changed = true;
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();

	return changed;
}

ImGuiKey keybind::vk_to_imgui_key(int vk_code)
{
	if (vk_code == 0) return ImGuiKey_None;

	if (vk_code >= 'A' && vk_code <= 'Z')
		return (ImGuiKey)(ImGuiKey_A + (vk_code - 'A'));

	if (vk_code >= '0' && vk_code <= '9')
		return (ImGuiKey)(ImGuiKey_0 + (vk_code - '0'));

	if (vk_code == VK_LBUTTON) return ImGuiKey_MouseLeft;
	if (vk_code == VK_RBUTTON) return ImGuiKey_MouseRight;
	if (vk_code == VK_MBUTTON) return ImGuiKey_MouseMiddle;
	if (vk_code == VK_XBUTTON1) return ImGuiKey_MouseX1;
	if (vk_code == VK_XBUTTON2) return ImGuiKey_MouseX2;

	if (vk_code == VK_TAB) return ImGuiKey_Tab;
	if (vk_code == VK_LEFT) return ImGuiKey_LeftArrow;
	if (vk_code == VK_RIGHT) return ImGuiKey_RightArrow;
	if (vk_code == VK_UP) return ImGuiKey_UpArrow;
	if (vk_code == VK_DOWN) return ImGuiKey_DownArrow;
	if (vk_code == VK_PRIOR) return ImGuiKey_PageUp;
	if (vk_code == VK_NEXT) return ImGuiKey_PageDown;
	if (vk_code == VK_HOME) return ImGuiKey_Home;
	if (vk_code == VK_END) return ImGuiKey_End;
	if (vk_code == VK_INSERT) return ImGuiKey_Insert;
	if (vk_code == VK_DELETE) return ImGuiKey_Delete;
	if (vk_code == VK_BACK) return ImGuiKey_Backspace;
	if (vk_code == VK_SPACE) return ImGuiKey_Space;
	if (vk_code == VK_RETURN) return ImGuiKey_Enter;
	if (vk_code == VK_ESCAPE) return ImGuiKey_Escape;
	if (vk_code == VK_LCONTROL || vk_code == VK_RCONTROL) return ImGuiKey_LeftCtrl;
	if (vk_code == VK_LSHIFT || vk_code == VK_RSHIFT) return ImGuiKey_LeftShift;
	if (vk_code == VK_LMENU || vk_code == VK_RMENU) return ImGuiKey_LeftAlt;
	if (vk_code == VK_LWIN || vk_code == VK_RWIN) return ImGuiKey_LeftSuper;
	if (vk_code == VK_MENU) return ImGuiKey_Menu;

	if (vk_code >= VK_F1 && vk_code <= VK_F24)
		return (ImGuiKey)(ImGuiKey_F1 + (vk_code - VK_F1));

	return ImGuiKey_None;
}

int keybind::imgui_key_to_vk(ImGuiKey imgui_key)
{
	if (imgui_key == ImGuiKey_None) return 0;

	if (imgui_key >= ImGuiKey_A && imgui_key <= ImGuiKey_Z)
		return 'A' + (imgui_key - ImGuiKey_A);

	if (imgui_key >= ImGuiKey_0 && imgui_key <= ImGuiKey_9)
		return '0' + (imgui_key - ImGuiKey_0);

	if (imgui_key == ImGuiKey_MouseLeft) return VK_LBUTTON;
	if (imgui_key == ImGuiKey_MouseRight) return VK_RBUTTON;
	if (imgui_key == ImGuiKey_MouseMiddle) return VK_MBUTTON;
	if (imgui_key == ImGuiKey_MouseX1) return VK_XBUTTON1;
	if (imgui_key == ImGuiKey_MouseX2) return VK_XBUTTON2;

	if (imgui_key == ImGuiKey_Tab) return VK_TAB;
	if (imgui_key == ImGuiKey_LeftArrow) return VK_LEFT;
	if (imgui_key == ImGuiKey_RightArrow) return VK_RIGHT;
	if (imgui_key == ImGuiKey_UpArrow) return VK_UP;
	if (imgui_key == ImGuiKey_DownArrow) return VK_DOWN;
	if (imgui_key == ImGuiKey_PageUp) return VK_PRIOR;
	if (imgui_key == ImGuiKey_PageDown) return VK_NEXT;
	if (imgui_key == ImGuiKey_Home) return VK_HOME;
	if (imgui_key == ImGuiKey_End) return VK_END;
	if (imgui_key == ImGuiKey_Insert) return VK_INSERT;
	if (imgui_key == ImGuiKey_Delete) return VK_DELETE;
	if (imgui_key == ImGuiKey_Backspace) return VK_BACK;
	if (imgui_key == ImGuiKey_Space) return VK_SPACE;
	if (imgui_key == ImGuiKey_Enter) return VK_RETURN;
	if (imgui_key == ImGuiKey_Escape) return VK_ESCAPE;
	if (imgui_key == ImGuiKey_LeftCtrl || imgui_key == ImGuiKey_RightCtrl) return VK_LCONTROL;
	if (imgui_key == ImGuiKey_LeftShift || imgui_key == ImGuiKey_RightShift) return VK_LSHIFT;
	if (imgui_key == ImGuiKey_LeftAlt || imgui_key == ImGuiKey_RightAlt) return VK_LMENU;
	if (imgui_key == ImGuiKey_LeftSuper || imgui_key == ImGuiKey_RightSuper) return VK_LWIN;
	if (imgui_key == ImGuiKey_Menu) return VK_MENU;

	if (imgui_key >= ImGuiKey_F1 && imgui_key <= ImGuiKey_F24)
		return VK_F1 + (imgui_key - ImGuiKey_F1);

	return 0;
}

std::string keybind::truncate_text_left(const std::string& text, float max_width)
{
	float text_width = ImGui::CalcTextSize(text.c_str()).x;
	if (text_width <= max_width)
		return text;

	const char* ellipsis = "...";
	float ellipsis_width = ImGui::CalcTextSize(ellipsis).x;
	float available_width = max_width - ellipsis_width;

	if (available_width <= 0)
		return ellipsis;

	std::string truncated = text;
	while (truncated.length() > 0)
	{
		float truncated_width = ImGui::CalcTextSize(truncated.c_str()).x;
		if (truncated_width <= available_width)
			break;
		truncated.erase(0, 1);
	}

	return ellipsis + truncated;
}

bool keybind::custom_keybind(const char* str_id, int& key, float max_width)
{
	return keybind_button(str_id, key, max_width > 0.0f ? max_width : 50.0f);
}

bool keybind::keybind_button(const char* str_id, int& key, float width)
{
	return keybind_button(str_id, key, nullptr, width);
}

bool keybind::keybind_button(const char* str_id, int& key, int* mode, float width)
{
	static const char* active_id = nullptr;
	static int frame_delay = 0;
	static std::unordered_map<std::string, int> internal_modes;
	bool changed = false;

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window || window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(str_id);

	int& current_mode = (mode != nullptr) ? *mode : internal_modes[str_id];

	std::string label;
	bool is_selecting = (active_id && strcmp(active_id, str_id) == 0);

	if (is_selecting)
	{
		label = "...";
	}
	else if (key == 0)
	{
		label = "-";
	}
	else
	{
		label = get_key_name(key);
	}

	ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);
	float actual_width = width > 0.0f ? width : (label_size.x + style.FramePadding.x * 2.0f + 10.0f);
	if (actual_width < 35.0f) actual_width = 35.0f;

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size(actual_width, g.FontSize + style.FramePadding.y * 2.0f - 3.0f);
	ImRect frame_bb(pos, pos + size);

	ImGui::ItemSize(frame_bb);
	if (!ImGui::ItemAdd(frame_bb, id))
		return false;

	const bool hovered = ImGui::ItemHoverable(frame_bb, id, 0);
	if (hovered)
	{
		ImGui::SetHoveredID(id);
		g.MouseCursor = ImGuiMouseCursor_Hand;
	}

	const bool left_clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool right_clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

	std::string popup_str_id = std::string(str_id) + "::keybind_popup";

	if (right_clicked)
	{
		ImGui::OpenPopup(popup_str_id.c_str());
	}
	else if (left_clicked)
	{
		if (is_selecting)
		{
			active_id = nullptr;
		}
		else
		{
			active_id = str_id;
			frame_delay = 0;
		}
	}

	// Draw button-style background + multi-color shadow + border behind keybind text
	ImU32 colFrame = ImGui::GetColorU32(is_selecting ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, colFrame, style.FrameRounding);
	window->DrawList->AddRectFilledMultiColor(frame_bb.Min, frame_bb.Max,
		ImGui::GetColorU32(ImGuiCol_ButtonShadow, 0.0f), ImGui::GetColorU32(ImGuiCol_ButtonShadow, 0.0f),
		ImGui::GetColorU32(ImGuiCol_ButtonShadow), ImGui::GetColorU32(ImGuiCol_ButtonShadow));
	ImAdd::RenderFrameBorder(frame_bb.Min, frame_bb.Max);

	ImVec2 text_pos = pos + ImVec2((size.x - label_size.x) / 2.0f, (size.y - label_size.y) / 2.0f);
	ImAdd::RenderText(text_pos, label.c_str(), 0, true, true);

	// Right-click Mode Popup (Hold, Toggle, Always)
	const float popup_width = ImGui::CalcTextSize("Always").x + style.FramePadding.x * 2.0f + 16.0f;

	ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_BorderShadow]);
	ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);

	if (ImGui::BeginPopup(popup_str_id.c_str(), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
	{
		ImGui::SetWindowPos(ImVec2(frame_bb.Max.x - popup_width, frame_bb.Max.y + style.FramePadding.y), ImGuiCond_Always);
		ImGui::SetWindowSize(ImVec2(popup_width, g.FontSize * 3 + style.WindowPadding.y * 4.0f + 12.0f), ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.WindowPadding);

		if (ImAdd::SelectableLabel("Hold", current_mode == 1, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
		{
			current_mode = 1;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImAdd::SelectableLabel("Toggle", current_mode == 0, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
		{
			current_mode = 0;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImAdd::SelectableLabel("Always", current_mode == 2, false, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
		{
			current_mode = 2;
			changed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}

	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);

	// Handle key capture
	if (is_selecting)
	{
		frame_delay++;
		if (frame_delay > 2)
		{
			for (int vk = 1; vk < 256; vk++)
			{
				if (vk == VK_LBUTTON && hovered)
					continue;

				if (GetAsyncKeyState(vk) & 0x8000)
				{
					if (vk == VK_ESCAPE)
					{
						key = 0;
					}
					else
					{
						key = vk;
					}
					changed = true;
					active_id = nullptr;
					break;
				}
			}
		}
	}

	return changed;
}

bool keybind::keybind_with_mode(const char* label, int& key, activation_mode& mode, const char* unique_id, float max_width)
{
	bool changed = false;

	ImGui::PushID(unique_id);

	if (custom_keybind(label, key, max_width))
	{
		changed = true;
	}

	if (ImGui::IsItemClicked(1))
	{
		ImGui::OpenPopup((std::string("keybind_mode_") + unique_id).c_str());
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::BeginPopup((std::string("keybind_mode_") + unique_id).c_str()))
	{
		ImGuiStyle& style = ImGui::GetStyle();

		if (ImGui::Selectable("Toggle", mode == activation_mode::toggle))
		{
			mode = activation_mode::toggle;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Selectable("Hold", mode == activation_mode::hold))
		{
			mode = activation_mode::hold;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Selectable("Always", mode == activation_mode::always))
		{
			mode = activation_mode::always;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
		if (ImGui::Selectable("Clear", false))
		{
			key = 0;
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(4);

	ImGui::PopID();
	return changed;
}
