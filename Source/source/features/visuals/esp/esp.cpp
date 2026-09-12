#include "esp.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <clipper2/clipper.h>

#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <sdk/math/math.h>
#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>
#include <features/system/settings/settings.h>
#include <core/logger/logger.h>
#include <features/system/playerlist/playerlist.h>
#include <features/combat/aimbot/aimbot.h>
#include <features/combat/silent/mouse/mouse.h>
#include <features/combat/silent/raycast/raycast.h>
#include <features/system/keybind/keybind.h>
#include <features/exploits/utility/misc/misc.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <ui/render/render.h>
#include <features/visuals/chams/chams.h>

namespace esp
{
	struct footprint_t {
		math::vector3 position;
		float time;
		bool is_left;
		math::vector3 forward;
	};

	struct trail_point_t {
		math::vector3 pos;
		float time;
	};

	static std::unordered_map<uint64_t, float> s_anim_hp;
	static std::unordered_map<uint64_t, std::vector<trail_point_t>> s_trail_history;
	static std::unordered_map<uint64_t, std::vector<std::pair<math::vector3, float>>> s_sound_events;
	static std::unordered_map<uint64_t, math::vector3> s_last_positions;
	static std::unordered_map<uint64_t, float> s_last_step_time;
	static std::unordered_map<uint64_t, std::vector<footprint_t>> s_footprint_events;
	static std::unordered_map<uint64_t, math::vector3> s_fp_last_pos;
	static std::unordered_map<uint64_t, bool> s_fp_last_side;
	static std::unordered_map<uint64_t, float> s_fp_last_step;

	static inline bool is_limb_part_name(const std::string& name)
	{
		return name == "Head" ||
			name == "UpperTorso" ||
			name == "LowerTorso" ||
			name == "Torso" ||
			name == "LeftHand" ||
			name == "RightHand" ||
			name == "LeftFoot" ||
			name == "RightFoot" ||
			name == "Left Arm" ||
			name == "Right Arm" ||
			name == "Left Leg" ||
			name == "Right Leg" ||
			name == "LeftArm" ||
			name == "RightArm" ||
			name == "LeftLeg" ||
			name == "RightLeg" ||
			name == "LeftUpperArm" ||
			name == "RightUpperArm" ||
			name == "LeftLowerArm" ||
			name == "RightLowerArm" ||
			name == "LeftUpperLeg" ||
			name == "RightUpperLeg" ||
			name == "LeftLowerLeg" ||
			name == "RightLowerLeg" ||
			name == "pfLimbs1" ||
			name == "pfLimbs2" ||
			name == "pfLimbs3" ||
			name == "pfLimbs4" ||
			name == "pfLimbs5" ||
			name.rfind("pfLimb", 0) == 0;
	}

	static inline void generate_hull_path(const ImVec2* pts, int num_pts, Clipper2Lib::Path64& out_path)
	{
		out_path.clear();
		if (num_pts < 3) return;

		ImVec2 sorted_pts[8];
		int n = (std::min)(num_pts, 8);
		for (int i = 0; i < n; ++i) sorted_pts[i] = pts[i];
		std::sort(sorted_pts, sorted_pts + n, [](const ImVec2& a, const ImVec2& b) {
			return a.x < b.x || (a.x == b.x && a.y < b.y);
		});

		ImVec2 hull[16];
		int k = 0;
		auto cross = [](const ImVec2& O, const ImVec2& A, const ImVec2& B) -> float {
			return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
		};

		for (int i = 0; i < n; ++i) {
			while (k >= 2 && cross(hull[k - 2], hull[k - 1], sorted_pts[i]) <= 0.0f)
				k--;
			hull[k++] = sorted_pts[i];
		}

		for (int i = n - 2, t = k + 1; i >= 0; --i) {
			while (k >= t && cross(hull[k - 2], hull[k - 1], sorted_pts[i]) <= 0.0f)
				k--;
			hull[k++] = sorted_pts[i];
		}

		int hull_count = k - 1;
		if (hull_count < 3) return;

		out_path.reserve(hull_count);
		for (int i = 0; i < hull_count; ++i) {
			out_path.push_back({ static_cast<int64_t>(hull[i].x * 1000.0f), static_cast<int64_t>(hull[i].y * 1000.0f) });
		}
	}

	__forceinline std::vector<ImVec2> generate_hull(const ImVec2* pts, int num_pts)
	{
		if (num_pts < 3) return {};

		ImVec2 sorted_pts[16];
		int n = (std::min)(num_pts, 16);
		for (int i = 0; i < n; ++i) sorted_pts[i] = pts[i];
		std::sort(sorted_pts, sorted_pts + n, [](const ImVec2& a, const ImVec2& b) {
			return a.x < b.x || (a.x == b.x && a.y < b.y);
		});

		ImVec2 hull_stack[32];
		int k = 0;
		auto cross = [](const ImVec2& O, const ImVec2& A, const ImVec2& B) -> float {
			return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
		};

		for (int i = 0; i < n; ++i) {
			while (k >= 2 && cross(hull_stack[k - 2], hull_stack[k - 1], sorted_pts[i]) <= 0.0f)
				k--;
			hull_stack[k++] = sorted_pts[i];
		}

		for (int i = n - 2, t = k + 1; i >= 0; --i) {
			while (k >= t && cross(hull_stack[k - 2], hull_stack[k - 1], sorted_pts[i]) <= 0.0f)
				k--;
			hull_stack[k++] = sorted_pts[i];
		}

		int hull_count = k - 1;
		if (hull_count < 3) return {};

		return std::vector<ImVec2>(hull_stack, hull_stack + hull_count);
	}

