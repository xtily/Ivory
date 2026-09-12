#pragma once
#include <sdk/math/math.h>
#include <sdk/cache/core/cache.h>
#include <imgui/imgui.h>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>

namespace hitvisuals
{
	struct HitLimbSnapshot
	{
		std::string name;
		math::vector3 world_pos;
		math::matrix3 world_rot;
		math::vector3 size;
		bool valid = false;
	};

	struct HitRecord
	{
		std::chrono::steady_clock::time_point spawn_time;
		math::vector3 local_pos;
		math::vector3 hit_pos;
		float damage = 0.0f;
		bool is_kill = false;
		bool is_r15 = false;
		std::string target_name;
		std::vector<HitLimbSnapshot> limbs;
	};

	void on_hit(const cache::entity_t& target, const math::vector3& local_pos, const math::vector3& hit_pos, float damage, bool is_kill);
	void render(ImDrawList* draw_list, const math::matrix4& view, const math::vector2& dims);
	void clear();
}
