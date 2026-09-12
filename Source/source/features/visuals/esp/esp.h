#pragma once
#include <mutex>
#include <string>
#include <vector>
#include <imgui/imgui.h>

namespace esp
{
	struct projected_part_t
	{
		std::string name;
		ImVec2 points[8];
		int num_points = 0;
	};

	void run();
}
