#include "aimbot.h"
#include <features/system/playerlist/playerlist.h>
#include <features/exploits/locomotion/movement/movement.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <sdk/game/game.h>
#include <features/system/settings/settings.h>
#include <sdk/math/math.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <features/system/keybind/keybind.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <sdk/cache/core/frame.h>
#include <ui/menu/settings/functions.h>
#include <sdk/wallcheck/wallcheck.h>

static float get_world_distance_to_entity(const cache::entity_t& entity, const math::vector3& cam_pos);

static math::vector3 get_velocity(const cache::entity_t& entity)
{
	if (entity.velocity.x != 0.0f || entity.velocity.y != 0.0f || entity.velocity.z != 0.0f)
	{
		return entity.velocity;
	}

	if (entity.hrp_prim_addr >= 0x10000 && entity.hrp_prim_addr < 0x7FFFFFFFFFFFull)
	{
		return memory->read<math::vector3>(entity.hrp_prim_addr + Offsets::Primitive::AssemblyLinearVelocity);
	}

	if (entity.humanoid_root_part.address != 0)
	{
		rbx::c_primitive prim = entity.humanoid_root_part.get_primitive();
		if (prim.address != 0)
		{
			return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
		}
	}

	return entity.velocity;
}

static float get_workspace_gravity()
{
	try
	{
		if (!game::datamodel || game::datamodel->address == 0)
			return 196.2f;

		uintptr_t workspace = memory->read<uintptr_t>(game::datamodel->address + Offsets::DataModel::Workspace);
		if (workspace == 0)
			return 196.2f;

		uintptr_t container = memory->read<uintptr_t>(workspace + Offsets::Workspace::World);
		if (container == 0)
			return 196.2f;

		float gravity = memory->read<float>(container + Offsets::World::Gravity);
		if (gravity <= 0.0f)
			return 196.2f;

		return gravity;
	}
	catch (...)
	{
		return 196.2f;
	}
}

static math::vector3 apply_prediction(const cache::entity_t& entity, const math::vector3& position)
{
	math::vector3 velocity = get_velocity(entity);
	float prediction_factor_x = settings::aimbot::prediction_x * 0.01f;
	float prediction_factor_y = settings::aimbot::prediction_y * 0.01f;

	math::vector3 predicted_offset;
	predicted_offset.x = velocity.x * prediction_factor_x;
	predicted_offset.y = velocity.y * prediction_factor_y;
	predicted_offset.z = velocity.z * prediction_factor_x;

	if (velocity.y > 1.0f && settings::aimbot::jump_prediction)
	{
		float t = settings::aimbot::jump_prediction_value * 0.01f;
		float gravity = get_workspace_gravity();
		predicted_offset.y = (velocity.y * t) - (0.5f * gravity * t * t);
	}
	else if (velocity.y < -1.0f && settings::aimbot::fall_prediction)
	{
		float t = settings::aimbot::fall_prediction_value * 0.01f;
		float gravity = get_workspace_gravity();
		predicted_offset.y = (velocity.y * t) - (0.5f * gravity * t * t);
	}
	return position + predicted_offset;
}

namespace easing {
    float linear(float t) {
        return t;
    }

    float ease_in_quad(float t) {
        return t * t;
    }

    float ease_out_quad(float t) {
        return t * (2 - t);
    }

    float ease_in_out_quad(float t) {
        return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
    }

    float ease_in_cubic(float t) {
        return t * t * t;
    }

    float ease_out_cubic(float t) {
        return (--t) * t * t + 1;
    }

    float ease_in_out_cubic(float t) {
        return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
    }

    float ease_in_sine(float t) {
        return 1 - cos((t * 3.14159265358979323846) / 2);
    }

    float ease_out_sine(float t) {
        return sin((t * 3.14159265358979323846) / 2);
    }

    float ease_in_out_sine(float t) {
        return -(cos(3.14159265358979323846 * t) - 1) / 2;
    }
}

