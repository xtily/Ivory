#pragma once
#include "lighting.h"
#include <sdk/sdk.h>
#include <sdk/offsets/offsets.h>
#include <sdk/offsets/rva.h>
#include <core/memory/memory.h>
#include <core/utility/creator/creator.h>
#include <features/lua/vm/LuaVM.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

inline auto _world_get_render_view() -> std::uint64_t {
	auto mod_base = memory->get_module_address();
	if (!mod_base)
		return 0;

	uintptr_t base = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
	if (!base || base < 0x10000 || base > 0x7FFFFFFFFFFFull)
		base = mod_base;

	auto visual_engine = memory->read<std::uint64_t>(base + RVA::VisualEngine::Pointer);
	if (!visual_engine)
		return 0;

	return memory->read<std::uint64_t>(visual_engine + Offsets::VisualEngine::RenderView);
}

inline auto _world_force_lighting_dirty() -> void {
	auto render_view = _world_get_render_view();
	if (!render_view)
		return;

	memory->write<bool>(render_view + Offsets::RenderView::LightingValid, false);
	memory->write<bool>(render_view + Offsets::RenderView::SkyValid, false);
}

inline constexpr float _world_pi = 3.1415927f;
inline constexpr float _world_hour = 3600.0f;
inline constexpr float _world_day = 86400.0f;
inline constexpr float _world_sunrise = 6.0f * _world_hour;
inline constexpr float _world_sunset = 18.0f * _world_hour;
inline constexpr float _world_rise_set = _world_hour;
inline constexpr float _world_solar_year = 365.2564f * _world_day;
inline constexpr float _world_half_solar = 182.6282f;

inline auto _world_deg_to_rad(float degrees) -> float {
	return degrees * (_world_pi / 180.0f);
}

inline auto _world_spline(float t, const float* times, const math::vector3* colors, int count) -> math::vector3 {
	if (count <= 0)
		return { 1.0f, 1.0f, 1.0f };
	if (t <= times[0])
		return colors[0];
	if (t >= times[count - 1])
		return colors[count - 1];

	for (int i = 0; i < count - 1; i++) {
		if (t < times[i] || t > times[i + 1])
			continue;

		auto span = times[i + 1] - times[i];
		auto a = span > 0.0f ? (t - times[i]) / span : 0.0f;
		return colors[i] * (1.0f - a) + colors[i + 1] * a;
	}

	return colors[0];
}

inline auto _world_sky_ambient(float t) -> math::vector3 {
	static const float times[] = {
		0.0f, _world_sunrise - 2.0f * _world_hour, _world_sunrise - _world_hour, _world_sunrise - _world_hour * 0.5f,
		_world_sunrise, _world_sunrise + _world_rise_set, _world_sunset - _world_rise_set, _world_sunset,
		_world_sunset + _world_hour / 3.0f, _world_day
	};
	static const math::vector3 colors[] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.07f, 0.07f, 0.1f }, { 0.2f, 0.15f, 0.01f },
		{ 0.2f, 0.15f, 0.01f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.4f, 0.2f, 0.05f },
		{ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
	};
	return _world_spline(t, times, colors, 10);
}

inline auto _world_sky_ambient2(float t) -> math::vector3 {
	static const float times[] = {
		0.0f, _world_sunrise - 3.0f * _world_hour, _world_sunrise - 2.0f * _world_hour, _world_sunrise - _world_hour * 0.5f,
		_world_sunrise, _world_sunrise + _world_rise_set, _world_sunset - _world_rise_set, _world_sunset,
		_world_sunset + _world_hour / 3.0f, _world_sunset + 2.0f * _world_hour, _world_sunset + 3.0f * _world_hour, _world_day
	};
	static const math::vector3 colors[] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.21f, 0.21f, 0.28f }, { 0.4f, 0.3f, 0.3f },
		{ 0.3f, 0.2f, 0.3f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.4f, 0.3f, 0.2f },
		{ 0.3f, 0.2f, 0.3f }, { 0.3f, 0.2f, 0.3f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
	};
	return _world_spline(t, times, colors, 12);
}

inline auto _world_light_color_for_time(float t) -> math::vector3 {
	constexpr math::vector3 day{ 0.75f, 0.75f, 0.75f };
	static const float times[] = {
		0.0f, _world_sunrise - _world_hour, _world_sunrise, _world_sunrise + _world_rise_set * 0.25f,
		_world_sunrise + _world_rise_set, _world_sunset - _world_rise_set, _world_sunset - _world_rise_set * 0.5f,
		_world_sunset, _world_sunset + _world_hour * 0.5f, _world_day
	};
	static const math::vector3 colors[] = {
		{ 0.2f, 0.2f, 0.2f }, { 0.1f, 0.1f, 0.1f }, { 0.0f, 0.0f, 0.0f }, { 0.6f, 0.6f, 0.0f },
		day, day, { 0.1f, 0.1f, 0.075f }, { 0.1f, 0.05f, 0.05f }, { 0.1f, 0.1f, 0.1f }, { 0.2f, 0.2f, 0.2f }
	};
	return _world_spline(t, times, colors, 10);
}

