#pragma once
#include <sdk/math/math.h>

#include <cstdint>
#include <string_view>

namespace cheat {
	struct world_color3_t {
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
	};

	struct world_settings_t {
		bool clock_time_enabled = false;
		float clock_time_value = 14.0f;

		bool ambient_enabled = false;
		float ambient_color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
		float outdoor_ambient_color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

		bool atmosphere_enabled = false;
		float atmosphere_color[4] = { 0.78f, 0.78f, 0.82f, 1.0f };
		float atmosphere_decay[4] = { 0.45f, 0.45f, 0.5f, 1.0f };
		float atmosphere_density = 0.3f;

		bool bloom_enabled = false;
		float bloom_intensity = 0.4f;
		float bloom_size = 24.0f;
		float bloom_threshold = 0.95f;

		bool blur_enabled = false;
		float blur_size = 8.0f;

		bool color_correction_enabled = false;
		float cc_brightness = 0.0f;
		float cc_contrast = 0.0f;
		float cc_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		bool fog_enabled = false;
		float fog_start = 0.0f;
		float fog_end = 500.0f;
		float fog_color[4] = { 0.7f, 0.7f, 0.7f, 1.0f };

		bool depth_of_field_enabled = false;
		float dof_far_intensity = 0.1f;
		float dof_focus_distance = 50.0f;
		float dof_in_focus_radius = 50.0f;
		float dof_near_intensity = 0.1f;

		bool exposure_enabled = false;
		float exposure_value = 0.0f;

		bool shadows_enabled_toggle = false;
		bool shadows_enabled_value = true;

		bool custom_fov = false;
		float custom_fov_value = 90.0f;

		bool skybox_enabled = false;
		int skybox_preset = 0;
		bool skybox_all_faces = false;
		char skybox_id[128]{};
		char skybox_bk[128]{};
		char skybox_dn[128]{};
		char skybox_ft[128]{};
		char skybox_lf[128]{};
		char skybox_rt[128]{};
		char skybox_up[128]{};
	};

	enum class skybox_preset_t : int {
		none = 0,
		axis,
		galaxy,
		heaven,
		hell,
		matrix,
		storm,
		sunset,
		night_city,
		aurora,
		pink,
		custom,
		count
	};

	inline world_settings_t world_settings_object{};
	inline world_settings_t* world_settings = &world_settings_object;

	struct world_lighting_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		float clock_time = 0.0f;
		std::uint32_t source = 0;
		math::vector3 light_direction{};
		math::vector3 light_color{};
		math::vector3 sun_position{};
		math::vector3 moon_position{};
		math::vector3 gradient_top{};
		math::vector3 gradient_bottom{};
		world_color3_t ambient{};
		world_color3_t outdoor_ambient{};
		world_color3_t color_shift_top{};
		world_color3_t color_shift_bottom{};
		float fog_start = 0.0f;
		float fog_end = 0.0f;
		world_color3_t fog_color{};
		float exposure = 0.0f;
		bool shadows = true;
	};

	struct world_atmosphere_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		world_color3_t color{};
		world_color3_t decay{};
		float density = 0.0f;
		float haze = 0.0f;
		float glare = 0.0f;
	};

	struct world_bloom_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		bool enabled = true;
		float intensity = 0.0f;
		float size = 0.0f;
		float threshold = 0.0f;
	};

	struct world_blur_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		bool enabled = true;
		float size = 0.0f;
	};

	struct world_color_correction_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		bool enabled = true;
		float brightness = 0.0f;
		float contrast = 0.0f;
		world_color3_t tint{};
	};

	struct world_depth_of_field_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		bool enabled = true;
		float far_intensity = 0.0f;
		float focus_distance = 0.0f;
		float in_focus_radius = 0.0f;
		float near_intensity = 0.0f;
	};

	struct world_skybox_backup_t {
		std::uint64_t address = 0;
		bool captured = false;
		bool owned = false;
		bool active = false;
		char bk[128]{};
		char dn[128]{};
		char ft[128]{};
		char lf[128]{};
		char rt[128]{};
		char up[128]{};
		float star_count = 0.0f;
		float brightness = 0.0f;
		world_color3_t ambient{};
		world_color3_t outdoor_ambient{};
		float clock_time = 0.0f;
		int last_preset = -1;
		char last_custom_sig[768]{};
	};

	struct world_create_status_t {
		bool creating = false;
		bool failed = false;
		bool created_by_us = false;
		char error[96]{};
	};

	struct world_state_t {
		world_lighting_backup_t lighting;
		bool clock_time_active = false;
		bool ambient_active = false;
		bool fog_active = false;
		bool exposure_active = false;
		bool shadows_active = false;
		bool fog_removed_atmosphere = false;

		world_atmosphere_backup_t atmosphere;
		world_bloom_backup_t bloom;
		world_blur_backup_t blur;
		world_color_correction_backup_t color_correction;
		world_depth_of_field_backup_t depth_of_field;
		world_skybox_backup_t skybox;

		world_create_status_t atmosphere_create;
		world_create_status_t bloom_create;
		world_create_status_t blur_create;
		world_create_status_t color_correction_create;
		world_create_status_t depth_of_field_create;
		world_create_status_t sky_create;
	};

	inline world_state_t world_state_object{};
	inline world_state_t* world_state = &world_state_object;

	class world_feature_t {
	public:
		auto name() const -> std::string_view {
			return "world";
		}

		auto tick_hz() const -> int {
			return 20;
		}

		auto on_worker_tick() -> void;
		auto has_lighting_child(const char* class_name) const -> bool;
		auto create_status_for(const char* class_name) const -> const world_create_status_t*;
		auto run() -> void;
	};

	inline world_feature_t world_feature_object{};
	inline world_feature_t* world_feature = &world_feature_object;
}

namespace lighting {
	void run_all();
}
