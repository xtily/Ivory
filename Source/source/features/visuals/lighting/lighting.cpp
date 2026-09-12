#include "lighting.h"
#include "lightinghelpers.h"
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <windows.h>

namespace {

constexpr float k_deg2rad = 0.01745329251f;

std::atomic<bool> g_fov_on{ false };
std::atomic<std::uint64_t> g_fov_camera{ 0 };
std::atomic<float> g_fov_rads{ 1.57079637f };

auto fov_writer_thread() -> void {
	while (true) {
		if (!g_fov_on.load(std::memory_order_acquire)) {
			Sleep(10);
			continue;
		}

		const auto camera = g_fov_camera.load(std::memory_order_acquire);
		const auto fov = g_fov_rads.load(std::memory_order_acquire);
		if (camera)
			memory->write<float>(camera + Offsets::Camera::FieldOfView, fov);
		Sleep(1);
	}
}

auto ensure_fov_thread() -> void {
	static std::once_flag once;
	std::call_once(once, [] {
		std::thread(fov_writer_thread).detach();
	});
}

auto push_fov(std::uint64_t camera, bool enabled, float degrees) -> void {
	ensure_fov_thread();
	if (!enabled || !camera) {
		g_fov_on.store(false, std::memory_order_release);
		return;
	}

	const auto rads = std::clamp(degrees, 1.0f, 120.0f) * k_deg2rad;
	g_fov_camera.store(camera, std::memory_order_release);
	g_fov_rads.store(rads, std::memory_order_release);
	g_fov_on.store(true, std::memory_order_release);
}

} // namespace

auto cheat::world_feature_t::on_worker_tick() -> void {
	{
		const auto camera = game::camera;
		push_fov(camera, world_settings->custom_fov, world_settings->custom_fov_value);
	}

	if (!game::datamodel || !game::datamodel->address)
		return;

	auto lighting_address = game::datamodel->find_first_child_by_class("Lighting");
	if (!lighting_address)
		return;

	const bool any_lighting = world_settings->clock_time_enabled || world_settings->ambient_enabled
		|| world_settings->fog_enabled || world_settings->exposure_enabled || world_settings->shadows_enabled_toggle;

	if (any_lighting && (!world_state->lighting.captured || world_state->lighting.address != lighting_address))
		world_state->lighting = _world_capture_lighting(lighting_address);

	if (world_settings->clock_time_enabled) {
		_world_apply_clock_time(lighting_address, world_settings->clock_time_value);
		world_state->clock_time_active = true;
	} else if (world_state->clock_time_active) {
		_world_restore_clock_params(world_state->lighting);
		world_state->clock_time_active = false;
	}

	if (world_settings->ambient_enabled) {
		auto ambient = _world_to_color3(world_settings->ambient_color);
		auto outdoor = _world_to_color3(world_settings->outdoor_ambient_color);

		memory->write<math::vector3>(lighting_address + Offsets::Lighting::Ambient, math::vector3{ ambient.r, ambient.g, ambient.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::OutdoorAmbient, math::vector3{ outdoor.r, outdoor.g, outdoor.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::ColorShift_Top, math::vector3{ outdoor.r, outdoor.g, outdoor.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::ColorShift_Bottom, math::vector3{ ambient.r, ambient.g, ambient.b });

		if (!world_settings->clock_time_enabled) {
			memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientTop, math::vector3{ outdoor.r, outdoor.g, outdoor.b });
			memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientBottom, math::vector3{ ambient.r, ambient.g, ambient.b });
		}

		world_state->ambient_active = true;
	} else if (world_state->ambient_active) {
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::Ambient, math::vector3{ world_state->lighting.ambient.r, world_state->lighting.ambient.g, world_state->lighting.ambient.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::OutdoorAmbient, math::vector3{ world_state->lighting.outdoor_ambient.r, world_state->lighting.outdoor_ambient.g, world_state->lighting.outdoor_ambient.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::ColorShift_Top, math::vector3{ world_state->lighting.color_shift_top.r, world_state->lighting.color_shift_top.g, world_state->lighting.color_shift_top.b });
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::ColorShift_Bottom, math::vector3{ world_state->lighting.color_shift_bottom.r, world_state->lighting.color_shift_bottom.g, world_state->lighting.color_shift_bottom.b });

		if (!world_state->clock_time_active) {
			memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientTop, world_state->lighting.gradient_top);
			memory->write<math::vector3>(lighting_address + Offsets::Lighting::GradientBottom, world_state->lighting.gradient_bottom);
		}

		world_state->ambient_active = false;
	}

	if (world_settings->atmosphere_enabled && world_settings->fog_enabled) {
		world_settings->fog_enabled = false;
	}

	if (world_settings->fog_enabled) {
		memory->write<float>(lighting_address + Offsets::Lighting::FogStart, world_settings->fog_start);
		memory->write<float>(lighting_address + Offsets::Lighting::FogEnd, world_settings->fog_end);
		auto fog_color = _world_to_color3(world_settings->fog_color);
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::FogColor, math::vector3{ fog_color.r, fog_color.g, fog_color.b });
		world_state->fog_active = true;
	} else if (world_state->fog_active) {
		memory->write<float>(lighting_address + Offsets::Lighting::FogStart, world_state->lighting.fog_start);
		memory->write<float>(lighting_address + Offsets::Lighting::FogEnd, world_state->lighting.fog_end);
		memory->write<math::vector3>(lighting_address + Offsets::Lighting::FogColor, math::vector3{ world_state->lighting.fog_color.r, world_state->lighting.fog_color.g, world_state->lighting.fog_color.b });
		world_state->fog_active = false;
	}

	if (world_settings->exposure_enabled) {
		memory->write<float>(lighting_address + Offsets::Lighting::ExposureCompensation, world_settings->exposure_value);
		world_state->exposure_active = true;
	} else if (world_state->exposure_active) {
		memory->write<float>(lighting_address + Offsets::Lighting::ExposureCompensation, world_state->lighting.exposure);
		world_state->exposure_active = false;
	}

	if (world_settings->shadows_enabled_toggle) {
		memory->write<bool>(lighting_address + Offsets::Lighting::GlobalShadows, world_settings->shadows_enabled_value);
		world_state->shadows_active = true;
	} else if (world_state->shadows_active) {
		memory->write<bool>(lighting_address + Offsets::Lighting::GlobalShadows, world_state->lighting.shadows);
		world_state->shadows_active = false;
	}

	if (!any_lighting && world_state->lighting.captured && world_state->lighting.address == lighting_address)
		world_state->lighting = {};

	_world_apply_atmosphere(lighting_address);
	_world_apply_bloom(lighting_address);
	_world_apply_blur(lighting_address);
	_world_apply_color_correction(lighting_address);
	_world_apply_depth_of_field(lighting_address);
	_world_apply_skybox(lighting_address);
}

auto cheat::world_feature_t::has_lighting_child(const char* class_name) const -> bool {
	if (!game::datamodel || !game::datamodel->address)
		return false;

	auto lighting_address = game::datamodel->find_first_child_by_class("Lighting");
	if (!lighting_address)
		return false;

	return _world_find_child(lighting_address, class_name) != 0;
}

auto cheat::world_feature_t::create_status_for(const char* class_name) const -> const world_create_status_t* {
	return _world_status_for(class_name);
}

auto cheat::world_feature_t::run() -> void {
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	std::thread([this]() {
		while (true) {
			try {
				on_worker_tick();
			}
			catch (...) {}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}).detach();
}

namespace lighting {
	void run_all() {
		cheat::world_feature->run();
	}
}