inline auto _world_rotate_axis_angle(const math::vector3& v, const math::vector3& axis, float angle) -> math::vector3 {
	auto cos_a = std::cos(angle);
	auto sin_a = std::sin(angle);
	return v * cos_a + axis.cross(v) * sin_a + axis * (axis.dot(v) * (1.0f - cos_a));
}

inline auto _world_true_sun_position(float source_angle, float abs_seconds, float latitude_deg) -> math::vector3 {
	math::vector3 sun{ std::sin(source_angle), -std::cos(source_angle), 0.0f };
	auto day_of_year = (abs_seconds - std::floor(abs_seconds / _world_solar_year) * _world_solar_year) / _world_day;
	auto sun_offset = -_world_deg_to_rad(23.5f) * std::cos(_world_pi * (day_of_year - _world_half_solar) / _world_half_solar) - _world_deg_to_rad(latitude_deg);
	auto axis = math::vector3{ 0.0f, 0.0f, 1.0f }.cross(sun).unit();
	return _world_rotate_axis_angle(sun, axis, sun_offset);
}

inline auto _world_hours_to_clock_us(float hours) -> std::int64_t {
	const auto clamped = std::clamp(hours, 0.0f, 24.0f);
	const auto seconds = static_cast<std::int64_t>(clamped * 3600.0f + 0.5f);
	return seconds * 1'000'000LL;
}

inline auto _world_clock_us_to_hours(std::int64_t us) -> float {
	const auto seconds = static_cast<float>(us / 1'000'000LL);
	return seconds / 3600.0f;
}

inline auto _world_apply_clock_time(std::uint64_t lighting_address, float hours) -> void {
	memory->write<std::int64_t>(lighting_address + Offsets::Lighting::ClockTime, _world_hours_to_clock_us(hours));

	auto seconds = hours * _world_hour;
	auto time_of_day = seconds - std::floor(seconds / _world_day) * _world_day;
	auto source_angle = (time_of_day * 2.0f * _world_pi) / _world_day;
	auto latitude = memory->read<float>(lighting_address + Offsets::Lighting::GeographicLatitude);

	auto sun = _world_true_sun_position(source_angle, seconds, latitude);
	auto moon = -sun;
	auto use_sun = sun.y > -0.3f;
	auto light_direction = use_sun ? sun : moon;
	std::uint32_t source = use_sun ? 0u : 1u;

	memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientTop, _world_sky_ambient(time_of_day));
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientBottom, _world_sky_ambient2(time_of_day));
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::LightColor, _world_light_color_for_time(time_of_day));
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::SunPosition, sun);
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::MoonPosition, moon);
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::LightDirection, light_direction);
	memory->write<std::uint32_t>(lighting_address + Offsets::Lighting::Source, source);
}

inline auto _world_to_color3(const float rgb[3]) -> cheat::world_color3_t {
	return cheat::world_color3_t{
		std::clamp(rgb[0], 0.0f, 1.0f),
		std::clamp(rgb[1], 0.0f, 1.0f),
		std::clamp(rgb[2], 0.0f, 1.0f)
	};
}

inline auto _world_capture_lighting(std::uint64_t address) -> cheat::world_lighting_backup_t {
	cheat::world_lighting_backup_t backup{};
	backup.address = address;
	backup.clock_time = _world_clock_us_to_hours(memory->read<std::int64_t>(address + Offsets::Lighting::ClockTime));
	backup.source = memory->read<std::uint32_t>(address + Offsets::Lighting::Source);
	backup.light_direction = memory->read<math::vector3>(address + Offsets::Lighting::LightDirection);
	backup.light_color = memory->read<math::vector3>(address + Offsets::Lighting::LightColor);
	backup.sun_position = memory->read<math::vector3>(address + Offsets::Lighting::SunPosition);
	backup.moon_position = memory->read<math::vector3>(address + Offsets::Lighting::MoonPosition);
	backup.gradient_top = memory->read<math::vector3>(address + Offsets::Lighting::GradientTop);
	backup.gradient_bottom = memory->read<math::vector3>(address + Offsets::Lighting::GradientBottom);
	backup.ambient = memory->read<cheat::world_color3_t>(address + Offsets::Lighting::Ambient);
	backup.outdoor_ambient = memory->read<cheat::world_color3_t>(address + Offsets::Lighting::OutdoorAmbient);
	backup.color_shift_top = memory->read<cheat::world_color3_t>(address + Offsets::Lighting::ColorShift_Top);
	backup.color_shift_bottom = memory->read<cheat::world_color3_t>(address + Offsets::Lighting::ColorShift_Bottom);
	backup.fog_start = memory->read<float>(address + Offsets::Lighting::FogStart);
	backup.fog_end = memory->read<float>(address + Offsets::Lighting::FogEnd);
	backup.fog_color = memory->read<cheat::world_color3_t>(address + Offsets::Lighting::FogColor);
	backup.exposure = memory->read<float>(address + Offsets::Lighting::ExposureCompensation);
	backup.shadows = memory->read<bool>(address + Offsets::Lighting::GlobalShadows);
	backup.captured = true;
	return backup;
}