	static void render_clipper_chams(
		ImDrawList* cham_draw,
		const std::vector<esp::projected_part_t>& projected_parts,
		ImU32 fill_col,
		ImU32 outline_col,
		float outline_thickness,
		float chams_thickness,
		bool enable_glow = false,
		ImU32 glow_col = 0,
		float glow_radius = 4.0f,
		float glow_intensity = 100.0f)
	{
		if (projected_parts.empty()) return;

		static thread_local Clipper2Lib::Paths64 s_all_parts;
		s_all_parts.clear();
		s_all_parts.reserve(projected_parts.size());

		Clipper2Lib::Path64 part_path;
		for (const auto& projected : projected_parts)
		{
			generate_hull_path(projected.points, projected.num_points, part_path);
			if (part_path.size() >= 3)
			{
				s_all_parts.push_back(part_path);
			}
		}

		if (s_all_parts.empty()) return;

		// Direct fast union on the small limb convex hulls (nearly instant)
		auto unified = Clipper2Lib::Union(s_all_parts, Clipper2Lib::FillRule::NonZero);
		if (unified.empty()) return;

		// 1. Render Glow if enabled
		if (enable_glow && glow_radius > 0.5f && (glow_col & IM_COL32_A_MASK) != 0)
		{
			int glow_layers = 5;
			float base_alpha = static_cast<float>((glow_col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
			base_alpha *= (glow_intensity / 100.0f);

			for (int layer = glow_layers; layer >= 1; --layer)
			{
				float layer_frac = static_cast<float>(layer) / static_cast<float>(glow_layers);
				float current_radius = glow_radius * layer_frac;
				double glow_delta = static_cast<double>(current_radius) * 1000.0;

				if (chams_thickness > 1.01f)
				{
					glow_delta += static_cast<double>(chams_thickness - 1.0f) * 1000.0 * 1.5;
				}

				auto glow_paths = Clipper2Lib::InflatePaths(unified, glow_delta, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0);
				if (glow_paths.empty()) continue;

				float alpha_falloff = (1.0f - layer_frac * 0.75f) * (base_alpha / static_cast<float>(glow_layers)) * 2.0f;
				alpha_falloff = std::clamp(alpha_falloff, 0.0f, 1.0f);
				int layer_a = static_cast<int>(alpha_falloff * 255.0f);
				if (layer_a <= 0) continue;

				ImU32 layer_color = (glow_col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(layer_a) << IM_COL32_A_SHIFT);

				static thread_local std::vector<ImVec2> s_glow_poly;
				for (const auto& gp : glow_paths)
				{
					if (gp.size() < 3) continue;
					if (std::abs(Clipper2Lib::Area(gp)) < 1.0) continue;

					s_glow_poly.clear();
					s_glow_poly.reserve(gp.size());
					for (const auto& pt : gp)
					{
						s_glow_poly.push_back(ImVec2(pt.x * 0.001f, pt.y * 0.001f));
					}
					cham_draw->AddPolyline(s_glow_poly.data(), static_cast<int>(s_glow_poly.size()), layer_color, ImDrawFlags_Closed, 2.0f + current_radius * 0.5f);
				}
			}
		}

		// If thickness > 1.01f, smoothly inflate the single merged silhouette
		Clipper2Lib::Paths64 final_paths;
		if (chams_thickness > 1.01f)
		{
			double inflate_delta = static_cast<double>(chams_thickness - 1.0f) * 1000.0 * 1.5;
			final_paths = Clipper2Lib::InflatePaths(unified, inflate_delta, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0);
		}
		else
		{
			final_paths = std::move(unified);
		}

		// Render each polygon directly via ImGui's built-in AddConcavePolyFilled & AddPolyline
		static thread_local std::vector<ImVec2> s_poly;
		for (const auto& sp : final_paths)
		{
			if (sp.size() < 3) continue;
			if (std::abs(Clipper2Lib::Area(sp)) < 1.0) continue;

			s_poly.clear();
			s_poly.reserve(sp.size());
			for (const auto& pt : sp)
			{
				s_poly.push_back(ImVec2(pt.x * 0.001f, pt.y * 0.001f));
			}

			if ((fill_col & IM_COL32_A_MASK) != 0)
			{
				cham_draw->AddConcavePolyFilled(s_poly.data(), static_cast<int>(s_poly.size()), fill_col);
			}

			if (outline_thickness > 0.0f && (outline_col & IM_COL32_A_MASK) != 0)
			{
				cham_draw->AddPolyline(s_poly.data(), static_cast<int>(s_poly.size()), outline_col, ImDrawFlags_Closed, outline_thickness);
			}
		}
	}

	static inline ImFont* get_esp_font(float& out_size)
	{
		int index = settings::visuals::esp_font_index;
		out_size = settings::visuals::esp_font_size;
		if (index == 0)
		{
			return esp_font_tahoma ? esp_font_tahoma : ImGui::GetFont();
		}
		else if (index == 1)
		{
			return esp_font_smallest_pixel ? esp_font_smallest_pixel : ImGui::GetFont();
		}
		else
		{
			return ImGui::GetFont();
		}
	}

	static math::vector3 local_corners[8] =
	{
		{ -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
		{ -1, -1, 1 }, { 1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }
	};

	static bool render_mesh_chams(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims,
		ImU32 fill_col, ImU32 outline_col, ImDrawList* draw)
	{
		if (!draw || entity.parts.empty()) return false;

		bool is_r15 = (entity.parts.count("UpperTorso") > 0 && entity.parts.count("LowerTorso") > 0);
		bool rendered_any = false;

		for (const auto& [part_name, part_inst] : entity.parts)
		{
			if (part_inst.address == 0) continue;
			rbx::c_primitive prim = part_inst.get_primitive();
			if (prim.address == 0) continue;

			math::vector3 part_pos = prim.get_position();
			math::matrix3 part_rot = prim.get_rotation();
			math::vector3 part_sz = prim.get_size();

			if (part_sz.x <= 0.001f || part_sz.y <= 0.001f || part_sz.z <= 0.001f) continue;

			uint64_t asset_id = assetmesh::get_mesh_asset_id_from_part(part_inst.address);
			if (asset_id == 0 && entity.user_id != 0)
			{
				assetmesh::request_avatar_assets(static_cast<int64_t>(entity.user_id));
				asset_id = assetmesh::get_mesh_asset_id_for_part(static_cast<int64_t>(entity.user_id), part_name.c_str(), is_r15);
			}
			if (asset_id == 0)
			{
				asset_id = assetmesh::get_default_asset_for_part(part_name.c_str(), is_r15);
			}

			if (asset_id != 0)
			{
				assetmesh::request_mesh(asset_id);
			}

			auto mesh = assetmesh::get_mesh_for_part(part_name.c_str(), is_r15, asset_id);
			if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) continue;

			const auto& b = mesh->bounds;
			float sx = b.size.x > 0.001f ? part_sz.x / b.size.x : 1.0f;
			float sy = b.size.y > 0.001f ? part_sz.y / b.size.y : 1.0f;
			float sz_scale = b.size.z > 0.001f ? part_sz.z / b.size.z : 1.0f;

			std::vector<ImVec2> screen_pts(mesh->vertices.size());
			std::vector<bool> valid_pts(mesh->vertices.size(), false);
			std::vector<math::vector3> world_pts(mesh->vertices.size());

			for (size_t vi = 0; vi < mesh->vertices.size(); ++vi)
			{
				const auto& v = mesh->vertices[vi].position;
				float lx = (v.x - b.center.x) * sx;
				float ly = (v.y - b.center.y) * sy;
				float lz = (v.z - b.center.z) * sz_scale;

				world_pts[vi] = math::vector3{
					part_rot.m[0][0] * lx + part_rot.m[0][1] * ly + part_rot.m[0][2] * lz + part_pos.x,
					part_rot.m[1][0] * lx + part_rot.m[1][1] * ly + part_rot.m[1][2] * lz + part_pos.y,
					part_rot.m[2][0] * lx + part_rot.m[2][1] * ly + part_rot.m[2][2] * lz + part_pos.z
				};

				math::vector2 scr{};
				if (game::visualengine->world_to_screen(view, dims, world_pts[vi], scr))
				{
					screen_pts[vi] = ImVec2(scr.x, scr.y);
					valid_pts[vi] = true;
				}
			}

			const size_t num_indices = mesh->indices.size();
			int drawn_part_tris = 0;
			for (size_t ii = 0; ii + 2 < num_indices; ii += 3)
			{
				uint32_t i0 = mesh->indices[ii];
				uint32_t i1 = mesh->indices[ii + 1];
				uint32_t i2 = mesh->indices[ii + 2];

				if (i0 >= valid_pts.size() || i1 >= valid_pts.size() || i2 >= valid_pts.size()) continue;
				if (!valid_pts[i0] || !valid_pts[i1] || !valid_pts[i2]) continue;

				const ImVec2& p0 = screen_pts[i0];
				const ImVec2& p1 = screen_pts[i1];
				const ImVec2& p2 = screen_pts[i2];

				// Calculate 3D face normal for directional lighting
				math::vector3 v10 = world_pts[i1] - world_pts[i0];
				math::vector3 v20 = world_pts[i2] - world_pts[i0];
				math::vector3 fn{
					v10.y * v20.z - v10.z * v20.y,
					v10.z * v20.x - v10.x * v20.z,
					v10.x * v20.y - v10.y * v20.x
				};
				float fn_len = std::sqrt(fn.x * fn.x + fn.y * fn.y + fn.z * fn.z);
				float light = 0.85f;
				if (fn_len > 0.0001f)
				{
					fn.x /= fn_len; fn.y /= fn_len; fn.z /= fn_len;
					float dot = std::abs(fn.x * 0.30f + fn.y * 0.80f + fn.z * 0.50f);
					light = 0.40f + 0.60f * dot;
				}

				int fr = (std::min)(255, static_cast<int>(((fill_col >> 0) & 0xFF) * light));
				int fg = (std::min)(255, static_cast<int>(((fill_col >> 8) & 0xFF) * light));
				int fb = (std::min)(255, static_cast<int>(((fill_col >> 16) & 0xFF) * light));
				int fa = static_cast<int>((fill_col >> 24) & 0xFF);
				ImU32 shaded_fill = IM_COL32(fr, fg, fb, fa);

				draw->AddTriangleFilled(p0, p1, p2, shaded_fill);

				if ((outline_col & 0xFF000000) != 0)
				{
					draw->AddTriangle(p0, p1, p2, outline_col, 1.0f);
				}
				drawn_part_tris++;
			}

			if (drawn_part_tris > 0)
				rendered_any = true;
		}

		return rendered_any;
	}
	__forceinline void outline(ImVec2& c1, ImVec2& c2, ImU32 col, float rounding = 0.f, bool draw_inline = false)
	{
		const ImVec2 rounded_position(std::round(c1.x), std::round(c1.y));
		const ImVec2 rounded_size(std::round(c2.x), std::round(c2.y));

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		ImVec2 rect_max = ImVec2(rounded_position.x + rounded_size.x, rounded_position.y + rounded_size.y);
		ImRect rectangle(rounded_position, rect_max);

		const float max_rounding = (std::min)(rounded_size.x, rounded_size.y) / 2.0f;
		rounding = (std::min)(rounding, max_rounding);

		if (settings::visuals::render_outlines[1]) // index 1 = Box
		{
			const int outline_alpha = static_cast<int>(((col >> 24) & 0xFF) * 0.5f);
			if (draw_inline)
			{
				draw->AddRect(rectangle.Min, rectangle.Max, IM_COL32(15, 15, 15, outline_alpha), rounding);
			}
			draw->AddRect(ImVec2(rectangle.Min.x - 2.0f, rectangle.Min.y - 2.0f), ImVec2(rectangle.Max.x + 2.0f, rectangle.Max.y + 2.0f), IM_COL32(15, 15, 15, outline_alpha), rounding);
		}
		draw->AddRect(ImVec2(rectangle.Min.x - 1.0f, rectangle.Min.y - 1.0f), ImVec2(rectangle.Max.x + 1.0f, rectangle.Max.y + 1.0f), col, rounding, 0, settings::visuals::box_thickness);
	}

	__forceinline void outline_gradient(ImVec2& c1, ImVec2& c2, ImU32 col_top, ImU32 col_mid, ImU32 col_low, int gradient_type, float pulse_speed, float transparency, float rounding = 0.f, bool draw_inline = false)
	{
		const ImVec2 rounded_position(std::round(c1.x), std::round(c1.y));
		const ImVec2 rounded_size(std::round(c2.x), std::round(c2.y));

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		ImVec2 rect_max = ImVec2(rounded_position.x + rounded_size.x, rounded_position.y + rounded_size.y);
		ImRect rectangle(rounded_position, rect_max);

		const float max_rounding = (std::min)(rounded_size.x, rounded_size.y) / 2.0f;
		rounding = (std::min)(rounding, max_rounding);

		if (settings::visuals::render_outlines[1])
		{
			const int outline_alpha = static_cast<int>(((col_top >> 24) & 0xFF) * 0.5f);
			if (draw_inline)
			{
				draw->AddRect(rectangle.Min, rectangle.Max, IM_COL32(15, 15, 15, outline_alpha), rounding);
			}
			draw->AddRect(ImVec2(rectangle.Min.x - 2.0f, rectangle.Min.y - 2.0f), ImVec2(rectangle.Max.x + 2.0f, rectangle.Max.y + 2.0f), IM_COL32(15, 15, 15, outline_alpha), rounding);
		}

		ImVec2 inner_min(rectangle.Min.x - 1.0f, rectangle.Min.y - 1.0f);
		ImVec2 inner_max(rectangle.Max.x + 1.0f, rectangle.Max.y + 1.0f);

		if (gradient_type == 0)
		{
			int vtx0 = draw->VtxBuffer.Size;
			draw->AddRect(inner_min, inner_max, col_top, rounding, 0, settings::visuals::box_thickness);
			int vtx1 = draw->VtxBuffer.Size;
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, vtx0, vtx1, inner_min, ImVec2(inner_min.x, inner_max.y), col_top, col_low);
		}
		else if (gradient_type == 1)
		{
			int vtx0 = draw->VtxBuffer.Size;
			draw->AddRect(inner_min, inner_max, col_top, rounding, 0, settings::visuals::box_thickness);
			int vtx1 = draw->VtxBuffer.Size;
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, vtx0, vtx1, inner_min, ImVec2(inner_max.x, inner_min.y), col_top, col_low);
		}
		else if (gradient_type == 2)
		{
			float time = static_cast<float>(ImGui::GetTime());
			float speed = (pulse_speed > 0.01f) ? pulse_speed : 2.0f;
			float phase = fmodf(time * speed, 2.0f);
			float t = phase < 1.0f ? phase : (2.0f - phase);
			float r, g, b, a;
			ImVec4 col_t = ImGui::ColorConvertU32ToFloat4(col_top);
			ImVec4 col_m = ImGui::ColorConvertU32ToFloat4(col_mid);
			ImVec4 col_l = ImGui::ColorConvertU32ToFloat4(col_low);

			if (t < 0.5f) {
				float factor = t * 2.0f;
				r = col_t.x * (1.f - factor) + col_m.x * factor;
				g = col_t.y * (1.f - factor) + col_m.y * factor;
				b = col_t.z * (1.f - factor) + col_m.z * factor;
				a = col_t.w * (1.f - factor) + col_m.w * factor;
			} else {
				float factor = (t - 0.5f) * 2.0f;
				r = col_m.x * (1.f - factor) + col_l.x * factor;
				g = col_m.y * (1.f - factor) + col_l.y * factor;
				b = col_m.z * (1.f - factor) + col_l.z * factor;
				a = col_m.w * (1.f - factor) + col_l.w * factor;
			}
			ImU32 pulse_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(a * 255.f));
			draw->AddRect(inner_min, inner_max, pulse_col, rounding, 0, settings::visuals::box_thickness);
		}
	}

	__forceinline void corner_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float thickness = 1.f, bool draw_inline = false)
	{
		if (!draw) return;

		const float min_x = std::round(min.x);
		const float min_y = std::round(min.y);
		const float max_x = std::round(max.x);
		const float max_y = std::round(max.y);

		const float width = max_x - min_x;
		const float height = max_y - min_y;
		if (width <= 0.0f || height <= 0.0f)
		{
			return;
		}

		const float stroke = (std::max)(1.0f, settings::visuals::box_thickness);
		const float min_side = (std::min)(width, height);
		const float len = std::clamp(min_side * 0.25f, stroke * 2.0f, min_side * 0.5f);

		const bool enable_outline = settings::visuals::render_outlines[1];
		const int outline_alpha = static_cast<int>(((col >> 24) & 0xFF) * 1.0f);
		const ImU32 outline_col = IM_COL32(0, 0, 0, outline_alpha);

		auto draw_bar = [&](float x0, float y0, float x1, float y1)
		{
			if (enable_outline)
			{
				if (draw_inline)
				{
					float ix0 = (x0 == min_x) ? x0 + stroke : x0;
					float ix1 = (x1 == max_x) ? x1 - stroke : x1;
					float iy0 = (y0 == min_y) ? y0 + stroke : y0;
					float iy1 = (y1 == max_y) ? y1 - stroke : y1;
					draw->AddRectFilled(ImVec2(ix0, iy0), ImVec2(ix1, iy1), outline_col);
				}
				draw->AddRectFilled(ImVec2(x0 - 1.0f, y0 - 1.0f), ImVec2(x1 + 1.0f, y1 + 1.0f), outline_col);
			}
			draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
		};

		// 1. Top-Left Corner
		draw_bar(min_x, min_y, min_x + len, min_y + stroke);
		draw_bar(min_x, min_y + stroke, min_x + stroke, min_y + len);

		// 2. Top-Right Corner
		draw_bar(max_x - len, min_y, max_x, min_y + stroke);
		draw_bar(max_x - stroke, min_y + stroke, max_x, min_y + len);

		// 3. Bottom-Left Corner
		draw_bar(min_x, max_y - stroke, min_x + len, max_y);
		draw_bar(min_x, max_y - len, min_x + stroke, max_y - stroke);

		// 4. Bottom-Right Corner
		draw_bar(max_x - len, max_y - stroke, max_x, max_y);
		draw_bar(max_x - stroke, max_y - len, max_x, max_y - stroke);
	}

	__forceinline void outlined_polyline(ImDrawList* draw, ImVec2* points, int count, ImU32 col, float thickness, ImU32 outline_col, float outline_thickness, bool use_outline, bool closed)
	{
		if (count < 2)
			return;

		if (use_outline && outline_thickness > 0.f)
		{
			draw->AddPolyline(points, count, outline_col, closed ? ImDrawFlags_Closed : 0, outline_thickness);
		}

		draw->AddPolyline(points, count, col, closed ? ImDrawFlags_Closed : 0, thickness);
	}

	__forceinline void draw_text_outlined(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr, bool use_outline = true)
	{
		pos.x = std::floor(pos.x);
		pos.y = std::floor(pos.y);

		if (use_outline)
		{
			ImU32 outline_col = IM_COL32(0, 0, 0, (col >> 24) & 0xFF);

			draw->AddText(font, font_size, ImVec2(pos.x - 1.0f, pos.y), outline_col, text_begin, text_end);
			draw->AddText(font, font_size, ImVec2(pos.x + 1.0f, pos.y), outline_col, text_begin, text_end);
			draw->AddText(font, font_size, ImVec2(pos.x, pos.y - 1.0f), outline_col, text_begin, text_end);
			draw->AddText(font, font_size, ImVec2(pos.x, pos.y + 1.0f), outline_col, text_begin, text_end);
		}

		draw->AddText(font, font_size, pos, col, text_begin, text_end);
	}

	static bool is_aimbot_keybind_active()
	{
		if (!settings::aimbot::enabled)
		{
			return false;
		}

		static keybind::keybind_t aimbot_kb{};
		aimbot_kb.key = settings::aimbot::keybind;
		aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

		return keybind::is_active(aimbot_kb);
	}

	static bool is_silentaim_keybind_active()
	{
		if (!settings::silentaim::enabled)
		{
			return false;
		}

		static keybind::keybind_t silentaim_kb{};
		silentaim_kb.key = settings::silentaim::keybind;
		silentaim_kb.mode = static_cast<keybind::activation_mode>(settings::silentaim::activation_mode);

		return keybind::is_active(silentaim_kb);
	}
}

