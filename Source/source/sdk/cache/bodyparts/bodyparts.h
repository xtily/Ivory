#pragma once
#include <sdk/cache/core/cache.h>
#include <sdk/math/math.h>
#include <string>
#include <vector>

namespace bodyparts
{
	inline const std::vector<std::string> target_part_types = {
		"Head",
		"Torso",
		"HumanoidRootPart",
		"LeftArm",
		"RightArm",
		"LeftLeg",
		"RightLeg"
	};

	const std::vector<std::string>& get_part_names(const cache::entity_t& entity, const std::string& part_type);
	
	bool get_part_position(const cache::entity_t& entity, const std::string& part_type, math::vector3& out_pos);
	
	bool is_r15(const cache::entity_t& entity);
}