inline auto _world_restore_clock_params(const cheat::world_lighting_backup_t& backup) -> void {
	if (!backup.captured)
		return;

	memory->write<std::int64_t>(backup.address + Offsets::Lighting::ClockTime, _world_hours_to_clock_us(backup.clock_time));
	memory->write<std::uint32_t>(backup.address + Offsets::Lighting::Source, backup.source);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::LightDirection, backup.light_direction);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::LightColor, backup.light_color);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::SunPosition, backup.sun_position);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::MoonPosition, backup.moon_position);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::GradientTop, backup.gradient_top);
	memory->write<math::vector3>(backup.address + Offsets::Lighting::GradientBottom, backup.gradient_bottom);
}

inline auto _world_find_child(std::uint64_t parent, const char* class_name) -> std::uint64_t {
	if (!parent)
		return 0;

	rbx::c_instance instance(parent);
	return instance.find_first_child_by_class(class_name);
}

inline auto _world_status_for(const char* class_name) -> cheat::world_create_status_t* {
	if (!class_name || !cheat::world_state)
		return nullptr;
	if (std::strcmp(class_name, "Atmosphere") == 0)
		return &cheat::world_state->atmosphere_create;
	if (std::strcmp(class_name, "BloomEffect") == 0)
		return &cheat::world_state->bloom_create;
	if (std::strcmp(class_name, "BlurEffect") == 0)
		return &cheat::world_state->blur_create;
	if (std::strcmp(class_name, "ColorCorrectionEffect") == 0)
		return &cheat::world_state->color_correction_create;
	if (std::strcmp(class_name, "DepthOfFieldEffect") == 0)
		return &cheat::world_state->depth_of_field_create;
	if (std::strcmp(class_name, "Sky") == 0)
		return &cheat::world_state->sky_create;
	return nullptr;
}

inline auto _world_ensure_child(std::uint64_t parent, const char* class_name, std::chrono::steady_clock::time_point& last_attempt) -> std::uint64_t {
	auto address = _world_find_child(parent, class_name);
	auto* status = _world_status_for(class_name);

	if (address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
		if (status) {
			status->creating = false;
			status->failed = false;
			status->error[0] = '\0';
		}
		return address;
	}

	if (!parent || !class_name)
		return 0;

	if (status && status->creating)
		return 0;

	auto now = std::chrono::steady_clock::now();
	if (last_attempt != std::chrono::steady_clock::time_point{} && now - last_attempt < std::chrono::milliseconds(1000))
		return 0;

	last_attempt = now;
	if (status) {
		status->creating = true;
		status->failed = false;
		status->error[0] = '\0';
	}

	std::string class_copy = class_name;
	std::thread([class_copy, status] {
		std::string script = "local l = game:GetService('Lighting') if not l:FindFirstChildOfClass('" + class_copy + "') then Instance.new('" + class_copy + "', l) end";
		bool ok = LuaVM::Execute(script);
		if (status) {
			status->creating = false;
			status->failed = !ok;
			status->created_by_us = ok;
			if (ok) {
				status->error[0] = '\0';
			} else {
				std::snprintf(status->error, sizeof(status->error), "Lua creation pending");
			}
		}
	}).detach();

	return _world_find_child(parent, class_name);
}

inline auto _world_disable_post_effect(std::uint64_t address, std::uint64_t enabled_off, std::uint64_t size_off, float size_value = 0.0f) -> void {
	if (address < 0x10000ull || address >= 0x00007FFFFFFFFFFFull)
		return;
	memory->write<bool>(address + enabled_off, false);
	if (size_off)
		memory->write<float>(address + size_off, size_value);
}

