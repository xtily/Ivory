#pragma once
#include <sdk/cache/core/cache.h>
#include <sdk/math/math.h>

namespace silentaim
{
	struct silent_state_t
	{
		bool data_ready{ false };

		cache::entity_t target{};
		math::vector2 target_screen_pos{};
		math::vector3 target_3d_pos{};
		std::uint64_t spoof_pos_x{ 0 };
		std::uint64_t spoof_pos_y{ 0 };

		rbx::c_instance aim_indicator{};
		math::vector2 original_size{};
		std::vector<std::pair<std::uint64_t, math::vector2>> original_children_sizes{};
		bool has_original_sizes{ false };
	};

	inline silent_state_t state{};

	inline std::mutex target_mtx{};

	inline cache::entity_t get_target()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		return state.target;
	}

	inline void set_target(const cache::entity_t& e)
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		state.target = e;
	}

	inline void clear_target()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		state.target = cache::entity_t{};
	}

	void run();
}