static float get_fov_scale()
{
	uint64_t cam = 0;
	if (game::datamodel && game::datamodel->address >= 0x10000)
	{
		uint64_t ws = memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::Workspace);
		if (ws >= 0x10000)
		{
			cam = memory->read<uint64_t>(ws + Offsets::Workspace::CurrentCamera);
		}
	}
	if (cam < 0x10000)
	{
		cam = game::camera;
	}

	if (cam < 0x10000)
		return 1.0f;

	float cur = memory->read<float>(cam + Offsets::Camera::FieldOfView);
	if (cur <= 0.001f || std::isnan(cur) || std::isinf(cur))
		return 1.0f;

	float cur_deg = (cur < 3.2f) ? (cur * (180.0f / 3.14159265358979323846f)) : cur;
	if (cur_deg <= 1.0f || cur_deg > 170.0f)
		return 1.0f;

	float scale = 70.0f / cur_deg;
	if (scale < 0.1f) scale = 0.1f;
	if (scale > 10.0f) scale = 10.0f;
	return scale;
}

static float get_distance_from_center(const math::vector2& point, const math::vector2& position)
{
	float dx{ position.x - point.x };
	float dy{ position.y - point.y };
	return std::sqrt(dx * dx + dy * dy);
}

static bool get_target_point_for_entity
(
	const cache::entity_t& entity,
	const math::matrix4& view,
	const math::vector2& dims,
	float fov,
	float& out_distance,
	math::vector2& out_screen,
	int target_part_override = -1,
	bool check_fov = true
)
{
	math::vector2 cursor_pos = dims * 0.5f;
	POINT cursor{};
	HWND roblox_hwnd = game::get_roblox_window();
	if (GetCursorPos(&cursor) && roblox_hwnd)
	{
		ScreenToClient(roblox_hwnd, &cursor);
		if (cursor.x >= 0 && cursor.y >= 0 && cursor.x <= dims.x && cursor.y <= dims.y)
		{
			cursor_pos = math::vector2{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
		}
	}

	float best_distance = FLT_MAX;
	bool found = false;

	auto try_part = [&](const char* name)
		{
			math::vector3 pos_3d{};
			if (!bodyparts::get_part_position(entity, name, pos_3d))
			{
				return;
			}

			if (settings::aimbot::enable_prediction)
			{
				pos_3d = apply_prediction(entity, pos_3d);
			}

			math::vector2 pos_2d{};

			if (!game::visualengine->world_to_screen(view, dims, pos_3d, pos_2d))
			{
				return;
			}

			const float distance = get_distance_from_center(cursor_pos, pos_2d);

			float current_fov = fov;
			if (settings::aimbot::dynamic_fov)
			{
				current_fov *= get_fov_scale();
			}

			if (check_fov && settings::aimbot::use_fov && distance > current_fov)
			{
				return;
			}

			if (distance < best_distance)
			{
				best_distance = distance;
				out_screen = pos_2d;
				found = true;
			}
		};

	int part_to_use = target_part_override != -1 ? target_part_override : settings::aimbot::target_part;
	switch (part_to_use)
	{
	case 0: 
		try_part("Head");
		break;
	case 1: 
	{
		auto part_names = bodyparts::get_part_names(entity, "Torso");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 2: 
		try_part("HumanoidRootPart");
		break;
	case 3: 
	{
		auto part_names = bodyparts::get_part_names(entity, "LeftArm");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 4: 
	{
		auto part_names = bodyparts::get_part_names(entity, "RightArm");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 5:
	{
		auto part_names = bodyparts::get_part_names(entity, "LeftLeg");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 6: 
	{
		auto part_names = bodyparts::get_part_names(entity, "RightLeg");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 7:
	case 8:
	{
		static const char* all_bones[] = {
			"Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart",
			"LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
			"RightArm", "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
			"LeftLeg", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
			"RightLeg", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg"
		};
		for (const char* bone : all_bones)
		{
			try_part(bone);
		}
		break;
	}
	default:
		try_part("Head");
		break;
	}

	if (!found)
	{
		try_part("Head");
		if (!found)
			try_part("HumanoidRootPart");
	}

	if (!found)
	{
		return false;
	}

	out_distance = best_distance;
	return true;
}

static void mouse_aim(const math::vector2& target, const math::vector2& dims)
{
	math::vector2 center = dims * 0.5f;
	math::vector2 screen_pos = center;

	HWND roblox_hwnd = game::get_roblox_window();
	POINT cursor{};
	if (GetCursorPos(&cursor) && roblox_hwnd)
	{
		ScreenToClient(roblox_hwnd, &cursor);
		if (cursor.x >= 0 && cursor.y >= 0 && cursor.x <= dims.x && cursor.y <= dims.y)
		{
			screen_pos = math::vector2{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
		}
	}

	float delta_x = target.x - screen_pos.x;
	float delta_y = target.y - screen_pos.y;

	const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
	if (distance < 1.0f)
	{
		return;
	}

	delta_x *= settings::aimbot::mouse_sensitivity;
	delta_y *= settings::aimbot::mouse_sensitivity;

	if (settings::aimbot::shake)
	{
		float range = settings::aimbot::shake_value;
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dis(-range, range);
		delta_x += dis(gen);
		delta_y += dis(gen);
	}

	if (!settings::aimbot::smoothing)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = static_cast<LONG>(std::round(delta_x));
		input.mi.dy = static_cast<LONG>(std::round(delta_y));
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));
		return;
	}

	float sx = (std::max)(1.0f, settings::aimbot::smoothingx);
	float sy = (std::max)(1.0f, settings::aimbot::smoothingy);

	float tx = std::clamp(1.0f / sx, 0.005f, 1.0f);
	float ty = std::clamp(1.0f / sy, 0.005f, 1.0f);

	float smooth_factor_x, smooth_factor_y;

	switch (settings::aimbot::smoothing_style) {
	case 1: smooth_factor_x = easing::linear(tx); smooth_factor_y = easing::linear(ty); break;
	case 2: smooth_factor_x = easing::ease_in_quad(tx); smooth_factor_y = easing::ease_in_quad(ty); break;
	case 3: smooth_factor_x = easing::ease_out_quad(tx); smooth_factor_y = easing::ease_out_quad(ty); break;
	case 4: smooth_factor_x = easing::ease_in_out_quad(tx); smooth_factor_y = easing::ease_in_out_quad(ty); break;
	case 5: smooth_factor_x = easing::ease_in_cubic(tx); smooth_factor_y = easing::ease_in_cubic(ty); break;
	case 6: smooth_factor_x = easing::ease_out_cubic(tx); smooth_factor_y = easing::ease_out_cubic(ty); break;
	case 7: smooth_factor_x = easing::ease_in_out_cubic(tx); smooth_factor_y = easing::ease_in_out_cubic(ty); break;
	case 8: smooth_factor_x = easing::ease_in_sine(tx); smooth_factor_y = easing::ease_in_sine(ty); break;
	case 9: smooth_factor_x = easing::ease_out_sine(tx); smooth_factor_y = easing::ease_out_sine(ty); break;
	case 10: smooth_factor_x = easing::ease_in_out_sine(tx); smooth_factor_y = easing::ease_in_out_sine(ty); break;
	default: smooth_factor_x = tx; smooth_factor_y = ty; break;
	}

	float step_x = delta_x * smooth_factor_x;
	float step_y = delta_y * smooth_factor_y;

	static thread_local float subpixel_x = 0.0f;
	static thread_local float subpixel_y = 0.0f;

	float to_move_x = step_x + subpixel_x;
	float to_move_y = step_y + subpixel_y;

	int move_x = static_cast<int>(std::round(to_move_x));
	int move_y = static_cast<int>(std::round(to_move_y));

	if (move_x != 0 || move_y != 0)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = move_x;
		input.mi.dy = move_y;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));

		subpixel_x = to_move_x - static_cast<float>(move_x);
		subpixel_y = to_move_y - static_cast<float>(move_y);
	}
}

static void mouse_aimbot()
{
	if (aimbot::player.instance.address == 0)
	{
		return;
	}

	const auto fc   = frame_cache::get_for_thread();
	const math::matrix4& view = fc.view;
	const math::vector2& dims = fc.dims;

	math::vector3 cam_pos{};
	if (game::camera != 0)
	{
		cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	}
	float world_distance = get_world_distance_to_entity(aimbot::player, cam_pos);
	if (world_distance == FLT_MAX)
	{
		return;
	}

	math::vector3 velocity = get_velocity(aimbot::player);
	bool is_in_air = std::abs(velocity.y) > 1.5f;
	int part_to_use = (is_in_air && settings::aimbot::air_part > 0) ? settings::aimbot::air_part : settings::aimbot::target_part;

	float distance{};
	math::vector2 target_screen{};
	if (get_target_point_for_entity(aimbot::player, view, dims, settings::aimbot::fov, distance, target_screen, part_to_use, !settings::aimbot::sticky_aim))
	{
		mouse_aim(target_screen, dims);
	}
}

static math::matrix3 look_at(const math::vector3& from, const math::vector3& to)
{
	math::vector3 forward = (to - from);
	float length = forward.length();
	if (length < 0.0001f)
	{
		return math::matrix3::identity();
	}
	forward = forward * (1.0f / length);

	math::vector3 world_up(0.0f, 1.0f, 0.0f);
	math::vector3 right = world_up.cross(forward);
	length = right.length();
	if (length < 0.0001f)
	{
		world_up = math::vector3(0.0f, 0.0f, 1.0f);
		right = world_up.cross(forward);
		length = right.length();
		if (length < 0.0001f)
		{
			return math::matrix3::identity();
		}
	}
	right = right * (1.0f / length);

	math::vector3 up = forward.cross(right);

	math::matrix3 result{};
	result.m[0][0] = -right.x;
	result.m[0][1] = up.x;
	result.m[0][2] = -forward.x;
	result.m[1][0] = -right.y;
	result.m[1][1] = up.y;
	result.m[1][2] = -forward.y;
	result.m[2][0] = -right.z;
	result.m[2][1] = up.z;
	result.m[2][2] = -forward.z;

	return result;
}

static math::matrix3 lerp_rotation(const math::matrix3& from, const math::matrix3& to, float t)
{
	math::vector3 from_forward = from.forward();
	math::vector3 to_forward = to.forward();
	math::vector3 from_right = from.right();
	math::vector3 to_right = to.right();
	math::vector3 from_up = from.up();
	math::vector3 to_up = to.up();
	math::vector3 lerped_forward = (from_forward + (to_forward - from_forward) * t).normalized();
	math::vector3 lerped_right = (from_right + (to_right - from_right) * t).normalized();
	math::vector3 lerped_up = lerped_forward.cross(lerped_right).normalized();
	lerped_right = lerped_up.cross(lerped_forward).normalized();
	math::matrix3 result{};
	result.m[0][0] = -lerped_right.x;
	result.m[0][1] = lerped_up.x;
	result.m[0][2] = -lerped_forward.x;
	result.m[1][0] = -lerped_right.y;
	result.m[1][1] = lerped_up.y;
	result.m[1][2] = -lerped_forward.y;
	result.m[2][0] = -lerped_right.z;
	result.m[2][1] = lerped_up.z;
	result.m[2][2] = -lerped_forward.z;
	return result;
}

static void camera_aimbot()
{
	if (aimbot::player.instance.address == 0)
	{
		return;
	}

	if (game::camera == 0)
	{
		return;
	}

	math::vector3 target_pos;
	bool found_part = false;

	math::vector3 velocity = get_velocity(aimbot::player);
	bool is_in_air = std::abs(velocity.y) > 1.5f;
	int part_to_use = (is_in_air && settings::aimbot::air_part > 0) ? settings::aimbot::air_part : settings::aimbot::target_part;

	switch (part_to_use)
	{
	case 0:
		found_part = bodyparts::get_part_position(aimbot::player, "Head", target_pos);
		break;
	case 1:
		found_part = bodyparts::get_part_position(aimbot::player, "Torso", target_pos);
		break;
	case 2:
		found_part = bodyparts::get_part_position(aimbot::player, "HumanoidRootPart", target_pos);
		break;
	case 3:
		found_part = bodyparts::get_part_position(aimbot::player, "LeftArm", target_pos);
		break;
	case 4:
		found_part = bodyparts::get_part_position(aimbot::player, "RightArm", target_pos);
		break;
	case 5:
		found_part = bodyparts::get_part_position(aimbot::player, "LeftLeg", target_pos);
		break;
	case 6:
		found_part = bodyparts::get_part_position(aimbot::player, "RightLeg", target_pos);
		break;
	case 7: 
	case 8: 
	{
		static const char* all_bones[] = {
			"Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart",
			"LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
			"RightArm", "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
			"LeftLeg", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
			"RightLeg", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg"
		};
		math::vector3 camera_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		float best_dist = FLT_MAX;
		for (const char* bone : all_bones)
		{
			math::vector3 p{};
			if (bodyparts::get_part_position(aimbot::player, bone, p))
			{
				float dist = (p - camera_pos).length();
				if (dist < best_dist)
				{
					best_dist = dist;
					target_pos = p;
					found_part = true;
				}
			}
		}
		break;
	}
	default:
		found_part = bodyparts::get_part_position(aimbot::player, "Head", target_pos);
		if (!found_part)
			found_part = bodyparts::get_part_position(aimbot::player, "HumanoidRootPart", target_pos);
		break;
	}

	if (!found_part)
	{
		return;
	}

	math::vector3 camera_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	if (settings::aimbot::enable_prediction)
	{
		target_pos = apply_prediction(aimbot::player, target_pos);
	}
	math::vector3 direction = target_pos - camera_pos;
	float distance = direction.length();
	if (distance < 0.1f)
	{
		return;
	}

	math::matrix3 target_rot = look_at(camera_pos, target_pos);
	math::matrix3 current_rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);

	math::vector3 current_forward = current_rot.forward();
	math::vector3 target_forward = target_rot.forward();

	float current_yaw = std::atan2(-current_forward.x, -current_forward.z);
	float current_pitch = std::asin(current_forward.y);

	float target_yaw = std::atan2(-target_forward.x, -target_forward.z);
	float target_pitch = std::asin(target_forward.y);

	float yaw_diff = target_yaw - current_yaw;
	if (yaw_diff > 3.14159f)
	{
		yaw_diff -= 6.28318f;
	}
	else if (yaw_diff < -3.14159f)
	{
		yaw_diff += 6.28318f;
	}

	float pitch_diff = target_pitch - current_pitch;

	if (settings::aimbot::shake)
	{
		float range_rad = settings::aimbot::shake_value * 0.001f;
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dis(-range_rad, range_rad);
		yaw_diff += dis(gen);
		pitch_diff += dis(gen);
	}

	uint64_t pf_cam_part_primitive = 0;
	if (game::camera != 0)
	{
		rbx::c_instance camera_inst(game::camera);
		uint64_t cam_part_addr = camera_inst.find_first_child("Part");
		if (cam_part_addr != 0)
		{
			rbx::c_part cam_part(cam_part_addr);
			rbx::c_primitive prim = cam_part.get_primitive();
			if (prim.address != 0)
			{
				pf_cam_part_primitive = prim.address;
			}
		}
	}

	if (settings::aimbot::smoothing) {
		float sx = max(1.0f, settings::aimbot::smoothingx);
		float sy = max(1.0f, settings::aimbot::smoothingy);
		float tx = std::clamp(1.0f / sx, 0.01f, 1.0f);
		float ty = std::clamp(1.0f / sy, 0.01f, 1.0f);

		float smooth_factor_x, smooth_factor_y;

		switch (settings::aimbot::smoothing_style) {
		case 1: smooth_factor_x = easing::linear(tx); smooth_factor_y = easing::linear(ty); break;
		case 2: smooth_factor_x = easing::ease_in_quad(tx); smooth_factor_y = easing::ease_in_quad(ty); break;
		case 3: smooth_factor_x = easing::ease_out_quad(tx); smooth_factor_y = easing::ease_out_quad(ty); break;
		case 4: smooth_factor_x = easing::ease_in_out_quad(tx); smooth_factor_y = easing::ease_in_out_quad(ty); break;
		case 5: smooth_factor_x = easing::ease_in_cubic(tx); smooth_factor_y = easing::ease_in_cubic(ty); break;
		case 6: smooth_factor_x = easing::ease_out_cubic(tx); smooth_factor_y = easing::ease_out_cubic(ty); break;
		case 7: smooth_factor_x = easing::ease_in_out_cubic(tx); smooth_factor_y = easing::ease_in_out_cubic(ty); break;
		case 8: smooth_factor_x = easing::ease_in_sine(tx); smooth_factor_y = easing::ease_in_sine(ty); break;
		case 9: smooth_factor_x = easing::ease_out_sine(tx); smooth_factor_y = easing::ease_out_sine(ty); break;
		case 10: smooth_factor_x = easing::ease_in_out_sine(tx); smooth_factor_y = easing::ease_in_out_sine(ty); break;
		default: smooth_factor_x = tx; smooth_factor_y = ty; break;
		}

		float avg_factor = std::clamp((smooth_factor_x + smooth_factor_y) * 0.5f, 0.001f, 1.0f);
		math::matrix3 smoothed_rotation = lerp_rotation(current_rot, target_rot, avg_factor);
		memory->write<math::matrix3>(game::camera + Offsets::Camera::Rotation, smoothed_rotation);
		if (pf_cam_part_primitive != 0)
		{
			memory->write<math::matrix3>(pf_cam_part_primitive + Offsets::Primitive::Rotation, smoothed_rotation);
		}
	} else {
		memory->write<math::matrix3>(game::camera + Offsets::Camera::Rotation, target_rot);
		if (pf_cam_part_primitive != 0)
		{
			memory->write<math::matrix3>(pf_cam_part_primitive + Offsets::Primitive::Rotation, target_rot);
		}
	}
}