inline auto _world_apply_atmosphere(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->atmosphere;

	if (cheat::world_settings->fog_enabled) {
		auto existing = _world_find_child(lighting_address, "Atmosphere");
		if (existing >= 0x10000ull && existing < 0x00007FFFFFFFFFFFull) {
			if (!backup.captured || backup.address != existing) {
				backup.address = existing;
				backup.color = memory->read<cheat::world_color3_t>(existing + Offsets::Atmosphere::Color);
				backup.decay = memory->read<cheat::world_color3_t>(existing + Offsets::Atmosphere::Decay);
				backup.density = memory->read<float>(existing + Offsets::Atmosphere::Density);
				backup.haze = memory->read<float>(existing + Offsets::Atmosphere::Haze);
				backup.glare = memory->read<float>(existing + Offsets::Atmosphere::Glare);
				backup.captured = true;
				backup.owned = false;
			}
			memory->write<float>(existing + Offsets::Atmosphere::Density, 0.0f);
			memory->write<float>(existing + Offsets::Atmosphere::Haze, 0.0f);
			memory->write<float>(existing + Offsets::Atmosphere::Glare, 0.0f);
		}
		return;
	}

	auto existed = _world_find_child(lighting_address, "Atmosphere") != 0;
	auto address = cheat::world_settings->atmosphere_enabled
		? _world_ensure_child(lighting_address, "Atmosphere", last_attempt)
		: _world_find_child(lighting_address, "Atmosphere");

	if (cheat::world_settings->atmosphere_enabled && address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
		if (!backup.captured || backup.address != address) {
			backup.address = address;
			auto* status = _world_status_for("Atmosphere");
			backup.owned = (status && status->created_by_us) || !existed;
			backup.color = memory->read<cheat::world_color3_t>(address + Offsets::Atmosphere::Color);
			backup.decay = memory->read<cheat::world_color3_t>(address + Offsets::Atmosphere::Decay);
			backup.density = memory->read<float>(address + Offsets::Atmosphere::Density);
			backup.haze = memory->read<float>(address + Offsets::Atmosphere::Haze);
			backup.glare = memory->read<float>(address + Offsets::Atmosphere::Glare);
			backup.captured = true;
		}

		auto color = _world_to_color3(cheat::world_settings->atmosphere_color);
		auto decay = _world_to_color3(cheat::world_settings->atmosphere_decay);
		memory->write<math::vector3>(address + Offsets::Atmosphere::Color, math::vector3{ color.r, color.g, color.b });
		memory->write<math::vector3>(address + Offsets::Atmosphere::Decay, math::vector3{ decay.r, decay.g, decay.b });
		memory->write<float>(address + Offsets::Atmosphere::Density, cheat::world_settings->atmosphere_density);
		memory->write<float>(address + Offsets::Atmosphere::Haze, std::clamp(cheat::world_settings->atmosphere_density * 2.0f, 0.0f, 10.0f));
		memory->write<float>(address + Offsets::Atmosphere::Glare, std::clamp(cheat::world_settings->atmosphere_density, 0.0f, 1.0f));
	} else if (backup.captured) {
		if (backup.owned) {
			LuaVM::Execute("local a = game:GetService('Lighting'):FindFirstChildOfClass('Atmosphere') if a then a:Destroy() end");
		} else if (_world_find_child(lighting_address, "Atmosphere") == backup.address) {
			memory->write<math::vector3>(backup.address + Offsets::Atmosphere::Color, math::vector3{ backup.color.r, backup.color.g, backup.color.b });
			memory->write<math::vector3>(backup.address + Offsets::Atmosphere::Decay, math::vector3{ backup.decay.r, backup.decay.g, backup.decay.b });
			memory->write<float>(backup.address + Offsets::Atmosphere::Density, backup.density);
			memory->write<float>(backup.address + Offsets::Atmosphere::Haze, backup.haze);
			memory->write<float>(backup.address + Offsets::Atmosphere::Glare, backup.glare);
		}
		backup = {};
	}
}

inline auto _world_apply_bloom(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->bloom;
	auto existed = _world_find_child(lighting_address, "BloomEffect") != 0;
	auto address = cheat::world_settings->bloom_enabled
		? _world_ensure_child(lighting_address, "BloomEffect", last_attempt)
		: _world_find_child(lighting_address, "BloomEffect");

	if (cheat::world_settings->bloom_enabled && address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
		if (!backup.captured || backup.address != address) {
			backup.address = address;
			auto* status = _world_status_for("BloomEffect");
			backup.owned = (status && status->created_by_us) || !existed;
			backup.enabled = memory->read<bool>(address + Offsets::BloomEffect::Enabled);
			backup.intensity = memory->read<float>(address + Offsets::BloomEffect::Intensity);
			backup.size = memory->read<float>(address + Offsets::BloomEffect::Size);
			backup.threshold = memory->read<float>(address + Offsets::BloomEffect::Threshold);
			backup.captured = true;
		}

		memory->write<bool>(address + Offsets::BloomEffect::Enabled, true);
		memory->write<float>(address + Offsets::BloomEffect::Intensity, cheat::world_settings->bloom_intensity);
		memory->write<float>(address + Offsets::BloomEffect::Size, cheat::world_settings->bloom_size);
		memory->write<float>(address + Offsets::BloomEffect::Threshold, cheat::world_settings->bloom_threshold);
	} else {
		address = _world_find_child(lighting_address, "BloomEffect");
		if (backup.owned && backup.address) {
			LuaVM::Execute("local b = game:GetService('Lighting'):FindFirstChildOfClass('BloomEffect') if b then b:Destroy() end");
		} else if (address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
			_world_disable_post_effect(address, Offsets::BloomEffect::Enabled, Offsets::BloomEffect::Size, 0.0f);
			if (backup.captured && backup.address == address) {
				memory->write<bool>(address + Offsets::BloomEffect::Enabled, backup.enabled);
				memory->write<float>(address + Offsets::BloomEffect::Intensity, backup.intensity);
				memory->write<float>(address + Offsets::BloomEffect::Size, backup.size);
				memory->write<float>(address + Offsets::BloomEffect::Threshold, backup.threshold);
			}
		}
		backup = {};
	}
}

