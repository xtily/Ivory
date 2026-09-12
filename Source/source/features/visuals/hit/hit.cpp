#include "hit.h"
#include <features/system/settings/settings.h>
#include <sdk/game/game.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <core/memory/memory.h>
#include <algorithm>
#include <cmath>
#include <deque>

namespace hitvisuals
{
	static std::mutex g_records_mtx;
	static std::deque<HitRecord> g_records;

	void clear()
	{
		std::lock_guard<std::mutex> lock(g_records_mtx);
		g_records.clear();
	}

	void on_hit(const cache::entity_t& target, const math::vector3& local_pos, const math::vector3& hit_pos, float damage, bool is_kill)
	{
		HitRecord rec{};
		rec.spawn_time = std::chrono::steady_clock::now();
		rec.local_pos = local_pos;
		rec.hit_pos = hit_pos;
		rec.damage = damage;
		rec.is_kill = is_kill;
		rec.is_r15 = (target.parts.count("UpperTorso") > 0 && target.parts.count("LowerTorso") > 0);
		rec.target_name = target.display_name.empty() ? target.name : target.display_name;

		static const std::vector<std::string> all_parts = {
			"Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart",
			"LeftArm", "RightArm", "LeftLeg", "RightLeg",
			"LeftUpperArm", "LeftLowerArm", "LeftHand",
			"RightUpperArm", "RightLowerArm", "RightHand",
			"LeftUpperLeg", "LeftLowerLeg", "LeftFoot",
			"RightUpperLeg", "RightLowerLeg", "RightFoot"
		};

		rec.limbs.reserve(all_parts.size());
		for (const auto& pname : all_parts)
		{
			HitLimbSnapshot limb{};
			limb.name = pname;

			auto it = target.parts.find(pname);
			if (it != target.parts.end() && it->second.address != 0)
			{
				rbx::c_primitive prim = it->second.get_primitive();
				if (prim.address != 0)
				{
					limb.world_pos = prim.get_position();
					limb.world_rot = prim.get_rotation();
					limb.size = prim.get_size();
					if (limb.world_pos.x != 0.0f || limb.world_pos.y != 0.0f || limb.world_pos.z != 0.0f)
					{
						limb.valid = true;
						rec.limbs.push_back(limb);
						continue;
					}
				}
			}

			math::vector3 fallback_pos{};
			if (bodyparts::get_part_position(target, pname, fallback_pos))
			{
				limb.world_pos = fallback_pos;
				limb.world_rot = target.rotation.rotation;
				limb.size = (pname == "Head") ? math::vector3(1.25f, 1.25f, 1.25f) : math::vector3(1.0f, 2.0f, 1.0f);
				limb.valid = (fallback_pos.x != 0.0f || fallback_pos.y != 0.0f || fallback_pos.z != 0.0f);
				if (limb.valid)
				{
					rec.limbs.push_back(limb);
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(g_records_mtx);
			if (g_records.size() > 32)
			{
				g_records.pop_front();
			}
			g_records.push_back(std::move(rec));
		}
	}

	static ImU32 apply_alpha(const float col[4], float alpha_scale)
	{
		int r = (int)(col[0] * 255.0f);
		int g = (int)(col[1] * 255.0f);
		int b = (int)(col[2] * 255.0f);
		int a = (int)(col[3] * alpha_scale * 255.0f);
		r = (std::clamp)(r, 0, 255);
		g = (std::clamp)(g, 0, 255);
		b = (std::clamp)(b, 0, 255);
		a = (std::clamp)(a, 0, 255);
		return IM_COL32(r, g, b, a);
	}

	static ImU32 get_rainbow_col(float time_val, float alpha)
	{
		float r = std::sin(time_val) * 0.5f + 0.5f;
		float g = std::sin(time_val + 2.094f) * 0.5f + 0.5f;
		float b = std::sin(time_val + 4.188f) * 0.5f + 0.5f;
		int a_int = (std::clamp)((int)(alpha * 255.0f), 0, 255);
		return IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), a_int);
	}