static float get_world_distance_to_entity(const cache::entity_t& entity, const math::vector3& cam_pos)
{
	if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
	{
		return cam_pos.distance(entity.position);
	}
	return FLT_MAX;
}

static cache::entity_t get_closest_player(const math::matrix4& view, const math::vector2& dims)
{
	cache::entity_t best_player{};
	float closest = FLT_MAX;
	cache::entity_t best_hostile_player{};
	float closest_hostile = FLT_MAX;

	bool ignore_friendly = (settings::aimbot::priorities & (1 << 0)) != 0;
	bool prioritise_hostile = (settings::aimbot::priorities & (1 << 1)) != 0;

	math::vector3 cam_pos{};
	if (game::camera != 0)
	{
		cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
	}

	auto entities_snap = cache::get_players_snap();
	auto local_snap    = cache::get_local_snap();
	if (!entities_snap) return cache::entity_t{};
	const std::vector<cache::entity_t>& entities_snapshot = *entities_snap;
	static cache::entity_t s_empty_local{};
	const cache::entity_t& local_entity = local_snap ? *local_snap : s_empty_local;

	const std::uint64_t local_addr = local_entity.instance.address;
	const std::uint64_t local_team = local_entity.team;
	const bool use_teamcheck = settings::teamcheck || settings::aimbot::teamcheck;

	if (settings::silentaim::shoot_spectating && playerlist::is_spectating())
	{
		std::string spec_name = playerlist::get_spectating_name();
		for (const auto& entity : entities_snapshot)
		{
			if (entity.name == spec_name && entity.health > 0.f)
			{
				return entity;
			}
		}
	}

	for (const cache::entity_t& entity : entities_snapshot)
	{
		if (!entity.instance.address)
		{
			continue;
		}

		if (entity.instance.address == local_addr)
		{
			continue;
		}

		if (use_teamcheck && local_team != 0 && entity.team == local_team)
		{
			continue;
		}

		if (settings::aimbot::knock_check && entity.knocked)
		{
			continue;
		}

		if (settings::aimbot::health_check_enabled && entity.health < settings::aimbot::min_health)
		{
			continue;
		}

		cache::player_priority priority = playerlist::get_priority(entity.name);
		if (ignore_friendly && priority == cache::player_priority::friendly)
		{
			continue;
		}

		float world_distance = get_world_distance_to_entity(entity, cam_pos);
		if (world_distance == FLT_MAX)
		{
			continue;
		}

		float distance{};
		math::vector2 target_screen{};
		if (!get_target_point_for_entity(entity, view, dims, settings::aimbot::fov, distance, target_screen))
		{
			continue;
		}

		if (prioritise_hostile)
		{
			if (priority == cache::player_priority::hostile)
			{
				if (distance < closest_hostile)
				{
					closest_hostile = distance;
					best_hostile_player = entity;
				}
			}
			else
			{
				if (distance < closest)
				{
					closest = distance;
					best_player = entity;
				}
			}
		}
		else
		{
			if (distance < closest)
			{
				closest = distance;
				best_player = entity;
			}
		}
	}

	if (prioritise_hostile && best_hostile_player.instance.address)
	{
		return best_hostile_player;
	}

	return best_player;
}