inline auto _world_apply_blur(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->blur;
	auto existed = _world_find_child(lighting_address, "BlurEffect") != 0;
	auto address = cheat::world_settings->blur_enabled
		? _world_ensure_child(lighting_address, "BlurEffect", last_attempt)
		: _world_find_child(lighting_address, "BlurEffect");

	if (cheat::world_settings->blur_enabled && address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
		if (!backup.captured || backup.address != address) {
			backup.address = address;
			auto* status = _world_status_for("BlurEffect");
			backup.owned = (status && status->created_by_us) || !existed;
			backup.enabled = memory->read<bool>(address + Offsets::BlurEffect::Enabled);
			backup.size = memory->read<float>(address + Offsets::BlurEffect::Size);
			backup.captured = true;
		}

		memory->write<bool>(address + Offsets::BlurEffect::Enabled, true);
		memory->write<float>(address + Offsets::BlurEffect::Size, cheat::world_settings->blur_size);
	} else {
		address = _world_find_child(lighting_address, "BlurEffect");
		if (address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull) {
			memory->write<bool>(address + Offsets::BlurEffect::Enabled, false);
			memory->write<float>(address + Offsets::BlurEffect::Size, 0.0f);
		}
		if (backup.owned && backup.address) {
			LuaVM::Execute("local b = game:GetService('Lighting'):FindFirstChildOfClass('BlurEffect') if b then b:Destroy() end");
		}
		backup = {};
	}
}

inline auto _world_apply_color_correction(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->color_correction;
	auto existed = _world_find_child(lighting_address, "ColorCorrectionEffect") != 0;
	auto address = cheat::world_settings->color_correction_enabled
		? _world_ensure_child(lighting_address, "ColorCorrectionEffect", last_attempt)
		: _world_find_child(lighting_address, "ColorCorrectionEffect");

	if (cheat::world_settings->color_correction_enabled && address) {
		if (!backup.captured || backup.address != address) {
			backup.address = address;
			auto* status = _world_status_for("ColorCorrectionEffect");
			backup.owned = (status && status->created_by_us) || !existed;
			backup.enabled = memory->read<bool>(address + Offsets::ColorCorrectionEffect::Enabled);
			backup.brightness = memory->read<float>(address + Offsets::ColorCorrectionEffect::Brightness);
			backup.contrast = memory->read<float>(address + Offsets::ColorCorrectionEffect::Contrast);
			backup.tint = memory->read<cheat::world_color3_t>(address + Offsets::ColorCorrectionEffect::TintColor);
			backup.captured = true;
		}

		auto tint = _world_to_color3(cheat::world_settings->cc_tint);
		memory->write<bool>(address + Offsets::ColorCorrectionEffect::Enabled, true);
		memory->write<float>(address + Offsets::ColorCorrectionEffect::Brightness, cheat::world_settings->cc_brightness);
		memory->write<float>(address + Offsets::ColorCorrectionEffect::Contrast, cheat::world_settings->cc_contrast);
		memory->write<math::vector3>(address + Offsets::ColorCorrectionEffect::TintColor, math::vector3{ tint.r, tint.g, tint.b });
	} else {
		address = _world_find_child(lighting_address, "ColorCorrectionEffect");
		if (backup.owned && backup.address) {
			creator::destroy(backup.address);
		} else if (address) {
			memory->write<bool>(address + Offsets::ColorCorrectionEffect::Enabled, false);
			if (backup.captured && backup.address == address) {
				memory->write<bool>(address + Offsets::ColorCorrectionEffect::Enabled, backup.enabled);
				memory->write<float>(address + Offsets::ColorCorrectionEffect::Brightness, backup.brightness);
				memory->write<float>(address + Offsets::ColorCorrectionEffect::Contrast, backup.contrast);
				memory->write<math::vector3>(address + Offsets::ColorCorrectionEffect::TintColor, math::vector3{ backup.tint.r, backup.tint.g, backup.tint.b });
			}
		}
		backup = {};
	}
}

