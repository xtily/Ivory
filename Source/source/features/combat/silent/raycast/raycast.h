#pragma once
#include <windows.h>

#include <sdk/math/math.h>

namespace raycast_silentaim
{
	extern bool target_acquired;
	extern math::vector2 target_screen_pos;
	extern uint64_t target_address;
	void init();
	void run();
	void draw_fov();
	uint64_t get_calls();
}