static bool is_target_valid(const cache::entity_t& entity)
{
	if (entity.instance.address == 0)
	{
		return false;
	}

	cache::entity_t local = cache::get_local_player();
	if (entity.instance.address == local.instance.address)
	{
		return false;
	}

	bool use_teamcheck = settings::teamcheck || settings::aimbot::teamcheck;
	if (use_teamcheck && local.team != 0 && entity.team == local.team)
	{
		return false;
	}

	if (settings::aimbot::knock_check && entity.knocked)
	{
		return false;
	}

	if (settings::aimbot::health_check_enabled && entity.health < settings::aimbot::min_health)
	{
		return false;
	}

	bool ignore_friendly = (settings::aimbot::priorities & (1 << 0)) != 0;
	if (ignore_friendly)
	{
		cache::player_priority priority = playerlist::get_priority(entity.name);
		if (priority == cache::player_priority::friendly)
		{
			return false;
		}
	}

	return true;
}

void aimbot::run()
{
	using namespace std::chrono_literals;

	keybind::keybind_t aimbot_kb{};

	for (;;)
	{
		if (!settings::aimbot::enabled)
		{
			aimbot::clear_sticky_target();
			aimbot::clear_player();
			Sleep(50);
			continue;
		}

		aimbot_kb.key = settings::aimbot::keybind;
		aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

		if (g_menu.IsMenuOpened() || !keybind::is_active(aimbot_kb))
		{
			aimbot::clear_sticky_target();
			aimbot::clear_player();
			Sleep(50);
			continue;
		}

		const auto fc   = frame_cache::get_for_thread();
		const math::matrix4& view = fc.view;
		const math::vector2& dims = fc.dims;

		cache::entity_t player{};

		if (settings::aimbot::sticky_aim && aimbot::sticky_target.instance.address != 0)
		{
			cache::entity_t refreshed_target{};
			bool target_found = false;
			auto snap = cache::get_players_snap();
			if (snap)
			{
				for (const auto& entity : *snap)
				{
					if (entity.instance.address == aimbot::sticky_target.instance.address)
					{
						refreshed_target = entity;
						target_found = true;
						break;
					}
				}
			}

			if (target_found && is_target_valid(refreshed_target))
			{
				player = refreshed_target;
				aimbot::set_sticky_target(refreshed_target);
			}
			else
			{
				aimbot::clear_sticky_target();
				player = get_closest_player(view, dims);
				if (settings::aimbot::sticky_aim && player.instance.address != 0)
				{
					aimbot::set_sticky_target(player);
				}
			}
		}
		else
		{
			player = get_closest_player(view, dims);
			if (settings::aimbot::sticky_aim && player.instance.address != 0)
			{
				aimbot::set_sticky_target(player);
			}
		}

		if (player.instance.address == 0)
		{
			aimbot::clear_player();
			Sleep(8);
			continue;
		}

		if (settings::aimbot::disable_on_kill && (player.health <= 0.0f || player.knocked))
		{
			settings::aimbot::enabled = false;
			aimbot::clear_sticky_target();
			aimbot::clear_player();
			Sleep(8);
			continue;
		}

		aimbot::set_player(player);

		if (settings::aimbot::mode == 0)
		{
			mouse_aimbot();
		}
		else if (settings::aimbot::mode == 1)
		{
			camera_aimbot();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(8));
	}
}