inline auto _world_apply_depth_of_field(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->depth_of_field;
	auto existed = _world_find_child(lighting_address, "DepthOfFieldEffect") != 0;
	auto address = cheat::world_settings->depth_of_field_enabled
		? _world_ensure_child(lighting_address, "DepthOfFieldEffect", last_attempt)
		: _world_find_child(lighting_address, "DepthOfFieldEffect");

	if (cheat::world_settings->depth_of_field_enabled && address) {
		if (!backup.captured || backup.address != address) {
			backup.address = address;
			auto* status = _world_status_for("DepthOfFieldEffect");
			backup.owned = (status && status->created_by_us) || !existed;
			backup.enabled = memory->read<bool>(address + Offsets::DepthOfFieldEffect::Enabled);
			backup.far_intensity = memory->read<float>(address + Offsets::DepthOfFieldEffect::FarIntensity);
			backup.focus_distance = memory->read<float>(address + Offsets::DepthOfFieldEffect::FocusDistance);
			backup.in_focus_radius = memory->read<float>(address + Offsets::DepthOfFieldEffect::InFocusRadius);
			backup.near_intensity = memory->read<float>(address + Offsets::DepthOfFieldEffect::NearIntensity);
			backup.captured = true;
		}

		memory->write<bool>(address + Offsets::DepthOfFieldEffect::Enabled, true);
		memory->write<float>(address + Offsets::DepthOfFieldEffect::FarIntensity, cheat::world_settings->dof_far_intensity);
		memory->write<float>(address + Offsets::DepthOfFieldEffect::FocusDistance, cheat::world_settings->dof_focus_distance);
		memory->write<float>(address + Offsets::DepthOfFieldEffect::InFocusRadius, cheat::world_settings->dof_in_focus_radius);
		memory->write<float>(address + Offsets::DepthOfFieldEffect::NearIntensity, cheat::world_settings->dof_near_intensity);
	} else {
		address = _world_find_child(lighting_address, "DepthOfFieldEffect");
		if (backup.owned && backup.address) {
			creator::destroy(backup.address);
		} else if (address) {
			memory->write<bool>(address + Offsets::DepthOfFieldEffect::Enabled, false);
			if (backup.captured && backup.address == address) {
				memory->write<bool>(address + Offsets::DepthOfFieldEffect::Enabled, backup.enabled);
				memory->write<float>(address + Offsets::DepthOfFieldEffect::FarIntensity, backup.far_intensity);
				memory->write<float>(address + Offsets::DepthOfFieldEffect::FocusDistance, backup.focus_distance);
				memory->write<float>(address + Offsets::DepthOfFieldEffect::InFocusRadius, backup.in_focus_radius);
				memory->write<float>(address + Offsets::DepthOfFieldEffect::NearIntensity, backup.near_intensity);
			}
		}
		backup = {};
	}
}

inline auto _world_normalize_asset_id(const char* id) -> std::string {
	if (!id || !id[0])
		return {};

	std::string value = id;
	if (value.rfind("rbxassetid://", 0) == 0)
		return value;

	std::string digits;
	digits.reserve(value.size());
	for (char ch : value) {
		if (ch >= '0' && ch <= '9')
			digits.push_back(ch);
	}

	if (digits.empty())
		return value;

	return "rbxassetid://" + digits;
}

inline auto _world_read_sky_string(std::uint64_t sky, std::uint64_t offset) -> std::string {
	if (!sky)
		return {};

	const auto string_obj = sky + offset;
	const auto length = memory->read<std::int32_t>(string_obj + 0x10);
	if (length <= 0 || length > 255)
		return {};

	std::uint64_t data = string_obj;
	if (length >= 16) {
		data = memory->read<std::uint64_t>(string_obj);
		if (!data)
			return {};
	}

	char buffer[256]{};
	if (!memory->read_raw(data, buffer, static_cast<std::uint32_t>(length)))
		return {};

	return std::string(buffer, static_cast<std::size_t>(length));
}

inline auto _world_write_sky_string(std::uint64_t sky, std::uint64_t offset, const std::string& value) -> void {
	if (!sky || value.empty() || value.size() > 200)
		return;

	const auto string_obj = sky + offset;
	const auto length = static_cast<std::int32_t>(value.size());
	auto capacity = memory->read<std::int32_t>(string_obj + 0x18);

	if (length < 16) {
		char inline_buffer[16]{};
		std::memcpy(inline_buffer, value.data(), value.size());
		memory->write_raw(string_obj, inline_buffer, sizeof(inline_buffer));
		memory->write<std::int32_t>(string_obj + 0x10, length);
		memory->write<std::int32_t>(string_obj + 0x18, 15);
		return;
	}

	std::uint64_t data_addr = 0;
	if (capacity >= 16)
		data_addr = memory->read<std::uint64_t>(string_obj);

	if (!data_addr || capacity < length) {
		auto* remote = VirtualAllocEx(
			memory->get_process_handle(),
			nullptr,
			static_cast<SIZE_T>(length) + 1,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE);
		if (!remote)
			return;

		data_addr = reinterpret_cast<std::uint64_t>(remote);
		memory->write<std::uint64_t>(string_obj, data_addr);
		memory->write<std::int32_t>(string_obj + 0x18, length);
		capacity = length;
	}

	char buffer[256]{};
	std::memcpy(buffer, value.data(), value.size());
	memory->write_raw(data_addr, buffer, static_cast<std::uint32_t>(length) + 1);
	memory->write<std::int32_t>(string_obj + 0x10, length);
	if (capacity < length)
		memory->write<std::int32_t>(string_obj + 0x18, length);
}

inline auto _world_copy_sky_string(char* dest, std::size_t dest_size, const std::string& value) -> void {
	if (!dest || dest_size == 0)
		return;
	std::snprintf(dest, dest_size, "%s", value.c_str());
}

inline auto _world_apply_skybox_faces(
	std::uint64_t sky,
	const std::string& bk,
	const std::string& dn,
	const std::string& ft,
	const std::string& lf,
	const std::string& rt,
	const std::string& up) -> void {
	_world_write_sky_string(sky, Offsets::Sky::SkyboxBk, bk);
	_world_write_sky_string(sky, Offsets::Sky::SkyboxDn, dn);
	_world_write_sky_string(sky, Offsets::Sky::SkyboxFt, ft);
	_world_write_sky_string(sky, Offsets::Sky::SkyboxLf, lf);
	_world_write_sky_string(sky, Offsets::Sky::SkyboxRt, rt);
	_world_write_sky_string(sky, Offsets::Sky::SkyboxUp, up);
}

