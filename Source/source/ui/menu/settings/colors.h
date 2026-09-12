#pragma once
#include "imgui.h"

class c_colors
{
public:
	ImColor accent{ 255, 255, 255 };

	struct
	{
		ImColor background_one{ 30, 30, 30 };
		ImColor background_two{ 30, 30, 30 };
		ImColor stroke{ 50, 50, 50 };
	} window;

	struct
	{
		ImColor stroke_two{ 50, 50, 50 };
		ImColor text{ 255, 255, 255 };
		ImColor text_inactive{ 132, 132, 132 };
	} widgets;
};

inline c_colors* clr = new c_colors();