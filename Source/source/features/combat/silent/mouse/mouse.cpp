#include "mouse.h"
#include <features/system/playerlist/playerlist.h>

#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <features/system/settings/settings.h>
#include <features/system/keybind/keybind.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <sdk/wallcheck/wallcheck.h>
#include <features/combat/aimbot/aimbot.h>
static math::vector3 get_velocity(cache::entity_t& entity)
{
	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
		return math::vector3{};

	rbx::c_primitive prim = root_it->second.get_primitive();
	if (!prim.address)
		return math::vector3{};

	return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
}

static math::vector3 apply_prediction(cache::entity_t& entity, const math::vector3& position)
{
	math::vector3 velocity = get_velocity(entity);
	float factor_x = (10.0f - settings::silentaim::prediction_x) * 0.1f;
	float factor_y = (10.0f - settings::silentaim::prediction_y) * 0.1f;

	return {
		position.x + velocity.x * factor_x,
		position.y + velocity.y * factor_y,
		position.z + velocity.z * factor_x
	};
}

static bool check_gun_equipped_impl();

static bool check_gun_equipped()
{
	static bool s_cached_result = false;
	static std::int64_t s_cached_at_ms = -10000;
	static std::uint64_t s_cached_character = 0;

	auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	std::uint64_t current_character = cache::local_character.address;

	if (s_cached_character == current_character &&
		now_ms - s_cached_at_ms < 250)
	{
		return s_cached_result;
	}

	s_cached_result = check_gun_equipped_impl();
	s_cached_at_ms = now_ms;
	s_cached_character = current_character;
	return s_cached_result;
}

static bool check_gun_equipped_impl()
{
	auto local_snap = cache::get_local_snap();
	if (local_snap && !local_snap->tool_name.empty())
	{
		return true;
	}

	if (silentaim::state.aim_indicator.address)
	{
		bool is_visible = memory->read<bool>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Visible);
		if (is_visible)
			return true;
	}

	if (cache::local_character.address != 0)
	{
		uint64_t tool = cache::local_character.find_first_child_by_class("Tool");
		if (tool != 0)
			return true;

		std::vector<uint64_t> children = cache::local_character.get_children();
		for (uint64_t child_addr : children)
		{
			rbx::c_instance child(child_addr);
			if (!child.address) continue;
			std::string name = child.get_name();
			std::string cls = child.get_class_name();
			if (cls == "Tool") return true;
			if (cls == "Model" || cls == "Folder" || cls == "Accessory")
			{
				std::string lower_name = name;
				std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
				if (lower_name.find("gun") != std::string::npos ||
					lower_name.find("weapon") != std::string::npos ||
					lower_name.find("rifle") != std::string::npos ||
					lower_name.find("pistol") != std::string::npos ||
					lower_name.find("shotgun") != std::string::npos ||
					lower_name.find("revolver") != std::string::npos ||
					lower_name.find("sniper") != std::string::npos ||
					lower_name.find("smg") != std::string::npos ||
					lower_name.find("arm") != std::string::npos ||
					lower_name.find("hold") != std::string::npos ||
					lower_name.find("combat") != std::string::npos ||
					lower_name.find("knife") != std::string::npos ||
					lower_name.find("sword") != std::string::npos)
				{
					return true;
				}
				if (child.find_first_child("Handle") || child.find_first_child("Muzzle") || child.find_first_child("Grip"))
				{
					return true;
				}
			}
		}
	}

	if (game::camera != 0)
	{
		rbx::c_instance cam(game::camera);
		std::vector<uint64_t> cam_children = cam.get_children();
		for (uint64_t c_addr : cam_children)
		{
			rbx::c_instance c(c_addr);
			if (!c.address) continue;
			std::string cls = c.get_class_name();
			if (cls == "Model" || cls == "Tool")
			{
				std::string nm = c.get_name();
				std::string lower_nm = nm;
				std::transform(lower_nm.begin(), lower_nm.end(), lower_nm.begin(), ::tolower);
				if (lower_nm.find("viewmodel") != std::string::npos ||
					lower_nm.find("gun") != std::string::npos ||
					lower_nm.find("weapon") != std::string::npos ||
					lower_nm.find("arms") != std::string::npos)
				{
					return true;
				}
			}
		}
	}

	if (cache::player_gui.address != 0)
	{
		std::vector<uint64_t> guis = cache::player_gui.get_children();
		for (uint64_t g_addr : guis)
		{
			rbx::c_instance g(g_addr);
			if (!g.address) continue;
			std::string g_name = g.get_name();
			std::string lower_g = g_name;
			std::transform(lower_g.begin(), lower_g.end(), lower_g.begin(), ::tolower);
			if (lower_g.find("gun") != std::string::npos ||
				lower_g.find("weapon") != std::string::npos ||
				lower_g.find("ammo") != std::string::npos ||
				lower_g.find("crosshair") != std::string::npos ||
				lower_g.find("cursor") != std::string::npos ||
				lower_g.find("reticle") != std::string::npos)
			{
				return true;
			}
		}
	}

	return false;
}