inline auto _world_apply_skybox_all(std::uint64_t sky, const std::string& id) -> void {
	_world_apply_skybox_faces(sky, id, id, id, id, id, id);
}

inline auto _world_capture_skybox(std::uint64_t lighting_address, std::uint64_t sky) -> void {
	auto& backup = cheat::world_state->skybox;
	backup.address = sky;
	_world_copy_sky_string(backup.bk, sizeof(backup.bk), _world_read_sky_string(sky, Offsets::Sky::SkyboxBk));
	_world_copy_sky_string(backup.dn, sizeof(backup.dn), _world_read_sky_string(sky, Offsets::Sky::SkyboxDn));
	_world_copy_sky_string(backup.ft, sizeof(backup.ft), _world_read_sky_string(sky, Offsets::Sky::SkyboxFt));
	_world_copy_sky_string(backup.lf, sizeof(backup.lf), _world_read_sky_string(sky, Offsets::Sky::SkyboxLf));
	_world_copy_sky_string(backup.rt, sizeof(backup.rt), _world_read_sky_string(sky, Offsets::Sky::SkyboxRt));
	_world_copy_sky_string(backup.up, sizeof(backup.up), _world_read_sky_string(sky, Offsets::Sky::SkyboxUp));
	backup.star_count = static_cast<float>(memory->read<std::int32_t>(sky + Offsets::Sky::StarCount));
	backup.brightness = memory->read<float>(lighting_address + Offsets::Lighting::Brightness);
	auto ambient = memory->read<math::vector3>(lighting_address + Offsets::Lighting::Ambient);
	auto outdoor = memory->read<math::vector3>(lighting_address + Offsets::Lighting::OutdoorAmbient);
	backup.ambient = { ambient.x, ambient.y, ambient.z };
	backup.outdoor_ambient = { outdoor.x, outdoor.y, outdoor.z };
	backup.clock_time = _world_clock_us_to_hours(memory->read<std::int64_t>(lighting_address + Offsets::Lighting::ClockTime));
	backup.captured = true;
}

inline auto _world_restore_skybox(std::uint64_t lighting_address) -> void {
	auto& backup = cheat::world_state->skybox;
	if (!backup.captured)
		return;

	auto sky = backup.address ? backup.address : _world_find_child(lighting_address, "Sky");
	if (sky) {
		_world_apply_skybox_faces(sky, backup.bk, backup.dn, backup.ft, backup.lf, backup.rt, backup.up);
		memory->write<std::int32_t>(sky + Offsets::Sky::StarCount, static_cast<std::int32_t>(backup.star_count));
	}

	memory->write<float>(lighting_address + Offsets::Lighting::Brightness, backup.brightness);
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::Ambient, math::vector3{ backup.ambient.r, backup.ambient.g, backup.ambient.b });
	memory->write<math::vector3>(lighting_address + Offsets::Lighting::OutdoorAmbient, math::vector3{ backup.outdoor_ambient.r, backup.outdoor_ambient.g, backup.outdoor_ambient.b });
	memory->write<std::int64_t>(lighting_address + Offsets::Lighting::ClockTime, _world_hours_to_clock_us(backup.clock_time));

	backup = {};
	_world_force_lighting_dirty();
}

inline auto _world_skybox_custom_sig() -> std::string {
	auto* settings = cheat::world_settings;
	std::string sig;
	sig.reserve(768);
	sig.append(settings->skybox_all_faces ? "1|" : "0|");
	sig.append(settings->skybox_id);
	sig.push_back('|');
	sig.append(settings->skybox_bk);
	sig.push_back('|');
	sig.append(settings->skybox_dn);
	sig.push_back('|');
	sig.append(settings->skybox_ft);
	sig.push_back('|');
	sig.append(settings->skybox_lf);
	sig.push_back('|');
	sig.append(settings->skybox_rt);
	sig.push_back('|');
	sig.append(settings->skybox_up);
	return sig;
}

