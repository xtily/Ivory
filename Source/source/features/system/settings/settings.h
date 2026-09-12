#pragma once
#include <imgui/imgui.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <atomic>

namespace settings
{
	inline bool streamproof = false;
	inline bool vsync = true;
	inline bool performance_mode = false;
	inline bool teamcheck = false;

	namespace general
	{
		inline bool confirm_unload = true;
	}

	namespace config
	{
		inline bool enabled = true;
		inline int keybind = 0;
		inline int activation_mode = 1;
	}

	namespace aimbot
	{
		inline bool enabled = false;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline int mode = 0;
		inline float mouse_sensitivity = 1.0f;

		inline int target_part = 0;
		inline int air_part = 0;

		inline float fov = 100.f;
		inline bool use_fov = false;
		inline bool dynamic_fov = false;
		inline bool ring_fov_enabled = false;
		inline float ring_fov_inner = 20.f;
		inline float ring_fov_outer = 100.f;

		inline bool smoothing = false;
		inline float smoothingx = 10.f;
		inline float smoothingy = 10.f;

		inline bool shake = false;
		inline float shake_value = 5.0f;

		inline bool enable_prediction = false;
		inline float prediction_x = 10.f;
		inline float prediction_y = 10.f;
		inline bool jump_prediction = false;
		inline float jump_prediction_value = 10.f;
		inline bool fall_prediction = false;
		inline float fall_prediction_value = 10.f;
		inline bool jump_fall_prediction = false;

		inline int smoothing_style = 0;

		inline bool teamcheck = false;
		inline bool knock_check = false;
		inline bool sticky_aim = false;
		inline bool disable_on_kill = false;

		inline int priorities = 0;
		inline bool health_check_enabled = false;
		inline float min_health = 0.0f;