	static void render_hit_tracer(
		ImDrawList* draw_list,
		const HitRecord& rec,
		float progress,
		const math::matrix4& view,
		const math::vector2& dims)
	{
		if (!settings::hit_tracers::enabled) return;

		math::vector2 hit_scr{};
		if (!game::visualengine->world_to_screen(view, dims, rec.hit_pos, hit_scr))
			return;

		ImVec2 end_pt(hit_scr.x, hit_scr.y);
		ImVec2 start_pt(dims.x * 0.5f, dims.y * 0.5f);

		if (settings::hit_tracers::origin_type == 1) // Bottom Center
		{
			start_pt = ImVec2(dims.x * 0.5f, dims.y);
		}
		else if (settings::hit_tracers::origin_type == 2) // Local Player 3D
		{
			math::vector2 loc_scr{};
			if (game::visualengine->world_to_screen(view, dims, rec.local_pos, loc_scr))
			{
				start_pt = ImVec2(loc_scr.x, loc_scr.y);
			}
		}

		float th = settings::hit_tracers::thickness;
		int style = settings::hit_tracers::style;
		float base_alpha = progress;

		ImU32 col = apply_alpha(settings::hit_tracers::colour, base_alpha);
		ImU32 outline_col = apply_alpha(settings::hit_tracers::outline_colour, base_alpha);

		if (settings::hit_tracers::draw_outline)
		{
			draw_list->AddLine(start_pt, end_pt, outline_col, th + 2.0f);
		}

		if (style == 0) // Solid Beam
		{
			draw_list->AddLine(start_pt, end_pt, col, th);
		}
		else if (style == 1) // Fadeout
		{
			draw_list->AddLine(start_pt, end_pt, col, th);
		}
		else if (style == 2) // Neon Glow (Multi-pass glow)
		{
			ImU32 glow_col1 = apply_alpha(settings::hit_tracers::colour, base_alpha * 0.30f);
			ImU32 glow_col2 = apply_alpha(settings::hit_tracers::colour, base_alpha * 0.60f);
			draw_list->AddLine(start_pt, end_pt, glow_col1, th * 3.5f);
			draw_list->AddLine(start_pt, end_pt, glow_col2, th * 2.0f);
			draw_list->AddLine(start_pt, end_pt, IM_COL32(255, 255, 255, (int)(base_alpha * 240.f)), th * 0.8f);
		}
		else if (style == 3) // Laser Beam
		{
			ImU32 laser_glow = apply_alpha(settings::hit_tracers::colour, base_alpha * 0.50f);
			draw_list->AddLine(start_pt, end_pt, laser_glow, th * 2.5f);
			draw_list->AddLine(start_pt, end_pt, IM_COL32(255, 255, 255, (int)(base_alpha * 255.f)), th * 1.0f);
		}
		else if (style == 4) // Gradient Pulse
		{
			float time_now = (float)ImGui::GetTime() * 8.0f;
			const int segments = 16;
			for (int i = 0; i < segments; ++i)
			{
				float t0 = (float)i / (float)segments;
				float t1 = (float)(i + 1) / (float)segments;
				ImVec2 p0(start_pt.x + (end_pt.x - start_pt.x) * t0, start_pt.y + (end_pt.y - start_pt.y) * t0);
				ImVec2 p1(start_pt.x + (end_pt.x - start_pt.x) * t1, start_pt.y + (end_pt.y - start_pt.y) * t1);

				float pulse = std::sin(t0 * 6.283f - time_now) * 0.5f + 0.5f;
				ImU32 seg_col = apply_alpha(settings::hit_tracers::colour, base_alpha * (0.4f + 0.6f * pulse));
				draw_list->AddLine(p0, p1, seg_col, th * (0.8f + 0.4f * pulse));
			}
		}
		else if (style == 5) // Segmented / Dotted
		{
			const int seg_count = 12;
			for (int i = 0; i < seg_count; i += 2)
			{
				float t0 = (float)i / (float)seg_count;
				float t1 = (float)(i + 1) / (float)seg_count;
				ImVec2 p0(start_pt.x + (end_pt.x - start_pt.x) * t0, start_pt.y + (end_pt.y - start_pt.y) * t0);
				ImVec2 p1(start_pt.x + (end_pt.x - start_pt.x) * t1, start_pt.y + (end_pt.y - start_pt.y) * t1);
				draw_list->AddLine(p0, p1, col, th);
			}
		}

		// Floating Damage Text
		if (settings::hit_tracers::damage_text && rec.damage > 0.0f)
		{
			float float_up = (1.0f - progress) * 35.0f;
			ImVec2 txt_pt(end_pt.x, end_pt.y - 15.0f - float_up);

			std::string dmg_str = "-" + std::to_string((int)std::round(rec.damage));
			if (rec.is_kill) dmg_str += " [KILL]";

			ImVec2 txt_size = ImGui::CalcTextSize(dmg_str.c_str());
			ImVec2 centered_pt(txt_pt.x - txt_size.x * 0.5f, txt_pt.y - txt_size.y * 0.5f);

			ImU32 txt_col = apply_alpha(settings::hit_tracers::damage_text_colour, base_alpha);
			ImU32 shadow_col = IM_COL32(0, 0, 0, (int)(base_alpha * 220.f));

			draw_list->AddText(ImVec2(centered_pt.x + 1, centered_pt.y + 1), shadow_col, dmg_str.c_str());
			draw_list->AddText(centered_pt, txt_col, dmg_str.c_str());
		}
	}

