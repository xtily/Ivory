#pragma once
#include <windows.h>
#include <string>
#include <imgui/imgui.h>

namespace keybind
{
	enum class activation_mode
	{
		toggle,
		hold,
		always
	};

	struct keybind_t
	{
		int key = 0;
		activation_mode mode = activation_mode::hold;
		bool state = false;
		bool was_pressed = false;
	};

	bool is_active(keybind_t& kb);
	bool is_key_active(int vk_code, int mode_int, const char* id = nullptr);
	std::string get_key_name(int vk_code);
	bool keybind_selector(int& key, activation_mode& mode, const char* unique_id = nullptr);

	ImGuiKey vk_to_imgui_key(int vk_code);
	int imgui_key_to_vk(ImGuiKey imgui_key);
	std::string truncate_text_left(const std::string& text, float max_width);
	bool custom_keybind(const char* str_id, int& key, float max_width = 0.0f);
	bool keybind_button(const char* str_id, int& key, int* mode = nullptr, float width = 45.0f);
	bool keybind_button(const char* str_id, int& key, float width);
	bool keybind_with_mode(const char* label, int& key, activation_mode& mode, const char* unique_id, float max_width = 0.0f);
}