static bool is_silent_aim_active()
{
	if (!game::datamodel || !game::visualengine)
		return false;

	if (settings::silentaim::shoot_spectating && playerlist::is_spectating())
		return true;

	if (!settings::silentaim::enabled)
		return false;

	keybind::keybind_t silent_kb{};
	silent_kb.key = settings::silentaim::keybind;
	silent_kb.mode = static_cast<keybind::activation_mode>(settings::silentaim::activation_mode);

	if (!keybind::is_active(silent_kb))
		return false;

	if (settings::silentaim::guncheck)
	{
		if (!check_gun_equipped())
			return false;
	}

	return true;
}

static bool is_valid_target(const cache::entity_t& entity, const cache::entity_t& local_player, bool ignore_friendly)
{
	if (entity.instance.address == local_player.instance.address)
		return false;

	if (settings::silentaim::shoot_spectating && playerlist::is_spectating() && playerlist::get_spectating_name() == entity.name)
		return true;

	if (settings::teamcheck && local_player.team != 0 && entity.team == local_player.team)
		return false;

	if (settings::silentaim::knock_check && entity.knocked)
		return false;

	if (settings::silentaim::health_check_enabled && entity.health < settings::silentaim::min_health)
		return false;

	if (ignore_friendly && playerlist::get_priority(entity.name) == cache::player_priority::friendly)
		return false;

	if (settings::silentaim::forcefield_check && entity.humanoid.address != 0)
	{
		std::uint64_t character_address = memory->read<std::uint64_t>(entity.humanoid.address + Offsets::Instance::Parent);
		if (character_address != 0)
		{
			rbx::c_instance character(character_address);
			if (character.find_first_child_by_class("ForceField") != 0)
				return false;
		}
	}

	return true;
}

