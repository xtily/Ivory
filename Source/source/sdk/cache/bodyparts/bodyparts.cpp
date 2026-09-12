#include "bodyparts.h"
#include <sdk/math/math.h>

namespace bodyparts
{
	bool is_r15(const cache::entity_t& entity)
	{
		auto upper_torso_it = entity.parts.find("UpperTorso");
		auto lower_torso_it = entity.parts.find("LowerTorso");
		return (upper_torso_it != entity.parts.end() && lower_torso_it != entity.parts.end());
	}

	const std::vector<std::string>& get_part_names(const cache::entity_t& entity, const std::string& part_type)
	{
		static const std::vector<std::string> empty_vec{};
		static const std::vector<std::string> head_vec{ "Head" };
		static const std::vector<std::string> torso_r15{ "UpperTorso", "LowerTorso" };
		static const std::vector<std::string> torso_r6{ "Torso" };
		static const std::vector<std::string> hrp_vec{ "HumanoidRootPart", "UpperTorso", "Torso", "Head" };
		static const std::vector<std::string> left_arm_r15{ "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm", "LeftArm" };
		static const std::vector<std::string> left_arm_r6{ "Left Arm", "LeftUpperArm", "LeftArm" };
		static const std::vector<std::string> right_arm_r15{ "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm", "RightArm" };
		static const std::vector<std::string> right_arm_r6{ "Right Arm", "RightUpperArm", "RightArm" };
		static const std::vector<std::string> left_leg_r15{ "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg", "LeftLeg" };
		static const std::vector<std::string> left_leg_r6{ "Left Leg", "LeftUpperLeg", "LeftLeg" };
		static const std::vector<std::string> right_leg_r15{ "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg", "RightLeg" };
		static const std::vector<std::string> right_leg_r6{ "Right Leg", "RightUpperLeg", "RightLeg" };

		bool r15 = is_r15(entity);

		if (part_type == "Head") return head_vec;
		if (part_type == "Torso" || part_type == "UpperTorso" || part_type == "LowerTorso") return r15 ? torso_r15 : torso_r6;
		if (part_type == "HumanoidRootPart") return hrp_vec;
		if (part_type == "LeftArm" || part_type == "LeftUpperArm" || part_type == "LeftLowerArm" || part_type == "LeftHand" || part_type == "Left Arm") return r15 ? left_arm_r15 : left_arm_r6;
		if (part_type == "RightArm" || part_type == "RightUpperArm" || part_type == "RightLowerArm" || part_type == "RightHand" || part_type == "Right Arm") return r15 ? right_arm_r15 : right_arm_r6;
		if (part_type == "LeftLeg" || part_type == "LeftUpperLeg" || part_type == "LeftLowerLeg" || part_type == "LeftFoot" || part_type == "Left Leg") return r15 ? left_leg_r15 : left_leg_r6;
		if (part_type == "RightLeg" || part_type == "RightUpperLeg" || part_type == "RightLowerLeg" || part_type == "RightFoot" || part_type == "Right Leg") return r15 ? right_leg_r15 : right_leg_r6;

		return empty_vec;
	}

	bool get_part_position(const cache::entity_t& entity, const std::string& part_type, math::vector3& out_pos)
	{
		auto valid_pos = [](const math::vector3& v) {
			return (v.x != 0.0f || v.y != 0.0f || v.z != 0.0f);
		};

		if (part_type == "HumanoidRootPart")
		{
			if (valid_pos(entity.position)) { out_pos = entity.position; return true; }
		}
		if (part_type == "Head")
		{
			if (valid_pos(entity.part_positions.head)) { out_pos = entity.part_positions.head; return true; }
		}
		if (part_type == "Torso" || part_type == "UpperTorso" || part_type == "LowerTorso")
		{
			if (valid_pos(entity.part_positions.torso)) { out_pos = entity.part_positions.torso; return true; }
		}
		if (part_type == "LeftArm" || part_type == "LeftUpperArm" || part_type == "LeftArm" || part_type == "Left Arm")
		{
			if (valid_pos(entity.part_positions.left_arm)) { out_pos = entity.part_positions.left_arm; return true; }
		}
		if (part_type == "RightArm" || part_type == "RightUpperArm" || part_type == "RightArm" || part_type == "Right Arm")
		{
			if (valid_pos(entity.part_positions.right_arm)) { out_pos = entity.part_positions.right_arm; return true; }
		}
		if (part_type == "LeftLeg" || part_type == "LeftUpperLeg" || part_type == "LeftLeg" || part_type == "Left Leg")
		{
			if (valid_pos(entity.part_positions.left_leg)) { out_pos = entity.part_positions.left_leg; return true; }
		}
		if (part_type == "RightLeg" || part_type == "RightUpperLeg" || part_type == "RightLeg" || part_type == "Right Leg")
		{
			if (valid_pos(entity.part_positions.right_leg)) { out_pos = entity.part_positions.right_leg; return true; }
		}

		if (part_type == "Head" && entity.head_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.head_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}
		if ((part_type == "Torso" || part_type == "UpperTorso") && entity.torso_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.torso_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}
		if ((part_type == "LeftArm" || part_type == "LeftUpperArm") && entity.left_arm_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.left_arm_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}
		if ((part_type == "RightArm" || part_type == "RightUpperArm") && entity.right_arm_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.right_arm_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}
		if ((part_type == "LeftLeg" || part_type == "LeftUpperLeg") && entity.left_leg_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.left_leg_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}
		if ((part_type == "RightLeg" || part_type == "RightUpperLeg") && entity.right_leg_prim_addr)
		{
			out_pos = memory->read<math::vector3>(entity.right_leg_prim_addr + Offsets::Primitive::Position);
			if (valid_pos(out_pos)) return true;
		}

		auto part_names = get_part_names(entity, part_type);
		for (const auto& name : part_names)
		{
			auto it = entity.parts.find(name);
			if (it != entity.parts.end() && it->second.address)
			{
				rbx::c_part part{ it->second };
				if (part.address)
				{
					rbx::c_primitive prim = part.get_primitive();
					if (prim.address)
					{
						out_pos = prim.get_position();
						if (valid_pos(out_pos)) return true;
					}
				}
			}
		}

		return false;
	}
}