		inline bool draw_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };
	}

	namespace triggerbot
	{
		inline bool enabled = false;
		inline int method = 0;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline int target_part = 0;
		inline float threshold = 15.0f;
		inline float delay_ms = 0.0f;
		inline float cooldown_ms = 35.0f;

		inline bool teamcheck = false;
		inline bool knock_check = false;
		inline bool guncheck = false;
		inline bool wallcheck = false;

		inline bool draw_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };
	}

	namespace silentaim
	{
		inline bool enabled = false;
		inline int method = 0;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline int target_part = 0;

		inline bool snapline = false;
		inline bool snapline_lerp = false;
		inline int snapline_origin = 0;
		inline float snapline_colour[4] = { 1.f, 1.f, 1.f, 1.f };

		inline float fov = 600.f;
		inline bool mode_360 = false;
		inline bool use_fov = false;
		inline bool dynamic_fov = false;
		inline bool draw_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };

		inline bool enable_prediction = false;
		inline float prediction_x = 10.f;
		inline float prediction_y = 10.f;

		inline bool sticky_aim = false;
		inline bool auto_switch = false;
		inline bool spoof_mouse = false;

		inline bool teamcheck = false;
		inline bool guncheck = false;
		inline bool knock_check = false;
		inline bool forcefield_check = false;
		inline bool shoot_spectating = true;
		inline bool katana_check = false;

		inline bool use_aimbot_target = false;

		inline int priorities = 0;
		inline bool health_check_enabled = false;
		inline float min_health = 0.0f;

	}

	namespace movement
	{
		namespace speedhack
		{
			inline bool enabled = false;
			inline int mode = 0;
			inline float speed = 50.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace flyhack
		{
			inline bool enabled = false;
			inline int mode = 0;
			inline float speed = 50.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace spam_tp
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
			inline float radius = 5.0f;
			inline float height = 3.0f;
			inline float speed = 5.0f;
			inline int mode = 0; // 0 = Orbit, 1 = Random, 2 = Behind, 3 = Above
		}

		namespace spin360
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline float speed = 1000.0f;
			inline float smoothness = 2.0f;
		}

		namespace spinbot
		{
			inline bool enabled = false;
			inline float speed = 50.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace gravity
		{
			inline bool enabled = false;
			inline float value = 196.2f;
		}

		namespace tickrate
		{
			inline bool enabled = false;
			inline float value = 240.0f;
		}

		namespace infjump
		{
			inline bool enabled = false;
		}

		namespace wallbug
		{
			inline bool enabled = false;
			inline float height = 30.0f;
		}

		namespace bhop
		{
			inline bool enabled = false;
			inline float speed = 50.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace noclip
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace hipheight
		{
			inline bool enabled = false;
			inline float value = 2.0f;
		}

		namespace wallslide
		{
			inline bool enabled = false;
			inline float speed = 15.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace pixelsurf
		{
			inline bool enabled = false;
			inline float speed = 50.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace voidhide
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace wallhop
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 0;
			inline int x1 = 1240;
			inline int y1 = 610;
			inline int delay_ms = 1;
			inline int x2 = 960;
			inline int y2 = 610;
			inline int mode = 0;
			inline int relative_delta = 280;
			inline bool auto_jump = true;
		}

		namespace third_person
		{
			inline bool enabled = false;
			inline float distance = 10.0f;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace fov_changer
		{
			inline bool enabled = false;
			inline float fov_value = 90.0f;
			inline bool dynamic = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace freecam
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1; // 0 = Hold, 1 = Toggle, 2 = Always
			inline int mouse_look_mode = 0; // 0 = Hold RMB, 1 = Always (FPS)
			inline float speed = 1.0f;
			inline float sensitivity = 0.0025f;
			inline float shift_multiplier = 2.5f;
			inline int shift_key = 0xA0; // VK_LSHIFT
			inline bool azerty = false;
			inline bool fov_override = false;
			inline float fov_value = 70.0f;
			inline bool lock_character = true;
		}

		namespace spiderman
		{
			inline bool enabled = false;
		}

		namespace no_fall_damage
		{
			inline bool enabled = false;
		}

		namespace anchor
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace sit
		{
			inline bool enabled = false;
		}

		namespace platformstand
		{
			inline bool enabled = false;
		}

		namespace longneck
		{
			inline bool enabled = false;
			inline float length = 5.0f;
		}

		namespace idealpeek
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace antiaim
		{
			inline bool enabled = false;
			inline int pitch_base = 0;
			inline float pitch_val = 0.0f;
			inline int yaw_base = 0;
			inline float yaw_val = 0.0f;
			inline float spin_speed = 50.0f;
			inline bool yaw_jitter = false;
			inline float jitter_val = 45.0f;
			inline bool upside_down = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace desync
		{
			inline bool enabled = false;
			inline int keybind = 0;
			inline int activation_mode = 1;
		}

		namespace antiafk
		{
			inline bool enabled = false;
		}
	}

	namespace misc
	{
		inline bool auto_rescan = true;
		inline bool custom_cursor = true;
		inline bool unlock_fps = false;
		inline int fps_cap = 180;
		inline bool keybind_list = false;
		inline bool keybind_indicator = false;
		inline float keybind_indicator_feature_colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float keybind_indicator_mode_colour[4] = { 0.65f, 0.65f, 0.65f, 1.0f };
		inline bool watermark = true;
		inline bool target_hud = false;
		inline bool topbar = true;
		inline uint32_t topbar_active_tabs = 0x01;
		inline float watermark_pos_x = -1.0f;
		inline float watermark_pos_y = -1.0f;
		inline float target_hud_pos_x = -1.0f;
		inline float target_hud_pos_y = -1.0f;
		inline float keybind_list_pos_x = -1.0f;
		inline float keybind_list_pos_y = -1.0f;
		inline int menu_background_style = 0;
		inline int watermark_position = 2;
		inline bool explorer_window = false;
		inline bool explorer_pinned = false;
		inline bool spotify_window = false;
		inline bool spotify_pinned = false;
		inline bool npc_system_window = false;
		inline bool npc_system_pinned = false;
		inline int npc_system_keybind = 0;
		inline int npc_system_keybind_mode = 0;
		
		inline int performance_keybind = 0;
		inline int performance_keybind_mode = 0;
		inline bool performance_window = false;
		inline bool performance_pinned = false;
		inline bool model_viewer_window = false;
		inline int model_viewer_keybind = 0;
		inline int model_viewer_keybind_mode = 0;
		inline int preview_model = 2; // 0 = Arsenal, 1 = Client (Avatar), 2 = Tung Tung Tung Sahur, 3 = Custom (Resources)
		inline char custom_model_path[260] = "";
		inline bool sticky_preview = false;
		inline bool hide_console = false;
		inline bool bot_support = false;
		inline bool only_workspace_bots = false;
		inline bool disable_scrollbars = false;
		inline bool disable_confirmations = false;
		inline int theme_index = 30;
		inline bool custom_colors_enabled = false;
		inline float custom_colors[14][4] = {
			{ 44 / 255.f, 48 / 255.f, 55 / 255.f, 1.0f },    // WindowBg
			{ 31 / 255.f, 33 / 255.f, 37 / 255.f, 1.0f },    // ChildBg
			{ 31 / 255.f, 33 / 255.f, 37 / 255.f, 1.0f },    // PopupBg
			{ 214 / 255.f, 217 / 255.f, 224 / 255.f, 1.0f }, // Text
			{ 118 / 255.f, 119 / 255.f, 123 / 255.f, 1.0f }, // TextDisabled
			{ 78 / 255.f, 81 / 255.f, 88 / 255.f, 1.0f },    // Border
			{ 0.00f, 0.00f, 0.00f, 1.0f },                   // BorderShadow
			{ 44 / 255.f, 48 / 255.f, 55 / 255.f, 1.0f },    // FrameBg
			{ 52 / 255.f, 56 / 255.f, 63 / 255.f, 1.0f },    // FrameBgHovered
			{ 39 / 255.f, 43 / 255.f, 50 / 255.f, 1.0f },    // FrameBgActive
			{ 0.00f, 0.00f, 0.00f, 0.30f },                  // FrameBgShadow
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.22f }, // Header
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.35f }, // HeaderHovered
			{ 221 / 255.f, 168 / 255.f, 93 / 255.f, 0.48f }  // HeaderActive
		};
	}

	namespace watermark
	{
		inline bool enabled = true;
		inline bool draggable = false;
		inline float pos_x = 10.0f;
		inline float pos_y = 10.0f;
		inline bool show_prefix = true;
		inline char prefix_text[64] = "^_^";
		inline bool show_username = true;
		inline bool show_fps = true;
		inline bool show_game_id = true;
		inline bool show_cpu_ram = true;
		inline bool show_job_id = false;
		inline bool show_ip_port = false;
		inline bool show_place_id = false;
		inline bool show_client_id = true;
		inline bool show_time = true;
		inline bool show_right_badge = true;
		inline char right_badge_text[64] = "Ivory v0.0.9.0";
		inline bool custom_accent = false;
		inline float accent_color[4] = { 1.0f, 0.341176f, 0.494118f, 1.0f };
	}

	namespace theme
	{
		inline int preset = 0;
		inline float accent[4] = { 141.0f / 255.0f, 41.0f / 255.0f, 81.0f / 255.0f, 1.0f }; // #8D2951
		inline float background_one[4] = { 26.0f / 255.0f, 10.0f / 255.0f, 19.0f / 255.0f, 1.0f }; // #1A0A13
		inline float background_two[4] = { 39.0f / 255.0f, 19.0f / 255.0f, 27.0f / 255.0f, 1.0f }; // #27131B
		inline float stroke[4] = { 39.0f / 255.0f, 19.0f / 255.0f, 27.0f / 255.0f, 1.0f }; // #27131B
		inline float stroke_two[4] = { 39.0f / 255.0f, 19.0f / 255.0f, 27.0f / 255.0f, 1.0f }; // #27131B
		inline float text[4] = { 200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f }; // #C8C8C8
		inline float text_inactive[4] = { 136.0f / 255.0f, 136.0f / 255.0f, 136.0f / 255.0f, 1.0f }; // #888888
	}

	namespace performance
	{
		inline int target_thread_sleep = 8;
		inline int aim_thread_sleep = 8;
		inline int cache_refresh_delay = 15;
		inline int main_loop_delay = 0;
		inline bool thread_throttling = true;

		inline float fps = 0.0f;
		inline float frametime = 0.0f;
		inline float target_thread_fps = 0.0f;
		inline float aim_thread_fps = 0.0f;
		inline float cache_thread_fps = 0.0f;
		inline float target_work_ms = 0.0f;
		inline float aim_work_ms = 0.0f;
		inline float cache_work_ms = 0.0f;

		inline std::atomic<uint64_t> target_loop_count = 0;
		inline std::atomic<uint64_t> aim_loop_count = 0;
		inline std::atomic<uint64_t> cache_loop_count = 0;
	}

	namespace visuals
	{
		struct priority_settings_t
		{
			bool box = false;
			int box_type = 1;
			int box_style = 0; // 0 = Regular, 1 = Corner, 2 = 3D
			float corner_box_length = 0.3f;
			bool box_inline = false;
			bool box_gradient = false;
			int box_gradient_type = 0;
			float box_pulse_speed = 2.0f;
			float colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float box_colour_mid[4] = { 1.f, 1.f, 1.f, 1.f };
			float box_colour_low[4] = { 1.f, 1.f, 1.f, 1.f };

			bool box_fill = false;
			int box_fill_style = 0;
			bool box_fill_gradient = false;
			bool box_fill_gradient_spin = false;
			float box_fill_gradient_speed = 2.0f;
			int box_fill_gradient_type = 0;
			int box_fill_gradient_mask = 1;
			float box_fill_pulse_speed = 2.0f;
			float box_fill_colour[4] = { 1.f, 1.f, 1.f, 0.2f };
			float box_fill_colour_mid[4] = { 1.f, 1.f, 1.f, 0.2f };
			float box_fill_colour_low[4] = { 1.f, 1.f, 1.f, 0.2f };

			bool username = false;
			int username_type = 0; // 0 = Display Name, 1 = Username, 2 = Both
			int username_position = 0; // 0 = Top, 1 = Bottom, 2 = Left, 3 = Right
			int name_transform = 0; // 0 = None, 1 = Uppercase, 2 = Lowercase
			float username_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			bool username_gradient = false;
			int username_gradient_type = 0; // 0 = Wave, 1 = Rainbow, 2 = Left to Right
			float username_gradient_speed = 2.5f;
			float username_colour_two[4] = { 1.f, 1.f, 1.f, 1.f };

			bool distance = false;
			float distance_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool tool = false;
			float tool_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool healthbar = false;
			int healthbar_style = 0;
			float healthbar_padding = 4.f;
			float healthbar_offset_x = 0.f;
			float healthbar_offset_y = 0.f;
			int healthbar_gradient_type = 0;
			float healthbar_pulse_speed = 1.f;
			int name_bracket_style = 0;
			bool healthbar_text = false;
			float healthbar_text_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			bool healthbar_lerp = true;
			bool value_follow = false;
			bool ignore_full = false;
			bool bar_fold = false;
			float bar_size = 1.f;
			float healthbar_colour[4] = { 0.f, 1.f, 0.f, 1.f };
			float healthbar_colour_mid[4] = { 1.f, 1.f, 0.f, 1.f };
			float healthbar_colour_low[4] = { 1.f, 0.f, 0.f, 1.f };

			bool tracers = false;
			int tracers_origin = 0;
			float tracers_thickness = 1.0f;
			float tracers_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool chams = false;
			int chams_type = 0;
			int chams_texture = 0;
			int mesh_shader = 0;
			bool mesh_outline = false;
			int mesh_outline_mode = 0; 
			int mesh_outline_style = 0;
			float mesh_outline_thickness = 2.5f;
			bool mesh_outline_only = false;
			bool mesh_accessories = false;
			float mesh_depth_bias = 0.0f;
			bool chams_corpse = false;
			bool chams_health_based = false;
			bool chams_flash = false;
			float chams_flash_speed = 1.0f;
			float chams_flash_colour[4] = { 1.f, 0.f, 0.f, 1.f };
			bool chams_fade = false;
			float chams_fade_speed = 1.0f;
			float chams_colour[4] = { 1.f, 1.f, 1.f, 0.25f };
			float chams_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			bool chams_outline_glow = false;
			float chams_glow_colour[4] = { 1.f, 1.f, 1.f, 0.6f };
			float chams_glow_radius = 4.0f;
			float chams_glow_alpha = 0.5f;
			float chams_glow_intensity = 100.0f;

			bool skeleton = false;
			float skeleton_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float skeleton_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };

			bool head_dot = false;
			int head_type = 0; // 0 = Dot, 1 = Hexagon
			int head_mode = 0; // 0 = Static, 1 = Bounding
			bool circular_head_dot = false;
			float head_dot_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float head_dot_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			float head_dot_size = 4.f;

			bool corpse = false;
			float corpse_colour[4] = { 1.f, 1.f, 1.f, 0.25f };
			float corpse_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
			bool corpse_names = false;
			float corpse_names_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool offscreen_arrows = false;
			int arrow_type = 0;
			int arrow_mode = 0;
			std::vector<int> arrows_info = { 1, 1 };
			float offscreen_arrows_size = 12.f;
			float offscreen_arrows_radius = 150.f;
			float offscreen_arrows_position = 40.f;
			float arrows_max_dist = 5000.f;
			bool arrows_glow = false;
			float offscreen_arrows_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float arrows_glow_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool movement_trails = false;
			float movement_trails_thickness = 1.0f;
			float movement_trails_length = 1.0f;
			float movement_trails_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool view_angle_lines = false;
			float view_angle_lines_thickness = 1.0f;
			float view_angle_lines_length = 10.0f;
			float view_angle_lines_colour[4] = { 1.f, 1.f, 1.f, 1.f };

			bool sound_esp = false;
			float sound_esp_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float sound_esp_radius = 5.f;
			float sound_esp_speed = 1.0f;

			bool footprints = false;
			bool footprints_glow = false;
			float footprints_colour[4] = { 1.f, 1.f, 1.f, 1.f };
			float footprints_radius = 1.f;
			float footprints_speed = 1.0f;

			bool state_flags = false;
			bool rig_flags = false;
		};

		inline priority_settings_t neutral;
		inline priority_settings_t friendly;
		inline priority_settings_t hostile;

		inline bool enabled = true;
		inline int keybind = 0;
		inline int activation_mode = 1;

		inline bool engine_chams = false;
		inline int engine_chams_style = 3;
		inline int engine_chams_color_index = 0;
		inline int engine_chams_mode = 0; // 0 = Glow (10), 1 = Invisible (14), 2 = Always On Top (13)
		inline int engine_chams_queue_id = 10;
		inline float engine_chams_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline bool engine_chams_weapons = false;
		inline int engine_chams_weapons_style = 2;
		inline float engine_chams_weapons_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline bool engine_chams_black_only = false;

		inline bool teamcheck = false;
		inline int esp_font_index = 0;
		inline float esp_font_size = 12.f;
		inline int bounding_type = 0;
		inline std::vector<int> render_outlines = { 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // Skeleton, Box, Name, Health Bar, Health Text, Held Item, Tracers, Head Dot, Distance
		inline float skeleton_thickness = 1.5f;
		inline float box_thickness = 1.0f;
		inline float healthbar_thickness = 2.0f;
		inline float chams_thickness = 1.0f;
		inline float chams_outline_thickness = 1.0f;
		inline float text_outline_thickness = 1.0f;
		inline int distance_unit = 0; // 0 = Studs, 1 = Meters
		inline bool distance_check = false;
		inline float max_distance = 500.0f;
		inline bool sort_by_status = false;
		inline bool knock_check = false;
		inline bool local_player = false;
		inline bool tool_check = false;
		inline bool godded_check = false;
		inline bool forcefield_check = false;
		inline bool alive_check = false;
		inline bool ragdoll_check = false;
		inline bool visible_check = false;
		inline bool static_on_void = false;
		inline bool show_none = false;
		inline bool use_display_name = false;
		inline bool combined_name = false;
		inline int global_outline_type = 1; // 0 = None, 1 = Outline, 2 = Shadow
		inline bool ignore_full_health = false;
		inline bool static_on_death = false;
		inline float corpse_shift = 0.0f;
		inline float corpse_max_dist = 5000.f;
		inline bool enemy_highlight = false;
		inline float enemy_highlight_colour[4] = { 1.f, 0.2f, 0.2f, 0.6f };
		inline bool friendly_highlight = false;
		inline float friendly_highlight_colour[4] = { 0.2f, 1.f, 0.2f, 0.6f };
		inline bool bar_fill = false;
		inline float bar_fill_colour[4] = { 0.1f, 0.1f, 0.1f, 0.5f };
		inline bool health_check_enabled = false;
		inline float min_health = 0.0f;
		inline bool chams_hit_impact = false;
		inline float chams_hit_impact_colour[4] = { 1.f, 0.2f, 0.2f, 0.8f };

		inline bool flags = false;
		inline int flags_mask = 0;
		inline float flags_state_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float flags_colour[4] = { 1.f, 1.f, 1.f, 1.f };

		inline bool client_korblox = false;
		inline bool client_headless = false;
		inline bool black_avatar = false;

		inline bool target_recolor = false;
		inline float target_recolor_colour[4] = { 1.f, 0.f, 0.f, 1.f };

		inline bool client_remove_hair = false;
		inline bool client_remove_accessories = false;
		inline bool avatar_recolor_custom = false;
		inline float avatar_recolor_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		inline bool avatar_recolor = false;
		inline float recolor_head[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float recolor_torso[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float recolor_left_arm[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float recolor_right_arm[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float recolor_left_leg[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		inline float recolor_right_leg[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	namespace hitboxexpander
	{
		inline bool enabled = false;
		inline bool teamcheck = false;
		inline bool knock_check = false;

		inline float size_x = 20.0f;
		inline float size_y = 20.0f;
		inline float size_z = 20.0f;

		inline bool visualize = false;
		inline float hitbox_colour[4] = { 0.f, 0.659f, 0.729f, 0.3f };
		inline float hitbox_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
	}

	namespace playerlist
	{
		inline char npc_path_input[256] = "Ugc.Workspace.Bots";
		inline std::unordered_set<std::string> active_npc_paths;
	}

	namespace lighting
	{
		namespace skybox
		{
			inline bool enabled = false;
			inline int preset = 0;
			inline char custom_bk[256] = "";
			inline char custom_dn[256] = "";
			inline char custom_ft[256] = "";
			inline char custom_lf[256] = "";
			inline char custom_rt[256] = "";
			inline char custom_up[256] = "";
		}
		namespace ambient
		{
			inline bool enabled = false;
			inline float color[4] = { 1.f, 1.f, 1.f, 1.f };
			inline float outdoor_color[4] = { 1.f, 1.f, 1.f, 1.f };
			inline bool pulse = false;
			inline float pulse_speed = 2.0f;
			inline float pulse_intensity = 0.5f;
		}
		namespace shadows
		{
			inline bool enabled = false;
			inline float value = 0.f;
		}
		namespace fog
		{
			inline bool enabled = false;
			inline float fog_start = 0.f;
			inline float fog_end = 0.f;
			inline float fog_color[4] = { 1.f, 1.f, 1.f, 1.f };
			inline bool pulse = false;
			inline int pulse_mode = 0; // 0 = Distance, 1 = Color, 2 = Both
			inline float pulse_speed = 2.0f;
			inline float pulse_intensity = 0.5f;
		}
		namespace clocktime
		{
			inline bool enabled = false;
			inline float time = 0.f;
		}
		namespace atmosphere
		{
			inline bool enabled = false;
			inline float color[4] = { 1.f, 1.f, 1.f, 1.f };
			inline float decay[4] = { 1.f, 1.f, 1.f, 1.f };
			inline float density = 0.f;
			inline float glare = 0.f;
			inline float haze = 0.f;
			inline float offset = 0.f;
			inline bool pulse = false;
			inline int pulse_mode = 0; // 0 = Density, 1 = Color, 2 = Both
			inline float pulse_speed = 2.0f;
			inline float pulse_intensity = 0.5f;
		}
		namespace colorshift
		{
			inline bool enabled = false;
			inline float bottom[4] = { 1.f, 1.f, 1.f, 1.f };
			inline float top[4] = { 1.f, 1.f, 1.f, 1.f };
		}
		namespace exposure
		{
			inline bool enabled = false;
			inline float exposure = 0.f;
		}
		namespace starcount
		{
			inline bool enabled = false;
			inline int star_count = 3000;
		}
		namespace sunmoon
		{
			inline bool enabled = false;
			inline char custom_sun_id[256] = "";
			inline char custom_moon_id[256] = "";
		}
		namespace brightness
		{
			inline bool enabled = false;
			inline float value = 1.f;
		}
		namespace environment
		{
			inline bool enabled = false;
			inline float diffuse_scale = 1.f;
			inline float specular_scale = 1.f;
		}
		namespace latitude
		{
			inline bool enabled = false;
			inline float value = 0.f;
		}
		namespace terrain
		{
			inline bool enabled = false;
			inline float grass_length = 0.7f;
			inline float water_color[4] = { 0.f, 0.4f, 0.8f, 1.f };
			inline float water_reflectance = 1.f;
			inline float water_transparency = 1.f;
			inline float water_wave_size = 0.15f;
			inline float water_wave_speed = 10.f;
		}
		namespace bloom
		{
			inline bool enabled = false;
			inline float intensity = 1.f;
			inline float size = 24.f;
			inline float threshold = 2.f;
		}
		namespace sunrays
		{
			inline bool enabled = false;
			inline float intensity = 0.25f;
			inline float spread = 0.1f;
		}
		namespace color_correction
		{
			inline bool enabled = false;
			inline float brightness = 0.f;
			inline float contrast = 0.f;
			inline float saturation = 0.f;
			inline float tint_color[4] = { 1.f, 1.f, 1.f, 1.f };
		}
		namespace depth_of_field
		{
			inline bool enabled = false;
			inline float density = 0.1f;
			inline float focus_distance = 0.05f;
			inline float in_focus_radius = 30.f;
			inline float near_intensity = 0.75f;
		}
	}

	namespace freezeplayer
	{
		inline bool enabled = false;
		inline int keybind = 0;
		inline int activation_mode = 1;
	}



	namespace typingcheck
	{
		inline bool enabled = true;
	}

	namespace notifications
	{
		inline bool enabled = false;
		inline bool welcome = false;
		inline char welcome_msg[128] = "Welcome to Inherently!";
		inline bool thankyou = false;
		inline char thankyou_msg[256] = "Thank you for choosing Inherently! We truly appreciate your support.";
		inline bool hit = false;
		inline char hit_msg[128] = "Hit {player} for {damage} HP!";
		inline bool kill = false;
		inline char kill_msg[128] = "Killed {player}!";
		inline float duration = 5.0f;
		inline int position = 3;
	}

	namespace raycast_silentaim
	{
		inline bool enabled = false;
		inline bool mode_360 = false;
		inline int mode_360_keybind = 0;
		inline int mode_360_activation_mode = 1;
		inline bool use_fov = true;
		inline bool draw_fov = false;
		inline bool enable_prediction = false;
		inline bool teamcheck = false;
		inline bool magic_bullet = false;
		inline int magic_bullet_method = 0; // 0 = Engine Hook, 1 = Position Spoof (Da Hood)
		inline bool forcefield_check = false;
		inline bool hitmarkers = false;
		inline float hitmarker_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float hitmarker_size = 6.f;
		inline float hitmarker_gap = 4.f;
		inline float hitmarker_duration = 0.25f;

		inline bool hit_flash = false;
		inline float hit_flash_colour[4] = { 1.f, 1.f, 1.f, 0.35f };
		inline float hit_flash_duration = 0.12f;

		inline int magic_bullet_keybind = 0;
		inline int magic_bullet_activation_mode = 1;
		inline float fov = 100.f;
		inline bool dynamic_fov = false;
		inline bool fill_fov = false;
		inline float fov_circle_colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4] = { 0.f, 0.f, 0.f, 1.f };
		inline float fov_fill_colour[4] = { 1.f, 1.f, 1.f, 0.15f };
		inline int keybind = 0;
		inline int activation_mode = 1;
	}

	namespace crosshair
	{
		inline bool enabled = false;
		inline bool attach_to_enemy = false;
		inline bool lerp = false;
		inline float lerp_speed = 18.0f;
		inline int style = 0; // 0 = Cross, 1 = T-Shape, 2 = Circle, 3 = Chevron, 4 = X-Shape
		inline float colour[4] = { 1.f, 1.f, 1.f, 1.f };
		inline float outline_colour[4] = { 0.f, 0.f, 0.f, 0.8f };
		inline bool outline = true;
		inline float size = 7.f;
		inline float gap = 3.f;
		inline float thickness = 1.5f;
		inline bool dot = true;
		inline float dot_size = 1.5f;

		inline bool rotate = false;
		inline float rotate_speed = 90.f;

		inline bool pulse = false;
		inline float pulse_speed = 3.f;
		inline float pulse_amount = 2.f;

		inline bool rainbow = false;
		inline float rainbow_speed = 0.2f;

		inline bool dynamic_gap = false;
		inline float expansion_amount = 5.f;
		inline float expansion_decay = 15.f;
	}

	namespace hitsounds
	{
		inline bool enabled = false;
		inline int type = 0;
		inline int method = 0;
		inline char custom_path[260] = "";
		inline float volume = 0.5f;
	}

	namespace killsounds
	{
		inline bool enabled = false;
		inline int type = 0;
		inline char custom_path[260] = "";
		inline float volume = 0.5f;
	}

	namespace godmode
	{
		inline bool enabled = false;
	}

	namespace config_system
	{
		inline bool disable_confirmations = false;
	}

	namespace counterblox
	{
		inline bool infammo       = false;
		inline int  ammo_value    = 999;
		inline bool spreadremove  = false;
		inline float spreadamount = 0.f;
	}

	namespace arsenal
	{
		inline bool fast_fire_rate = false;
		inline bool no_recoil      = false;
		inline bool all_auto       = false;
		inline bool infinite_ammo  = false;
	}

	namespace mm2
	{
		inline bool role_esp = false;
		inline bool coin_esp = false;
	}

	namespace spamtp
	{
		inline bool enabled = false;
		inline int keybind = 0;
		inline int activation_mode = 1;
		inline float interval_ms = 50.f;
		inline float offset = 5.f;
		inline bool teamcheck = false;
		inline bool knock_check = false;
	}

	namespace autoshoot
	{
		inline bool enabled = false;
		inline float fire_interval_ms = 50.f;
	}

	namespace skinchanger
	{
		inline bool enabled = false;
		inline int category_index = 0;
		inline int skin_index = 0;
		inline char search_filter[64] = "";
		inline std::vector<std::string> favorites;
	}

	namespace dh_skinchanger
	{
		inline bool enabled = false;
		inline int skin_index = 0;
	}

	namespace rivals_skinchanger
	{
		inline bool enabled = false;
		inline char mesh_id[256] = "";
		inline int preset_index = 0;
	}

	namespace animationchanger
	{
		inline bool enabled = false;
		inline int type = 0;
		inline int mode = 0;
	}

	namespace materialchanger
	{
		inline bool enabled = false;
		inline int material_index = 0;
		inline bool affect_accessories = false;
	}

	namespace skyboxchanger
	{
		inline bool enabled = false;
		inline int preset = 0;
	}

	namespace btools
	{
		inline bool enabled = false;
		inline int tool_type = 0; // 0 = Hammer, 1 = Grab, 2 = Clone
		inline int keybind = 0;
		inline int activation_mode = 1;
	}

	namespace hit_tracers
	{
		inline bool enabled = false;
		inline int origin_type = 0; // 0 = Screen Center, 1 = Bottom Center, 2 = Local Player / Muzzle
		inline int style = 2;       // 0 = Solid Beam, 1 = Fadeout, 2 = Neon Glow, 3 = Laser Beam, 4 = Gradient Pulse, 5 = Segmented
		inline float duration = 1.5f;
		inline float thickness = 2.0f;
		inline float colour[4] = { 1.0f, 0.25f, 0.45f, 1.0f };
		inline float outline_colour[4] = { 0.0f, 0.0f, 0.0f, 0.8f };
		inline bool draw_outline = true;
		inline bool damage_text = true;
		inline float damage_text_colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	namespace hit_chams
	{
		inline bool enabled = false;
		inline int style = 0;       // 0 = Ghost Fade, 1 = Wireframe, 2 = Solid Neon, 3 = Hologram, 4 = Rainbow Shimmer, 5 = Pulse Glow
		inline int fade_easing = 1; // 0 = Linear, 1 = Smooth Exponential, 2 = Pulse Fade, 3 = Shrink & Fade
		inline float duration = 1.2f;
		inline float scale = 1.0f;
		inline float fill_colour[4] = { 0.2f, 0.6f, 1.0f, 0.45f };
		inline float outline_colour[4] = { 0.5f, 0.85f, 1.0f, 0.9f };
		inline float outline_thickness = 1.5f;
		inline bool draw_outline = true;
	}

	namespace hit_skeleton
	{
		inline bool enabled = false;
		inline int style = 1;       // 0 = Solid, 1 = Neon Glow, 2 = Pulse, 3 = Rainbow
		inline int fade_easing = 1; // 0 = Linear, 1 = Smooth Exponential, 2 = Dissolve
		inline float duration = 1.5f;
		inline float thickness = 2.0f;
		inline float joint_radius = 3.5f;
		inline float colour[4] = { 1.0f, 0.3f, 0.3f, 1.0f };
		inline float outline_colour[4] = { 0.0f, 0.0f, 0.0f, 0.8f };
		inline float joint_colour[4] = { 1.0f, 0.9f, 0.3f, 1.0f };
		inline bool draw_joints = true;
		inline bool draw_outline = true;
	}

	namespace mcp
	{
		inline bool enabled = false;
		inline int port = 28888;
	}
}

