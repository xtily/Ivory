#pragma once
#include <string>
#include <vector>
#include "imgui.h"

class c_variables
{
public:
	struct
	{
		ImGuiWindowFlags main_flags{ ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip };
		ImGuiWindowFlags flags{ ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground };
		ImVec2 padding{ 0, 0 };
		ImVec2 spacing{ 5, 6 };
		float shadow_size{ 30 };
		float shadow_alpha{ 0.3f };
		float border_size{ 0 };
		float rounding{ 4 };
		float width{ 0 };
		float titlebar{ 20 };
		float scrollbar_size{ 2 };
		bool hover_hightlight{ true };
		bool lerp_animations{ true };
		bool window_gradients{ true };
		bool window_glow{ true };
	} window;

	struct
	{
		bool current_section[10] = { true, false, false, false, false, false, false, false, false, false };
		const char* section_icons[IM_ARRAYSIZE(current_section)] = {"A", "B", "C", "D", "E", "F", "G", "{/}", "SVR", "NPC"};

		float menu_alpha{ 0 };
		bool menu_opened{ false };
		int menu_key{ 46 };
		bool custom_cursor{ true };
	} gui;

	struct
	{
		ImFont* icons[2];
		ImFont* tahoma;
	} font;
};

inline c_variables* var = new c_variables();