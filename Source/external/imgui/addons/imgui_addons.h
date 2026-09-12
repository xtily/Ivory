//============ Copyright KiwiHax, All rights reserved ============//
//
//  Purpose: 
//
//================================================================//

#pragma once

#include <vector>
#include "../imgui.h"

#define IMADD_ANIMATIONS_SPEED	g_AnimationSpeed

extern float g_AnimationSpeed;
extern float g_TabHeight;
extern bool  g_TabFillSpace;

struct												ImGuiWindow;
typedef int											ImGuiKeyType;
typedef std::vector<std::pair<bool, const char*>>	ImMultiComboItems;

enum ImGuiKeyType_ : int
{
	ImGuiKeyType_Toggle = 0,
	ImGuiKeyType_Hold = 1,
	ImGuiKeyType_Always = 2,

	ImGuiKeyType_COUNT
};

namespace ImAdd
{
	// Helpers
	ImVec4  HexToColorVec4(unsigned int hex_color, float alpha = 1.0f);
	float	GetColorPickerWidth();
	float   CalcKeyBindWidth(int key);
	float   CalcKeyBindWidth(ImGuiKey key);
	
	// Separators
	void    SeparatorText(const char* label, float thickness = 1.0f);
	void    VSeparator(float margin = 0.0f, float thickness = 1.0f);

	// Widgets
	bool	SelectableLabel(const char* label, bool selected, bool centered = false, const ImVec2& size_arg = ImVec2(0, 0));
	bool    CheckBox(const char* label, bool* v);
	bool	Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags button_flags = 0);
	bool	ButtonAccent(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags button_flags = 0);
	bool	Combo(const char* label, int* selected_index, std::vector<const char*> items);
	bool	ComboMulti(const char* label, ImMultiComboItems* items);
	bool	ColorButton(const char* desc_id, const ImVec4& col, const ImVec2& size_arg = ImVec2(0, 0), bool has_alpha = true);
	bool	ColorEdit4(const char* label, float col[4]);
	bool	KeyBind(const char* str_id, int* k, int* type = nullptr, const ImVec2& size_arg = ImVec2(0, 0));
	bool	KeyBind(const char* str_id, ImGuiKey* k, ImGuiKeyType* type = nullptr, const ImVec2& size_arg = ImVec2(0, 0));

	// Child Windows
	bool	Tab(const char* label, bool selected, const ImVec2& size_arg = ImVec2(0, 0));
	void	ScrollBar(const char* str_id, ImGuiWindow* window, const ImVec2& size_arg = ImVec2(0, 0));
	bool	BeginChild(const char* str_id, std::vector<const char*> tabs, int* selected_tab_index_callback, const ImVec2& size_arg = ImVec2(0, 0));
	bool	BeginChild(const char* str_id, std::vector<const char*> tabs, const ImVec2& size_arg = ImVec2(0, 0));
	bool	BeginChild(const char* str_id, const ImVec2& size_arg = ImVec2(0, 0));
	void    EndChild();

	// Sliders
	bool	SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = NULL);
	bool	SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
	bool	SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d");

	// Drawing
	void	RenderArrow(ImDrawList* draw_list, ImVec2 pos, ImU32 col, float sz, ImGuiDir direction);
	void	RenderText(ImVec2 pos, const char* text, const char* text_end = NULL, bool hide_text_after_hash = true, bool has_outlines = false);
	void	RenderFrame(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool borders = true, bool shadows = true, bool inverted = false, float rounding = 0.0f);
	void	RenderFrameBorder(ImVec2 p_min, ImVec2 p_max, bool inverted = false, bool shadow = true, float rounding = 0.0f);
}