	static void render_hit_chams(
		ImDrawList* draw_list,
		const HitRecord& rec,
		float progress,
		const math::matrix4& view,
		const math::vector2& dims)
	{
		if (!settings::hit_chams::enabled) return;

		float alpha_scale = progress;
		int easing = settings::hit_chams::fade_easing;
		if (easing == 1) // Smooth Exponential
		{
			alpha_scale = progress * progress;
		}
		else if (easing == 2) // Pulse Fade
		{
			float pulse = std::sin((float)ImGui::GetTime() * 12.0f) * 0.2f + 0.8f;
			alpha_scale = progress * pulse;
		}
		else if (easing == 3) // Shrink & Fade
		{
			alpha_scale = progress;
		}

		float scale_mod = settings::hit_chams::scale;
		if (easing == 3)
		{
			scale_mod *= (0.3f + 0.7f * progress);
		}

		int style = settings::hit_chams::style;
		float time_now = (float)ImGui::GetTime() * 3.0f;

		for (const auto& limb : rec.limbs)
		{
			if (!limb.valid) continue;

			math::vector3 half_sz = limb.size * 0.5f * scale_mod;
			math::vector3 ax(limb.world_rot.m[0][0], limb.world_rot.m[0][1], limb.world_rot.m[0][2]);
			math::vector3 ay(limb.world_rot.m[1][0], limb.world_rot.m[1][1], limb.world_rot.m[1][2]);
			math::vector3 az(limb.world_rot.m[2][0], limb.world_rot.m[2][1], limb.world_rot.m[2][2]);

			math::vector3 corners[8] = {
				limb.world_pos - ax * half_sz.x - ay * half_sz.y - az * half_sz.z,
				limb.world_pos + ax * half_sz.x - ay * half_sz.y - az * half_sz.z,
				limb.world_pos + ax * half_sz.x + ay * half_sz.y - az * half_sz.z,
				limb.world_pos - ax * half_sz.x + ay * half_sz.y - az * half_sz.z,
				limb.world_pos - ax * half_sz.x - ay * half_sz.y + az * half_sz.z,
				limb.world_pos + ax * half_sz.x - ay * half_sz.y + az * half_sz.z,
				limb.world_pos + ax * half_sz.x + ay * half_sz.y + az * half_sz.z,
				limb.world_pos - ax * half_sz.x + ay * half_sz.y + az * half_sz.z
			};

			ImVec2 scr[8];
			bool all_visible = true;
			for (int i = 0; i < 8; ++i)
			{
				math::vector2 pt{};
				if (!game::visualengine->world_to_screen(view, dims, corners[i], pt))
				{
					all_visible = false;
					break;
				}
				scr[i] = ImVec2(pt.x, pt.y);
			}

			if (!all_visible) continue;

			ImU32 fill_col = apply_alpha(settings::hit_chams::fill_colour, alpha_scale);
			ImU32 outline_col = apply_alpha(settings::hit_chams::outline_colour, alpha_scale);

			if (style == 4) // Rainbow Shimmer
			{
				fill_col = get_rainbow_col(time_now + limb.world_pos.y * 0.5f, alpha_scale * settings::hit_chams::fill_colour[3]);
				outline_col = get_rainbow_col(time_now + limb.world_pos.y * 0.5f + 1.0f, alpha_scale * settings::hit_chams::outline_colour[3]);
			}
			else if (style == 5) // Pulse Glow
			{
				float pulse = std::sin(time_now * 4.0f) * 0.3f + 0.7f;
				fill_col = apply_alpha(settings::hit_chams::fill_colour, alpha_scale * pulse);
				outline_col = apply_alpha(settings::hit_chams::outline_colour, alpha_scale * pulse);
			}

			static const int faces[6][4] = {
				{ 0, 1, 2, 3 }, // Front
				{ 5, 4, 7, 6 }, // Back
				{ 4, 0, 3, 7 }, // Left
				{ 1, 5, 6, 2 }, // Right
				{ 3, 2, 6, 7 }, // Top
				{ 4, 5, 1, 0 }  // Bottom
			};

			// Draw faces if not wireframe
			if (style != 1)
			{
				for (int f = 0; f < 6; ++f)
				{
					draw_list->AddQuadFilled(
						scr[faces[f][0]],
						scr[faces[f][1]],
						scr[faces[f][2]],
						scr[faces[f][3]],
						fill_col
					);
				}

				if (style == 3) // Hologram Scanlines
				{
					ImU32 scan_col = apply_alpha(settings::hit_chams::outline_colour, alpha_scale * 0.40f);
					for (int step = 1; step < 4; ++step)
					{
						float t = (float)step / 4.0f;
						ImVec2 p0(scr[0].x + (scr[3].x - scr[0].x) * t, scr[0].y + (scr[3].y - scr[0].y) * t);
						ImVec2 p1(scr[1].x + (scr[2].x - scr[1].x) * t, scr[1].y + (scr[2].y - scr[1].y) * t);
						draw_list->AddLine(p0, p1, scan_col, 1.0f);
					}
				}
			}

			// Draw 12 Edges
			if (settings::hit_chams::draw_outline || style == 1)
			{
				float th = settings::hit_chams::outline_thickness;
				static const int edges[12][2] = {
					{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
					{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
					{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
				};

				for (int e = 0; e < 12; ++e)
				{
					draw_list->AddLine(scr[edges[e][0]], scr[edges[e][1]], outline_col, th);
				}
			}
		}
	}

	static void render_hit_skeleton(
		ImDrawList* draw_list,
		const HitRecord& rec,
		float progress,
		const math::matrix4& view,
		const math::vector2& dims)
	{
		if (!settings::hit_skeleton::enabled) return;

		float alpha_scale = progress;
		if (settings::hit_skeleton::fade_easing == 1) // Smooth
		{
			alpha_scale = progress * progress;
		}
		else if (settings::hit_skeleton::fade_easing == 2) // Dissolve
		{
			alpha_scale = (progress > 0.3f) ? (progress - 0.3f) / 0.7f : 0.0f;
		}

		auto find_limb = [&](const std::string& name) -> const HitLimbSnapshot*
		{
			for (const auto& l : rec.limbs)
			{
				if (l.valid && l.name == name) return &l;
			}
			return nullptr;
		};

		auto project_limb = [&](const std::string& name, const math::vector3& offset = math::vector3()) -> std::optional<ImVec2>
		{
			const auto* l = find_limb(name);
			if (!l) return std::nullopt;
			math::vector2 scr{};
			math::vector3 world_p = l->world_pos + (l->world_rot * offset);
			if (!game::visualengine->world_to_screen(view, dims, world_p, scr)) return std::nullopt;
			return ImVec2(scr.x, scr.y);
		};

		float th = settings::hit_skeleton::thickness;
		int style = settings::hit_skeleton::style;
		float time_now = (float)ImGui::GetTime() * 4.0f;

		ImU32 sk_col = apply_alpha(settings::hit_skeleton::colour, alpha_scale);
		ImU32 outline_col = apply_alpha(settings::hit_skeleton::outline_colour, alpha_scale);

		if (style == 3) // Rainbow
		{
			sk_col = get_rainbow_col(time_now, alpha_scale * settings::hit_skeleton::colour[3]);
		}
		else if (style == 2) // Pulse
		{
			float pulse = std::sin(time_now * 3.0f) * 0.3f + 0.7f;
			sk_col = apply_alpha(settings::hit_skeleton::colour, alpha_scale * pulse);
		}

		auto add_bone = [&](const std::optional<ImVec2>& from, const std::optional<ImVec2>& to)
		{
			if (!from || !to) return;

			if (settings::hit_skeleton::draw_outline)
			{
				draw_list->AddLine(*from, *to, outline_col, th + 2.0f);
			}

			if (style == 1) // Neon Glow
			{
				ImU32 glow_col = apply_alpha(settings::hit_skeleton::colour, alpha_scale * 0.35f);
				draw_list->AddLine(*from, *to, glow_col, th * 2.8f);
				draw_list->AddLine(*from, *to, sk_col, th);
			}
			else
			{
				draw_list->AddLine(*from, *to, sk_col, th);
			}
		};

		std::vector<std::optional<ImVec2>> joint_nodes;

		if (rec.is_r15)
		{
			const auto head = project_limb("Head");
			const auto upper_torso = project_limb("UpperTorso", math::vector3(0.0f, 0.2f, 0.0f));
			const auto lower_torso = project_limb("LowerTorso");

			const auto left_upper_arm = project_limb("LeftUpperArm");
			const auto left_lower_arm = project_limb("LeftLowerArm");
			const auto left_hand = project_limb("LeftHand");

			const auto right_upper_arm = project_limb("RightUpperArm");
			const auto right_lower_arm = project_limb("RightLowerArm");
			const auto right_hand = project_limb("RightHand");

			const auto left_upper_leg = project_limb("LeftUpperLeg");
			const auto left_lower_leg = project_limb("LeftLowerLeg");
			const auto left_foot = project_limb("LeftFoot");

			const auto right_upper_leg = project_limb("RightUpperLeg");
			const auto right_lower_leg = project_limb("RightLowerLeg");
			const auto right_foot = project_limb("RightFoot");

			add_bone(head, upper_torso);
			add_bone(upper_torso, lower_torso);

			add_bone(upper_torso, left_upper_arm);
			add_bone(left_upper_arm, left_lower_arm);
			add_bone(left_lower_arm, left_hand);

			add_bone(upper_torso, right_upper_arm);
			add_bone(right_upper_arm, right_lower_arm);
			add_bone(right_lower_arm, right_hand);

			add_bone(lower_torso, left_upper_leg);
			add_bone(left_upper_leg, left_lower_leg);
			add_bone(left_lower_leg, left_foot);

			add_bone(lower_torso, right_upper_leg);
			add_bone(right_upper_leg, right_lower_leg);
			add_bone(right_lower_leg, right_foot);

			joint_nodes = { head, upper_torso, lower_torso, left_upper_arm, left_lower_arm, left_hand,
							right_upper_arm, right_lower_arm, right_hand, left_upper_leg, left_lower_leg, left_foot,
							right_upper_leg, right_lower_leg, right_foot };
		}
		else // R6
		{
			const auto head = project_limb("Head");
			const auto torso = project_limb("Torso");
			const auto torso_top = project_limb("Torso", math::vector3(0.0f, 0.5f, 0.0f));
			const auto torso_bottom = project_limb("Torso", math::vector3(0.0f, -0.5f, 0.0f));

			const auto left_arm = project_limb("LeftArm");
			const auto right_arm = project_limb("RightArm");
			const auto left_leg = project_limb("LeftLeg");
			const auto right_leg = project_limb("RightLeg");

			add_bone(head, torso_top ? torso_top : torso);
			add_bone(torso_top ? torso_top : torso, torso_bottom ? torso_bottom : torso);

			add_bone(torso_top ? torso_top : torso, left_arm);
			add_bone(torso_top ? torso_top : torso, right_arm);
			add_bone(torso_bottom ? torso_bottom : torso, left_leg);
			add_bone(torso_bottom ? torso_bottom : torso, right_leg);

			joint_nodes = { head, torso, left_arm, right_arm, left_leg, right_leg };
		}

		// Draw Glowing Joint Dots
		if (settings::hit_skeleton::draw_joints && settings::hit_skeleton::joint_radius > 0.1f)
		{
			float jr = settings::hit_skeleton::joint_radius;
			ImU32 j_col = apply_alpha(settings::hit_skeleton::joint_colour, alpha_scale);
			ImU32 j_glow = apply_alpha(settings::hit_skeleton::joint_colour, alpha_scale * 0.40f);

			for (const auto& node : joint_nodes)
			{
				if (!node) continue;
				draw_list->AddCircleFilled(*node, jr * 1.5f, j_glow, 12);
				draw_list->AddCircleFilled(*node, jr, j_col, 12);
				if (settings::hit_skeleton::draw_outline)
				{
					draw_list->AddCircle(*node, jr, outline_col, 12, 1.0f);
				}
			}
		}
	}

	void render(ImDrawList* draw_list, const math::matrix4& view, const math::vector2& dims)
	{
		if (!settings::hit_tracers::enabled && !settings::hit_chams::enabled && !settings::hit_skeleton::enabled)
			return;

		if (!draw_list) return;

		auto now = std::chrono::steady_clock::now();
		std::vector<HitRecord> active_records;

		{
			std::lock_guard<std::mutex> lock(g_records_mtx);
			for (auto it = g_records.begin(); it != g_records.end();)
			{
				float max_dur = (std::max)({
					settings::hit_tracers::enabled ? settings::hit_tracers::duration : 0.0f,
					settings::hit_chams::enabled ? settings::hit_chams::duration : 0.0f,
					settings::hit_skeleton::enabled ? settings::hit_skeleton::duration : 0.0f,
					0.5f
				});

				float elapsed = std::chrono::duration<float>(now - it->spawn_time).count();
				if (elapsed >= max_dur)
				{
					it = g_records.erase(it);
				}
				else
				{
					active_records.push_back(*it);
					++it;
				}
			}
		}

		for (const auto& rec : active_records)
		{
			float elapsed = std::chrono::duration<float>(now - rec.spawn_time).count();

			// 1. Hit Chams
			if (settings::hit_chams::enabled)
			{
				float chams_dur = (std::max)(0.1f, settings::hit_chams::duration);
				if (elapsed < chams_dur)
				{
					float p = 1.0f - (elapsed / chams_dur);
					render_hit_chams(draw_list, rec, p, view, dims);
				}
			}

			// 2. Hit Skeleton
			if (settings::hit_skeleton::enabled)
			{
				float skel_dur = (std::max)(0.1f, settings::hit_skeleton::duration);
				if (elapsed < skel_dur)
				{
					float p = 1.0f - (elapsed / skel_dur);
					render_hit_skeleton(draw_list, rec, p, view, dims);
				}
			}

			// 3. Hit Tracer
			if (settings::hit_tracers::enabled)
			{
				float tracer_dur = (std::max)(0.1f, settings::hit_tracers::duration);
				if (elapsed < tracer_dur)
				{
					float p = 1.0f - (elapsed / tracer_dur);
					render_hit_tracer(draw_list, rec, p, view, dims);
				}
			}
		}
	}
}