static bool get_target_position(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims, const math::vector2& mouse_pos, math::vector3& out_pos)
{
	int part_idx = settings::silentaim::target_part;

	bool got_part = false;
	if (part_idx == 0)
	{
		got_part = bodyparts::get_part_position(entity, "Head", out_pos);
	}
	else if (part_idx == 1)
	{
		got_part = bodyparts::get_part_position(entity, "Torso", out_pos);
	}
	else if (part_idx == 2)
	{
		got_part = bodyparts::get_part_position(entity, "HumanoidRootPart", out_pos);
	}
	else if (part_idx == 3)
	{
		got_part = bodyparts::get_part_position(entity, "LeftArm", out_pos);
	}
	else if (part_idx == 4) 
	{
		got_part = bodyparts::get_part_position(entity, "RightArm", out_pos);
	}
	else if (part_idx == 5)
	{
		got_part = bodyparts::get_part_position(entity, "LeftLeg", out_pos);
	}
	else if (part_idx == 6)
	{
		got_part = bodyparts::get_part_position(entity, "RightLeg", out_pos);
	}
	else if (part_idx == 7 || part_idx == 8) // Closest Part / Nearest
	{
		static const char* all_bones[] = {
			"Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart",
			"LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
			"RightArm", "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
			"LeftLeg", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
			"RightLeg", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg"
		};
		float best_dist = FLT_MAX;
		for (const char* bone : all_bones)
		{
			math::vector3 p{};
			if (bodyparts::get_part_position(entity, bone, p))
			{
				math::vector2 scr{};
				if (game::visualengine && game::visualengine->world_to_screen(view, dims, p, scr))
				{
					float dx = scr.x - mouse_pos.x;
					float dy = scr.y - mouse_pos.y;
					float dist = dx * dx + dy * dy;
					if (dist < best_dist)
					{
						best_dist = dist;
						out_pos = p;
						got_part = true;
					}
				}
				else if (!got_part)
				{
					out_pos = p;
					got_part = true;
				}
			}
		}
	}
	else
	{
		got_part = bodyparts::get_part_position(entity, "Head", out_pos);
	}

	if (!got_part)
	{
		if (!bodyparts::get_part_position(entity, "Head", out_pos))
		{
			if (!bodyparts::get_part_position(entity, "HumanoidRootPart", out_pos))
				return false;
		}
	}

	if (settings::silentaim::enable_prediction)
		out_pos = apply_prediction(const_cast<cache::entity_t&>(entity), out_pos);

	return true;
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

static cache::entity_t get_closest_to_mouse()
{
	if (!game::visualengine || !game::datamodel)
		return cache::entity_t{};

	const bool ignore_friendly = (settings::silentaim::priorities & (1 << 0)) != 0;
	const bool prioritise_hostile = (settings::silentaim::priorities & (1 << 1)) != 0;

	const auto fc   = frame_cache::get_for_thread();
	const math::matrix4& view = fc.view;
	const math::vector2& dims = fc.dims;

	POINT cursor{};
	GetCursorPos(&cursor);
	const math::vector2 mouse_pos{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

	auto entities_snap = cache::get_players_snap();
	auto local_snap    = cache::get_local_snap();
	if (!entities_snap) return cache::entity_t{};
	const std::vector<cache::entity_t>& entities_snapshot = *entities_snap;
	static cache::entity_t s_empty_local{};
	const cache::entity_t& local_player_snapshot = local_snap ? *local_snap : s_empty_local;

	cache::entity_t best_target{};
	float best_distance = FLT_MAX;
	cache::entity_t best_hostile_target{};
	float best_hostile_distance = FLT_MAX;
	// If spectating someone, directly target them so shots land on them!
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

	for (const auto& entity : entities_snapshot)
	{
		if (!is_valid_target(entity, local_player_snapshot, ignore_friendly))
			continue;

		math::vector3 target_pos;
		if (!get_target_position(entity, view, dims, mouse_pos, target_pos))
			continue;

		math::vector2 screen_pos{};
		if (!game::visualengine->world_to_screen(view, dims, target_pos, screen_pos))
			continue;

		const float distance = mouse_pos.distance(screen_pos);
		float fov_limit = settings::silentaim::fov;
		if (settings::silentaim::dynamic_fov)
		{
			fov_limit *= get_fov_scale();
		}
		if (distance > fov_limit)
			continue;

		if (prioritise_hostile && playerlist::get_priority(entity.name) == cache::player_priority::hostile)
		{
			if (distance < best_hostile_distance)
			{
				best_hostile_distance = distance;
				best_hostile_target = entity;
			}
		}
		else if (distance < best_distance)
		{
			best_distance = distance;
			best_target = entity;
		}
	}

	return (prioritise_hostile && best_hostile_target.instance.address) ? best_hostile_target : best_target;
}

static void targeting_thread()
{
	using namespace std::chrono_literals;

	static std::int32_t aim_indicator_check_counter = 0;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if (!game::datamodel || !game::visualengine)
		{
			std::this_thread::sleep_for(100ms);
			continue;
		}

		if (aim_indicator_check_counter++ % 10 == 0)
		{
			try
			{
				rbx::c_instance player_gui;
				{
					std::lock_guard<std::mutex> lock(cache::mtx);
					player_gui = cache::player_gui;
				}

				if (!player_gui.address)
				{
					silentaim::state.aim_indicator = rbx::c_instance{};
					continue;
				}

				rbx::c_instance aim_frame{};
				std::vector<std::uint64_t> children = player_gui.get_children();

				for (std::uint64_t child_addr : children)
				{
					rbx::c_instance child(child_addr);
					if (!child.address)
						continue;

					std::string child_name = child.get_name();
					if (child_name == "Aim")
					{
						aim_frame = child;
						break;
					}

					std::string child_class = child.get_class_name();
					if (child_class == "Frame" || child_class == "ScreenGui" || child_class == "GuiObject")
					{
						std::string child_lower = child_name;
						std::transform(child_lower.begin(), child_lower.end(), child_lower.begin(), ::tolower);

						if (child_lower.find("main") != std::string::npos)
						{
							std::vector<std::uint64_t> grandchildren = child.get_children();
							for (std::uint64_t grandchild_addr : grandchildren)
							{
								rbx::c_instance grandchild(grandchild_addr);
								if (grandchild.address && grandchild.get_name() == "Aim")
								{
									aim_frame = grandchild;
									break;
								}
							}

							if (aim_frame.address)
								break;
						}
					}
				}

				if (aim_frame.address != silentaim::state.aim_indicator.address)
				{
					if (silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
					{
						std::string old_name = rbx::c_instance(silentaim::state.aim_indicator.address).get_name();
						if (old_name == "Aim")
						{
							memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
							for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
							{
								memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
							}
						}
					}

					silentaim::state.aim_indicator = aim_frame;
					silentaim::state.has_original_sizes = false;
					silentaim::state.original_children_sizes.clear();

					if (silentaim::state.aim_indicator.address)
					{
						silentaim::state.original_size = memory->read<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size);
						std::vector<std::uint64_t> children = silentaim::state.aim_indicator.get_children();
						for (std::uint64_t child : children)
						{
							math::vector2 child_size = memory->read<math::vector2>(child + Offsets::GuiObject::Size);
							silentaim::state.original_children_sizes.push_back({ child, child_size });
						}
						silentaim::state.has_original_sizes = true;
					}
				}

				if (settings::silentaim::enabled && silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
				{
					memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, { 0, 0 });

					std::vector<std::uint64_t> children = silentaim::state.aim_indicator.get_children();
					for (std::uint64_t child : children)
					{
						memory->write<math::vector2>(child + Offsets::GuiObject::Size, { 0, 0 });
					}
				}
				else if (!settings::silentaim::enabled && silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
				{
					memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
					for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
					{
						memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
					}
				}
			}
			catch (...)
			{
				silentaim::state.aim_indicator = rbx::c_instance{};
			}
		}

		if (!settings::silentaim::enabled && silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
		{
			std::string old_name = rbx::c_instance(silentaim::state.aim_indicator.address).get_name();
			if (old_name == "Aim")
			{
				memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
				for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
				{
					memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
				}
			}
		}

		const bool active = is_silent_aim_active();
		if (!active)
		{
			silentaim::state.data_ready = false;
			silentaim::clear_target();
			std::this_thread::sleep_for(100ms);
			continue;
		}

		const bool ignore_friendly = (settings::silentaim::priorities & (1 << 0)) != 0;

		if (settings::silentaim::use_aimbot_target)
		{
			if (aimbot::player.instance.address == 0)
			{
				silentaim::clear_target();
				silentaim::state.data_ready = false;
				std::this_thread::sleep_for(10ms);
				continue;
			}
			silentaim::set_target(aimbot::get_player());
		}
		else
		{
			if (settings::silentaim::sticky_aim && silentaim::state.target.instance.address)
			{
				cache::entity_t refreshed_target{};
				cache::entity_t local_player_snapshot{};
				bool target_found = false;
				{
					std::lock_guard<std::mutex> lock(cache::mtx);
					local_player_snapshot = cache::get_local_player();
					for (const auto& entity : cache::players)
					{
						if (entity.instance.address == silentaim::state.target.instance.address)
						{
							refreshed_target = entity;
							target_found = true;
							break;
						}
					}
				}

				if (target_found)
				{
					if (is_valid_target(refreshed_target, local_player_snapshot, ignore_friendly))
					{
						silentaim::set_target(refreshed_target);
					}
					else
					{
						silentaim::clear_target();
						silentaim::state.data_ready = false;
						continue;
					}
				}
				else
				{
					silentaim::clear_target();
					silentaim::state.data_ready = false;
					continue;
				}
			}
			else if (!settings::silentaim::sticky_aim)
			{
				silentaim::clear_target();
			}

			if (!silentaim::state.target.instance.address)
			{
				cache::entity_t new_target = get_closest_to_mouse();
				if (new_target.instance.address)
					silentaim::set_target(new_target);
				else
				{
					silentaim::state.data_ready = false;
					continue;
				}
			}
		}

		if (!silentaim::state.target.instance.address || !game::visualengine)
		{
			silentaim::state.data_ready = false;
			continue;
		}

		const auto fc   = frame_cache::get_for_thread();
		const math::matrix4& view = fc.view;
		const math::vector2& dims = fc.dims;

		POINT cursor{};
		GetCursorPos(&cursor);
		const math::vector2 mouse_pos{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

		math::vector3 target_pos;
		if (!get_target_position(silentaim::state.target, view, dims, mouse_pos, target_pos))
		{
			silentaim::state.data_ready = false;
			continue;
		}

		silentaim::state.target_3d_pos = target_pos;

		if (!game::visualengine->world_to_screen(view, dims, target_pos, silentaim::state.target_screen_pos))
		{
			silentaim::state.data_ready = false;
			continue;
		}

		HWND roblox_window = game::get_roblox_window();
		math::vector2 client_target_pos = silentaim::state.target_screen_pos;

		POINT client_cursor = cursor;
		if (roblox_window)
		{
			ScreenToClient(roblox_window, &client_cursor);

			RECT client_rect{};
			POINT client_pos{};
			if (GetClientRect(roblox_window, &client_rect))
			{
				client_pos.x = client_rect.left;
				client_pos.y = client_rect.top;
				ClientToScreen(roblox_window, &client_pos);
				client_target_pos.x -= static_cast<float>(client_pos.x);
				client_target_pos.y -= static_cast<float>(client_pos.y);
			}
		}

		silentaim::state.spoof_pos_x = static_cast<std::uint64_t>(client_cursor.x);
		silentaim::state.spoof_pos_y = static_cast<std::uint64_t>(dims.y - std::abs(dims.y - static_cast<float>(client_cursor.y)) - 58);
		silentaim::state.target_screen_pos = client_target_pos;
		silentaim::state.data_ready = true;
		settings::performance::target_loop_count++;
		std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::target_thread_sleep));
	}
}

static void aim_application_thread()
{
	using namespace std::chrono_literals;

	std::uint64_t mouse_service = 0;
	rbx::c_silent_help mouse_helper{};
	bool mouse_initialized = false;

	std::uint64_t last_spoof_pos_x = 0;
	std::uint64_t last_spoof_pos_y = 0;
	math::vector2 last_target_pos{};
	auto last_valid_target_time = std::chrono::steady_clock::now();
	bool had_valid_target = false;

	for (;;)
	{
		const bool active = is_silent_aim_active();
		const bool has_target = silentaim::state.data_ready && silentaim::state.target.instance.address != 0;
		const auto current_time = std::chrono::steady_clock::now();

		if (!active)
		{
			mouse_service = 0;
			mouse_initialized = false;
			had_valid_target = false;
			std::this_thread::sleep_for(100ms);
			continue;
		}

		if (!game::datamodel)
		{
			mouse_service = 0;
			mouse_initialized = false;
			std::this_thread::sleep_for(10ms);
			continue;
		}

		if (has_target)
		{
			had_valid_target = true;
			last_valid_target_time = current_time;
			last_spoof_pos_x = silentaim::state.spoof_pos_x;
			last_spoof_pos_y = silentaim::state.spoof_pos_y;
			last_target_pos = silentaim::state.target_screen_pos;
		}
		else if (had_valid_target)
		{
			auto time_since = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_valid_target_time);
			if (time_since.count() > 1)
			{
				had_valid_target = false;
				std::this_thread::sleep_for(8ms);
				continue;
			}
		}
		else
		{
			std::this_thread::sleep_for(8ms);
			continue;
		}

		math::vector2 target_pos = has_target ? silentaim::state.target_screen_pos : last_target_pos;
		if (target_pos.x < 0.0f || target_pos.y < 0.0f || target_pos.x > 10000.0f || target_pos.y > 10000.0f)
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}

		try
		{
			if (settings::silentaim::method == 1)
			{
				if (!mouse_service)
				{
					rbx::c_instance mouse_service_instance = game::datamodel->find_first_child("MouseService");
					mouse_service = mouse_service_instance.address;
					if (!mouse_service)
					{
						std::this_thread::sleep_for(10ms);
						continue;
					}
				}

				if (!mouse_initialized)
				{
					mouse_helper.initialize_mouse_service(mouse_service);
					mouse_initialized = true;
				}

				float mouse_y = has_target ? target_pos.y : target_pos.y + 58.0f;
				mouse_helper.write_mouse_position(mouse_service, target_pos.x, mouse_y);
			}
		}
		catch (...)
		{
			mouse_service = 0;
			mouse_initialized = false;
			std::this_thread::sleep_for(10ms);
		}
		settings::performance::aim_loop_count++;
		std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::aim_thread_sleep));
	}
}

void silentaim::run()
{
	std::thread(targeting_thread).detach();
	std::thread(aim_application_thread).detach();
}
