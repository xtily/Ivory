#include "trigger.h"
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <sdk/game/game.h>
#include <sdk/math/math.h>
#include <sdk/offsets/offsets.h>
#include <sdk/wallcheck/wallcheck.h>
#include <core/memory/memory.h>
#include <features/system/settings/settings.h>
#include <features/system/keybind/keybind.h>
#include <features/system/playerlist/playerlist.h>
#include <windows.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <algorithm>

namespace triggerbot
{
	struct Ray3D
	{
		math::vector3 origin;
		math::vector3 dir;
	};

	static bool ray_intersects_obb(
		const Ray3D& ray,
		const math::vector3& center,
		const math::vector3& size,
		const math::matrix3& rot,
		float max_dist,
		float& out_t)
	{
		math::vector3 half_size = size * 0.5f;
		math::vector3 p = center - ray.origin;

		float tmin = 0.0f;
		float tmax = max_dist;

		math::vector3 axes[3] = {
			math::vector3(rot.m[0][0], rot.m[0][1], rot.m[0][2]),
			math::vector3(rot.m[1][0], rot.m[1][1], rot.m[1][2]),
			math::vector3(rot.m[2][0], rot.m[2][1], rot.m[2][2])
		};

		for (int i = 0; i < 3; ++i)
		{
			float e = axes[i].dot(p);
			float f = axes[i].dot(ray.dir);

			const float EPS = 1e-6f;
			if (std::fabs(f) > EPS)
			{
				float t1 = (e - half_size[i]) / f;
				float t2 = (e + half_size[i]) / f;
				if (t1 > t2) std::swap(t1, t2);

				tmin = (std::max)(tmin, t1);
				tmax = (std::min)(tmax, t2);

				if (tmax < tmin) return false;
			}
			else
			{
				if ((-e - half_size[i] > 0.0f) || (-e + half_size[i] < 0.0f))
					return false;
			}
		}

		if (tmax < 0.0f) return false;
		out_t = (tmin >= 0.0f) ? tmin : tmax;
		return true;
	}