void esp::run()
{
	try
	{
		if (!game::visualengine || game::visualengine->address == 0 || !game::datamodel || game::datamodel->address == 0)
		{
			return;
		}

		bool active = settings::visuals::enabled &&
			(settings::visuals::keybind == 0 || keybind::is_key_active(settings::visuals::keybind, settings::visuals::activation_mode, "visuals"));
		bool want_engine = (settings::visuals::hostile.chams && settings::visuals::hostile.chams_type == 2) ||
			(settings::visuals::friendly.chams && settings::visuals::friendly.chams_type == 2) ||
			(settings::visuals::neutral.chams && settings::visuals::neutral.chams_type == 2);
		settings::visuals::engine_chams = active && want_engine;

		if (!active)
		{
			return;
		}

		const auto     fc   = frame_cache::get_for_thread();
		const math::matrix4& view = fc.view;
		const math::vector2& dims = fc.dims;

	static math::vector3 local_corners[8] =
	{
		{ -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
		{ -1, -1, 1 }, { 1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }
	};

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	// Disable AA globally for all ESP draw calls — saves significant per-vertex cost
	const ImDrawListFlags saved_flags = draw->Flags;
	draw->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);

	// Use atomic snapshots — zero mutex, zero deep-copy of entity_t per frame.
	auto players_snap_ptr = cache::get_players_snap();
	auto local_snap_ptr   = cache::get_local_snap();
	if (!players_snap_ptr || players_snap_ptr->empty())
	{
		draw->Flags = saved_flags;
		return;
	}
	const std::vector<cache::entity_t>& players_snapshot = *players_snap_ptr;
	static cache::entity_t s_empty_local{};
	const cache::entity_t& local_snapshot = local_snap_ptr ? *local_snap_ptr : s_empty_local;

	math::vector3 local_root_pos{};
	bool has_local_root_pos = false;

	rbx::c_part local_hrp = local_snapshot.humanoid_root_part.address ? local_snapshot.humanoid_root_part : rbx::c_part{};
	if (!local_hrp.address)
	{
		auto local_root_it = local_snapshot.parts.find("HumanoidRootPart");
		if (local_root_it != local_snapshot.parts.end())
		{
			local_hrp = local_root_it->second;
		}
	}

	if (local_hrp.address)
	{
		local_root_pos = local_snapshot.position;
		has_local_root_pos = std::isfinite(local_root_pos.x);
	}

	HWND roblox_window = game::get_roblox_window();
	POINT roblox_screen_pt{ 0, 0 };
	if (roblox_window)
	{
		ClientToScreen(roblox_window, &roblox_screen_pt);
	}

	float tracer_screen_x = static_cast<float>(roblox_screen_pt.x) + dims.x * 0.5f;
	float tracer_screen_y = static_cast<float>(roblox_screen_pt.y) + dims.y;
	if (settings::visuals::neutral.tracers_origin == 1) // Center
	{
		tracer_screen_y = static_cast<float>(roblox_screen_pt.y) + dims.y * 0.5f;
	}
	else if (settings::visuals::neutral.tracers_origin == 2) // Top
	{
		tracer_screen_y = static_cast<float>(roblox_screen_pt.y);
	}

	math::vector3 camera_pos{};
	if (game::camera != 0)
	{
		rbx::c_primitive cam_prim(game::camera);
		camera_pos = cam_prim.get_position();
	}

	static thread_local std::vector<esp::projected_part_t> s_projected_parts;
	static thread_local std::unordered_map<std::string, ImVec2> s_part_centers;
	static thread_local std::unordered_set<std::uint64_t> drawn_entities;
	static thread_local std::unordered_set<std::uint64_t> drawn_addresses;
	drawn_entities.clear();
	drawn_addresses.clear();
	static thread_local std::vector<const cache::entity_t*> sorted_entities;
	sorted_entities.clear();
	sorted_entities.reserve(players_snapshot.size());
	for (const auto& ent : players_snapshot)
	{
		sorted_entities.push_back(&ent);
	}

	if (settings::visuals::sort_by_status)
	{
		std::sort(sorted_entities.begin(), sorted_entities.end(), [&](const cache::entity_t* pa, const cache::entity_t* pb)
		{
			const auto& a = *pa;
			const auto& b = *pb;
			int prio_a = (a.priority == cache::player_priority::hostile) ? 2 : ((a.priority == cache::player_priority::friendly) ? 0 : 1);
			int prio_b = (b.priority == cache::player_priority::hostile) ? 2 : ((b.priority == cache::player_priority::friendly) ? 0 : 1);
			if (prio_a != prio_b)
				return prio_a < prio_b;

			bool dead_a = (a.health <= 0.f || a.knocked);
			bool dead_b = (b.health <= 0.f || b.knocked);
			if (dead_a != dead_b)
				return dead_a > dead_b;

			float dist_a = 0.f;
			if (has_local_root_pos && a.humanoid_root_part.address)
			{
				dist_a = local_root_pos.distance(a.position);
			}
			float dist_b = 0.f;
			if (has_local_root_pos && b.humanoid_root_part.address)
			{
				dist_b = local_root_pos.distance(b.position);
			}
			return dist_a > dist_b;
		});
	}

	for (const auto* p_entity : sorted_entities)
	{
		const auto& entity = *p_entity;
		std::uint64_t entity_id = entity.humanoid_root_part.address ? entity.humanoid_root_part.address : entity.instance.address;
		if (entity_id != 0 && !drawn_entities.insert(entity_id).second)
		{
			continue;
		}

		rbx::c_part check_hrp = entity.humanoid_root_part.address ? entity.humanoid_root_part : rbx::c_part{};
		if (settings::visuals::distance_check && has_local_root_pos)
		{
			float dist = local_root_pos.distance(entity.position);
			if (dist > settings::visuals::max_distance)
				continue;
		}
		drawn_addresses.insert(entity.instance.address);

		bool is_local = (local_snapshot.instance.address != 0 && entity.instance.address == local_snapshot.instance.address) ||
		                (local_snapshot.user_id != 0 && entity.user_id == local_snapshot.user_id) ||
		                (local_snapshot.humanoid_root_part.address != 0 && entity.humanoid_root_part.address == local_snapshot.humanoid_root_part.address) ||
		                (cache::local_character.address != 0 && entity.instance.address == cache::local_character.address);
		if (is_local)
		{
			bool client_active = settings::visuals::local_player ||
			                     settings::visuals::neutral.box ||
			                     settings::visuals::neutral.box_fill ||
			                     settings::visuals::neutral.chams ||
			                     settings::visuals::neutral.skeleton ||
			                     settings::visuals::neutral.healthbar ||
			                     settings::visuals::neutral.username ||
			                     settings::visuals::neutral.distance ||
			                     settings::visuals::neutral.tool ||
			                     settings::visuals::neutral.head_dot ||
			                     settings::visuals::neutral.tracers ||
			                     settings::visuals::neutral.view_angle_lines ||
			                     settings::visuals::neutral.movement_trails ||
			                     settings::visuals::neutral.footprints ||
			                     settings::visuals::neutral.sound_esp ||
			                     settings::visuals::neutral.corpse;

			if (!client_active)
			{
				continue;
			}
		}

		bool is_friendly = false;
		if (!is_local)
		{
			if (entity.priority == cache::player_priority::friendly)
				is_friendly = true;
			else if (entity.priority == cache::player_priority::hostile)
				is_friendly = false;
			else if (local_snapshot.team != 0 && entity.team != 0 && entity.team == local_snapshot.team)
			{
				if (!game::datamodel->is_mm2() && !game::datamodel->is_arsenal())
				{
					is_friendly = true;
				}
			}
			else if (!entity.name.empty() && playerlist::get_priority(entity.name) == cache::player_priority::friendly)
				is_friendly = true;
		}

		if (is_friendly && settings::visuals::teamcheck)
		{
			continue;
		}

		settings::visuals::priority_settings_t* priority_settings = nullptr;
		if (is_local)
			priority_settings = &settings::visuals::neutral;
		else if (is_friendly)
			priority_settings = &settings::visuals::friendly;
		else
			priority_settings = &settings::visuals::hostile;

		// In-view Frustum Culling: Skip processing players that aren't rendered in view
		math::vector3 entity_pos = entity.position;
		bool is_zero_pos = (entity_pos.x == 0.0f && entity_pos.y == 0.0f && entity_pos.z == 0.0f);
		if (is_zero_pos && check_hrp.address)
		{
			rbx::c_primitive prim = check_hrp.get_primitive();
			if (prim.address)
			{
				entity_pos = prim.get_position();
				is_zero_pos = false;
			}
		}

		math::vector2 entity_screen_pos{};
		bool is_on_screen = !is_zero_pos && game::visualengine->world_to_screen(view, dims, entity_pos, entity_screen_pos);
		if (!is_zero_pos && !priority_settings->offscreen_arrows)
		{
			if (!is_on_screen)
			{
				continue;
			}
			if (entity_screen_pos.x < -150.0f || entity_screen_pos.x > dims.x + 150.0f ||
			    entity_screen_pos.y < -150.0f || entity_screen_pos.y > dims.y + 150.0f)
			{
				continue;
			}
		}

		bool is_dead = (entity.health <= 0.f || entity.knocked);

		if (is_dead)
		{
			if (!priority_settings->corpse && !priority_settings->corpse_names)
			{
				continue;
			}
		}
		else
		{
			if (settings::visuals::alive_check && entity.health <= 0.0f)
			{
				continue;
			}

			if (settings::visuals::ragdoll_check && entity.knocked)
			{
				continue;
			}

			if (settings::visuals::knock_check && entity.knocked)
			{
				continue;
			}

			if (settings::visuals::tool_check && entity.tool_name.empty())
			{
				continue;
			}

			if (settings::visuals::godded_check && (entity.health > 1000.0f || entity.health <= 0.0f))
			{
				continue;
			}

			if (settings::visuals::ignore_full_health && entity.health >= entity.max_health && entity.max_health > 0.0f)
			{
				continue;
			}

			if (settings::visuals::forcefield_check && entity.instance.address != 0)
			{
				rbx::c_instance character(entity.instance.address);
				if (character.find_first_child_by_class("ForceField") != 0)
				{
					continue;
				}
			}

			if (settings::visuals::health_check_enabled && entity.health < settings::visuals::min_health)
			{
				continue;
			}
		}

		bool valid = false;
		float left = FLT_MAX, top = FLT_MAX;
		float right = -FLT_MAX, bottom = -FLT_MAX;

		s_projected_parts.clear();
		s_part_centers.clear();
		auto& projected_parts = s_projected_parts;
		auto& part_centers = s_part_centers;

		rbx::c_part target_hrp = entity.humanoid_root_part.address ? entity.humanoid_root_part : rbx::c_part{};
		if (!target_hrp.address)
		{
			auto hrp_it = entity.parts.find("HumanoidRootPart");
			if (hrp_it != entity.parts.end())
			{
				target_hrp = hrp_it->second;
			}
		}

		rbx::c_primitive hrp_prim{};
		math::vector3 hrp_pos{};
		math::vector3 hrp_size{};
		math::matrix3 hrp_rot{};
		bool has_hrp_prim = false;

		if (target_hrp.address)
		{
			hrp_pos = entity_pos;
			hrp_size = math::vector3(2.0f, 2.0f, 1.0f);
			hrp_rot = entity.rotation.rotation;
			has_hrp_prim = true;
		}
		if (!has_hrp_prim && (entity_pos.x != 0.0f || entity_pos.y != 0.0f || entity_pos.z != 0.0f))
		{
			hrp_pos = entity_pos;
			hrp_size = math::vector3(2.0f, 5.0f, 1.0f);
			has_hrp_prim = true;
		}

		settings::visuals::priority_settings_t target_priority_copy;
		if (settings::visuals::target_recolor && aimbot::get_player_address() == entity.instance.address)
		{
			target_priority_copy = *priority_settings;
			for (int i = 0; i < 4; ++i)
			{
				target_priority_copy.colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.box_fill_colour[i] = (i == 3) ? (settings::visuals::target_recolor_colour[i] * 0.25f) : settings::visuals::target_recolor_colour[i];
				target_priority_copy.username_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.distance_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.tool_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.tracers_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.chams_colour[i] = (i == 3) ? (settings::visuals::target_recolor_colour[i] * 0.40f) : settings::visuals::target_recolor_colour[i];
				target_priority_copy.skeleton_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.head_dot_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.offscreen_arrows_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.movement_trails_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.view_angle_lines_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.sound_esp_colour[i] = settings::visuals::target_recolor_colour[i];
				target_priority_copy.footprints_colour[i] = settings::visuals::target_recolor_colour[i];
			}
			priority_settings = &target_priority_copy;
		}

		const bool want_box = priority_settings->box || priority_settings->box_fill;
		const bool want_tracers = priority_settings->tracers;
		const bool want_health = priority_settings->healthbar;
		const bool want_username = priority_settings->username;
		const bool want_distance = priority_settings->distance;
		const bool want_tool = priority_settings->tool;
		const bool want_chams = priority_settings->chams;
		const bool want_skeleton = priority_settings->skeleton;
		const bool want_hitbox = settings::hitboxexpander::visualize && settings::hitboxexpander::enabled;
		const bool want_flags = settings::visuals::flags;
		const bool want_offscreen_arrows = priority_settings->offscreen_arrows;
		const bool want_movement_trails = priority_settings->movement_trails;
		const bool want_view_lines = priority_settings->view_angle_lines;
		const bool want_sound_esp = priority_settings->sound_esp;
		const bool want_footprints = priority_settings->footprints;
		const bool want_head_dot   = priority_settings->head_dot;

		const bool want_corpse = is_dead && priority_settings->corpse;
		const bool want_corpse_names = is_dead && priority_settings->corpse_names;

		if (is_dead)
		{
			if (!want_corpse && !want_corpse_names)
			{
				continue;
			}
		}
		else
		{
			if (!(want_box || want_tracers || want_health || want_username || want_distance || want_tool || want_chams || want_skeleton || want_hitbox || want_offscreen_arrows || want_movement_trails || want_view_lines || want_sound_esp || want_footprints || want_head_dot || settings::visuals::enemy_highlight || settings::visuals::friendly_highlight))
			{
				continue;
			}
		}

		bool need_full_chams_projection = (want_chams && priority_settings->chams_type == 0) || want_corpse || settings::visuals::enemy_highlight || settings::visuals::friendly_highlight;

		// --- Accurate 3D Oriented Multi-Bone Bounding Box & Clipper Projection ---
		{
			bool is_dynamic = (settings::visuals::bounding_type == 0); // 0 = Dynamic, 1 = Static

			float min_x = FLT_MAX;
			float min_y = FLT_MAX;
			float max_x = -FLT_MAX;
			float max_y = -FLT_MAX;
			bool has_point = false;

			struct cached_geom_t {
				math::vector3 size;
			};
			static thread_local std::unordered_map<std::uint64_t, math::vector3> s_size_cache;
			static thread_local std::unordered_map<std::uint64_t, std::uint64_t> s_prim_addr_cache;

			auto get_cached_prim = [&](std::uint64_t part_addr) -> std::uint64_t {
				auto it = s_prim_addr_cache.find(part_addr);
				if (it != s_prim_addr_cache.end()) return it->second;
				std::uint64_t addr = memory->read<std::uint64_t>(part_addr + Offsets::BasePart::Primitive);
				if (addr != 0)
				{
					if (s_prim_addr_cache.size() > 1024) s_prim_addr_cache.clear();
					s_prim_addr_cache[part_addr] = addr;
				}
				return addr;
			};

			auto get_cached_size = [&](std::uint64_t prim_addr) -> math::vector3 {
				auto it = s_size_cache.find(prim_addr);
				if (it != s_size_cache.end()) return it->second;
				rbx::c_primitive prim(prim_addr);
				math::vector3 sz = prim.get_size();
				if (sz.x > 0.05f && sz.y > 0.05f && sz.z > 0.05f)
				{
					if (s_size_cache.size() > 1024) s_size_cache.clear();
					s_size_cache[prim_addr] = sz;
				}
				return sz;
			};

			bool has_any_limbs = false;
			for (const auto& [part_name, part_obj] : entity.parts)
			{
				if (part_obj.address != 0 && is_limb_part_name(part_name))
				{
					has_any_limbs = true;
					break;
				}
			}

			// If no recognized body parts exist, skip parts loop entirely.
			// The static fallback (hrp_pos-based box) will handle drawing.
			if (!has_any_limbs) goto skip_parts_box;

			for (const auto& [part_name, part_obj] : entity.parts)
			{
				if (part_obj.address == 0) continue;
				if (part_name == "HumanoidRootPart") continue;
				if (part_name.rfind("Accessory_", 0) == 0) continue;
				bool is_pf_limb = (part_name.rfind("pfLimb", 0) == 0);
				if (has_any_limbs && !is_limb_part_name(part_name) && !is_pf_limb) continue;

				// Skip known weapon/tool part names regardless of has_any_limbs
				static const std::unordered_set<std::string> s_esp_weapon_parts = {
					"Handle", "Muzzle", "Barrel", "Grip", "Blade", "Primary",
					"Flash", "Sight", "Stock", "Mag", "Magazine", "Trigger",
					"_center", "_grip", "FrontSight", "BackSight", "Suppressor",
					"Shield", "Bullet", "Projectile", "Trail", "Effect"
				};
				if (s_esp_weapon_parts.count(part_name) > 0) continue;

				std::uint64_t prim_addr = get_cached_prim(part_obj.address);
				if (prim_addr == 0) continue;

				rbx::c_primitive prim(prim_addr);
				math::cframe cf = prim.get_cframe();
				math::vector3 pos = cf.position;
				if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) continue;

				if (is_dead && settings::visuals::corpse_shift != 0.0f)
				{
					pos.y += settings::visuals::corpse_shift;
				}

				if (!is_dead && has_hrp_prim && (pos - hrp_pos).length() > 35.0f)
				{
					continue;
				}

				math::matrix3 rot = cf.rotation;
				math::vector3 size = get_cached_size(prim_addr);

				bool has_size = (size.x > 0.05f && size.y > 0.05f && size.z > 0.05f && size.x < 30.0f && size.y < 30.0f && size.z < 30.0f);
				if (!has_size)
				{
					if (part_name == "Head") size = { 1.2f, 1.2f, 1.2f };
					else if (part_name == "Torso" || part_name == "UpperTorso" || part_name == "LowerTorso") size = { 2.0f, 2.0f, 1.0f };
					else size = { 1.0f, 2.0f, 1.0f };
					has_size = true;
				}

				projected_part_t proj_part;
				proj_part.name = part_name.c_str();

				math::vector3 half = size * 0.5f;
				math::vector3 basis_x(rot.m[0][0], rot.m[1][0], rot.m[2][0]);
				math::vector3 basis_y(rot.m[0][1], rot.m[1][1], rot.m[2][1]);
				math::vector3 basis_z(rot.m[0][2], rot.m[1][2], rot.m[2][2]);

				for (const math::vector3& lc : local_corners)
				{
					math::vector3 world = pos;
					world = world + basis_x * (half.x * lc.x);
					world = world + basis_y * (half.y * lc.y);
					world = world + basis_z * (half.z * lc.z);

					math::vector2 screen{};
					if (game::visualengine->world_to_screen(view, dims, world, screen))
					{
						has_point = true;
						min_x = (std::min)(min_x, screen.x);
						min_y = (std::min)(min_y, screen.y);
						max_x = (std::max)(max_x, screen.x);
						max_y = (std::max)(max_y, screen.y);

						if (proj_part.num_points < 8)
						{
							proj_part.points[proj_part.num_points++] = ImVec2(screen.x, screen.y);
						}
					}
				}

				if (proj_part.num_points >= 3)
				{
					projected_parts.push_back(std::move(proj_part));
				}
			} // end for parts

			skip_parts_box:
			if (has_point)
			{
				constexpr float k_padding = 2.0f;
				min_x = (std::max)(0.0f, min_x - k_padding);
				min_y = (std::max)(0.0f, min_y - k_padding);
				max_x = (std::min)(dims.x, max_x + k_padding);
				max_y = (std::min)(dims.y, max_y + k_padding);

				if (max_x - min_x > 2.0f && max_y - min_y > 2.0f)
				{
					left = min_x;
					top = min_y;
					right = max_x;
					bottom = max_y;
					valid = true;
				}
			}

			if (!valid || !is_dynamic) // Static or fallback box
			{
				math::vector3 head_pos = (entity.part_positions.head.y != 0.0f) ? entity.part_positions.head : (has_hrp_prim ? hrp_pos + math::vector3(0.0f, 1.5f, 0.0f) : entity.position + math::vector3(0.0f, 1.5f, 0.0f));
				math::vector3 root_pos = has_hrp_prim ? hrp_pos : entity.position;

				if (is_dead && settings::visuals::corpse_shift != 0.0f)
				{
					head_pos.y += settings::visuals::corpse_shift;
					root_pos.y += settings::visuals::corpse_shift;
				}

				math::vector3 top_world    = head_pos + math::vector3(0.0f, 1.0f, 0.0f);
				math::vector3 bottom_world = root_pos - math::vector3(0.0f, 3.2f, 0.0f);

				math::vector2 top_scr, bottom_scr;
				if (game::visualengine->world_to_screen(view, dims, top_world, top_scr) &&
					game::visualengine->world_to_screen(view, dims, bottom_world, bottom_scr))
				{
					const float h = abs(bottom_scr.y - top_scr.y);
					if (h > 2.0f)
					{
						const float w = h * 0.58f;
						float center_x = (top_scr.x + bottom_scr.x) * 0.5f;
						left   = center_x - w * 0.5f;
						right  = center_x + w * 0.5f;
						top    = min(top_scr.y, bottom_scr.y);
						bottom = max(top_scr.y, bottom_scr.y);
						valid  = true;
					}
				}
			}
		}

		if (!valid)
		{
			continue;
		}

		float transparency = 255.f;

		float box_left = std::round(left);
		float box_top = std::round(top);
		float box_right = std::round(right);
		float box_bottom = std::round(bottom);
		float box_width = box_right - box_left;
		float box_height = box_bottom - box_top;
		float box_center_x = box_left + box_width * 0.5f;
		float box_center_y = box_top + box_height * 0.5f;

		if (is_dead)
		{
			if (want_corpse)
			{
				ImDrawList* cham_draw = ImGui::GetBackgroundDrawList();
				ImU32 cham_fill_col    = IM_COL32(priority_settings->corpse_colour[0] * 255.f, priority_settings->corpse_colour[1] * 255.f, priority_settings->corpse_colour[2] * 255.f, priority_settings->corpse_colour[3] * transparency);
				ImU32 cham_outline_col = IM_COL32(priority_settings->corpse_outline_colour[0] * 255.f, priority_settings->corpse_outline_colour[1] * 255.f, priority_settings->corpse_outline_colour[2] * 255.f, priority_settings->corpse_outline_colour[3] * transparency);

				if (priority_settings->chams_type == 0)
				{
					render_clipper_chams(cham_draw, projected_parts, cham_fill_col, cham_outline_col, settings::visuals::chams_outline_thickness, settings::visuals::chams_thickness);
				}
				// chams_type == 1 (Memory Mesh): handled by GPU pipeline in render.cpp — skip 2D software path
			}

			if (want_corpse_names)
			{
				const char* name_text = entity.name.c_str();
				float font_size = 0.0f;
				ImFont* font = get_esp_font(font_size);
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, name_text).x;

				esp::draw_text_outlined(draw, font, font_size,
					ImVec2(box_center_x - text_width * 0.5f, box_top - font_size - 5.f),
					IM_COL32(priority_settings->corpse_names_colour[0] * 255.f, priority_settings->corpse_names_colour[1] * 255.f, priority_settings->corpse_names_colour[2] * 255.f, priority_settings->corpse_names_colour[3] * transparency),
					name_text);
			}

			continue;
		}

		ImVec2 c1(box_left, box_top);
		ImVec2 c2(box_width, box_height);

		if (want_box)
		{
			ImRect rect_bb(c1.x, c1.y, box_right, box_bottom);

			ImU32 box_col_top = IM_COL32(priority_settings->colour[0] * 255.f, priority_settings->colour[1] * 255.f, priority_settings->colour[2] * 255.f, priority_settings->colour[3] * transparency);
			ImU32 box_col_mid = IM_COL32(priority_settings->box_colour_mid[0] * 255.f, priority_settings->box_colour_mid[1] * 255.f, priority_settings->box_colour_mid[2] * 255.f, priority_settings->box_colour_mid[3] * transparency);
			ImU32 box_col_low = IM_COL32(priority_settings->box_colour_low[0] * 255.f, priority_settings->box_colour_low[1] * 255.f, priority_settings->box_colour_low[2] * 255.f, priority_settings->box_colour_low[3] * transparency);

			ImU32 box_col = box_col_top;
			if (priority_settings->box_gradient && priority_settings->box_gradient_type == 2)
			{
				float time = static_cast<float>(ImGui::GetTime());
				float speed = (priority_settings->box_pulse_speed > 0.01f) ? priority_settings->box_pulse_speed : 2.0f;
				float phase = fmodf(time * speed, 2.0f);
				float t = phase < 1.0f ? phase : (2.0f - phase);
				float r, g, b, a;
				if (t < 0.5f) {
					float factor = t * 2.0f;
					r = priority_settings->colour[0] * (1.f - factor) + priority_settings->box_colour_mid[0] * factor;
					g = priority_settings->colour[1] * (1.f - factor) + priority_settings->box_colour_mid[1] * factor;
					b = priority_settings->colour[2] * (1.f - factor) + priority_settings->box_colour_mid[2] * factor;
					a = priority_settings->colour[3] * (1.f - factor) + priority_settings->box_colour_mid[3] * factor;
				} else {
					float factor = (t - 0.5f) * 2.0f;
					r = priority_settings->box_colour_mid[0] * (1.f - factor) + priority_settings->box_colour_low[0] * factor;
					g = priority_settings->box_colour_mid[1] * (1.f - factor) + priority_settings->box_colour_low[1] * factor;
					b = priority_settings->box_colour_mid[2] * (1.f - factor) + priority_settings->box_colour_low[2] * factor;
					a = priority_settings->box_colour_mid[3] * (1.f - factor) + priority_settings->box_colour_low[3] * factor;
				}
				box_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(a * transparency));
			}

			if (priority_settings->box_fill && priority_settings->box_type != 2)
			{
				ImU32 fill_col_top = IM_COL32(priority_settings->box_fill_colour[0] * 255.f, priority_settings->box_fill_colour[1] * 255.f, priority_settings->box_fill_colour[2] * 255.f, priority_settings->box_fill_colour[3] * transparency);
				ImU32 fill_col_mid = IM_COL32(priority_settings->box_fill_colour_mid[0] * 255.f, priority_settings->box_fill_colour_mid[1] * 255.f, priority_settings->box_fill_colour_mid[2] * 255.f, priority_settings->box_fill_colour_mid[3] * transparency);
				ImU32 fill_col_low = IM_COL32(priority_settings->box_fill_colour_low[0] * 255.f, priority_settings->box_fill_colour_low[1] * 255.f, priority_settings->box_fill_colour_low[2] * 255.f, priority_settings->box_fill_colour_low[3] * transparency);

				if (priority_settings->box_fill_gradient)
				{
					if (priority_settings->box_fill_gradient_type == 0)
					{
						float mid_y = (rect_bb.Min.y + rect_bb.Max.y) * 0.5f;
						draw->AddRectFilledMultiColor(rect_bb.Min, ImVec2(rect_bb.Max.x, mid_y), fill_col_top, fill_col_top, fill_col_mid, fill_col_mid);
						draw->AddRectFilledMultiColor(ImVec2(rect_bb.Min.x, mid_y), rect_bb.Max, fill_col_mid, fill_col_mid, fill_col_low, fill_col_low);
					}
					else if (priority_settings->box_fill_gradient_type == 1)
					{
						float mid_x = (rect_bb.Min.x + rect_bb.Max.x) * 0.5f;
						draw->AddRectFilledMultiColor(rect_bb.Min, ImVec2(mid_x, rect_bb.Max.y), fill_col_top, fill_col_mid, fill_col_mid, fill_col_top);
						draw->AddRectFilledMultiColor(ImVec2(mid_x, rect_bb.Min.y), rect_bb.Max, fill_col_mid, fill_col_low, fill_col_low, fill_col_mid);
					}
					else if (priority_settings->box_fill_gradient_type == 2)
					{
						float time = static_cast<float>(ImGui::GetTime());
						float speed = (priority_settings->box_fill_pulse_speed > 0.01f) ? priority_settings->box_fill_pulse_speed : 2.0f;
						float phase = fmodf(time * speed, 2.0f);
						float t = phase < 1.0f ? phase : (2.0f - phase);
						float r, g, b, a;
						if (t < 0.5f) {
							float factor = t * 2.0f;
							r = priority_settings->box_fill_colour[0] * (1.f - factor) + priority_settings->box_fill_colour_mid[0] * factor;
							g = priority_settings->box_fill_colour[1] * (1.f - factor) + priority_settings->box_fill_colour_mid[1] * factor;
							b = priority_settings->box_fill_colour[2] * (1.f - factor) + priority_settings->box_fill_colour_mid[2] * factor;
							a = priority_settings->box_fill_colour[3] * (1.f - factor) + priority_settings->box_fill_colour_mid[3] * factor;
						} else {
							float factor = (t - 0.5f) * 2.0f;
							r = priority_settings->box_fill_colour_mid[0] * (1.f - factor) + priority_settings->box_fill_colour_low[0] * factor;
							g = priority_settings->box_fill_colour_mid[1] * (1.f - factor) + priority_settings->box_fill_colour_low[1] * factor;
							b = priority_settings->box_fill_colour_mid[2] * (1.f - factor) + priority_settings->box_fill_colour_low[2] * factor;
							a = priority_settings->box_fill_colour_mid[3] * (1.f - factor) + priority_settings->box_fill_colour_low[3] * factor;
						}
						ImU32 pulse_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(a * transparency));
						draw->AddRectFilled(rect_bb.Min, rect_bb.Max, pulse_col);
					}
				}
				else
				{
					draw->AddRectFilled(rect_bb.Min, rect_bb.Max, fill_col_top);
				}
			}

			// Box Outlines: 0 = "2D Corner", 1 = "2D Full", 2 = "3D Box"
			ImU32 active_box_col = (priority_settings->box_gradient && (priority_settings->box_gradient_type == 0 || priority_settings->box_gradient_type == 1)) ? box_col_top : box_col;

			if (priority_settings->box_type == 0) // 2D Corner
			{
				esp::corner_box(draw, rect_bb.Min, rect_bb.Max, active_box_col, 1.f, priority_settings->box_inline);
			}
			else if (priority_settings->box_type == 1) // 2D Full
			{
				if (priority_settings->box_gradient)
				{
					esp::outline_gradient(c1, c2, box_col_top, box_col_mid, box_col_low, priority_settings->box_gradient_type, priority_settings->box_pulse_speed, transparency, 0.f, priority_settings->box_inline);
				}
				else
				{
					esp::outline(c1, c2, active_box_col, 0.f, priority_settings->box_inline);
				}
			}
		}

		if (want_tracers)
		{
			float cur_tracer_x = static_cast<float>(roblox_screen_pt.x) + dims.x * 0.5f;
			float cur_tracer_y = static_cast<float>(roblox_screen_pt.y) + dims.y; // Bottom (0)
			if (priority_settings->tracers_origin == 1) // Center
			{
				cur_tracer_y = static_cast<float>(roblox_screen_pt.y) + dims.y * 0.5f;
			}
			else if (priority_settings->tracers_origin == 2) // Top
			{
				cur_tracer_y = static_cast<float>(roblox_screen_pt.y);
			}
			else if (priority_settings->tracers_origin == 3) // Cursor / Mouse
			{
				cur_tracer_x = ImGui::GetMousePos().x;
				cur_tracer_y = ImGui::GetMousePos().y;
			}

			ImU32 tracer_col = IM_COL32(priority_settings->tracers_colour[0] * 255.f, priority_settings->tracers_colour[1] * 255.f, priority_settings->tracers_colour[2] * 255.f, priority_settings->tracers_colour[3] * transparency);
			ImU32 outline_col = IM_COL32(0, 0, 0, 255);
			float tracer_thickness = (priority_settings->tracers_thickness > 0.0f) ? priority_settings->tracers_thickness : 1.0f;
			if (settings::visuals::render_outlines[6]) // index 6 = Tracers
			{
				draw->AddLine(ImVec2(cur_tracer_x, cur_tracer_y), ImVec2(box_center_x, box_bottom + 2.f), outline_col, tracer_thickness + 2.f);
			}
			draw->AddLine(ImVec2(cur_tracer_x, cur_tracer_y), ImVec2(box_center_x, box_bottom + 2.f), tracer_col, tracer_thickness);
		}

		if (want_health && entity.max_health > 0.f && !(priority_settings->ignore_full && entity.health >= entity.max_health))
		{
			float target_hp_percent = entity.health / entity.max_health;
			target_hp_percent = (target_hp_percent < 0.f) ? 0.f : (target_hp_percent > 1.f) ? 1.f : target_hp_percent;

			uint64_t entity_id = entity.instance.address;

			float dt = ImGui::GetIO().DeltaTime;
			if (dt <= 0.f) dt = 0.016f;

			float current_anim = target_hp_percent;
			if (priority_settings->healthbar_lerp)
			{
				auto anim_it = s_anim_hp.find(entity_id);
				if (anim_it == s_anim_hp.end())
				{
					s_anim_hp[entity_id] = target_hp_percent;
				}

				current_anim = s_anim_hp[entity_id];
				current_anim += (target_hp_percent - current_anim) * (dt * 14.0f < 1.0f ? dt * 14.0f : 1.0f);
				if (std::abs(current_anim - target_hp_percent) < 0.001f) current_anim = target_hp_percent;
				s_anim_hp[entity_id] = current_anim;
			}

			float bar_thickness = settings::visuals::healthbar_thickness;
			float bar_spacing = priority_settings->healthbar_padding;

			ImVec2 bg_min, bg_max;
			ImVec2 outline_min, outline_max;
			ImVec2 fill_min, fill_max;

			float off_x = priority_settings->healthbar_offset_x;
			float off_y = priority_settings->healthbar_offset_y;

			int style = priority_settings->healthbar_style;
			if (style == 0) // Left
			{
				float health_bar_height = box_height + 2.f;
				bg_min = ImVec2(box_left - bar_spacing - bar_thickness + off_x, box_top - 1.f + off_y);
				bg_max = ImVec2(box_left - bar_spacing + off_x, box_bottom + 1.f + off_y);

				float health_fill_height = health_bar_height * current_anim;
				fill_min = ImVec2(box_left - bar_spacing - bar_thickness + off_x, box_bottom + 1.f - health_fill_height + off_y);
				fill_max = ImVec2(box_left - bar_spacing + off_x, box_bottom + 1.f + off_y);
			}
			else if (style == 1) // Right
			{
				float health_bar_height = box_height + 2.f;
				bg_min = ImVec2(box_right + bar_spacing + off_x, box_top - 1.f + off_y);
				bg_max = ImVec2(box_right + bar_spacing + bar_thickness + off_x, box_bottom + 1.f + off_y);

				float health_fill_height = health_bar_height * current_anim;
				fill_min = ImVec2(box_right + bar_spacing + off_x, box_bottom + 1.f - health_fill_height + off_y);
				fill_max = ImVec2(box_right + bar_spacing + bar_thickness + off_x, box_bottom + 1.f + off_y);
			}
			else if (style == 2) // Bottom
			{
				float health_bar_width = box_width + 2.f;
				bg_min = ImVec2(box_left - 1.f + off_x, box_bottom + bar_spacing + off_y);
				bg_max = ImVec2(box_right + 1.f + off_x, box_bottom + bar_spacing + bar_thickness + off_y);

				float health_fill_width = health_bar_width * current_anim;
				fill_min = ImVec2(box_left - 1.f + off_x, box_bottom + bar_spacing + off_y);
				fill_max = ImVec2(box_left - 1.f + health_fill_width + off_x, box_bottom + bar_spacing + bar_thickness + off_y);
			}
			else // Top (3)
			{
				float health_bar_width = box_width + 2.f;
				bg_min = ImVec2(box_left - 1.f + off_x, box_top - bar_spacing - bar_thickness + off_y);
				bg_max = ImVec2(box_right + 1.f + off_x, box_top - bar_spacing + off_y);

				float health_fill_width = health_bar_width * current_anim;
				fill_min = ImVec2(box_left - 1.f + off_x, box_top - bar_spacing - bar_thickness + off_y);
				fill_max = ImVec2(box_left - 1.f + health_fill_width + off_x, box_top - bar_spacing + off_y);
			}

			if (priority_settings->bar_fold)
			{
				bg_min = fill_min;
				bg_max = fill_max;
			}

			outline_min = ImVec2(bg_min.x - 1.f, bg_min.y - 1.f);
			outline_max = ImVec2(bg_max.x + 1.f, bg_max.y + 1.f);

			if (settings::visuals::bar_fill)
			{
				ImU32 bg_fill = IM_COL32(static_cast<int>(settings::visuals::bar_fill_colour[0] * 255.f), static_cast<int>(settings::visuals::bar_fill_colour[1] * 255.f), static_cast<int>(settings::visuals::bar_fill_colour[2] * 255.f), static_cast<int>(settings::visuals::bar_fill_colour[3] * transparency));
				draw->AddRectFilled(bg_min, bg_max, bg_fill);
			}

			if (settings::visuals::render_outlines[3]) // index 3 = Health Bar
			{
				draw->AddRectFilled(outline_min, outline_max, IM_COL32(0, 0, 0, static_cast<int>(transparency * 0.85f)));
			}

			if (current_anim > 0.f)
			{
				ImU32 col_top = IM_COL32(priority_settings->healthbar_colour[0] * 255.f, priority_settings->healthbar_colour[1] * 255.f, priority_settings->healthbar_colour[2] * 255.f, priority_settings->healthbar_colour[3] * transparency);
				ImU32 col_mid = IM_COL32(priority_settings->healthbar_colour_mid[0] * 255.f, priority_settings->healthbar_colour_mid[1] * 255.f, priority_settings->healthbar_colour_mid[2] * 255.f, priority_settings->healthbar_colour_mid[3] * transparency);
				ImU32 col_low = IM_COL32(priority_settings->healthbar_colour_low[0] * 255.f, priority_settings->healthbar_colour_low[1] * 255.f, priority_settings->healthbar_colour_low[2] * 255.f, priority_settings->healthbar_colour_low[3] * transparency);

				if (priority_settings->healthbar_gradient_type == 0)
				{
					float mid_y = (fill_min.y + fill_max.y) * 0.5f;
					draw->AddRectFilledMultiColor(fill_min, ImVec2(fill_max.x, mid_y), col_top, col_top, col_mid, col_mid);
					draw->AddRectFilledMultiColor(ImVec2(fill_min.x, mid_y), fill_max, col_mid, col_mid, col_low, col_low);
				}
				else if (priority_settings->healthbar_gradient_type == 1)
				{
					float mid_x = (fill_min.x + fill_max.x) * 0.5f;
					draw->AddRectFilledMultiColor(fill_min, ImVec2(mid_x, fill_max.y), col_top, col_mid, col_mid, col_top);
					draw->AddRectFilledMultiColor(ImVec2(mid_x, fill_min.y), fill_max, col_mid, col_low, col_low, col_mid);
				}
				else if (priority_settings->healthbar_gradient_type == 2)
				{
					float time = static_cast<float>(ImGui::GetTime());
					float speed = (priority_settings->healthbar_pulse_speed > 0.01f) ? priority_settings->healthbar_pulse_speed : 2.5f;
					float phase = fmodf(time * speed, 2.0f);
					float t = phase < 1.0f ? phase : (2.0f - phase);
					float r, g, b, a;
					if (t < 0.5f) {
						float factor = t * 2.0f;
						r = priority_settings->healthbar_colour[0] * (1.f - factor) + priority_settings->healthbar_colour_mid[0] * factor;
						g = priority_settings->healthbar_colour[1] * (1.f - factor) + priority_settings->healthbar_colour_mid[1] * factor;
						b = priority_settings->healthbar_colour[2] * (1.f - factor) + priority_settings->healthbar_colour_mid[2] * factor;
						a = priority_settings->healthbar_colour[3] * (1.f - factor) + priority_settings->healthbar_colour_mid[3] * factor;
					} else {
						float factor = (t - 0.5f) * 2.0f;
						r = priority_settings->healthbar_colour_mid[0] * (1.f - factor) + priority_settings->healthbar_colour_low[0] * factor;
						g = priority_settings->healthbar_colour_mid[1] * (1.f - factor) + priority_settings->healthbar_colour_low[1] * factor;
						b = priority_settings->healthbar_colour_mid[2] * (1.f - factor) + priority_settings->healthbar_colour_low[2] * factor;
						a = priority_settings->healthbar_colour_mid[3] * (1.f - factor) + priority_settings->healthbar_colour_low[3] * factor;
					}
					ImU32 pulse_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(a * transparency));
					draw->AddRectFilled(fill_min, fill_max, pulse_col);
				}
			}

			if (priority_settings->healthbar_text && current_anim > 0.f)
			{
				std::string hp_str = std::to_string(static_cast<int>(entity.health));
				float hp_font_size = 0.0f;
				ImFont* hp_font = get_esp_font(hp_font_size);

				ImVec2 hp_size = hp_font->CalcTextSizeA(hp_font_size, FLT_MAX, 0.0f, hp_str.c_str());
				ImVec2 hp_pos{};

				if (style == 0) // Left
				{
					hp_pos.x = fill_min.x - hp_size.x - 2.f;
					hp_pos.y = priority_settings->value_follow ? (fill_min.y - hp_size.y * 0.5f) : (bg_min.y + 2.f);
				}
				else if (style == 1) // Right
				{
					hp_pos.x = fill_max.x + 2.f;
					hp_pos.y = priority_settings->value_follow ? (fill_min.y - hp_size.y * 0.5f) : (bg_min.y + 2.f);
				}
				else if (style == 2) // Bottom
				{
					hp_pos.x = priority_settings->value_follow ? (fill_max.x - hp_size.x * 0.5f) : (bg_max.x + 2.f);
					hp_pos.y = fill_max.y + 2.f;
				}
				else // Top
				{
					hp_pos.x = priority_settings->value_follow ? (fill_max.x - hp_size.x * 0.5f) : (bg_max.x + 2.f);
					hp_pos.y = fill_min.y - hp_size.y - 2.f;
				}

				ImU32 text_col = IM_COL32(priority_settings->healthbar_text_colour[0] * 255.f, priority_settings->healthbar_text_colour[1] * 255.f, priority_settings->healthbar_text_colour[2] * 255.f, priority_settings->healthbar_text_colour[3] * transparency);
				esp::draw_text_outlined(draw, hp_font, hp_font_size, hp_pos, text_col, hp_str.c_str(), nullptr, settings::visuals::render_outlines[4]); // index 4 = Health Text
			}
		}

		float base_font_size = 0.0f;
		ImFont* font = get_esp_font(base_font_size);
		float font_size = base_font_size;

		float top_text_y = box_top - 3.f;
		float bottom_text_y = box_bottom + 3.f;

		if (priority_settings->healthbar && !(priority_settings->ignore_full))
		{
			float bar_h = settings::visuals::healthbar_thickness + priority_settings->healthbar_padding + 3.f;
			if (priority_settings->healthbar_style == 3) // Top
			{
				top_text_y -= (bar_h + 2.f);
			}
			else if (priority_settings->healthbar_style == 2) // Bottom
			{
				bottom_text_y += (bar_h + 2.f);
			}
		}

		if (want_username)
		{
			std::string formatted_name;
			if (priority_settings->username_type == 1) // Username
			{
				formatted_name = entity.name.empty() ? "unknown" : entity.name;
			}
			else if (priority_settings->username_type == 2) // Both
			{
				std::string dn = entity.display_name.empty() ? entity.name : entity.display_name;
				if (!entity.name.empty() && dn != entity.name)
					formatted_name = dn + " (@" + entity.name + ")";
				else
					formatted_name = dn;
			}
			else // 0 = Display Name
			{
				formatted_name = entity.display_name.empty() ? entity.name : entity.display_name;
			}

			if (priority_settings->name_transform == 1) // Uppercase
			{
				for (char& c : formatted_name) c = (char)toupper(c);
			}
			else if (priority_settings->name_transform == 2) // Lowercase
			{
				for (char& c : formatted_name) c = (char)tolower(c);
			}

			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, formatted_name.c_str()).x;

			ImVec2 name_pos;
			if (priority_settings->username_position == 1) // Bottom
			{
				name_pos = ImVec2(box_center_x - text_width * 0.5f, bottom_text_y);
				bottom_text_y += (font_size + 2.f);
			}
			else if (priority_settings->username_position == 2) // Left
			{
				name_pos = ImVec2(box_left - text_width - 4.f, box_top);
			}
			else if (priority_settings->username_position == 3) // Right
			{
				name_pos = ImVec2(box_right + 4.f, box_top);
			}
			else // 0 = Top
			{
				name_pos = ImVec2(box_center_x - text_width * 0.5f, top_text_y - font_size);
				top_text_y -= (font_size + 2.f);
			}

			if (priority_settings->username_gradient)
			{
				float cur_time = static_cast<float>(ImGui::GetTime());
				if (priority_settings->username_gradient_type == 1) // Rainbow
				{
					float hue = std::fmodf(cur_time * (priority_settings->username_gradient_speed * 0.2f), 1.0f);
					float r, g, b;
					ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.0f, r, g, b);
					ImU32 name_col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(priority_settings->username_colour[3] * transparency));
					esp::draw_text_outlined(draw, font, font_size, name_pos, name_col, formatted_name.c_str(), nullptr, settings::visuals::render_outlines[2]);
				}
				else if (priority_settings->username_gradient_type == 2) // Left to Right
				{
					float cur_x = name_pos.x;
					for (size_t ci = 0; ci < formatted_name.size(); ++ci)
					{
						char char_str[2] = { formatted_name[ci], '\0' };
						float char_t = std::sinf(cur_time * priority_settings->username_gradient_speed + ci * 0.4f) * 0.5f + 0.5f;
						float cr = priority_settings->username_colour[0] + (priority_settings->username_colour_two[0] - priority_settings->username_colour[0]) * char_t;
						float cg = priority_settings->username_colour[1] + (priority_settings->username_colour_two[1] - priority_settings->username_colour[1]) * char_t;
						float cb = priority_settings->username_colour[2] + (priority_settings->username_colour_two[2] - priority_settings->username_colour[2]) * char_t;
						ImU32 char_col = IM_COL32(static_cast<int>(cr * 255.f), static_cast<int>(cg * 255.f), static_cast<int>(cb * 255.f), static_cast<int>(priority_settings->username_colour[3] * transparency));

						esp::draw_text_outlined(draw, font, font_size, ImVec2(cur_x, name_pos.y), char_col, char_str, nullptr, settings::visuals::render_outlines[2]);
						cur_x += font->CalcTextSizeA(font_size, FLT_MAX, 0.f, char_str).x;
					}
				}
				else // 0 = Wave
				{
					float wave_t = std::sinf(cur_time * priority_settings->username_gradient_speed) * 0.5f + 0.5f;
					float wr = priority_settings->username_colour[0] + (priority_settings->username_colour_two[0] - priority_settings->username_colour[0]) * wave_t;
					float wg = priority_settings->username_colour[1] + (priority_settings->username_colour_two[1] - priority_settings->username_colour[1]) * wave_t;
					float wb = priority_settings->username_colour[2] + (priority_settings->username_colour_two[2] - priority_settings->username_colour[2]) * wave_t;
					ImU32 name_col = IM_COL32(static_cast<int>(wr * 255.f), static_cast<int>(wg * 255.f), static_cast<int>(wb * 255.f), static_cast<int>(priority_settings->username_colour[3] * transparency));
					esp::draw_text_outlined(draw, font, font_size, name_pos, name_col, formatted_name.c_str(), nullptr, settings::visuals::render_outlines[2]);
				}
			}
			else
			{
				esp::draw_text_outlined(draw, font, font_size,
					name_pos,
					IM_COL32(priority_settings->username_colour[0] * 255.f, priority_settings->username_colour[1] * 255.f, priority_settings->username_colour[2] * 255.f, priority_settings->username_colour[3] * transparency),
					formatted_name.c_str(), nullptr, settings::visuals::render_outlines[2]); // index 2 = Name
			}
		}

		if (settings::mm2::role_esp && game::datamodel->is_mm2())
		{
			const char* role_text = entity.mm2_role.c_str();
			ImU32 role_col = IM_COL32(80, 255, 80, static_cast<int>(255.f * transparency));
			if (entity.mm2_role == "Murderer") {
				role_col = IM_COL32(255, 50, 50, static_cast<int>(255.f * transparency));
			} else if (entity.mm2_role == "Sheriff") {
				role_col = IM_COL32(80, 160, 255, static_cast<int>(255.f * transparency));
			}

			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, role_text).x;
			esp::draw_text_outlined(draw, font, font_size,
				ImVec2(box_center_x - text_width * 0.5f, top_text_y - font_size),
				role_col, role_text, nullptr, true);

			top_text_y -= (font_size + 2.f);
		}

		if (want_distance && has_local_root_pos && has_hrp_prim)
		{
			char distance_str[32];
			float dist = local_root_pos.distance(hrp_pos);
			if (settings::visuals::distance_unit == 1) // Meters
			{
				std::snprintf(distance_str, sizeof(distance_str), "%.0fm", dist * 0.28f);
			}
			else // Studs
			{
				std::snprintf(distance_str, sizeof(distance_str), "%.0f studs", dist);
			}

			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, distance_str).x;

			esp::draw_text_outlined(draw, font, font_size,
				ImVec2(box_center_x - text_width * 0.5f, bottom_text_y),
				IM_COL32(priority_settings->distance_colour[0] * 255.f, priority_settings->distance_colour[1] * 255.f, priority_settings->distance_colour[2] * 255.f, priority_settings->distance_colour[3] * transparency),
				distance_str, nullptr, settings::visuals::render_outlines[2]); // index 2 = Name

			bottom_text_y += (font_size + 2.f);
		}

		if (want_tool)
		{
			const char* tool_text = entity.tool_name.empty() ? "None" : entity.tool_name.c_str();

			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, tool_text).x;
			float tool_y = box_bottom + 3.f;
			if (want_distance)
			{
				tool_y += font_size + 2.f;
			}

			ImU32 tool_col = IM_COL32(priority_settings->tool_colour[0] * 255.f, priority_settings->tool_colour[1] * 255.f, priority_settings->tool_colour[2] * 255.f, priority_settings->tool_colour[3] * transparency);
			esp::draw_text_outlined(draw, font, font_size, ImVec2(box_center_x - text_width * 0.5f, tool_y), tool_col, tool_text, nullptr, settings::visuals::render_outlines[5]); // index 5 = Held Item
		}

			if (want_flags && box_height >= 14.0f)
			{
				float current_y = box_top;
				// Fixed arena instead of a vector<std::pair<std::string, ImU32>>
				// built per player per frame — zero allocations.
				struct flag_item_t { char text[72]; ImU32 col; bool is_hp; };
				static flag_item_t flags_buf[16];
				int flag_count = 0;
				auto push_flag = [&](const char* text, ImU32 col, bool is_hp = false) {
					if (flag_count >= 16) return;
					flag_item_t& f = flags_buf[flag_count++];
					strncpy_s(f.text, sizeof(f.text), text, _TRUNCATE);
					f.col = col;
					f.is_hp = is_hp;
				};

			ImU32 default_flag_col = IM_COL32(
				static_cast<int>(settings::visuals::flags_colour[0] * 255.f),
				static_cast<int>(settings::visuals::flags_colour[1] * 255.f),
				static_cast<int>(settings::visuals::flags_colour[2] * 255.f),
				static_cast<int>(settings::visuals::flags_colour[3] * transparency)
			);

			ImU32 state_flag_col = IM_COL32(
				static_cast<int>(settings::visuals::flags_state_colour[0] * 255.f),
				static_cast<int>(settings::visuals::flags_state_colour[1] * 255.f),
				static_cast<int>(settings::visuals::flags_state_colour[2] * 255.f),
				static_cast<int>(settings::visuals::flags_state_colour[3] * transparency)
			);

			int mask = settings::visuals::flags_mask;
			bool show_priority = (mask == 0 || mask == 1 || mask == 2 || mask == 5);
			bool show_state    = (mask == 0 || mask == 1 || mask == 2 || mask == 4);
			bool show_ko       = (mask == 1 || mask == 4 || mask == 5);
			bool show_hp       = (mask == 1 || mask == 3);
			bool show_dist     = (mask == 1 || mask == 3);
			bool show_tool     = (mask == 1 || mask == 3);
			bool show_alt      = (mask == 1);
			bool show_air      = (mask == 1);
			bool show_speed    = (mask == 1);

			if (show_priority)
			{
				cache::player_priority priority = entity.priority;
				const char* priority_text = "[Neutral]";
				ImU32 priority_col = default_flag_col;
				switch (priority)
				{
				case cache::player_priority::neutral:
					priority_text = "[Neutral]";
					priority_col = default_flag_col;
					break;
				case cache::player_priority::friendly:
					priority_text = "[Friendly]";
					priority_col = IM_COL32(50, 255, 50, static_cast<int>(transparency));
					break;
				case cache::player_priority::hostile:
					priority_text = "[Hostile]";
					priority_col = IM_COL32(255, 50, 50, static_cast<int>(transparency));
					break;
				}
				push_flag(priority_text, priority_col);
			}

			if (priority_settings->state_flags && show_state && entity.humanoid.address != 0)
			{
				int humanoid_state = entity.humanoid_state;
				const char* state = "Unknown";
				if (humanoid_state == 8)
				{
					state = "Running";
				}
				if (strcmp(state, "Unknown") == 0)
				{
					static const char* states[] = { "FallingDown", "Ragdoll", "GettingUp", "Jumping", "Swimming", "Freefall", "Flying", "Landed", "Running", "Unknown", "RunningNoPhysics", "StrafingNoPhysics", "Climbing", "Seated", "PlatformStanding", "Dead", "Physics", "Unknown", "None" };
					if (humanoid_state >= 0 && humanoid_state < 19)
					{
						state = states[humanoid_state];
					}
				}
				char state_buffer[64];
				std::snprintf(state_buffer, sizeof(state_buffer), "[%s]", state);
				push_flag(state_buffer, state_flag_col);
			}

			if (priority_settings->rig_flags)
			{
				bool is_r15 = (entity.parts.count("UpperTorso") > 0 || entity.parts.count("LeftHand") > 0 || entity.parts.count("LeftUpperArm") > 0);
				push_flag(is_r15 ? "[R15]" : "[R6]", default_flag_col);
			}

			if (show_ko)
			{
				if (entity.knocked)
				{
					push_flag("[KO]", IM_COL32(255, 60, 60, static_cast<int>(transparency)));
				}
				else if (entity.health <= 0.f)
				{
					push_flag("[Dead]", IM_COL32(200, 50, 50, static_cast<int>(transparency)));
				}
			}

			if (show_hp && entity.humanoid.address != 0 && entity.max_health > 0.f)
			{
				char hp_buf[32];
				std::snprintf(hp_buf, sizeof(hp_buf), "[%.0f HP]", entity.health);
				push_flag(hp_buf, default_flag_col, true);
			}

			if (show_dist && has_local_root_pos && has_hrp_prim)
			{
				char dist_buf[32];
				float dist = local_root_pos.distance(hrp_pos);
				if (settings::visuals::distance_unit == 1) // Meters
				{
					std::snprintf(dist_buf, sizeof(dist_buf), "[%.0fm]", dist * 0.28f);
				}
				else // Studs
				{
					std::snprintf(dist_buf, sizeof(dist_buf), "[%.0fs]", dist);
				}
				push_flag(dist_buf, default_flag_col);
			}

			if (show_tool && !entity.tool_name.empty())
			{
				char tool_buf[64];
				std::snprintf(tool_buf, sizeof(tool_buf), "[%s]", entity.tool_name.c_str());
				push_flag(tool_buf, default_flag_col);
			}

			if (show_alt && has_hrp_prim)
			{
				char alt_buf[32];
				std::snprintf(alt_buf, sizeof(alt_buf), "[Y: %.0f]", hrp_pos.y);
				push_flag(alt_buf, default_flag_col);
			}

			if (show_air && entity.humanoid.address != 0)
			{
				int hs = entity.humanoid_state;
				if (hs == 3 || hs == 5 || hs == 6)
				{
					push_flag("[In Air]", IM_COL32(255, 200, 50, static_cast<int>(transparency)));
				}
			}

			if (show_speed && has_hrp_prim)
			{
				math::vector3 vel = entity.velocity;
				float spd = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				if (spd > 1.0f)
				{
					char spd_buf[32];
					std::snprintf(spd_buf, sizeof(spd_buf), "[%.1f m/s]", spd);
					push_flag(spd_buf, default_flag_col);
				}
			}

			for (int fi = 0; fi < flag_count; ++fi)
			{
				const flag_item_t& flag = flags_buf[fi];
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, flag.text).x;
				float flag_x = box_right + 5.f;
				bool use_flag_outline = settings::visuals::render_outlines[2]; // index 2 = Name (default for general flags)
				if (flag.is_hp)
				{
					use_flag_outline = settings::visuals::render_outlines[4]; // index 4 = Health Text
				}
				esp::draw_text_outlined(draw, font, font_size, ImVec2(flag_x, current_y), flag.col, flag.text, nullptr, use_flag_outline);
				current_y += font_size + 2.f;
			}
		}

		if (want_skeleton)
		{
			ImDrawList* skel_draw = ImGui::GetBackgroundDrawList();
			skel_draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;

			ImU32 sk_col = IM_COL32(priority_settings->skeleton_colour[0] * 255.f, priority_settings->skeleton_colour[1] * 255.f, priority_settings->skeleton_colour[2] * 255.f, priority_settings->skeleton_colour[3] * transparency);
			ImU32 outline_col = IM_COL32(priority_settings->skeleton_outline_colour[0] * 255.f, priority_settings->skeleton_outline_colour[1] * 255.f, priority_settings->skeleton_outline_colour[2] * 255.f, priority_settings->skeleton_outline_colour[3] * transparency);
			bool enable_outline = settings::visuals::render_outlines[0] && (priority_settings->skeleton_outline_colour[3] > 0.01f);
			float thickness = settings::visuals::skeleton_thickness;
			float outline_extra = 2.0f;

			auto add_skeleton_segment = [&](const std::optional<ImVec2>& from, const std::optional<ImVec2>& to)
			{
				if (!from || !to) return;
				if (enable_outline)
				{
					skel_draw->AddLine(*from, *to, outline_col, thickness + outline_extra);
				}
				skel_draw->AddLine(*from, *to, sk_col, thickness);
			};

			struct part_memo_t {
				std::string name;
				math::vector3 pos;
				math::matrix3 rot;
				math::vector3 size;
				bool ok;
			};
			std::vector<part_memo_t> memo;

			auto get_part_info = [&](const std::string& part_name, math::vector3& out_pos, math::matrix3& out_rot, math::vector3& out_size) -> bool
			{
				for (const auto& m : memo)
				{
					if (m.name == part_name)
					{
						out_pos = m.pos; out_rot = m.rot; out_size = m.size;
						return m.ok;
					}
				}

				bool ok = false;
				auto it = entity.parts.find(part_name);
				if (it != entity.parts.end() && it->second.address != 0)
				{
					rbx::c_primitive prim = it->second.get_primitive();
					if (prim.address != 0)
					{
						out_pos = prim.get_position();
						out_rot = prim.get_rotation();
						out_size = prim.get_size();
						if (out_pos.x != 0.0f || out_pos.y != 0.0f || out_pos.z != 0.0f)
							ok = true;
					}
				}
				if (!ok && bodyparts::get_part_position(entity, part_name, out_pos))
				{
					out_rot = entity.rotation.rotation;
					out_size = math::vector3(1.0f, 2.0f, 1.0f);
					if (part_name == "Head")
					{
						out_size = math::vector3(1.0f, 1.0f, 1.0f);
					}
					ok = (out_pos.x != 0.0f || out_pos.y != 0.0f || out_pos.z != 0.0f);
				}

				memo.push_back({ part_name, out_pos, out_rot, out_size, ok });
				return ok;
			};

			auto project_part_to_screen = [&](const std::string& part_name) -> std::optional<ImVec2>
			{
				math::vector3 pos{}, size{};
				math::matrix3 rot{};
				if (!get_part_info(part_name, pos, rot, size)) return std::nullopt;
				math::vector2 scr{};
				if (!game::visualengine->world_to_screen(view, dims, pos, scr)) return std::nullopt;
				return ImVec2(std::round(scr.x), std::round(scr.y));
			};

			auto project_part_to_screen_offset = [&](const std::string& part_name, const math::vector3& world_offset) -> std::optional<ImVec2>
			{
				math::vector3 pos{}, size{};
				math::matrix3 rot{};
				if (!get_part_info(part_name, pos, rot, size)) return std::nullopt;
				math::vector2 scr{};
				if (!game::visualengine->world_to_screen(view, dims, pos + world_offset, scr)) return std::nullopt;
				return ImVec2(std::round(scr.x), std::round(scr.y));
			};

			auto project_part_to_screen_local_offset = [&](const std::string& part_name, const math::vector3& local_offset) -> std::optional<ImVec2>
			{
				math::vector3 pos{}, size{};
				math::matrix3 rot{};
				if (!get_part_info(part_name, pos, rot, size)) return std::nullopt;
				math::vector3 world_offset = rot * local_offset;
				math::vector2 scr{};
				if (!game::visualengine->world_to_screen(view, dims, pos + world_offset, scr)) return std::nullopt;
				return ImVec2(std::round(scr.x), std::round(scr.y));
			};

			bool is_r15_rig = (entity.parts.count("UpperTorso") > 0 && entity.parts.count("LowerTorso") > 0);

			if (is_r15_rig)
			{
				const auto head = project_part_to_screen("Head");
				math::vector3 pos{}, size{2.0f, 2.0f, 1.0f}; math::matrix3 rot{};
				get_part_info("UpperTorso", pos, rot, size);
				float upper_torso_offset = size.y * 0.1f;
				const auto upper_torso = project_part_to_screen_local_offset("UpperTorso", math::vector3(0.0f, upper_torso_offset, 0.0f));
				const auto lower_torso = project_part_to_screen("LowerTorso");

				const auto left_upper_arm = project_part_to_screen("LeftUpperArm");
				const auto left_lower_arm = project_part_to_screen("LeftLowerArm");
				const auto left_hand = project_part_to_screen("LeftHand");
				const auto right_upper_arm = project_part_to_screen("RightUpperArm");
				const auto right_lower_arm = project_part_to_screen("RightLowerArm");
				const auto right_hand = project_part_to_screen("RightHand");

				const auto left_upper_leg = project_part_to_screen("LeftUpperLeg");
				const auto left_lower_leg = project_part_to_screen("LeftLowerLeg");
				const auto left_foot = project_part_to_screen("LeftFoot");
				const auto right_upper_leg = project_part_to_screen("RightUpperLeg");
				const auto right_lower_leg = project_part_to_screen("RightLowerLeg");
				const auto right_foot = project_part_to_screen("RightFoot");

				add_skeleton_segment(head, upper_torso);
				add_skeleton_segment(upper_torso, lower_torso);

				add_skeleton_segment(upper_torso, left_upper_arm);
				add_skeleton_segment(left_upper_arm, left_lower_arm);
				add_skeleton_segment(left_lower_arm, left_hand);

				add_skeleton_segment(upper_torso, right_upper_arm);
				add_skeleton_segment(right_upper_arm, right_lower_arm);
				add_skeleton_segment(right_lower_arm, right_hand);

				add_skeleton_segment(lower_torso, left_upper_leg);
				add_skeleton_segment(left_upper_leg, left_lower_leg);
				add_skeleton_segment(left_lower_leg, left_foot);

				add_skeleton_segment(lower_torso, right_upper_leg);
				add_skeleton_segment(right_upper_leg, right_lower_leg);
				add_skeleton_segment(right_lower_leg, right_foot);
			}
			else
			{
				const auto head = project_part_to_screen("Head");

				math::vector3 torso_pos{}, torso_size{2.0f, 2.0f, 1.0f}, l_arm_size{1.0f, 2.0f, 1.0f}, l_leg_size{1.0f, 2.0f, 1.0f};
				math::matrix3 torso_rot{};
				get_part_info("Torso", torso_pos, torso_rot, torso_size);

				float torso_top_offset = torso_size.y * 0.275f;
				const auto torso_top = project_part_to_screen_local_offset("Torso", math::vector3(0.0f, torso_top_offset, 0.0f));
				float torso_bottom_offset = -torso_size.y * 0.4f;
				const auto torso_bottom = project_part_to_screen_local_offset("Torso", math::vector3(0.0f, torso_bottom_offset, 0.0f));

				math::vector3 p{}, r_arm_sz{1.0f, 2.0f, 1.0f}; math::matrix3 m{};
				get_part_info("Left Arm", p, m, l_arm_size);
				get_part_info("Right Arm", p, m, r_arm_sz);

				float arm_offset = l_arm_size.y * 0.30f;
				const auto left_arm_top = project_part_to_screen_local_offset("Left Arm", math::vector3(0.0f, arm_offset, 0.0f));
				const auto right_arm_top = project_part_to_screen_local_offset("Right Arm", math::vector3(0.0f, arm_offset, 0.0f));
				const auto left_arm_bottom = project_part_to_screen_local_offset("Left Arm", math::vector3(0.0f, -arm_offset, 0.0f));
				const auto right_arm_bottom = project_part_to_screen_local_offset("Right Arm", math::vector3(0.0f, -arm_offset, 0.0f));

				math::vector3 r_leg_sz{1.0f, 2.0f, 1.0f};
				get_part_info("Left Leg", p, m, l_leg_size);
				get_part_info("Right Leg", p, m, r_leg_sz);

				float leg_offset = l_leg_size.y * 0.35f;
				const auto left_leg_top = project_part_to_screen_local_offset("Left Leg", math::vector3(0.0f, leg_offset, 0.0f));
				const auto right_leg_top = project_part_to_screen_local_offset("Right Leg", math::vector3(0.0f, leg_offset, 0.0f));
				const auto left_leg_bottom = project_part_to_screen_local_offset("Left Leg", math::vector3(0.0f, -leg_offset, 0.0f));
				const auto right_leg_bottom = project_part_to_screen_local_offset("Right Leg", math::vector3(0.0f, -leg_offset, 0.0f));

				add_skeleton_segment(head, torso_top);
				add_skeleton_segment(torso_top, torso_bottom);

				add_skeleton_segment(torso_top, left_arm_top);
				add_skeleton_segment(torso_top, right_arm_top);

				add_skeleton_segment(left_arm_top, left_arm_bottom);
				add_skeleton_segment(right_arm_top, right_arm_bottom);

				add_skeleton_segment(torso_bottom, left_leg_top);
				add_skeleton_segment(torso_bottom, right_leg_top);

				add_skeleton_segment(left_leg_top, left_leg_bottom);
				add_skeleton_segment(right_leg_top, right_leg_bottom);
			}
		}

		if (want_chams)
		{
			if (priority_settings->chams_type == 2)
			{
				settings::visuals::engine_chams = true;
			}
			else
			{
				ImDrawList* cham_draw = ImGui::GetBackgroundDrawList();
				float cham_col[4];
				chams_utils::get_chams_color(entity, *priority_settings, cham_col);
				ImU32 cham_fill_col = IM_COL32(
					static_cast<int>(cham_col[0] * 255.f),
					static_cast<int>(cham_col[1] * 255.f),
					static_cast<int>(cham_col[2] * 255.f),
					static_cast<int>(cham_col[3] * transparency)
				);
				ImU32 cham_outline_col = IM_COL32(
					static_cast<int>(priority_settings->chams_outline_colour[0] * 255.f),
					static_cast<int>(priority_settings->chams_outline_colour[1] * 255.f),
					static_cast<int>(priority_settings->chams_outline_colour[2] * 255.f),
					static_cast<int>(priority_settings->chams_outline_colour[3] * transparency)
				);

				if (priority_settings->chams_type == 0)
				{
					ImU32 cham_glow_col = IM_COL32(
						static_cast<int>(priority_settings->chams_glow_colour[0] * 255.f),
						static_cast<int>(priority_settings->chams_glow_colour[1] * 255.f),
						static_cast<int>(priority_settings->chams_glow_colour[2] * 255.f),
						static_cast<int>(priority_settings->chams_glow_colour[3] * transparency)
					);
					render_clipper_chams(
						cham_draw,
						projected_parts,
						cham_fill_col,
						cham_outline_col,
						priority_settings->mesh_outline ? priority_settings->mesh_outline_thickness : 0.0f,
						settings::visuals::chams_thickness,
						priority_settings->chams_outline_glow,
						cham_glow_col,
						priority_settings->chams_glow_radius,
						priority_settings->chams_glow_intensity
					);
				}
				// chams_type == 1 (Memory Mesh): handled by GPU pipeline in render.cpp — skip 2D software path
			}
		}

		if (entity.priority == cache::player_priority::hostile && settings::visuals::enemy_highlight && !projected_parts.empty())
		{
			ImDrawList* hl_draw = ImGui::GetBackgroundDrawList();
			ImU32 hl_col = IM_COL32(static_cast<int>(settings::visuals::enemy_highlight_colour[0] * 255.f), static_cast<int>(settings::visuals::enemy_highlight_colour[1] * 255.f), static_cast<int>(settings::visuals::enemy_highlight_colour[2] * 255.f), static_cast<int>(settings::visuals::enemy_highlight_colour[3] * transparency));
			render_clipper_chams(hl_draw, projected_parts, hl_col, hl_col, 1.5f, 1.2f);
		}
		else if (entity.priority == cache::player_priority::friendly && settings::visuals::friendly_highlight && !projected_parts.empty())
		{
			ImDrawList* hl_draw = ImGui::GetBackgroundDrawList();
			ImU32 hl_col = IM_COL32(static_cast<int>(settings::visuals::friendly_highlight_colour[0] * 255.f), static_cast<int>(settings::visuals::friendly_highlight_colour[1] * 255.f), static_cast<int>(settings::visuals::friendly_highlight_colour[2] * 255.f), static_cast<int>(settings::visuals::friendly_highlight_colour[3] * transparency));
			render_clipper_chams(hl_draw, projected_parts, hl_col, hl_col, 1.5f, 1.2f);
		}

		if (want_head_dot)
		{
			if (entity.head_part.address != 0)
			{
				math::vector3 head_world = entity.part_positions.head;
				math::vector2 head_scr;
				if (game::visualengine->world_to_screen(view, dims, head_world, head_scr)
					&& head_scr.x > 0.f && head_scr.y > 0.f)
				{
					ImDrawList* dot_draw = ImGui::GetBackgroundDrawList();
					float r = priority_settings->head_dot_size;
					ImVec2 center(head_scr.x, head_scr.y);

					ImU32 dot_col = IM_COL32(
						priority_settings->head_dot_colour[0] * 255.f,
						priority_settings->head_dot_colour[1] * 255.f,
						priority_settings->head_dot_colour[2] * 255.f,
						priority_settings->head_dot_colour[3] * transparency);
					ImU32 outline_col = IM_COL32(
						priority_settings->head_dot_outline_colour[0] * 255.f,
						priority_settings->head_dot_outline_colour[1] * 255.f,
						priority_settings->head_dot_outline_colour[2] * 255.f,
						priority_settings->head_dot_outline_colour[3] * transparency);

					if (settings::visuals::render_outlines[7]) // index 7 = Head Dot
					{
						dot_draw->AddCircleFilled(center, r + 2.f, outline_col, 32);
					}
					dot_draw->AddCircleFilled(center, r,        dot_col,     32);

					dot_draw->AddCircleFilled(center, r * 0.35f, IM_COL32(255, 255, 255, static_cast<int>(130.f * priority_settings->head_dot_colour[3])), 16);
				}
			}
		}

		if (want_hitbox && has_hrp_prim && hrp_size.x > 0.f && hrp_size.y > 0.f && hrp_size.z > 0.f)
		{
			ImDrawList* hitbox_draw = ImGui::GetBackgroundDrawList();

			std::vector<ImVec2> projected;
			projected.reserve(8);

			for (math::vector3& lc : local_corners)
			{
				math::vector3 world = hrp_pos + hrp_rot * math::vector3
				{
					lc.x * hrp_size.x * 0.5f,
					lc.y * hrp_size.y * 0.5f,
					lc.z * hrp_size.z * 0.5f
				};

				math::vector2 out;
				if (game::visualengine->world_to_screen(view, dims, world, out))
				{
					projected.push_back(ImVec2(out.x, out.y));
				}
			}

			auto hull = esp::generate_hull(projected.data(), (int)projected.size());
			if (!hull.empty())
			{
				ImU32 hitbox_fill_col = IM_COL32(
					static_cast<int>(settings::hitboxexpander::hitbox_colour[0] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_colour[1] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_colour[2] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_colour[3] * transparency)
				);

				ImU32 hitbox_outline_col = IM_COL32(
					static_cast<int>(settings::hitboxexpander::hitbox_outline_colour[0] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_outline_colour[1] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_outline_colour[2] * 255.f),
					static_cast<int>(settings::hitboxexpander::hitbox_outline_colour[3] * transparency)
				);

				hitbox_draw->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), hitbox_fill_col);
				hitbox_draw->AddPolyline(hull.data(), static_cast<int>(hull.size()), hitbox_outline_col, ImDrawFlags_Closed, 1.0f);
			}
		}

		if (want_offscreen_arrows && has_hrp_prim)
		{
			math::vector2 screen_pos;
			bool on_screen = game::visualengine->world_to_screen(view, dims, hrp_pos, screen_pos);
			if (!on_screen || screen_pos.x < 0.f || screen_pos.x > dims.x || screen_pos.y < 0.f || screen_pos.y > dims.y)
			{
				ImVec2 center(dims.x * 0.5f, dims.y * 0.5f);
				
				math::vector3 delta = (hrp_pos - camera_pos).normalized();

				float angle = atan2f(delta.z, delta.x);
				float radius = priority_settings->offscreen_arrows_radius;
				float size = priority_settings->offscreen_arrows_size;

				ImVec2 dir(cosf(angle), sinf(angle));
				ImVec2 p1(center.x + dir.x * radius, center.y + dir.y * radius);
				ImVec2 p2(p1.x - (dir.x + dir.y) * size, p1.y - (dir.y - dir.x) * size);
				ImVec2 p3(p1.x - (dir.x - dir.y) * size, p1.y - (dir.y + dir.x) * size);

				ImU32 arrow_col = IM_COL32(
					static_cast<int>(priority_settings->offscreen_arrows_colour[0] * 255.f),
					static_cast<int>(priority_settings->offscreen_arrows_colour[1] * 255.f),
					static_cast<int>(priority_settings->offscreen_arrows_colour[2] * 255.f),
					static_cast<int>(priority_settings->offscreen_arrows_colour[3] * transparency)
				);

				draw->AddTriangleFilled(p1, p2, p3, arrow_col);
				draw->AddTriangle(p1, p2, p3, IM_COL32(0, 0, 0, 255), 1.0f);
			}
		}

		if (want_movement_trails && has_hrp_prim)
		{
			auto& points = s_trail_history[entity.instance.address];
			float cur_time = static_cast<float>(ImGui::GetTime());

			// Remove expired points older than 1.1 seconds
			while (!points.empty() && (cur_time - points.front().time > 1.1f))
			{
				points.erase(points.begin());
			}

			// Add new sample if moved enough
			if (points.empty() || (hrp_pos - points.back().pos).length() > 0.15f)
			{
				points.push_back({ hrp_pos, cur_time });
				if (points.size() > 60)
				{
					points.erase(points.begin());
				}
			}

			// Build control points list including the live hrp_pos at head for zero latency
			std::vector<trail_point_t> ctrl_pts = points;
			if (ctrl_pts.empty() || (hrp_pos - ctrl_pts.back().pos).length() > 0.05f)
			{
				ctrl_pts.push_back({ hrp_pos, cur_time });
			}

			if (ctrl_pts.size() >= 2)
			{
				ImU32 base_col = IM_COL32(
					static_cast<int>(priority_settings->movement_trails_colour[0] * 255.f),
					static_cast<int>(priority_settings->movement_trails_colour[1] * 255.f),
					static_cast<int>(priority_settings->movement_trails_colour[2] * 255.f),
					static_cast<int>(priority_settings->movement_trails_colour[3] * transparency)
				);

				std::vector<std::pair<math::vector3, float>> smooth_pts;
				int n_pts = static_cast<int>(ctrl_pts.size());
				smooth_pts.reserve(n_pts * 6);

				for (int i = 0; i < n_pts - 1; ++i)
				{
					math::vector3 p0 = (i > 0) ? ctrl_pts[i - 1].pos : ctrl_pts[i].pos;
					math::vector3 p1 = ctrl_pts[i].pos;
					math::vector3 p2 = ctrl_pts[i + 1].pos;
					math::vector3 p3 = (i + 2 < n_pts) ? ctrl_pts[i + 2].pos : p2;

					float t0_time = ctrl_pts[i].time;
					float t1_time = ctrl_pts[i + 1].time;

					int subdivisions = 5;
					for (int step = 0; step < subdivisions; ++step)
					{
						float t = static_cast<float>(step) / static_cast<float>(subdivisions);
						float t2 = t * t;
						float t3 = t2 * t;

						math::vector3 pt = (p1 * 2.0f + 
							(p2 - p0) * t + 
							(p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + 
							(p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;

						float pt_time = t0_time + (t1_time - t0_time) * t;
						float age = cur_time - pt_time;
						float frac = (std::clamp)(1.0f - (age / 1.1f), 0.0f, 1.0f);

						smooth_pts.push_back({ pt, frac });
					}
				}
				float final_age = cur_time - ctrl_pts.back().time;
				float final_frac = (std::clamp)(1.0f - (final_age / 1.1f), 0.0f, 1.0f);
				smooth_pts.push_back({ ctrl_pts.back().pos, final_frac });

				struct screen_pt_t {
					ImVec2 pt;
					float alpha;
					bool valid;
				};
				std::vector<screen_pt_t> screen_pts;
				screen_pts.reserve(smooth_pts.size());

				for (const auto& sp : smooth_pts)
				{
					math::vector2 scr;
					bool visible = game::visualengine->world_to_screen(view, dims, sp.first, scr);
					screen_pts.push_back({ ImVec2(scr.x, scr.y), sp.second, visible });
				}

				for (size_t i = 0; i + 1 < screen_pts.size(); ++i)
				{
					if (screen_pts[i].valid && screen_pts[i + 1].valid)
					{
						float a = screen_pts[i + 1].alpha;
						if (a <= 0.01f) continue;

						ImVec4 c = ImGui::ColorConvertU32ToFloat4(base_col);
						c.w *= (a * a); // quadratic fade for ultra smooth tail

						float thickness = 1.0f + 2.2f * a; // smooth taper from 1.0px at tail to 3.2px at head

						draw->AddLine(screen_pts[i].pt, screen_pts[i + 1].pt, ImGui::ColorConvertFloat4ToU32(c), thickness);
					}
				}
			}
		}

		if (want_view_lines)
		{
			auto head_it = entity.parts.find("Head");
			if (head_it != entity.parts.end() && head_it->second.address != 0)
			{
				if (entity.head_part.address != 0)
				{
					math::vector3 head_pos = entity.part_positions.head;
					math::matrix3 head_rot = entity.rotation.rotation;

					math::vector3 forward = {
						-head_rot.m[0][2],
						-head_rot.m[1][2],
						-head_rot.m[2][2]
					};

					math::vector3 end_pos = head_pos + (forward * 12.0f);
					math::vector2 start_scr, end_scr;
					if (game::visualengine->world_to_screen(view, dims, head_pos, start_scr) &&
						game::visualengine->world_to_screen(view, dims, end_pos, end_scr))
					{
						ImU32 view_col = IM_COL32(
							static_cast<int>(priority_settings->view_angle_lines_colour[0] * 255.f),
							static_cast<int>(priority_settings->view_angle_lines_colour[1] * 255.f),
							static_cast<int>(priority_settings->view_angle_lines_colour[2] * 255.f),
							static_cast<int>(priority_settings->view_angle_lines_colour[3] * transparency)
						);
						draw->AddLine(ImVec2(start_scr.x, start_scr.y), ImVec2(end_scr.x, end_scr.y), IM_COL32(0, 0, 0, 255), 3.0f);
						draw->AddLine(ImVec2(start_scr.x, start_scr.y), ImVec2(end_scr.x, end_scr.y), view_col, 1.5f);
					}
				}
			}
		}

		if (want_sound_esp && has_hrp_prim)
		{
			float current_time = static_cast<float>(ImGui::GetTime());
			auto& events = s_sound_events[entity.instance.address];

			events.erase(std::remove_if(events.begin(), events.end(), [current_time](const auto& event) {
				return (current_time - event.second) > 2.0f;
			}), events.end());

			if (s_last_positions.count(entity.instance.address))
			{
				float dist = (hrp_pos - s_last_positions[entity.instance.address]).length();
				if (dist > 0.5f && (current_time - s_last_step_time[entity.instance.address] > 0.4f))
				{
					events.push_back({ hrp_pos, current_time });
					s_last_step_time[entity.instance.address] = current_time;
				}
			}
			s_last_positions[entity.instance.address] = hrp_pos;

			ImU32 base_col = IM_COL32(
				static_cast<int>(priority_settings->sound_esp_colour[0] * 255.f),
				static_cast<int>(priority_settings->sound_esp_colour[1] * 255.f),
				static_cast<int>(priority_settings->sound_esp_colour[2] * 255.f),
				255
			);

			for (const auto& event : events)
			{
				math::vector2 center_scr;
				if (!game::visualengine->world_to_screen(view, dims, event.first, center_scr))
					continue;

				float life = (current_time - event.second) / 2.0f;
				float radius = life * priority_settings->sound_esp_radius * 10.0f;
				ImU32 alpha = static_cast<ImU32>((1.0f - life) * 255.0f);
				ImU32 final_color = (base_col & 0x00FFFFFF) | (alpha << 24);

				const int segments = 16;
				std::vector<ImVec2> points;
				points.reserve(segments);
				for (int i = 0; i < segments; i++)
				{
					float a = (i / static_cast<float>(segments)) * 6.2831853f;
					math::vector3 p_world = { event.first.x + cosf(a) * radius, event.first.y - 3.0f, event.first.z + sinf(a) * radius };
					math::vector2 p_screen;
					if (game::visualengine->world_to_screen(view, dims, p_world, p_screen))
					{
						points.push_back(ImVec2(p_screen.x, p_screen.y));
					}
				}

				if (points.size() > 2)
				{
					draw->AddPolyline(points.data(), static_cast<int>(points.size()), final_color, ImDrawFlags_Closed, 1.5f);
				}
			}
		}

		if (want_footprints && has_hrp_prim)
		{
			float current_time = static_cast<float>(ImGui::GetTime());
			if (s_fp_last_pos.count(entity.instance.address))
			{
				float dist = (hrp_pos - s_fp_last_pos[entity.instance.address]).length();
				if (dist > 1.5f && (current_time - s_fp_last_step[entity.instance.address] > 0.3f))
				{
					bool side = !s_fp_last_side[entity.instance.address];
					s_fp_last_side[entity.instance.address] = side;

					math::vector3 forward = (hrp_pos - s_fp_last_pos[entity.instance.address]).normalized();
					if (forward.length() < 0.1f) forward = { 0, 0, 1 };

					s_footprint_events[entity.instance.address].push_back({ hrp_pos, current_time, side, forward });
					s_fp_last_step[entity.instance.address] = current_time;
				}
			}
			s_fp_last_pos[entity.instance.address] = hrp_pos;

			auto& events = s_footprint_events[entity.instance.address];
			events.erase(std::remove_if(events.begin(), events.end(), [current_time](const footprint_t& e) {
				return (current_time - e.time) > 4.0f;
			}), events.end());

			ImU32 base_col = IM_COL32(
				static_cast<int>(priority_settings->footprints_colour[0] * 255.f),
				static_cast<int>(priority_settings->footprints_colour[1] * 255.f),
				static_cast<int>(priority_settings->footprints_colour[2] * 255.f),
				255
			);

			for (const auto& event : events)
			{
				float life = (current_time - event.time) / 4.0f;
				ImU32 alpha = static_cast<ImU32>((1.0f - life) * 255.0f);
				ImU32 final_color = (base_col & 0x00FFFFFF) | (alpha << 24);

				math::vector3 right = event.forward.cross({ 0, 1, 0 }).normalized();
				float offset_dist = 0.6f;
				math::vector3 foot_pos = event.position + (event.is_left ? right * -offset_dist : right * offset_dist);
				foot_pos.y -= 3.0f;

				const int segments = 8;
				std::vector<ImVec2> points;
				float f_radius_x = 0.3f * priority_settings->footprints_radius;
				float f_radius_z = 0.5f * priority_settings->footprints_radius;

				for (int i = 0; i < segments; i++)
				{
					float a = (i / static_cast<float>(segments)) * 6.2831853f;
					math::vector3 p_local = { cosf(a) * f_radius_x, 0, sinf(a) * f_radius_z };
					math::vector3 p_rot;
					p_rot.x = p_local.x * right.x + p_local.z * event.forward.x;
					p_rot.y = 0;
					p_rot.z = p_local.x * right.z + p_local.z * event.forward.z;

					math::vector2 p_screen;
					if (game::visualengine->world_to_screen(view, dims, foot_pos + p_rot, p_screen))
					{
						points.push_back(ImVec2(p_screen.x, p_screen.y));
					}
				}

				if (points.size() > 2)
				{
					draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), final_color);
				}
			}
		}

			if (want_corpse)
			{
				ImU32 corpse_col = IM_COL32(
					priority_settings->corpse_colour[0] * 255.f,
					priority_settings->corpse_colour[1] * 255.f,
					priority_settings->corpse_colour[2] * 255.f,
					priority_settings->corpse_colour[3] * transparency
				);
				esp::outline(c1, c2, corpse_col);
			}

			if (want_corpse_names)
			{
				const char* name_text = entity.display_name.empty() ? entity.name.c_str() : entity.display_name.c_str();
				float font_size = 0.0f;
				ImFont* font = get_esp_font(font_size);
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, name_text).x;

				ImU32 name_col = IM_COL32(
					priority_settings->corpse_names_colour[0] * 255.f,
					priority_settings->corpse_names_colour[1] * 255.f,
					priority_settings->corpse_names_colour[2] * 255.f,
					priority_settings->corpse_names_colour[3] * transparency
				);

				esp::draw_text_outlined(draw, font, font_size,
					ImVec2(box_center_x - text_width * 0.5f, box_top - font_size - 5.f),
					name_col,
					name_text);
			}
		}

	for (auto it = s_anim_hp.begin(); it != s_anim_hp.end();)
	{
		if (drawn_entities.find(it->first) == drawn_entities.end())
			it = s_anim_hp.erase(it);
		else
			++it;
	}

	auto clean_addr_map = [&](auto& m) {
		for (auto it = m.begin(); it != m.end();)
		{
			if (drawn_addresses.find(it->first) == drawn_addresses.end())
				it = m.erase(it);
			else
				++it;
		}
	};

	clean_addr_map(s_trail_history);
	clean_addr_map(s_sound_events);
	clean_addr_map(s_last_positions);
	clean_addr_map(s_last_step_time);
	clean_addr_map(s_footprint_events);
	clean_addr_map(s_fp_last_pos);
	clean_addr_map(s_fp_last_side);
	clean_addr_map(s_fp_last_step);

	if (settings::mm2::coin_esp && game::datamodel->is_mm2()) {
		uint64_t workspace = game::datamodel->find_first_child_by_class("Workspace");
		if (workspace) {
			rbx::c_instance ws_inst(workspace);
			for (const auto& ws_child : ws_inst.get_children()) {
				rbx::c_instance child_inst(ws_child);
				uint64_t coin_container = child_inst.find_first_child("CoinContainer");
				if (!coin_container) continue;

				rbx::c_instance coin_container_inst(coin_container);
				for (const auto& coin : coin_container_inst.get_children()) {
					rbx::c_instance coin_inst(coin);
					uint64_t part = memory->read<uint64_t>(coin + Offsets::Model::PrimaryPart);
					if (!part) {
						for (const auto& coin_child : coin_inst.get_children()) {
							rbx::c_instance cc_inst(coin_child);
							const std::string cls = cc_inst.get_class_name();
							if (cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" || cls == "BasePart") {
								part = coin_child;
								break;
							}
						}
					}
					if (!part) {
						const std::string cls = coin_inst.get_class_name();
						if (cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" || cls == "BasePart")
							part = coin;
					}
					if (!part) continue;

					const uint64_t prim = memory->read<uint64_t>(part + Offsets::BasePart::Primitive);
					if (!prim) continue;

					const math::vector3 pos = memory->read<math::vector3>(prim + Offsets::Primitive::Position);
					math::vector2 scr;
					if (!game::visualengine->world_to_screen(view, dims, pos + math::vector3(0.0f, 1.5f, 0.0f), scr))
						continue;

					const char* lbl = "Coin";
					float f_sz = 0.0f;
					ImFont* esp_font = get_esp_font(f_sz);
					const ImVec2 tsz = esp_font->CalcTextSizeA(f_sz, FLT_MAX, 0.f, lbl);
					const ImVec2 tpos = ImVec2(scr.x - tsz.x / 2.0f, scr.y);
					esp::draw_text_outlined(draw, esp_font, f_sz, tpos, IM_COL32(255, 220, 0, 255), lbl, nullptr, true);
				}
			}
		}
	}

	// Restore draw flags so UI/other draw lists are not affected
	draw->Flags = saved_flags;
	}
	catch (...)
	{
	}
}