inline auto _world_apply_skybox_preset(std::uint64_t lighting_address, std::uint64_t sky, int preset) -> void {
	using cheat::skybox_preset_t;
	auto* settings = cheat::world_settings;

	switch (static_cast<skybox_preset_t>(preset)) {
	case skybox_preset_t::axis:
		_world_apply_skybox_all(sky, "rbxassetid://130664341191149");
		break;

	case skybox_preset_t::galaxy:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://15536110634", "rbxassetid://15536112543", "rbxassetid://15536116141",
			"rbxassetid://15536114370", "rbxassetid://15536118762", "rbxassetid://15536117282");
		memory->write<std::int32_t>(sky + Offsets::Sky::StarCount, 10000);
		break;

	case skybox_preset_t::heaven:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://7951826533", "rbxassetid://7951706908", "rbxassetid://7951694757",
			"rbxassetid://7951697216", "rbxassetid://7951700251", "rbxassetid://7951703855");
		memory->write<float>(lighting_address + Offsets::Lighting::Brightness, 1.5f);
		break;

	case skybox_preset_t::hell:
		_world_apply_skybox_all(sky, "rbxassetid://128415603056471");
		memory->write<float>(lighting_address + Offsets::Lighting::Brightness, 0.6f);
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::Ambient, math::vector3{ 0.4f, 0.1f, 0.05f });
		break;

	case skybox_preset_t::matrix:
		_world_apply_skybox_all(sky, "rbxassetid://16495688536");
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::Ambient, math::vector3{ 0.0f, 0.15f, 0.0f });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::OutdoorAmbient, math::vector3{ 0.0f, 0.2f, 0.0f });
		break;

	case skybox_preset_t::storm:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://18703245834", "rbxassetid://18703243349", "rbxassetid://18703240532",
			"rbxassetid://18703237556", "rbxassetid://18703235430", "rbxassetid://18703232671");
		memory->write<float>(lighting_address + Offsets::Lighting::Brightness, 0.4f);
		break;

	case skybox_preset_t::sunset:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://653719502", "rbxassetid://653718790", "rbxassetid://653719067",
			"rbxassetid://653719190", "rbxassetid://653718931", "rbxassetid://653719321");
		break;

	case skybox_preset_t::night_city:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://15502511288", "rbxassetid://15502508460", "rbxassetid://15502510289",
			"rbxassetid://15502507918", "rbxassetid://15502509398", "rbxassetid://15502511911");
		memory->write<std::int32_t>(sky + Offsets::Sky::StarCount, 0);
		break;

	case skybox_preset_t::aurora:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://159454286", "rbxassetid://159454299", "rbxassetid://159454296",
			"rbxassetid://159454293", "rbxassetid://159454290", "rbxassetid://159454283");
		memory->write<std::int64_t>(lighting_address + Offsets::Lighting::ClockTime, _world_hours_to_clock_us(22.0f));
		memory->write<std::int32_t>(sky + Offsets::Sky::StarCount, 8000);
		break;

	case skybox_preset_t::pink:
		_world_apply_skybox_faces(
			sky,
			"rbxassetid://12216109205", "rbxassetid://12216109875", "rbxassetid://12216109489",
			"rbxassetid://12216110170", "rbxassetid://12216110471", "rbxassetid://12216108877");
		break;

	case skybox_preset_t::custom: {
		if (settings->skybox_all_faces && settings->skybox_id[0]) {
			auto id = _world_normalize_asset_id(settings->skybox_id);
			_world_apply_skybox_faces(sky, id, id, id, id, id, id);
		} else {
			_world_apply_skybox_faces(
				sky,
				settings->skybox_bk[0] ? _world_normalize_asset_id(settings->skybox_bk) : "",
				settings->skybox_dn[0] ? _world_normalize_asset_id(settings->skybox_dn) : "",
				settings->skybox_ft[0] ? _world_normalize_asset_id(settings->skybox_ft) : "",
				settings->skybox_lf[0] ? _world_normalize_asset_id(settings->skybox_lf) : "",
				settings->skybox_rt[0] ? _world_normalize_asset_id(settings->skybox_rt) : "",
				settings->skybox_up[0] ? _world_normalize_asset_id(settings->skybox_up) : "");
		}
		break;
	}

	case skybox_preset_t::none:
	default:
		break;
	}

	_world_force_lighting_dirty();
}

inline auto _world_apply_skybox(std::uint64_t lighting_address) -> void {
	static std::chrono::steady_clock::time_point last_attempt{};
	auto& backup = cheat::world_state->skybox;
	auto* settings = cheat::world_settings;
	const int preset = settings->skybox_preset;
	const bool want = settings->skybox_enabled && preset > static_cast<int>(cheat::skybox_preset_t::none)
		&& preset < static_cast<int>(cheat::skybox_preset_t::count);

	if (!want) {
		if (backup.active || backup.captured)
			_world_restore_skybox(lighting_address);
		return;
	}

	auto existed = _world_find_child(lighting_address, "Sky") != 0;
	auto sky = _world_ensure_child(lighting_address, "Sky", last_attempt);
	if (!sky)
		return;

	if (!backup.captured || backup.address != sky)
		_world_capture_skybox(lighting_address, sky);

	backup.owned = !existed || cheat::world_state->sky_create.created_by_us;

	const auto custom_sig = _world_skybox_custom_sig();
	const bool custom_changed = preset == static_cast<int>(cheat::skybox_preset_t::custom)
		&& std::strcmp(backup.last_custom_sig, custom_sig.c_str()) != 0;
	const bool needs_apply = !backup.active || backup.last_preset != preset || custom_changed || backup.address != sky;

	if (needs_apply) {
		_world_apply_skybox_preset(lighting_address, sky, preset);
		backup.last_preset = preset;
		std::snprintf(backup.last_custom_sig, sizeof(backup.last_custom_sig), "%s", custom_sig.c_str());
		backup.active = true;
		backup.address = sky;
	}
}