	static bool is_gun_equipped(const cache::entity_t& local_player)
	{
		if (local_player.instance.address == 0)
			return false;

		std::uint64_t character_addr = local_player.instance.address;
		rbx::c_instance character(character_addr);

		for (const auto& child : character.get_children<rbx::c_instance>())
		{
			if (!child.address) continue;
			std::string cls = child.get_class_name();
			if (cls == "Tool")
			{
				return true;
			}
		}

		if (game::camera != 0)
		{
			rbx::c_instance cam(game::camera);
			for (const auto& c : cam.get_children<rbx::c_instance>())
			{
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

		return false;
	}

	void run()
	{
		while (true)
		{
			if (!settings::triggerbot::enabled)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}

			keybind::keybind_t kb{};
			kb.key = settings::triggerbot::keybind;
			kb.mode = static_cast<keybind::activation_mode>(settings::triggerbot::activation_mode);

			if (!keybind::is_active(kb))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				continue;
			}

			HWND roblox_window = game::get_roblox_window();
			HWND fg = GetForegroundWindow();
			if (roblox_window && fg != roblox_window)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			if (!game::visualengine || game::visualengine->address == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			const auto fc   = frame_cache::get_for_thread();
			const math::matrix4& view = fc.view;
			const math::vector2& dims = fc.dims;
			if (dims.x < 100.0f || dims.y < 100.0f)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			math::vector2 crosshair{ dims.x * 0.5f, dims.y * 0.5f };
			POINT pt{};
			if (roblox_window && GetCursorPos(&pt))
			{
				ScreenToClient(roblox_window, &pt);
				if (pt.x >= 0 && pt.x <= (int)dims.x && pt.y >= 0 && pt.y <= (int)dims.y)
				{
					crosshair = { (float)pt.x, (float)pt.y };
				}
			}

			cache::entity_t local_player{};
			{
				std::lock_guard<std::mutex> lock(cache::local_player_mtx);
				local_player = cache::local_player;
			}

			if (settings::triggerbot::guncheck && !is_gun_equipped(local_player))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			std::vector<cache::entity_t> entities;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				entities = cache::players;
			}

			bool should_shoot = false;
			int tpart = settings::triggerbot::target_part;

			if (settings::triggerbot::method == 1) // Raycast Method (3D World Raycast)
			{
				math::vector3 cam_pos{};
				math::matrix3 cam_rot = math::matrix3::identity();
				float cam_fov = 70.0f;
				if (game::camera != 0)
				{
					cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
					cam_rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);
					cam_fov = memory->read<float>(game::camera + Offsets::Camera::FieldOfView);
					if (cam_fov <= 1.0f || cam_fov > 170.0f) cam_fov = 70.0f;
				}

				if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
				{
					math::vector3 look_dir(-cam_rot.m[2][0], -cam_rot.m[2][1], -cam_rot.m[2][2]);
					math::vector3 right_dir(cam_rot.m[0][0], cam_rot.m[0][1], cam_rot.m[0][2]);
					math::vector3 up_dir(cam_rot.m[1][0], cam_rot.m[1][1], cam_rot.m[1][2]);

					look_dir = look_dir.normalized();
					right_dir = right_dir.normalized();
					up_dir = up_dir.normalized();

					float tan_fov = std::tanf((cam_fov * 3.14159265358979323846f / 180.0f) * 0.5f);
					float ndc_x = (crosshair.x - dims.x * 0.5f) / (dims.y * 0.5f);
					float ndc_y = (dims.y * 0.5f - crosshair.y) / (dims.y * 0.5f);

					math::vector3 ray_dir = (look_dir + right_dir * (ndc_x * tan_fov) + up_dir * (ndc_y * tan_fov)).normalized();
					Ray3D ray{ cam_pos, ray_dir };

					float padding = (std::max)(0.0f, (settings::triggerbot::threshold - 1.0f) * 0.05f);
					float max_ray_dist = 1000.0f;
					float closest_player_hit_t = max_ray_dist;
					bool hit_enemy = false;
					math::vector3 hit_point{};

					for (const auto& entity : entities)
					{
						if (entity.instance.address == 0 || entity.instance.address == local_player.instance.address)
							continue;
						if (entity.health <= 0.0f)
							continue;
						if (settings::triggerbot::teamcheck && local_player.team != 0 && entity.team == local_player.team)
							continue;
						if (settings::triggerbot::knock_check && entity.knocked)
							continue;
						if (playerlist::get_priority(entity.name) == cache::player_priority::friendly)
							continue;

						std::vector<std::string> part_names_to_check;
						if (tpart == 0) {
							part_names_to_check.push_back("Head");
						}
						else if (tpart == 1) {
							part_names_to_check.push_back("Torso");
							part_names_to_check.push_back("UpperTorso");
							part_names_to_check.push_back("LowerTorso");
						}
						else if (tpart == 2) {
							part_names_to_check.push_back("HumanoidRootPart");
						}
						else if (tpart == 3) {
							part_names_to_check.push_back("LeftArm");
							part_names_to_check.push_back("LeftUpperArm");
							part_names_to_check.push_back("LeftLowerArm");
							part_names_to_check.push_back("LeftHand");
							part_names_to_check.push_back("Left Arm");
						}
						else if (tpart == 4) {
							part_names_to_check.push_back("RightArm");
							part_names_to_check.push_back("RightUpperArm");
							part_names_to_check.push_back("RightLowerArm");
							part_names_to_check.push_back("RightHand");
							part_names_to_check.push_back("Right Arm");
						}
						else if (tpart == 5) {
							part_names_to_check.push_back("LeftLeg");
							part_names_to_check.push_back("LeftUpperLeg");
							part_names_to_check.push_back("LeftLowerLeg");
							part_names_to_check.push_back("LeftFoot");
							part_names_to_check.push_back("Left Leg");
						}
						else if (tpart == 6) {
							part_names_to_check.push_back("RightLeg");
							part_names_to_check.push_back("RightUpperLeg");
							part_names_to_check.push_back("RightLowerLeg");
							part_names_to_check.push_back("RightFoot");
							part_names_to_check.push_back("Right Leg");
						}
						else if (tpart == 7 || tpart == 8) { // Closest Part / Nearest
							part_names_to_check.push_back("Head");
							part_names_to_check.push_back("Torso");
							part_names_to_check.push_back("UpperTorso");
							part_names_to_check.push_back("LowerTorso");
							part_names_to_check.push_back("HumanoidRootPart");
							part_names_to_check.push_back("LeftArm");
							part_names_to_check.push_back("RightArm");
							part_names_to_check.push_back("LeftLeg");
							part_names_to_check.push_back("RightLeg");
						}
						else {
							part_names_to_check.push_back("Head");
						}

						for (const auto& pname : part_names_to_check)
						{
							auto it = entity.parts.find(pname);
							if (it == entity.parts.end())
							{
								if (pname == "Head" && entity.head_part.address)
								{
									rbx::c_primitive prim = entity.head_part.get_primitive();
									if (prim.address)
									{
										math::vector3 c = prim.get_position();
										math::vector3 sz = prim.get_size();
										if (sz.x > 0.01f && sz.y > 0.01f && sz.z > 0.01f)
										{
											math::matrix3 r = prim.get_rotation();
											math::vector3 padded_sz = sz + math::vector3(padding, padding, padding);
											float t_hit = 0.0f;
											if (ray_intersects_obb(ray, c, padded_sz, r, closest_player_hit_t, t_hit))
											{
												closest_player_hit_t = t_hit;
												hit_enemy = true;
												hit_point = ray.origin + ray.dir * t_hit;
											}
										}
									}
								}
								continue;
							}

							rbx::c_part part_inst = it->second;
							if (!part_inst.address) continue;
							rbx::c_primitive prim = part_inst.get_primitive();
							if (!prim.address) continue;

							math::vector3 c = prim.get_position();
							math::vector3 sz = prim.get_size();
							if (sz.x <= 0.01f || sz.y <= 0.01f || sz.z <= 0.01f) continue;
							math::matrix3 r = prim.get_rotation();

							math::vector3 padded_sz = sz + math::vector3(padding, padding, padding);
							float t_hit = 0.0f;
							if (ray_intersects_obb(ray, c, padded_sz, r, closest_player_hit_t, t_hit))
							{
								closest_player_hit_t = t_hit;
								hit_enemy = true;
								hit_point = ray.origin + ray.dir * t_hit;
							}
						}
					}

					if (hit_enemy)
					{
						should_shoot = true;
						if (settings::triggerbot::wallcheck && wallcheck)
						{
							if (!wallcheck->is_visible(cam_pos, hit_point))
							{
								should_shoot = false;
							}
						}
					}
				}
			}
			else // Normal Method (2D Crosshair Distance / Threshold)
			{
				float thresh = (std::max)(1.0f, settings::triggerbot::threshold);
				float thresh_sq = thresh * thresh;

				for (const auto& entity : entities)
				{
					if (entity.instance.address == 0 || entity.instance.address == local_player.instance.address)
						continue;
					if (entity.health <= 0.0f)
						continue;
					if (settings::triggerbot::teamcheck && local_player.team != 0 && entity.team == local_player.team)
						continue;
					if (settings::triggerbot::knock_check && entity.knocked)
						continue;
					if (playerlist::get_priority(entity.name) == cache::player_priority::friendly)
						continue;

					std::vector<math::vector3> candidate_positions;
					candidate_positions.reserve(8);

					if (tpart == 0) 
					{
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, "Head", p)) candidate_positions.push_back(p);
					}
					else if (tpart == 1) 
					{
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, "HumanoidRootPart", p)) candidate_positions.push_back(p);
					}
					else if (tpart == 2)
					{
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, "Torso", p)) candidate_positions.push_back(p);
					}
					else if (tpart == 3)
					{
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, "UpperTorso", p)) candidate_positions.push_back(p);
					}
					else if (tpart == 4)
					{
						static const std::vector<std::string> random_candidates = { "Head", "HumanoidRootPart", "Torso", "UpperTorso", "LeftArm", "RightArm" };
						int r = rand() % (int)random_candidates.size();
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, random_candidates[r], p)) candidate_positions.push_back(p);
						else if (bodyparts::get_part_position(entity, "Head", p)) candidate_positions.push_back(p);
					}
					else
					{
						math::vector3 p{};
						if (bodyparts::get_part_position(entity, "Head", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "Torso", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "UpperTorso", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "HumanoidRootPart", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "LeftArm", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "RightArm", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "LeftLeg", p)) candidate_positions.push_back(p);
						if (bodyparts::get_part_position(entity, "RightLeg", p)) candidate_positions.push_back(p);
					}

					for (const auto& wpos : candidate_positions)
					{
						math::vector2 scr{};
						if (!game::visualengine->world_to_screen(view, dims, wpos, scr))
							continue;

						float dx = scr.x - crosshair.x;
						float dy = scr.y - crosshair.y;
						if ((dx * dx + dy * dy) <= thresh_sq)
						{
							if (settings::triggerbot::wallcheck && wallcheck)
							{
								math::vector3 cam_pos{};
								if (game::camera != 0)
								{
									cam_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
								}
								if (cam_pos.x != 0.f || cam_pos.y != 0.f || cam_pos.z != 0.f)
								{
									if (!wallcheck->is_visible(cam_pos, wpos))
										continue;
								}
							}
							should_shoot = true;
							break;
						}
					}

					if (should_shoot) break;
				}
			}

			if (should_shoot)
			{
				if (settings::triggerbot::delay_ms > 0.0f)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds((int)settings::triggerbot::delay_ms));
				}

				INPUT inputs[2] = {};
				inputs[0].type = INPUT_MOUSE;
				inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
				inputs[1].type = INPUT_MOUSE;
				inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
				SendInput(2, inputs, sizeof(INPUT));

				int cd = (std::max)(5, (int)settings::triggerbot::cooldown_ms);
				std::this_thread::sleep_for(std::chrono::milliseconds(cd));
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}
}
