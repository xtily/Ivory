#pragma once

#include <cmath>
#include <algorithm>
#include <imgui/imgui.h>
#include <sdk/cache/core/cache.h>
#include <features/system/settings/settings.h>

#include "gpu/meshgpu.h"
#include "mesh/memorymesh.h"
#include "mesh/assetmesh.h"
#include "mesh/avatarmesh.h"
#include "engine_chams.h"

namespace chams_utils {

inline void get_chams_color(const cache::entity_t& entity, const settings::visuals::priority_settings_t& cfg, float out_col[4])
{
	float r = cfg.chams_colour[0];
	float g = cfg.chams_colour[1];
	float b = cfg.chams_colour[2];
	float a = cfg.chams_colour[3];

	if (cfg.chams_health_based)
	{
		float hp_ratio = 1.0f;
		if (entity.max_health > 0.0f)
		{
			hp_ratio = std::clamp(entity.health / entity.max_health, 0.0f, 1.0f);
		}
		if (hp_ratio > 0.5f)
		{
			float t = (hp_ratio - 0.5f) * 2.0f;
			r = cfg.healthbar_colour_mid[0] * (1.0f - t) + cfg.healthbar_colour[0] * t;
			g = cfg.healthbar_colour_mid[1] * (1.0f - t) + cfg.healthbar_colour[1] * t;
			b = cfg.healthbar_colour_mid[2] * (1.0f - t) + cfg.healthbar_colour[2] * t;
		}
		else
		{
			float t = hp_ratio * 2.0f;
			r = cfg.healthbar_colour_low[0] * (1.0f - t) + cfg.healthbar_colour_mid[0] * t;
			g = cfg.healthbar_colour_low[1] * (1.0f - t) + cfg.healthbar_colour_mid[1] * t;
			b = cfg.healthbar_colour_low[2] * (1.0f - t) + cfg.healthbar_colour_mid[2] * t;
		}
	}

	if (cfg.chams_flash)
	{
		float time = static_cast<float>(ImGui::GetTime());
		float speed = (cfg.chams_flash_speed > 0.01f) ? cfg.chams_flash_speed * 4.0f : 4.0f;
		float t = 0.5f + 0.5f * std::sin(time * speed);
		r = r * (1.0f - t) + cfg.chams_flash_colour[0] * t;
		g = g * (1.0f - t) + cfg.chams_flash_colour[1] * t;
		b = b * (1.0f - t) + cfg.chams_flash_colour[2] * t;
	}

	if (cfg.chams_fade)
	{
		float time = static_cast<float>(ImGui::GetTime());
		float speed = (cfg.chams_fade_speed > 0.01f) ? cfg.chams_fade_speed * 3.0f : 3.0f;
		float t = 0.2f + 0.8f * (0.5f + 0.5f * std::sin(time * speed));
		a *= t;
	}

	out_col[0] = r;
	out_col[1] = g;
	out_col[2] = b;
	out_col[3] = a;
}

} // namespace chams_utils