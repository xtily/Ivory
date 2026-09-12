#include "config.h"
#include <features/system/settings/settings.h>
#include <ui/menu/settings/functions.h>
#include <ui/menu/settings/theme.h>
#include <mutex>
#include <chrono>
#include <sdk/cache/core/cache.h>
#include <sdk/game/game.h>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <imgui/imgui.h>

namespace config
{
	std::string get_base_directory()
	{
		return "C:\\Ivory";
	}

	std::string get_config_directory()
	{
		return "C:\\Ivory\\Configs";
	}

	std::string get_dumps_directory()
	{
		return "C:\\Ivory\\Dumps";
	}

	bool ensure_dumps_directory()
	{
		std::string dir = get_dumps_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string get_resources_directory()
	{
		return "C:\\Ivory\\Resources";
	}

	bool ensure_resources_directory()
	{
		std::string dir = get_resources_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string get_config_path(const std::string& name)
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return "";

		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		return dir + "\\" + filename;
	}

	bool ensure_config_directory()
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return false;

		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<config_info_t> get_config_list()
	{
		// Called every frame while the configs tab is open — cache the directory
		// scan for a short window so we're not doing filesystem I/O per frame.
		static std::mutex cache_mu;
		static std::vector<config_info_t> cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::vector<config_info_t> configs;
		std::string dir = get_config_directory();

		if (!dir.empty() && std::filesystem::exists(dir))
		{
			try
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file() && entry.path().extension() == ".json")
					{
						std::string stem = entry.path().stem().string();
						if (stem == "theme" || stem == "autoload_theme" || stem == "autoload" || stem == "window_state" || stem.find("theme") != std::string::npos)
						{
							continue;
						}
						config_info_t info;
						info.name = stem;
						info.path = entry.path().string();
						configs.push_back(info);
					}
				}
			}
			catch (...)
			{
			}
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = configs;
		has_scanned = true;
		return cached;
	}

	static void write_color(std::ostringstream& ss, const char* name, const float col[4])
	{
		ss << "    \"" << name << "\": [" << col[0] << ", " << col[1] << ", " << col[2] << ", " << col[3] << "],\n";
	}

	static std::string extract_section(const std::string& json, const char* section_name)
	{
		std::string search = "\"" + std::string(section_name) + "\": {";
		size_t start = json.find(search);
		if (start == std::string::npos)
			return "";

		start = json.find("{", start);
		if (start == std::string::npos)
			return "";

		int depth = 0;
		size_t pos = start;
		for (; pos < json.length(); pos++)
		{
			if (json[pos] == '{')
				depth++;
			else if (json[pos] == '}')
			{
				depth--;
				if (depth == 0)
					break;
			}
		}

		if (pos < json.length())
			return json.substr(start, pos - start + 1);
		return "";
	}

	static void read_color(const std::string& json, const char* name, float col[4])
	{
		std::string search = "\"" + std::string(name) + "\": [";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("]", pos);
			if (end != std::string::npos)
			{
				std::string values = json.substr(pos, end - pos);
				sscanf_s(values.c_str(), "%f, %f, %f, %f", &col[0], &col[1], &col[2], &col[3]);
			}
		}
	}

	static void write_float(std::ostringstream& ss, const char* name, float value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static float read_float(const std::string& json, const char* name, float default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return static_cast<float>(atof(value.c_str()));
			}
		}
		return default_value;
	}

	static void write_int(std::ostringstream& ss, const char* name, int value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static int read_int(const std::string& json, const char* name, int default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return atoi(value.c_str());
			}
		}
		return default_value;
	}

	static void write_bool(std::ostringstream& ss, const char* name, bool value)
	{
		ss << "    \"" << name << "\": " << (value ? "true" : "false") << ",\n";
	}

	static bool read_bool(const std::string& json, const char* name, bool default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			std::string value = json.substr(pos, 5);
			if (value.find("true") == 0)
				return true;
			if (value.find("false") == 0)
				return false;
		}
		return default_value;
	}
	static void write_string(std::ostringstream& ss, const char* name, const char* value)
	{
		ss << "    \"" << name << "\": \"" << value << "\",\n";
	}

	static std::string read_string(const std::string& json, const char* name, const std::string& default_value)
	{
		std::string search = "\"" + std::string(name) + "\": \"";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("\"", pos);
			if (end != std::string::npos)
			{
				return json.substr(pos, end - pos);
			}
		}
		return default_value;
	}

	static void write_string_vector(std::ostringstream& ss, const char* name, const std::vector<std::string>& vec)
	{
		ss << "    \"" << name << "\": [";
		for (size_t i = 0; i < vec.size(); i++)
		{
			ss << "\"" << vec[i] << "\"";
			if (i + 1 < vec.size())
				ss << ", ";
		}
		ss << "],\n";
	}

	static std::vector<std::string> read_string_vector(const std::string& json, const char* name)
	{
		std::vector<std::string> vec;
		std::string search = "\"" + std::string(name) + "\": [";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("]", pos);
			if (end != std::string::npos)
			{
				std::string elements_str = json.substr(pos, end - pos);
				size_t el_start = 0;
				while (true)
				{
					size_t q_start = elements_str.find("\"", el_start);
					if (q_start == std::string::npos)
						break;
					size_t q_end = elements_str.find("\"", q_start + 1);
					if (q_end == std::string::npos)
						break;
					vec.push_back(elements_str.substr(q_start + 1, q_end - q_start - 1));
					el_start = q_end + 1;
				}
			}
		}
		return vec;
	}

	static void write_priority_settings(std::ostringstream& ss, const char* priority_name, const settings::visuals::priority_settings_t& settings)
	{
		ss << "    \"" << priority_name << "\": {\n";
		write_bool(ss, "box", settings.box);
		write_int(ss, "box_type", settings.box_type);
		write_int(ss, "box_style", settings.box_style);
		write_float(ss, "corner_box_length", settings.corner_box_length);
		write_bool(ss, "box_inline", settings.box_inline);
		write_bool(ss, "box_gradient", settings.box_gradient);
		write_int(ss, "box_gradient_type", settings.box_gradient_type);
		write_float(ss, "box_pulse_speed", settings.box_pulse_speed);
		write_color(ss, "colour", settings.colour);
		write_color(ss, "box_colour_mid", settings.box_colour_mid);
		write_color(ss, "box_colour_low", settings.box_colour_low);
		write_bool(ss, "box_fill", settings.box_fill);
		write_int(ss, "box_fill_style", settings.box_fill_style);
		write_bool(ss, "box_fill_gradient", settings.box_fill_gradient);
		write_bool(ss, "box_fill_gradient_spin", settings.box_fill_gradient_spin);
		write_float(ss, "box_fill_gradient_speed", settings.box_fill_gradient_speed);
		write_int(ss, "box_fill_gradient_type", settings.box_fill_gradient_type);
		write_int(ss, "box_fill_gradient_mask", settings.box_fill_gradient_mask);
		write_float(ss, "box_fill_pulse_speed", settings.box_fill_pulse_speed);
		write_color(ss, "box_fill_colour", settings.box_fill_colour);
		write_color(ss, "box_fill_colour_mid", settings.box_fill_colour_mid);
		write_color(ss, "box_fill_colour_low", settings.box_fill_colour_low);
		write_bool(ss, "username", settings.username);
		write_int(ss, "username_type", settings.username_type);
		write_int(ss, "name_transform", settings.name_transform);
		write_int(ss, "name_bracket_style", settings.name_bracket_style);
		write_color(ss, "username_colour", settings.username_colour);
		write_bool(ss, "distance", settings.distance);
		write_color(ss, "distance_colour", settings.distance_colour);
		write_bool(ss, "tool", settings.tool);
		write_color(ss, "tool_colour", settings.tool_colour);
		write_bool(ss, "healthbar", settings.healthbar);
		write_int(ss, "healthbar_style", settings.healthbar_style);
		write_float(ss, "healthbar_padding", settings.healthbar_padding);
		write_int(ss, "healthbar_gradient_type", settings.healthbar_gradient_type);
		write_float(ss, "healthbar_pulse_speed", settings.healthbar_pulse_speed);
		write_bool(ss, "healthbar_text", settings.healthbar_text);
		write_color(ss, "healthbar_text_colour", settings.healthbar_text_colour);
		write_bool(ss, "healthbar_lerp", settings.healthbar_lerp);
		write_bool(ss, "value_follow", settings.value_follow);
		write_bool(ss, "ignore_full", settings.ignore_full);
		write_bool(ss, "bar_fold", settings.bar_fold);
		write_float(ss, "bar_size", settings.bar_size);
		write_color(ss, "healthbar_colour", settings.healthbar_colour);
		write_color(ss, "healthbar_colour_mid", settings.healthbar_colour_mid);
		write_color(ss, "healthbar_colour_low", settings.healthbar_colour_low);
		write_bool(ss, "tracers", settings.tracers);
		write_int(ss, "tracers_origin", settings.tracers_origin);
		write_float(ss, "tracers_thickness", settings.tracers_thickness);
		write_color(ss, "tracers_colour", settings.tracers_colour);
		write_bool(ss, "chams", settings.chams);
		write_int(ss, "chams_type", settings.chams_type);
		write_int(ss, "chams_texture", settings.chams_texture);
		write_int(ss, "mesh_shader", settings.mesh_shader);
		write_bool(ss, "mesh_outline", settings.mesh_outline);
		write_int(ss, "mesh_outline_mode", settings.mesh_outline_mode);
		write_int(ss, "mesh_outline_style", settings.mesh_outline_style);
		write_float(ss, "mesh_outline_thickness", settings.mesh_outline_thickness);
		write_bool(ss, "mesh_outline_only", settings.mesh_outline_only);
		write_bool(ss, "mesh_accessories", settings.mesh_accessories);
		write_float(ss, "mesh_depth_bias", settings.mesh_depth_bias);
		write_bool(ss, "chams_corpse", settings.chams_corpse);
		write_bool(ss, "chams_health_based", settings.chams_health_based);
		write_bool(ss, "chams_flash", settings.chams_flash);
		write_float(ss, "chams_flash_speed", settings.chams_flash_speed);
		write_color(ss, "chams_flash_colour", settings.chams_flash_colour);
		write_bool(ss, "chams_fade", settings.chams_fade);
		write_float(ss, "chams_fade_speed", settings.chams_fade_speed);
		write_color(ss, "chams_colour", settings.chams_colour);
		write_color(ss, "chams_outline_colour", settings.chams_outline_colour);
		write_bool(ss, "chams_outline_glow", settings.chams_outline_glow);
		write_color(ss, "chams_glow_colour", settings.chams_glow_colour);
		write_float(ss, "chams_glow_radius", settings.chams_glow_radius);
		write_float(ss, "chams_glow_alpha", settings.chams_glow_alpha);
		write_float(ss, "chams_glow_intensity", settings.chams_glow_intensity);
		write_bool(ss, "skeleton", settings.skeleton);
		write_color(ss, "skeleton_colour", settings.skeleton_colour);
		write_color(ss, "skeleton_outline_colour", settings.skeleton_outline_colour);
		write_bool(ss, "head_dot", settings.head_dot);
		write_int(ss, "head_type", settings.head_type);
		write_int(ss, "head_mode", settings.head_mode);
		write_bool(ss, "circular_head_dot", settings.circular_head_dot);
		write_color(ss, "head_dot_colour", settings.head_dot_colour);
		write_color(ss, "head_dot_outline_colour", settings.head_dot_outline_colour);
		write_float(ss, "head_dot_size", settings.head_dot_size);
		write_bool(ss, "corpse", settings.corpse);
		write_color(ss, "corpse_colour", settings.corpse_colour);
		write_color(ss, "corpse_outline_colour", settings.corpse_outline_colour);
		write_bool(ss, "corpse_names", settings.corpse_names);
		write_color(ss, "corpse_names_colour", settings.corpse_names_colour);
		write_bool(ss, "offscreen_arrows", settings.offscreen_arrows);
		write_int(ss, "arrow_type", settings.arrow_type);
		write_int(ss, "arrow_mode", settings.arrow_mode);
		write_float(ss, "offscreen_arrows_size", settings.offscreen_arrows_size);
		write_float(ss, "offscreen_arrows_radius", settings.offscreen_arrows_radius);
		write_float(ss, "offscreen_arrows_position", settings.offscreen_arrows_position);
		write_float(ss, "arrows_max_dist", settings.arrows_max_dist);
		write_bool(ss, "arrows_glow", settings.arrows_glow);
		write_color(ss, "offscreen_arrows_colour", settings.offscreen_arrows_colour);
		write_color(ss, "arrows_glow_colour", settings.arrows_glow_colour);
		write_bool(ss, "movement_trails", settings.movement_trails);
		write_float(ss, "movement_trails_thickness", settings.movement_trails_thickness);
		write_float(ss, "movement_trails_length", settings.movement_trails_length);
		write_color(ss, "movement_trails_colour", settings.movement_trails_colour);
		write_bool(ss, "view_angle_lines", settings.view_angle_lines);
		write_float(ss, "view_angle_lines_thickness", settings.view_angle_lines_thickness);
		write_float(ss, "view_angle_lines_length", settings.view_angle_lines_length);
		write_color(ss, "view_angle_lines_colour", settings.view_angle_lines_colour);
		write_bool(ss, "sound_esp", settings.sound_esp);
		write_color(ss, "sound_esp_colour", settings.sound_esp_colour);
		write_float(ss, "sound_esp_radius", settings.sound_esp_radius);
		write_float(ss, "sound_esp_speed", settings.sound_esp_speed);
		write_bool(ss, "footprints", settings.footprints);
		write_bool(ss, "footprints_glow", settings.footprints_glow);
		write_color(ss, "footprints_colour", settings.footprints_colour);
		write_float(ss, "footprints_radius", settings.footprints_radius);
		write_float(ss, "footprints_speed", settings.footprints_speed);
		write_bool(ss, "state_flags", settings.state_flags);
		write_bool(ss, "rig_flags", settings.rig_flags);
		ss << "    },\n";
	}

	static void read_priority_settings(const std::string& visuals_json, const char* priority_name, settings::visuals::priority_settings_t& settings)
	{
		std::string search = "\"" + std::string(priority_name) + "\": {";
		size_t start = visuals_json.find(search);
		if (start == std::string::npos)
			return;

		start = visuals_json.find("{", start);
		if (start == std::string::npos)
			return;

		int depth = 0;
		size_t pos = start;
		for (; pos < visuals_json.length(); pos++)
		{
			if (visuals_json[pos] == '{')
				depth++;
			else if (visuals_json[pos] == '}')
			{
				depth--;
				if (depth == 0)
					break;
			}
		}

		if (pos >= visuals_json.length())
			return;

		std::string section = visuals_json.substr(start, pos - start + 1);

		settings.box = read_bool(section, "box", settings.box);
		settings.box_type = read_int(section, "box_type", settings.box_type);
		settings.box_style = read_int(section, "box_style", settings.box_style);
		settings.corner_box_length = read_float(section, "corner_box_length", settings.corner_box_length);
		settings.box_inline = read_bool(section, "box_inline", settings.box_inline);
		settings.box_gradient = read_bool(section, "box_gradient", settings.box_gradient);
		settings.box_gradient_type = read_int(section, "box_gradient_type", settings.box_gradient_type);
		settings.box_pulse_speed = read_float(section, "box_pulse_speed", settings.box_pulse_speed);
		read_color(section, "colour", settings.colour);
		read_color(section, "box_colour_mid", settings.box_colour_mid);
		read_color(section, "box_colour_low", settings.box_colour_low);
		settings.box_fill = read_bool(section, "box_fill", settings.box_fill);
		settings.box_fill_style = read_int(section, "box_fill_style", settings.box_fill_style);
		settings.box_fill_gradient = read_bool(section, "box_fill_gradient", settings.box_fill_gradient);
		settings.box_fill_gradient_spin = read_bool(section, "box_fill_gradient_spin", settings.box_fill_gradient_spin);
		settings.box_fill_gradient_speed = read_float(section, "box_fill_gradient_speed", settings.box_fill_gradient_speed);
		settings.box_fill_gradient_type = read_int(section, "box_fill_gradient_type", settings.box_fill_gradient_type);
		settings.box_fill_gradient_mask = read_int(section, "box_fill_gradient_mask", settings.box_fill_gradient_mask);
		settings.box_fill_pulse_speed = read_float(section, "box_fill_pulse_speed", settings.box_fill_pulse_speed);
		read_color(section, "box_fill_colour", settings.box_fill_colour);
		read_color(section, "box_fill_colour_mid", settings.box_fill_colour_mid);
		read_color(section, "box_fill_colour_low", settings.box_fill_colour_low);
		settings.username = read_bool(section, "username", settings.username);
		settings.username_type = read_int(section, "username_type", settings.username_type);
		settings.name_transform = read_int(section, "name_transform", settings.name_transform);
		settings.name_bracket_style = read_int(section, "name_bracket_style", settings.name_bracket_style);
		read_color(section, "username_colour", settings.username_colour);
		settings.distance = read_bool(section, "distance", settings.distance);
		read_color(section, "distance_colour", settings.distance_colour);
		settings.tool = read_bool(section, "tool", settings.tool);
		read_color(section, "tool_colour", settings.tool_colour);
		settings.healthbar = read_bool(section, "healthbar", settings.healthbar);
		settings.healthbar_style = read_int(section, "healthbar_style", settings.healthbar_style);
		settings.healthbar_padding = read_float(section, "healthbar_padding", settings.healthbar_padding);
		settings.healthbar_gradient_type = read_int(section, "healthbar_gradient_type", settings.healthbar_gradient_type);
		settings.healthbar_pulse_speed = read_float(section, "healthbar_pulse_speed", settings.healthbar_pulse_speed);
		settings.healthbar_text = read_bool(section, "healthbar_text", settings.healthbar_text);
		read_color(section, "healthbar_text_colour", settings.healthbar_text_colour);
		settings.healthbar_lerp = read_bool(section, "healthbar_lerp", settings.healthbar_lerp);
		settings.value_follow = read_bool(section, "value_follow", settings.value_follow);
		settings.ignore_full = read_bool(section, "ignore_full", settings.ignore_full);
		settings.bar_fold = read_bool(section, "bar_fold", settings.bar_fold);
		settings.bar_size = read_float(section, "bar_size", settings.bar_size);
		read_color(section, "healthbar_colour", settings.healthbar_colour);
		read_color(section, "healthbar_colour_mid", settings.healthbar_colour_mid);
		read_color(section, "healthbar_colour_low", settings.healthbar_colour_low);
		settings.tracers = read_bool(section, "tracers", settings.tracers);
		settings.tracers_origin = read_int(section, "tracers_origin", settings.tracers_origin);
		settings.tracers_thickness = read_float(section, "tracers_thickness", settings.tracers_thickness);
		read_color(section, "tracers_colour", settings.tracers_colour);
		settings.chams = read_bool(section, "chams", settings.chams);
		settings.chams_type = read_int(section, "chams_type", settings.chams_type);
		settings.chams_texture = read_int(section, "chams_texture", settings.chams_texture);
		settings.mesh_shader = read_int(section, "mesh_shader", settings.mesh_shader);
		settings.mesh_outline = read_bool(section, "mesh_outline", settings.mesh_outline);
		settings.mesh_outline_mode = read_int(section, "mesh_outline_mode", settings.mesh_outline_mode);
		settings.mesh_outline_style = read_int(section, "mesh_outline_style", settings.mesh_outline_style);
		settings.mesh_outline_thickness = read_float(section, "mesh_outline_thickness", settings.mesh_outline_thickness);
		settings.mesh_outline_only = read_bool(section, "mesh_outline_only", settings.mesh_outline_only);
		settings.mesh_accessories = read_bool(section, "mesh_accessories", settings.mesh_accessories);
		settings.mesh_depth_bias = read_float(section, "mesh_depth_bias", settings.mesh_depth_bias);
		settings.chams_corpse = read_bool(section, "chams_corpse", settings.chams_corpse);
		settings.chams_health_based = read_bool(section, "chams_health_based", settings.chams_health_based);
		settings.chams_flash = read_bool(section, "chams_flash", settings.chams_flash);
		settings.chams_flash_speed = read_float(section, "chams_flash_speed", settings.chams_flash_speed);
		read_color(section, "chams_flash_colour", settings.chams_flash_colour);
		settings.chams_fade = read_bool(section, "chams_fade", settings.chams_fade);
		settings.chams_fade_speed = read_float(section, "chams_fade_speed", settings.chams_fade_speed);
		read_color(section, "chams_colour", settings.chams_colour);
		read_color(section, "chams_outline_colour", settings.chams_outline_colour);
		settings.chams_outline_glow = read_bool(section, "chams_outline_glow", settings.chams_outline_glow);
		read_color(section, "chams_glow_colour", settings.chams_glow_colour);
		settings.chams_glow_radius = read_float(section, "chams_glow_radius", settings.chams_glow_radius);
		settings.chams_glow_alpha = read_float(section, "chams_glow_alpha", settings.chams_glow_alpha);
		settings.chams_glow_intensity = read_float(section, "chams_glow_intensity", settings.chams_glow_intensity);
		settings.skeleton = read_bool(section, "skeleton", settings.skeleton);
		read_color(section, "skeleton_colour", settings.skeleton_colour);
		read_color(section, "skeleton_outline_colour", settings.skeleton_outline_colour);
		settings.head_dot = read_bool(section, "head_dot", settings.head_dot);
		settings.head_type = read_int(section, "head_type", settings.head_type);
		settings.head_mode = read_int(section, "head_mode", settings.head_mode);
		settings.circular_head_dot = read_bool(section, "circular_head_dot", settings.circular_head_dot);
		read_color(section, "head_dot_colour", settings.head_dot_colour);
		read_color(section, "head_dot_outline_colour", settings.head_dot_outline_colour);
		settings.head_dot_size = read_float(section, "head_dot_size", settings.head_dot_size);
		settings.corpse = read_bool(section, "corpse", settings.corpse);
		read_color(section, "corpse_colour", settings.corpse_colour);
		read_color(section, "corpse_outline_colour", settings.corpse_outline_colour);
		settings.corpse_names = read_bool(section, "corpse_names", settings.corpse_names);
		read_color(section, "corpse_names_colour", settings.corpse_names_colour);
		settings.offscreen_arrows = read_bool(section, "offscreen_arrows", settings.offscreen_arrows);
		settings.arrow_type = read_int(section, "arrow_type", settings.arrow_type);
		settings.arrow_mode = read_int(section, "arrow_mode", settings.arrow_mode);
		settings.offscreen_arrows_size = read_float(section, "offscreen_arrows_size", settings.offscreen_arrows_size);
		settings.offscreen_arrows_radius = read_float(section, "offscreen_arrows_radius", settings.offscreen_arrows_radius);
		settings.offscreen_arrows_position = read_float(section, "offscreen_arrows_position", settings.offscreen_arrows_position);
		settings.arrows_max_dist = read_float(section, "arrows_max_dist", settings.arrows_max_dist);
		settings.arrows_glow = read_bool(section, "arrows_glow", settings.arrows_glow);
		read_color(section, "offscreen_arrows_colour", settings.offscreen_arrows_colour);
		read_color(section, "arrows_glow_colour", settings.arrows_glow_colour);
		settings.movement_trails = read_bool(section, "movement_trails", settings.movement_trails);
		settings.movement_trails_thickness = read_float(section, "movement_trails_thickness", settings.movement_trails_thickness);
		settings.movement_trails_length = read_float(section, "movement_trails_length", settings.movement_trails_length);
		read_color(section, "movement_trails_colour", settings.movement_trails_colour);
		settings.view_angle_lines = read_bool(section, "view_angle_lines", settings.view_angle_lines);
		settings.view_angle_lines_thickness = read_float(section, "view_angle_lines_thickness", settings.view_angle_lines_thickness);
		settings.view_angle_lines_length = read_float(section, "view_angle_lines_length", settings.view_angle_lines_length);
		read_color(section, "view_angle_lines_colour", settings.view_angle_lines_colour);
		settings.sound_esp = read_bool(section, "sound_esp", settings.sound_esp);
		read_color(section, "sound_esp_colour", settings.sound_esp_colour);
		settings.sound_esp_radius = read_float(section, "sound_esp_radius", settings.sound_esp_radius);
		settings.sound_esp_speed = read_float(section, "sound_esp_speed", settings.sound_esp_speed);
		settings.footprints = read_bool(section, "footprints", settings.footprints);
		settings.footprints_glow = read_bool(section, "footprints_glow", settings.footprints_glow);
		read_color(section, "footprints_colour", settings.footprints_colour);
		settings.footprints_radius = read_float(section, "footprints_radius", settings.footprints_radius);
		settings.footprints_speed = read_float(section, "footprints_speed", settings.footprints_speed);
		settings.state_flags = read_bool(section, "state_flags", settings.state_flags);
		settings.rig_flags = read_bool(section, "rig_flags", settings.rig_flags);
	}

	bool save_config(const std::string& name)
	{
		if (!ensure_config_directory())
			return false;

		std::string path = get_config_path(name);
		if (path.empty())
			return false;

		std::ostringstream ss;
		ss << "{\n";
		write_bool(ss, "teamcheck", settings::teamcheck);

		ss << "  \"aimbot\": {\n";
		write_bool(ss, "enabled", settings::aimbot::enabled);
		write_int(ss, "keybind", settings::aimbot::keybind);
		write_int(ss, "activation_mode", settings::aimbot::activation_mode);
		write_int(ss, "mode", settings::aimbot::mode);
		write_float(ss, "mouse_sensitivity", settings::aimbot::mouse_sensitivity);
		write_int(ss, "target_part", settings::aimbot::target_part);
		write_int(ss, "air_part", settings::aimbot::air_part);
		write_float(ss, "fov", settings::aimbot::fov);
		write_bool(ss, "use_fov", settings::aimbot::use_fov);
		write_bool(ss, "dynamic_fov", settings::aimbot::dynamic_fov);
		write_bool(ss, "ring_fov_enabled", settings::aimbot::ring_fov_enabled);
		write_float(ss, "ring_fov_inner", settings::aimbot::ring_fov_inner);
		write_float(ss, "ring_fov_outer", settings::aimbot::ring_fov_outer);
		write_bool(ss, "smoothing", settings::aimbot::smoothing);
		write_float(ss, "smoothingx", settings::aimbot::smoothingx);
		write_float(ss, "smoothingy", settings::aimbot::smoothingy);
		write_int(ss, "smoothing_style", settings::aimbot::smoothing_style);
		write_bool(ss, "shake", settings::aimbot::shake);
		write_float(ss, "shake_value", settings::aimbot::shake_value);
		write_bool(ss, "enable_prediction", settings::aimbot::enable_prediction);
		write_float(ss, "prediction_x", settings::aimbot::prediction_x);
		write_float(ss, "prediction_y", settings::aimbot::prediction_y);
		write_bool(ss, "jump_prediction", settings::aimbot::jump_prediction);
		write_float(ss, "jump_prediction_value", settings::aimbot::jump_prediction_value);
		write_bool(ss, "fall_prediction", settings::aimbot::fall_prediction);
		write_float(ss, "fall_prediction_value", settings::aimbot::fall_prediction_value);
		write_bool(ss, "jump_fall_prediction", settings::aimbot::jump_fall_prediction);
		write_bool(ss, "teamcheck", settings::aimbot::teamcheck);
		write_bool(ss, "knock_check", settings::aimbot::knock_check);
		write_bool(ss, "disable_on_kill", settings::aimbot::disable_on_kill);
		write_int(ss, "priorities", settings::aimbot::priorities);
		write_bool(ss, "health_check_enabled", settings::aimbot::health_check_enabled);
		write_float(ss, "min_health", settings::aimbot::min_health);
		write_bool(ss, "sticky_aim", settings::aimbot::sticky_aim);
		write_bool(ss, "draw_fov", settings::aimbot::draw_fov);
		write_bool(ss, "fill_fov", settings::aimbot::fill_fov);
		write_color(ss, "fov_circle_colour", settings::aimbot::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::aimbot::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::aimbot::fov_fill_colour);
		ss << "  },\n";

		ss << "  \"triggerbot\": {\n";
		write_bool(ss, "enabled", settings::triggerbot::enabled);
		write_int(ss, "method", settings::triggerbot::method);
		write_int(ss, "keybind", settings::triggerbot::keybind);
		write_int(ss, "activation_mode", settings::triggerbot::activation_mode);
		write_int(ss, "target_part", settings::triggerbot::target_part);
		write_float(ss, "threshold", settings::triggerbot::threshold);
		write_float(ss, "delay_ms", settings::triggerbot::delay_ms);
		write_float(ss, "cooldown_ms", settings::triggerbot::cooldown_ms);
		write_bool(ss, "teamcheck", settings::triggerbot::teamcheck);
		write_bool(ss, "knock_check", settings::triggerbot::knock_check);
		write_bool(ss, "guncheck", settings::triggerbot::guncheck);
		write_bool(ss, "wallcheck", settings::triggerbot::wallcheck);
		write_bool(ss, "draw_fov", settings::triggerbot::draw_fov);
		write_bool(ss, "fill_fov", settings::triggerbot::fill_fov);
		write_color(ss, "fov_circle_colour", settings::triggerbot::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::triggerbot::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::triggerbot::fov_fill_colour);
		ss << "  },\n";

		ss << "  \"silentaim\": {\n";
		write_bool(ss, "enabled", settings::silentaim::enabled);
		write_int(ss, "method", settings::silentaim::method);
		write_int(ss, "keybind", settings::silentaim::keybind);
		write_int(ss, "activation_mode", settings::silentaim::activation_mode);
		write_int(ss, "target_part", settings::silentaim::target_part);
		write_bool(ss, "snapline", settings::silentaim::snapline);
		write_bool(ss, "snapline_lerp", settings::silentaim::snapline_lerp);
		write_int(ss, "snapline_origin", settings::silentaim::snapline_origin);
		write_color(ss, "snapline_colour", settings::silentaim::snapline_colour);
		write_float(ss, "fov", settings::silentaim::fov);
		write_bool(ss, "mode_360", settings::silentaim::mode_360);
		write_bool(ss, "use_fov", settings::silentaim::use_fov);
		write_bool(ss, "dynamic_fov", settings::silentaim::dynamic_fov);
		write_bool(ss, "use_aimbot_target", settings::silentaim::use_aimbot_target);
		write_bool(ss, "draw_fov", settings::silentaim::draw_fov);
		write_bool(ss, "fill_fov", settings::silentaim::fill_fov);
		write_color(ss, "fov_circle_colour", settings::silentaim::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::silentaim::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::silentaim::fov_fill_colour);
		write_bool(ss, "sticky_aim", settings::silentaim::sticky_aim);
		write_bool(ss, "knock_check", settings::silentaim::knock_check);
		write_bool(ss, "auto_switch", settings::silentaim::auto_switch);
		write_bool(ss, "spoof_mouse", settings::silentaim::spoof_mouse);
		write_bool(ss, "teamcheck", settings::silentaim::teamcheck);
		write_bool(ss, "guncheck", settings::silentaim::guncheck);
		write_int(ss, "priorities", settings::silentaim::priorities);
		write_bool(ss, "health_check_enabled", settings::silentaim::health_check_enabled);
		write_float(ss, "min_health", settings::silentaim::min_health);
		write_bool(ss, "enable_prediction", settings::silentaim::enable_prediction);
		write_float(ss, "prediction_x", settings::silentaim::prediction_x);
		write_float(ss, "prediction_y", settings::silentaim::prediction_y);
		write_bool(ss, "forcefield_check", settings::silentaim::forcefield_check);
		write_bool(ss, "katana_check", settings::silentaim::katana_check);
		ss << "  },\n";

		ss << "  \"raycast_silentaim\": {\n";
		write_bool(ss, "enabled", settings::raycast_silentaim::enabled);
		write_bool(ss, "mode_360", settings::raycast_silentaim::mode_360);
		write_int(ss, "mode_360_keybind", settings::raycast_silentaim::mode_360_keybind);
		write_int(ss, "mode_360_activation_mode", settings::raycast_silentaim::mode_360_activation_mode);
		write_bool(ss, "draw_fov", settings::raycast_silentaim::draw_fov);
		write_bool(ss, "fill_fov", settings::raycast_silentaim::fill_fov);
		write_bool(ss, "enable_prediction", settings::raycast_silentaim::enable_prediction);
		write_bool(ss, "teamcheck", settings::raycast_silentaim::teamcheck);
		write_bool(ss, "magic_bullet", settings::raycast_silentaim::magic_bullet);
		write_bool(ss, "forcefield_check", settings::raycast_silentaim::forcefield_check);
		write_bool(ss, "hitmarkers", settings::raycast_silentaim::hitmarkers);
		write_color(ss, "hitmarker_colour", settings::raycast_silentaim::hitmarker_colour);
		write_float(ss, "hitmarker_size", settings::raycast_silentaim::hitmarker_size);
		write_float(ss, "hitmarker_gap", settings::raycast_silentaim::hitmarker_gap);
		write_float(ss, "hitmarker_duration", settings::raycast_silentaim::hitmarker_duration);
		write_bool(ss, "hit_flash", settings::raycast_silentaim::hit_flash);
		write_color(ss, "hit_flash_colour", settings::raycast_silentaim::hit_flash_colour);
		write_float(ss, "hit_flash_duration", settings::raycast_silentaim::hit_flash_duration);
		write_int(ss, "magic_bullet_keybind", settings::raycast_silentaim::magic_bullet_keybind);
		write_int(ss, "magic_bullet_activation_mode", settings::raycast_silentaim::magic_bullet_activation_mode);
		write_int(ss, "magic_bullet_method", settings::raycast_silentaim::magic_bullet_method);
		write_bool(ss, "use_fov", settings::raycast_silentaim::use_fov);
		write_bool(ss, "dynamic_fov", settings::raycast_silentaim::dynamic_fov);
		write_float(ss, "fov", settings::raycast_silentaim::fov);
		write_color(ss, "fov_circle_colour", settings::raycast_silentaim::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::raycast_silentaim::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::raycast_silentaim::fov_fill_colour);
		write_int(ss, "keybind", settings::raycast_silentaim::keybind);
		write_int(ss, "activation_mode", settings::raycast_silentaim::activation_mode);
		ss << "  },\n";

		ss << "  \"crosshair\": {\n";
		write_bool(ss, "enabled", settings::crosshair::enabled);
		write_bool(ss, "attach_to_enemy", settings::crosshair::attach_to_enemy);
		write_bool(ss, "lerp", settings::crosshair::lerp);
		write_float(ss, "lerp_speed", settings::crosshair::lerp_speed);
		write_int(ss, "style", settings::crosshair::style);
		write_color(ss, "colour", settings::crosshair::colour);
		write_color(ss, "outline_colour", settings::crosshair::outline_colour);
		write_bool(ss, "outline", settings::crosshair::outline);
		write_float(ss, "size", settings::crosshair::size);
		write_float(ss, "gap", settings::crosshair::gap);
		write_float(ss, "thickness", settings::crosshair::thickness);
		write_bool(ss, "dot", settings::crosshair::dot);
		write_float(ss, "dot_size", settings::crosshair::dot_size);
		write_bool(ss, "rotate", settings::crosshair::rotate);
		write_float(ss, "rotate_speed", settings::crosshair::rotate_speed);
		write_bool(ss, "pulse", settings::crosshair::pulse);
		write_float(ss, "pulse_speed", settings::crosshair::pulse_speed);
		write_float(ss, "pulse_amount", settings::crosshair::pulse_amount);
		write_bool(ss, "rainbow", settings::crosshair::rainbow);
		write_float(ss, "rainbow_speed", settings::crosshair::rainbow_speed);
		write_bool(ss, "dynamic_gap", settings::crosshair::dynamic_gap);
		write_float(ss, "expansion_amount", settings::crosshair::expansion_amount);
		write_float(ss, "expansion_decay", settings::crosshair::expansion_decay);
		ss << "  },\n";

		ss << "  \"hitsounds\": {\n";
		write_bool(ss, "enabled", settings::hitsounds::enabled);
		write_int(ss, "type", settings::hitsounds::type);
		write_int(ss, "method", settings::hitsounds::method);
		write_string(ss, "custom_path", settings::hitsounds::custom_path);
		write_float(ss, "volume", settings::hitsounds::volume);
		ss << "  },\n";

		ss << "  \"killsounds\": {\n";
		write_bool(ss, "enabled", settings::killsounds::enabled);
		write_int(ss, "type", settings::killsounds::type);
		write_string(ss, "custom_path", settings::killsounds::custom_path);
		write_float(ss, "volume", settings::killsounds::volume);
		ss << "  },\n";

		ss << "  \"animationchanger\": {\n";
		write_bool(ss, "enabled", settings::animationchanger::enabled);
		write_int(ss, "type", settings::animationchanger::type);
		write_int(ss, "mode", settings::animationchanger::mode);
		ss << "  },\n";

		ss << "  \"materialchanger\": {\n";
		write_bool(ss, "enabled", settings::materialchanger::enabled);
		write_int(ss, "material_index", settings::materialchanger::material_index);
		write_bool(ss, "affect_accessories", settings::materialchanger::affect_accessories);
		ss << "  },\n";

		ss << "  \"godmode\": {\n";
		write_bool(ss, "enabled", settings::godmode::enabled);
		ss << "  },\n";

		ss << "  \"movement\": {\n";
		ss << "    \"speedhack\": {\n";
		write_bool(ss, "enabled", settings::movement::speedhack::enabled);
		write_int(ss, "mode", settings::movement::speedhack::mode);
		write_float(ss, "speed", settings::movement::speedhack::speed);
		write_int(ss, "keybind", settings::movement::speedhack::keybind);
		write_int(ss, "activation_mode", settings::movement::speedhack::activation_mode);
		ss << "    },\n";
		ss << "    \"flyhack\": {\n";
		write_bool(ss, "enabled", settings::movement::flyhack::enabled);
		write_int(ss, "mode", settings::movement::flyhack::mode);
		write_float(ss, "speed", settings::movement::flyhack::speed);
		write_int(ss, "keybind", settings::movement::flyhack::keybind);
		write_int(ss, "activation_mode", settings::movement::flyhack::activation_mode);
		ss << "    },\n";

		ss << "    \"spin360\": {\n";
		write_bool(ss, "enabled", settings::movement::spin360::enabled);
		write_int(ss, "keybind", settings::movement::spin360::keybind);
		write_float(ss, "speed", settings::movement::spin360::speed);
		write_float(ss, "smoothness", settings::movement::spin360::smoothness);
		ss << "    },\n";
		ss << "    \"gravity\": {\n";
		write_bool(ss, "enabled", settings::movement::gravity::enabled);
		write_float(ss, "value", settings::movement::gravity::value);
		ss << "    },\n";
		ss << "    \"tickrate\": {\n";
		write_bool(ss, "enabled", settings::movement::tickrate::enabled);
		write_float(ss, "value", settings::movement::tickrate::value);
		ss << "    },\n";
		ss << "    \"wallbug\": {\n";
		write_bool(ss, "enabled", settings::movement::wallbug::enabled);
		write_float(ss, "height", settings::movement::wallbug::height);
		ss << "    },\n";
		ss << "    \"bhop\": {\n";
		write_bool(ss, "enabled", settings::movement::bhop::enabled);
		write_float(ss, "speed", settings::movement::bhop::speed);
		write_int(ss, "keybind", settings::movement::bhop::keybind);
		write_int(ss, "activation_mode", settings::movement::bhop::activation_mode);
		ss << "    },\n";
		write_bool(ss, "noclip_enabled", settings::movement::noclip::enabled);
		write_int(ss, "noclip_keybind", settings::movement::noclip::keybind);
		write_int(ss, "noclip_activation_mode", settings::movement::noclip::activation_mode);
		write_bool(ss, "wallslide_enabled", settings::movement::wallslide::enabled);
		write_float(ss, "wallslide_speed", settings::movement::wallslide::speed);
		write_int(ss, "wallslide_keybind", settings::movement::wallslide::keybind);
		write_int(ss, "wallslide_activation_mode", settings::movement::wallslide::activation_mode);
		write_bool(ss, "pixelsurf_enabled", settings::movement::pixelsurf::enabled);
		write_float(ss, "pixelsurf_speed", settings::movement::pixelsurf::speed);
		write_int(ss, "pixelsurf_keybind", settings::movement::pixelsurf::keybind);
		write_int(ss, "pixelsurf_activation_mode", settings::movement::pixelsurf::activation_mode);
		write_bool(ss, "voidhide_enabled", settings::movement::voidhide::enabled);
		write_int(ss, "voidhide_keybind", settings::movement::voidhide::keybind);
		write_int(ss, "voidhide_activation_mode", settings::movement::voidhide::activation_mode);
		write_bool(ss, "third_person_enabled", settings::movement::third_person::enabled);
		write_float(ss, "third_person_distance", settings::movement::third_person::distance);
		write_int(ss, "third_person_keybind", settings::movement::third_person::keybind);
		write_int(ss, "third_person_activation_mode", settings::movement::third_person::activation_mode);
		write_bool(ss, "fov_changer_enabled", settings::movement::fov_changer::enabled);
		write_float(ss, "fov_changer_value", settings::movement::fov_changer::fov_value);
		write_bool(ss, "fov_changer_dynamic", settings::movement::fov_changer::dynamic);
		write_int(ss, "fov_changer_keybind", settings::movement::fov_changer::keybind);
		write_int(ss, "fov_changer_activation_mode", settings::movement::fov_changer::activation_mode);
		write_bool(ss, "freecam_enabled", settings::movement::freecam::enabled);
		write_int(ss, "freecam_keybind", settings::movement::freecam::keybind);
		write_int(ss, "freecam_activation_mode", settings::movement::freecam::activation_mode);
		write_int(ss, "freecam_mouse_look_mode", settings::movement::freecam::mouse_look_mode);
		write_float(ss, "freecam_speed", settings::movement::freecam::speed);
		write_float(ss, "freecam_sensitivity", settings::movement::freecam::sensitivity);
		write_float(ss, "freecam_shift_multiplier", settings::movement::freecam::shift_multiplier);
		write_bool(ss, "freecam_azerty", settings::movement::freecam::azerty);
		write_bool(ss, "freecam_fov_override", settings::movement::freecam::fov_override);
		write_float(ss, "freecam_fov_value", settings::movement::freecam::fov_value);
		write_bool(ss, "freecam_lock_character", settings::movement::freecam::lock_character);
		write_bool(ss, "infjump", settings::movement::infjump::enabled);
		write_bool(ss, "hipheight_enabled", settings::movement::hipheight::enabled);
		write_float(ss, "hipheight_value", settings::movement::hipheight::value);
		ss << "  },\n";

		ss << "  \"visuals\": {\n";
		write_bool(ss, "enabled", settings::visuals::enabled);
		write_int(ss, "keybind", settings::visuals::keybind);
		write_int(ss, "activation_mode", settings::visuals::activation_mode);
		write_priority_settings(ss, "neutral", settings::visuals::neutral);
		write_priority_settings(ss, "friendly", settings::visuals::friendly);
		write_priority_settings(ss, "hostile", settings::visuals::hostile);
		write_bool(ss, "teamcheck", settings::visuals::teamcheck);
		write_int(ss, "esp_font_index", settings::visuals::esp_font_index);
		write_float(ss, "esp_font_size", settings::visuals::esp_font_size);
		write_int(ss, "bounding_type", settings::visuals::bounding_type);
		for (size_t i = 0; i < settings::visuals::render_outlines.size(); i++) {
			std::string name = "render_outline_" + std::to_string(i);
			write_int(ss, name.c_str(), settings::visuals::render_outlines[i]);
		}
		write_float(ss, "skeleton_thickness", settings::visuals::skeleton_thickness);
		write_float(ss, "box_thickness", settings::visuals::box_thickness);
		write_float(ss, "healthbar_thickness", settings::visuals::healthbar_thickness);
		write_float(ss, "chams_thickness", settings::visuals::chams_thickness);
		write_float(ss, "chams_outline_thickness", settings::visuals::chams_outline_thickness);
		write_float(ss, "text_outline_thickness", settings::visuals::text_outline_thickness);
		write_int(ss, "distance_unit", settings::visuals::distance_unit);
		write_bool(ss, "distance_check", settings::visuals::distance_check);
		write_float(ss, "max_distance", settings::visuals::max_distance);
		write_bool(ss, "sort_by_status", settings::visuals::sort_by_status);
		write_bool(ss, "knock_check", settings::visuals::knock_check);
		write_bool(ss, "local_player", settings::visuals::local_player);
		write_bool(ss, "tool_check", settings::visuals::tool_check);
		write_bool(ss, "godded_check", settings::visuals::godded_check);
		write_bool(ss, "forcefield_check", settings::visuals::forcefield_check);
		write_bool(ss, "alive_check", settings::visuals::alive_check);
		write_bool(ss, "ragdoll_check", settings::visuals::ragdoll_check);
		write_bool(ss, "visible_check", settings::visuals::visible_check);
		write_bool(ss, "static_on_void", settings::visuals::static_on_void);
		write_bool(ss, "show_none", settings::visuals::show_none);
		write_bool(ss, "use_display_name", settings::visuals::use_display_name);
		write_bool(ss, "combined_name", settings::visuals::combined_name);
		write_int(ss, "global_outline_type", settings::visuals::global_outline_type);
		write_bool(ss, "ignore_full_health", settings::visuals::ignore_full_health);
		write_bool(ss, "static_on_death", settings::visuals::static_on_death);
		write_float(ss, "corpse_shift", settings::visuals::corpse_shift);
		write_float(ss, "corpse_max_dist", settings::visuals::corpse_max_dist);
		write_bool(ss, "enemy_highlight", settings::visuals::enemy_highlight);
		write_color(ss, "enemy_highlight_colour", settings::visuals::enemy_highlight_colour);
		write_bool(ss, "friendly_highlight", settings::visuals::friendly_highlight);
		write_color(ss, "friendly_highlight_colour", settings::visuals::friendly_highlight_colour);
		write_bool(ss, "bar_fill", settings::visuals::bar_fill);
		write_color(ss, "bar_fill_colour", settings::visuals::bar_fill_colour);
		write_bool(ss, "health_check_enabled", settings::visuals::health_check_enabled);
		write_float(ss, "min_health", settings::visuals::min_health);
		write_bool(ss, "chams_hit_impact", settings::visuals::chams_hit_impact);
		write_color(ss, "chams_hit_impact_colour", settings::visuals::chams_hit_impact_colour);
		write_bool(ss, "flags", settings::visuals::flags);
		write_int(ss, "flags_mask", settings::visuals::flags_mask);
		write_color(ss, "flags_state_colour", settings::visuals::flags_state_colour);
		write_color(ss, "flags_colour", settings::visuals::flags_colour);
		write_bool(ss, "client_korblox", settings::visuals::client_korblox);
		write_bool(ss, "client_headless", settings::visuals::client_headless);
		write_bool(ss, "black_avatar", settings::visuals::black_avatar);
		write_bool(ss, "target_recolor", settings::visuals::target_recolor);
		write_color(ss, "target_recolor_colour", settings::visuals::target_recolor_colour);
		write_bool(ss, "client_remove_hair", settings::visuals::client_remove_hair);
		write_bool(ss, "client_remove_accessories", settings::visuals::client_remove_accessories);
		write_bool(ss, "avatar_recolor_custom", settings::visuals::avatar_recolor_custom);
		write_color(ss, "avatar_recolor_color", settings::visuals::avatar_recolor_color);
		write_bool(ss, "avatar_recolor", settings::visuals::avatar_recolor);
		write_color(ss, "recolor_head", settings::visuals::recolor_head);
		write_color(ss, "recolor_torso", settings::visuals::recolor_torso);
		write_color(ss, "recolor_left_arm", settings::visuals::recolor_left_arm);
		write_color(ss, "recolor_right_arm", settings::visuals::recolor_right_arm);
		write_color(ss, "recolor_left_leg", settings::visuals::recolor_left_leg);
		write_color(ss, "recolor_right_leg", settings::visuals::recolor_right_leg);
		write_bool(ss, "engine_chams", settings::visuals::engine_chams);
		write_int(ss, "engine_chams_style", settings::visuals::engine_chams_style);
		write_int(ss, "engine_chams_color_index", settings::visuals::engine_chams_color_index);
		write_int(ss, "engine_chams_mode", settings::visuals::engine_chams_mode);
		write_int(ss, "engine_chams_queue_id", settings::visuals::engine_chams_queue_id);
		write_color(ss, "engine_chams_colour", settings::visuals::engine_chams_colour);
		write_bool(ss, "engine_chams_weapons", settings::visuals::engine_chams_weapons);
		write_int(ss, "engine_chams_weapons_style", settings::visuals::engine_chams_weapons_style);
		write_color(ss, "engine_chams_weapons_colour", settings::visuals::engine_chams_weapons_colour);
		write_bool(ss, "engine_chams_black_only", settings::visuals::engine_chams_black_only);
		ss << "  },\n";

		ss << "  \"hitboxexpander\": {\n";
		write_bool(ss, "enabled", settings::hitboxexpander::enabled);
		write_float(ss, "size_x", settings::hitboxexpander::size_x);
		write_float(ss, "size_y", settings::hitboxexpander::size_y);
		write_float(ss, "size_z", settings::hitboxexpander::size_z);
		write_bool(ss, "visualize", settings::hitboxexpander::visualize);
		write_color(ss, "hitbox_colour", settings::hitboxexpander::hitbox_colour);
		write_color(ss, "hitbox_outline_colour", settings::hitboxexpander::hitbox_outline_colour);
		ss << "  },\n";

		ss << "  \"hit_tracers\": {\n";
		write_bool(ss, "enabled", settings::hit_tracers::enabled);
		write_int(ss, "origin_type", settings::hit_tracers::origin_type);
		write_int(ss, "style", settings::hit_tracers::style);
		write_float(ss, "duration", settings::hit_tracers::duration);
		write_float(ss, "thickness", settings::hit_tracers::thickness);
		write_color(ss, "colour", settings::hit_tracers::colour);
		write_color(ss, "outline_colour", settings::hit_tracers::outline_colour);
		write_bool(ss, "draw_outline", settings::hit_tracers::draw_outline);
		write_bool(ss, "damage_text", settings::hit_tracers::damage_text);
		write_color(ss, "damage_text_colour", settings::hit_tracers::damage_text_colour);
		ss << "  },\n";

		ss << "  \"hit_chams\": {\n";
		write_bool(ss, "enabled", settings::hit_chams::enabled);
		write_int(ss, "style", settings::hit_chams::style);
		write_int(ss, "fade_easing", settings::hit_chams::fade_easing);
		write_float(ss, "duration", settings::hit_chams::duration);
		write_float(ss, "scale", settings::hit_chams::scale);
		write_color(ss, "fill_colour", settings::hit_chams::fill_colour);
		write_color(ss, "outline_colour", settings::hit_chams::outline_colour);
		write_float(ss, "outline_thickness", settings::hit_chams::outline_thickness);
		write_bool(ss, "draw_outline", settings::hit_chams::draw_outline);
		ss << "  },\n";

		ss << "  \"hit_skeleton\": {\n";
		write_bool(ss, "enabled", settings::hit_skeleton::enabled);
		write_int(ss, "style", settings::hit_skeleton::style);
		write_int(ss, "fade_easing", settings::hit_skeleton::fade_easing);
		write_float(ss, "duration", settings::hit_skeleton::duration);
		write_float(ss, "thickness", settings::hit_skeleton::thickness);
		write_float(ss, "joint_radius", settings::hit_skeleton::joint_radius);
		write_color(ss, "colour", settings::hit_skeleton::colour);
		write_color(ss, "outline_colour", settings::hit_skeleton::outline_colour);
		write_color(ss, "joint_colour", settings::hit_skeleton::joint_colour);
		write_bool(ss, "draw_joints", settings::hit_skeleton::draw_joints);
		write_bool(ss, "draw_outline", settings::hit_skeleton::draw_outline);
		ss << "  },\n";

		ImGuiStyle& style = ImGui::GetStyle();
		ss << "  \"theme\": {\n";
		write_color(ss, "WindowBg", &style.Colors[ImGuiCol_WindowBg].x);
		write_color(ss, "ChildBg", &style.Colors[ImGuiCol_ChildBg].x);
		write_color(ss, "Header", &style.Colors[ImGuiCol_Header].x);
		write_color(ss, "PopupBg", &style.Colors[ImGuiCol_PopupBg].x);
		write_color(ss, "Text", &style.Colors[ImGuiCol_Text].x);
		write_color(ss, "TextDisabled", &style.Colors[ImGuiCol_TextDisabled].x);
		write_color(ss, "SliderGrab", &style.Colors[ImGuiCol_SliderGrab].x);
		write_color(ss, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
		write_color(ss, "Border", &style.Colors[ImGuiCol_Border].x);
		write_color(ss, "Button", &style.Colors[ImGuiCol_Button].x);
		write_color(ss, "ButtonHovered", &style.Colors[ImGuiCol_ButtonHovered].x);
		write_color(ss, "ButtonActive", &style.Colors[ImGuiCol_ButtonActive].x);
		write_color(ss, "FrameBg", &style.Colors[ImGuiCol_FrameBg].x);
		write_color(ss, "FrameBgHovered", &style.Colors[ImGuiCol_FrameBgHovered].x);
		write_color(ss, "FrameBgActive", &style.Colors[ImGuiCol_FrameBgActive].x);
		write_color(ss, "ScrollbarBg", &style.Colors[ImGuiCol_ScrollbarBg].x);
		write_color(ss, "ScrollbarGrab", &style.Colors[ImGuiCol_ScrollbarGrab].x);
		write_color(ss, "menu_color", orok_config.menu_color);
		write_color(ss, "menu_color_secondary", orok_config.menu_color_secondary);
		write_color(ss, "menu_color_text", orok_config.menu_color_text);
		write_color(ss, "menu_color_border", orok_config.menu_color_border);
		write_color(ss, "menu_color_child", orok_config.menu_color_child);
		write_color(ss, "menu_color_button", orok_config.menu_color_button);
		write_color(ss, "menu_color_button_hover", orok_config.menu_color_button_hover);
		write_color(ss, "menu_color_button_active", orok_config.menu_color_button_active);
		write_color(ss, "menu_color_frame", orok_config.menu_color_frame);
		write_color(ss, "menu_color_frame_hover", orok_config.menu_color_frame_hover);
		write_color(ss, "menu_color_frame_active", orok_config.menu_color_frame_active);
		write_color(ss, "menu_color_popup", orok_config.menu_color_popup);
		write_color(ss, "menu_color_header", orok_config.menu_color_header);
		write_color(ss, "menu_color_header_hover", orok_config.menu_color_header_hover);
		write_color(ss, "menu_color_header_active", orok_config.menu_color_header_active);
		write_color(ss, "menu_color_checkmark", orok_config.menu_color_checkmark);
		write_color(ss, "menu_color_slider_grab", orok_config.menu_color_slider_grab);
		write_color(ss, "menu_color_slider_grab_active", orok_config.menu_color_slider_grab_active);
		write_color(ss, "menu_color_scrollbar_bg", orok_config.menu_color_scrollbar_bg);
		write_color(ss, "menu_color_scrollbar_grab", orok_config.menu_color_scrollbar_grab);
		ss << "  },\n";

		ss << "  \"misc\": {\n";
		write_bool(ss, "streamproof", settings::streamproof);
		write_bool(ss, "teamcheck", settings::teamcheck);
		write_bool(ss, "vsync", settings::vsync);
		write_bool(ss, "performance_mode", settings::performance_mode);
		write_bool(ss, "unlock_fps", settings::misc::unlock_fps);
		write_int(ss, "fps_cap", settings::misc::fps_cap);
		write_bool(ss, "auto_rescan", settings::misc::auto_rescan);
		write_bool(ss, "keybind_list", settings::misc::keybind_list);
		write_bool(ss, "keybind_indicator", settings::misc::keybind_indicator);
		write_color(ss, "keybind_indicator_feature_colour", settings::misc::keybind_indicator_feature_colour);
		write_color(ss, "keybind_indicator_mode_colour", settings::misc::keybind_indicator_mode_colour);
		write_bool(ss, "watermark_enabled", settings::watermark::enabled);
		write_bool(ss, "watermark_draggable", settings::watermark::draggable);
		write_float(ss, "watermark_pos_x", settings::watermark::pos_x);
		write_float(ss, "watermark_pos_y", settings::watermark::pos_y);
		write_bool(ss, "watermark_show_prefix", settings::watermark::show_prefix);
		write_string(ss, "watermark_prefix_text", settings::watermark::prefix_text);
		write_bool(ss, "watermark_show_right_badge", settings::watermark::show_right_badge);
		write_string(ss, "watermark_right_badge_text", settings::watermark::right_badge_text);
		write_bool(ss, "watermark_show_username", settings::watermark::show_username);
		write_bool(ss, "watermark_show_fps", settings::watermark::show_fps);
		write_bool(ss, "watermark_show_game_id", settings::watermark::show_game_id);
		write_bool(ss, "watermark_show_place_id", settings::watermark::show_place_id);
		write_bool(ss, "watermark_show_job_id", settings::watermark::show_job_id);
		write_bool(ss, "watermark_show_cpu_ram", settings::watermark::show_cpu_ram);
		write_bool(ss, "watermark_show_ip_port", settings::watermark::show_ip_port);
		write_bool(ss, "watermark_show_client_id", settings::watermark::show_client_id);
		write_bool(ss, "watermark_show_time", settings::watermark::show_time);
		write_bool(ss, "watermark_custom_accent", settings::watermark::custom_accent);
		write_color(ss, "watermark_accent_color", settings::watermark::accent_color);
		write_float(ss, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		write_float(ss, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		write_int(ss, "menu_background_style", settings::misc::menu_background_style);
		write_int(ss, "watermark_position", settings::misc::watermark_position);
		write_bool(ss, "explorer_window", settings::misc::explorer_window);
		write_bool(ss, "spotify_window", settings::misc::spotify_window);
		write_bool(ss, "performance_window", settings::misc::performance_window);
		write_int(ss, "target_thread_sleep", settings::performance::target_thread_sleep);
		write_int(ss, "aim_thread_sleep", settings::performance::aim_thread_sleep);
		write_int(ss, "cache_refresh_delay", settings::performance::cache_refresh_delay);
		write_int(ss, "main_loop_delay", settings::performance::main_loop_delay);
		write_bool(ss, "thread_throttling", settings::performance::thread_throttling);
		write_bool(ss, "hide_console", settings::misc::hide_console);
		write_bool(ss, "bot_support", settings::misc::bot_support);
		write_bool(ss, "only_workspace_bots", settings::misc::only_workspace_bots);
		write_bool(ss, "disable_scrollbars", settings::misc::disable_scrollbars);
		write_int(ss, "preview_model", settings::misc::preview_model);
		write_string(ss, "custom_model_path", settings::misc::custom_model_path);
		write_bool(ss, "sticky_preview", settings::misc::sticky_preview);
		write_bool(ss, "disable_confirmations", settings::config_system::disable_confirmations);
		write_bool(ss, "confirm_unload", settings::general::confirm_unload);
		write_int(ss, "theme_index", settings::misc::theme_index);
		write_bool(ss, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
			write_color(ss, ("custom_color_" + std::to_string(i)).c_str(), settings::misc::custom_colors[i]);
		write_int(ss, "theme_preset", settings::theme::preset);
		write_color(ss, "theme_menu_accent", settings::theme::accent);
		write_color(ss, "theme_background_one", settings::theme::background_one);
		write_color(ss, "theme_background_two", settings::theme::background_two);
		write_color(ss, "theme_stroke", settings::theme::stroke);
		write_color(ss, "theme_stroke_two", settings::theme::stroke_two);
		write_color(ss, "theme_text", settings::theme::text);
		write_color(ss, "theme_text_inactive", settings::theme::text_inactive);
		ss << "  },\n";

		ss << "  \"notifications\": {\n";
		write_bool(ss, "enabled", settings::notifications::enabled);
		write_bool(ss, "welcome", settings::notifications::welcome);
		write_string(ss, "welcome_msg", settings::notifications::welcome_msg);
		write_bool(ss, "thankyou", settings::notifications::thankyou);
		write_string(ss, "thankyou_msg", settings::notifications::thankyou_msg);
		write_bool(ss, "hit", settings::notifications::hit);
		write_string(ss, "hit_msg", settings::notifications::hit_msg);
		write_bool(ss, "kill", settings::notifications::kill);
		write_string(ss, "kill_msg", settings::notifications::kill_msg);
		write_float(ss, "duration", settings::notifications::duration);
		write_int(ss, "position", settings::notifications::position);
		ss << "  },\n";

		ss << "  \"btools\": {\n";
		write_bool(ss, "enabled", settings::btools::enabled);
		write_int(ss, "tool_type", settings::btools::tool_type);
		write_int(ss, "keybind", settings::btools::keybind);
		write_int(ss, "activation_mode", settings::btools::activation_mode);
		ss << "  },\n";

		ss << "  \"lighting\": {\n";
		ss << "    \"ambient\": {\n";
		write_bool(ss, "enabled", settings::lighting::ambient::enabled);
		write_color(ss, "color", settings::lighting::ambient::color);
		write_color(ss, "outdoor_color", settings::lighting::ambient::outdoor_color);
		write_bool(ss, "pulse", settings::lighting::ambient::pulse);
		write_float(ss, "pulse_speed", settings::lighting::ambient::pulse_speed);
		write_float(ss, "pulse_intensity", settings::lighting::ambient::pulse_intensity);
		ss << "    },\n";
		ss << "    \"shadows\": {\n";
		write_bool(ss, "enabled", settings::lighting::shadows::enabled);
		write_float(ss, "value", settings::lighting::shadows::value);
		ss << "    },\n";
		ss << "    \"fog\": {\n";
		write_bool(ss, "enabled", settings::lighting::fog::enabled);
		write_float(ss, "fog_start", settings::lighting::fog::fog_start);
		write_float(ss, "fog_end", settings::lighting::fog::fog_end);
		write_color(ss, "fog_color", settings::lighting::fog::fog_color);
		write_bool(ss, "pulse", settings::lighting::fog::pulse);
		write_int(ss, "pulse_mode", settings::lighting::fog::pulse_mode);
		write_float(ss, "pulse_speed", settings::lighting::fog::pulse_speed);
		write_float(ss, "pulse_intensity", settings::lighting::fog::pulse_intensity);
		ss << "    },\n";
		ss << "    \"clocktime\": {\n";
		write_bool(ss, "enabled", settings::lighting::clocktime::enabled);
		write_float(ss, "time", settings::lighting::clocktime::time);
		ss << "    },\n";
		ss << "    \"atmosphere\": {\n";
		write_bool(ss, "enabled", settings::lighting::atmosphere::enabled);
		write_color(ss, "color", settings::lighting::atmosphere::color);
		write_color(ss, "decay", settings::lighting::atmosphere::decay);
		write_float(ss, "density", settings::lighting::atmosphere::density);
		write_float(ss, "glare", settings::lighting::atmosphere::glare);
		write_float(ss, "haze", settings::lighting::atmosphere::haze);
		write_float(ss, "offset", settings::lighting::atmosphere::offset);
		write_bool(ss, "pulse", settings::lighting::atmosphere::pulse);
		write_int(ss, "pulse_mode", settings::lighting::atmosphere::pulse_mode);
		write_float(ss, "pulse_speed", settings::lighting::atmosphere::pulse_speed);
		write_float(ss, "pulse_intensity", settings::lighting::atmosphere::pulse_intensity);
		ss << "    },\n";
		ss << "    \"colorshift\": {\n";
		write_bool(ss, "enabled", settings::lighting::colorshift::enabled);
		write_color(ss, "bottom", settings::lighting::colorshift::bottom);
		write_color(ss, "top", settings::lighting::colorshift::top);
		ss << "    },\n";
		ss << "    \"exposure\": {\n";
		write_bool(ss, "enabled", settings::lighting::exposure::enabled);
		write_float(ss, "exposure", settings::lighting::exposure::exposure);
		ss << "    },\n";
		ss << "    \"starcount\": {\n";
		write_bool(ss, "enabled", settings::lighting::starcount::enabled);
		write_int(ss, "star_count", settings::lighting::starcount::star_count);
		ss << "    },\n";
		ss << "    \"sunmoon\": {\n";
		write_bool(ss, "enabled", settings::lighting::sunmoon::enabled);
		write_string(ss, "custom_sun_id", settings::lighting::sunmoon::custom_sun_id);
		write_string(ss, "custom_moon_id", settings::lighting::sunmoon::custom_moon_id);
		ss << "    },\n";
		ss << "    \"brightness\": {\n";
		write_bool(ss, "enabled", settings::lighting::brightness::enabled);
		write_float(ss, "value", settings::lighting::brightness::value);
		ss << "    },\n";
		ss << "    \"environment\": {\n";
		write_bool(ss, "enabled", settings::lighting::environment::enabled);
		write_float(ss, "diffuse_scale", settings::lighting::environment::diffuse_scale);
		write_float(ss, "specular_scale", settings::lighting::environment::specular_scale);
		ss << "    },\n";
		ss << "    \"latitude\": {\n";
		write_bool(ss, "enabled", settings::lighting::latitude::enabled);
		write_float(ss, "value", settings::lighting::latitude::value);
		ss << "    },\n";
		ss << "    \"terrain\": {\n";
		write_bool(ss, "enabled", settings::lighting::terrain::enabled);
		write_float(ss, "grass_length", settings::lighting::terrain::grass_length);
		write_color(ss, "water_color", settings::lighting::terrain::water_color);
		write_float(ss, "water_reflectance", settings::lighting::terrain::water_reflectance);
		write_float(ss, "water_transparency", settings::lighting::terrain::water_transparency);
		write_float(ss, "water_wave_size", settings::lighting::terrain::water_wave_size);
		write_float(ss, "water_wave_speed", settings::lighting::terrain::water_wave_speed);
		ss << "    },\n";
		ss << "    \"bloom\": {\n";
		write_bool(ss, "enabled", settings::lighting::bloom::enabled);
		write_float(ss, "intensity", settings::lighting::bloom::intensity);
		write_float(ss, "size", settings::lighting::bloom::size);
		write_float(ss, "threshold", settings::lighting::bloom::threshold);
		ss << "    },\n";
		ss << "    \"sunrays\": {\n";
		write_bool(ss, "enabled", settings::lighting::sunrays::enabled);
		write_float(ss, "intensity", settings::lighting::sunrays::intensity);
		write_float(ss, "spread", settings::lighting::sunrays::spread);
		ss << "    },\n";
		ss << "    \"color_correction\": {\n";
		write_bool(ss, "enabled", settings::lighting::color_correction::enabled);
		write_float(ss, "brightness", settings::lighting::color_correction::brightness);
		write_float(ss, "contrast", settings::lighting::color_correction::contrast);
		write_float(ss, "saturation", settings::lighting::color_correction::saturation);
		write_color(ss, "tint_color", settings::lighting::color_correction::tint_color);
		ss << "    },\n";
		ss << "    \"depth_of_field\": {\n";
		write_bool(ss, "enabled", settings::lighting::depth_of_field::enabled);
		write_float(ss, "density", settings::lighting::depth_of_field::density);
		write_float(ss, "focus_distance", settings::lighting::depth_of_field::focus_distance);
		write_float(ss, "in_focus_radius", settings::lighting::depth_of_field::in_focus_radius);
		write_float(ss, "near_intensity", settings::lighting::depth_of_field::near_intensity);
		ss << "    }\n";
		ss << "  },\n";

		ss << "  \"freezeplayer\": {\n";
		write_bool(ss, "enabled", settings::freezeplayer::enabled);
		write_int(ss, "keybind", settings::freezeplayer::keybind);
		write_int(ss, "activation_mode", settings::freezeplayer::activation_mode);
		ss << "  },\n";

		ss << "  \"typingcheck\": {\n";
		write_bool(ss, "enabled", settings::typingcheck::enabled);
		ss << "  },\n";

		

		ss << "  \"skinchanger\": {\n";
		write_bool(ss, "enabled", settings::skinchanger::enabled);
		write_int(ss, "category_index", settings::skinchanger::category_index);
		write_int(ss, "skin_index", settings::skinchanger::skin_index);
		write_string(ss, "search_filter", settings::skinchanger::search_filter);
		write_string_vector(ss, "favorites", settings::skinchanger::favorites);
		ss << "  },\n";

		ss << "  \"dhskinchanger\": {\n";
		write_bool(ss, "enabled", settings::dh_skinchanger::enabled);
		write_int(ss, "skin_index", settings::dh_skinchanger::skin_index);
		ss << "  },\n";

		ss << "  \"skyboxchanger\": {\n";
		write_bool(ss, "enabled", settings::skyboxchanger::enabled);
		write_int(ss, "preset", settings::skyboxchanger::preset);
		ss << "  },\n";

		ss << "  \"config_system\": {\n";
		write_bool(ss, "disable_confirmations", settings::config_system::disable_confirmations);
		ss << "  },\n";


		ss << "  \"hitsounds\": {\n";
		write_bool(ss, "enabled", settings::hitsounds::enabled);
		write_int(ss, "type", settings::hitsounds::type);
		write_string(ss, "custom_path", settings::hitsounds::custom_path);
		write_float(ss, "volume", settings::hitsounds::volume);
		ss << "  },\n";

		ss << "  \"killsounds\": {\n";
		write_bool(ss, "enabled", settings::killsounds::enabled);
		write_int(ss, "type", settings::killsounds::type);
		write_string(ss, "custom_path", settings::killsounds::custom_path);
		write_float(ss, "volume", settings::killsounds::volume);
		ss << "  },\n";

		auto get_win_metrics = [](const char* name, float def_px, float def_py, float def_sx, float def_sy,
		                          float& out_px, float& out_py, float& out_sx, float& out_sy) {
			ImGuiWindow* w = ImGui::FindWindowByName(name);
			if (w) {
				out_px = w->Pos.x;
				out_py = w->Pos.y;
				out_sx = w->Size.x;
				out_sy = w->Size.y;
			} else {
				out_px = def_px;
				out_py = def_py;
				out_sx = def_sx;
				out_sy = def_sy;
			}
		};

		float m_px, m_py, m_sx, m_sy;
		get_win_metrics("ImMagic - Menu", g_main_menu_pos.x, g_main_menu_pos.y, g_main_menu_size.x, g_main_menu_size.y, m_px, m_py, m_sx, m_sy);

		float s_px, s_py, s_sx, s_sy;
		get_win_metrics("ImMagic - Settings Window", -1, -1, 580, 440, s_px, s_py, s_sx, s_sy);

		float l_px, l_py, l_sx, l_sy;
		get_win_metrics("ImMagic - Luau Editor", -1, -1, 620, 520, l_px, l_py, l_sx, l_sy);

		float p_px, p_py, p_sx, p_sy;
		get_win_metrics("ImMagic - Preview", -1, -1, 236, 339, p_px, p_py, p_sx, p_sy);

		float pf_px, pf_py, pf_sx, pf_sy;
		get_win_metrics("ImMagic - Performance Window", -1, -1, 480, 550, pf_px, pf_py, pf_sx, pf_sy);

		float sp_px, sp_py, sp_sx, sp_sy;
		get_win_metrics("ImMagic - Spotify", -1, -1, 350, 100, sp_px, sp_py, sp_sx, sp_sy);

		float ex_px, ex_py, ex_sx, ex_sy;
		get_win_metrics("ImMagic - Explorer Window", -1, -1, 720, 480, ex_px, ex_py, ex_sx, ex_sy);

		ss << "  \"windows\": {\n";
		write_float(ss, "menu_pos_x", m_px);
		write_float(ss, "menu_pos_y", m_py);
		write_float(ss, "menu_size_w", m_sx);
		write_float(ss, "menu_size_h", m_sy);

		write_float(ss, "settings_pos_x", s_px);
		write_float(ss, "settings_pos_y", s_py);
		write_float(ss, "settings_size_w", s_sx);
		write_float(ss, "settings_size_h", s_sy);

		write_float(ss, "luau_pos_x", l_px);
		write_float(ss, "luau_pos_y", l_py);
		write_float(ss, "luau_size_w", l_sx);
		write_float(ss, "luau_size_h", l_sy);

		write_float(ss, "preview_pos_x", p_px);
		write_float(ss, "preview_pos_y", p_py);
		write_float(ss, "preview_size_w", p_sx);
		write_float(ss, "preview_size_h", p_sy);

		write_float(ss, "perf_pos_x", pf_px);
		write_float(ss, "perf_pos_y", pf_py);
		write_float(ss, "perf_size_w", pf_sx);
		write_float(ss, "perf_size_h", pf_sy);

		write_float(ss, "spotify_pos_x", sp_px);
		write_float(ss, "spotify_pos_y", sp_py);
		write_float(ss, "spotify_size_w", sp_sx);
		write_float(ss, "spotify_size_h", sp_sy);

		write_float(ss, "explorer_pos_x", ex_px);
		write_float(ss, "explorer_pos_y", ex_py);
		write_float(ss, "explorer_size_w", ex_sx);
		write_float(ss, "explorer_size_h", ex_sy);
		ss << "  }\n";

		ss << "}\n";

		std::ofstream file(path);
		if (!file.is_open())
			return false;

		file << ss.str();
		file.close();
		return true;
	}

	static void reset_to_defaults()
	{
		settings::teamcheck = false;
		settings::aimbot::enabled = false;
		settings::aimbot::keybind = 0;
		settings::aimbot::activation_mode = 1;
		settings::aimbot::mode = 0;
		settings::aimbot::mouse_sensitivity = 1.0f;
		settings::aimbot::target_part = 0;
		settings::aimbot::fov = 100.f;
		settings::aimbot::use_fov = true;
		settings::aimbot::ring_fov_enabled = false;
		settings::aimbot::ring_fov_inner = 20.f;
		settings::aimbot::ring_fov_outer = 100.f;
		settings::aimbot::smoothing = false;
		settings::aimbot::smoothingx = 10.f;
		settings::aimbot::smoothingy = 10.f;
		settings::aimbot::smoothing_style = 0;
		settings::aimbot::enable_prediction = false;
		settings::aimbot::prediction_x = 10.f;
		settings::aimbot::prediction_y = 10.f;
		settings::aimbot::jump_prediction = false;
		settings::aimbot::jump_prediction_value = 10.f;
		settings::aimbot::fall_prediction = false;
		settings::aimbot::fall_prediction_value = 10.f;
		settings::aimbot::teamcheck = false;
		settings::aimbot::knock_check = false;
		settings::aimbot::priorities = 0;
		settings::aimbot::health_check_enabled = false;
		settings::aimbot::min_health = 0.f;
		settings::aimbot::sticky_aim = false;
		settings::aimbot::use_fov = true;
		settings::aimbot::draw_fov = false;
		settings::aimbot::fill_fov = false;
		settings::aimbot::fov_circle_colour[0] = 1.f; settings::aimbot::fov_circle_colour[1] = 1.f; settings::aimbot::fov_circle_colour[2] = 1.f; settings::aimbot::fov_circle_colour[3] = 1.f;
		settings::aimbot::fov_outline_colour[0] = 0.f; settings::aimbot::fov_outline_colour[1] = 0.f; settings::aimbot::fov_outline_colour[2] = 0.f; settings::aimbot::fov_outline_colour[3] = 1.f;
		settings::aimbot::fov_fill_colour[0] = 1.f; settings::aimbot::fov_fill_colour[1] = 1.f; settings::aimbot::fov_fill_colour[2] = 1.f; settings::aimbot::fov_fill_colour[3] = 0.15f;

		settings::triggerbot::enabled = false;
		settings::triggerbot::method = 0;
		settings::triggerbot::keybind = 0;
		settings::triggerbot::activation_mode = 1;
		settings::triggerbot::target_part = 0;
		settings::triggerbot::threshold = 15.0f;
		settings::triggerbot::delay_ms = 0.0f;
		settings::triggerbot::cooldown_ms = 35.0f;
		settings::triggerbot::teamcheck = false;
		settings::triggerbot::knock_check = false;
		settings::triggerbot::guncheck = false;
		settings::triggerbot::wallcheck = false;
		settings::triggerbot::draw_fov = false;
		settings::triggerbot::fill_fov = false;
		settings::triggerbot::fov_circle_colour[0] = 1.f; settings::triggerbot::fov_circle_colour[1] = 1.f; settings::triggerbot::fov_circle_colour[2] = 1.f; settings::triggerbot::fov_circle_colour[3] = 1.f;
		settings::triggerbot::fov_outline_colour[0] = 0.f; settings::triggerbot::fov_outline_colour[1] = 0.f; settings::triggerbot::fov_outline_colour[2] = 0.f; settings::triggerbot::fov_outline_colour[3] = 1.f;
		settings::triggerbot::fov_fill_colour[0] = 1.f; settings::triggerbot::fov_fill_colour[1] = 1.f; settings::triggerbot::fov_fill_colour[2] = 1.f; settings::triggerbot::fov_fill_colour[3] = 0.15f;

		settings::silentaim::enabled = false;
		settings::silentaim::method = 0;
		settings::silentaim::keybind = 0;
		settings::silentaim::activation_mode = 1;
		settings::silentaim::target_part = 0;
		settings::silentaim::snapline = false;
		settings::silentaim::snapline_lerp = true;
		settings::silentaim::snapline_origin = 0;
		settings::silentaim::snapline_colour[0] = 1.f; settings::silentaim::snapline_colour[1] = 1.f; settings::silentaim::snapline_colour[2] = 1.f; settings::silentaim::snapline_colour[3] = 1.f;
		settings::silentaim::fov = 100.f;
		settings::silentaim::mode_360 = false;
		settings::silentaim::use_fov = true;
		settings::silentaim::use_aimbot_target = false;
		settings::silentaim::draw_fov = false;
		settings::silentaim::fill_fov = false;
		settings::silentaim::fov_circle_colour[0] = 1.f; settings::silentaim::fov_circle_colour[1] = 1.f; settings::silentaim::fov_circle_colour[2] = 1.f; settings::silentaim::fov_circle_colour[3] = 1.f;
		settings::silentaim::fov_outline_colour[0] = 0.f; settings::silentaim::fov_outline_colour[1] = 0.f; settings::silentaim::fov_outline_colour[2] = 0.f; settings::silentaim::fov_outline_colour[3] = 1.f;
		settings::silentaim::fov_fill_colour[0] = 1.f; settings::silentaim::fov_fill_colour[1] = 1.f; settings::silentaim::fov_fill_colour[2] = 1.f; settings::silentaim::fov_fill_colour[3] = 0.15f;
		settings::silentaim::sticky_aim = false;
		settings::silentaim::knock_check = false;
		settings::silentaim::auto_switch = false;
		settings::silentaim::teamcheck = false;
		settings::silentaim::priorities = 0;
		settings::silentaim::health_check_enabled = false;
		settings::silentaim::min_health = 0.0f;
		settings::silentaim::guncheck = false;
		settings::silentaim::knock_check = false;
		settings::silentaim::enable_prediction = false;
		settings::silentaim::prediction_x = 10.f;
		settings::silentaim::prediction_y = 10.f;
		settings::silentaim::forcefield_check = false;
		settings::silentaim::katana_check = false;

		settings::raycast_silentaim::enabled = false;
		settings::raycast_silentaim::mode_360 = false;
		settings::raycast_silentaim::mode_360_keybind = 0;
		settings::raycast_silentaim::mode_360_activation_mode = 1;
		settings::raycast_silentaim::draw_fov = false;
		settings::raycast_silentaim::fill_fov = false;
		settings::raycast_silentaim::fov_circle_colour[0] = 1.f; settings::raycast_silentaim::fov_circle_colour[1] = 1.f; settings::raycast_silentaim::fov_circle_colour[2] = 1.f; settings::raycast_silentaim::fov_circle_colour[3] = 1.f;
		settings::raycast_silentaim::fov_outline_colour[0] = 0.f; settings::raycast_silentaim::fov_outline_colour[1] = 0.f; settings::raycast_silentaim::fov_outline_colour[2] = 0.f; settings::raycast_silentaim::fov_outline_colour[3] = 1.f;
		settings::raycast_silentaim::fov_fill_colour[0] = 1.f; settings::raycast_silentaim::fov_fill_colour[1] = 1.f; settings::raycast_silentaim::fov_fill_colour[2] = 1.f; settings::raycast_silentaim::fov_fill_colour[3] = 0.15f;
		settings::raycast_silentaim::enable_prediction = false;
		settings::raycast_silentaim::teamcheck = false;
		settings::raycast_silentaim::magic_bullet = false;
		settings::raycast_silentaim::forcefield_check = false;
		settings::raycast_silentaim::hitmarkers = false;
		settings::raycast_silentaim::hitmarker_colour[0] = 1.f; settings::raycast_silentaim::hitmarker_colour[1] = 1.f; settings::raycast_silentaim::hitmarker_colour[2] = 1.f; settings::raycast_silentaim::hitmarker_colour[3] = 1.f;
		settings::raycast_silentaim::hit_flash_colour[0] = 1.f; settings::raycast_silentaim::hit_flash_colour[1] = 1.f; settings::raycast_silentaim::hit_flash_colour[2] = 1.f; settings::raycast_silentaim::hit_flash_colour[3] = 0.35f;
		settings::raycast_silentaim::magic_bullet_keybind = 0;
		settings::raycast_silentaim::magic_bullet_activation_mode = 1;
		settings::raycast_silentaim::fov = 100.f;
		settings::raycast_silentaim::dynamic_fov = false;
		settings::raycast_silentaim::keybind = 0;
		settings::raycast_silentaim::activation_mode = 1;

		settings::crosshair::enabled = false;
		settings::crosshair::attach_to_enemy = false;
		settings::crosshair::lerp = false;
		settings::crosshair::lerp_speed = 18.0f;
		settings::crosshair::style = 0;
		settings::crosshair::colour[0] = 1.f; settings::crosshair::colour[1] = 1.f; settings::crosshair::colour[2] = 1.f; settings::crosshair::colour[3] = 1.f;
		settings::crosshair::outline_colour[0] = 0.f; settings::crosshair::outline_colour[1] = 0.f; settings::crosshair::outline_colour[2] = 0.f; settings::crosshair::outline_colour[3] = 0.8f;
		settings::crosshair::outline = true;
		settings::crosshair::size = 7.f;
		settings::crosshair::gap = 3.f;
		settings::crosshair::thickness = 1.5f;
		settings::crosshair::dot = true;
		settings::crosshair::dot_size = 1.5f;
		settings::crosshair::rotate = false;
		settings::crosshair::rotate_speed = 90.f;
		settings::crosshair::pulse = false;
		settings::crosshair::pulse_speed = 3.f;
		settings::crosshair::pulse_amount = 2.f;
		settings::crosshair::rainbow = false;
		settings::crosshair::rainbow_speed = 0.2f;
		settings::crosshair::dynamic_gap = false;
		settings::crosshair::expansion_amount = 5.f;
		settings::crosshair::expansion_decay = 15.f;

		settings::hitsounds::enabled = false;
		settings::hitsounds::type = 0;
		settings::hitsounds::method = 0;
		memset(settings::hitsounds::custom_path, 0, sizeof(settings::hitsounds::custom_path));
		settings::hitsounds::volume = 0.5f;

		settings::killsounds::enabled = false;
		settings::killsounds::type = 0;
		memset(settings::killsounds::custom_path, 0, sizeof(settings::killsounds::custom_path));
		settings::killsounds::volume = 0.5f;

		settings::animationchanger::enabled = false;
		settings::animationchanger::type = 0;
		settings::animationchanger::mode = 0;

		settings::materialchanger::enabled = false;
		settings::materialchanger::material_index = 0;
		settings::materialchanger::affect_accessories = false;

		settings::godmode::enabled = false;

		settings::movement::speedhack::enabled = false;
		settings::movement::speedhack::mode = 0;
		settings::movement::speedhack::speed = 16.0f;
		settings::movement::speedhack::keybind = 0;
		settings::movement::speedhack::activation_mode = 1;
		settings::movement::flyhack::enabled = false;
		settings::movement::flyhack::mode = 0;
		settings::movement::flyhack::speed = 50.0f;
		settings::movement::flyhack::keybind = 0;
		settings::movement::flyhack::activation_mode = 1;

		settings::movement::spin360::enabled = false;
		settings::movement::spin360::keybind = 0;
		settings::movement::spin360::speed = 100.0f;
		settings::movement::spin360::smoothness = 10.0f;
		settings::movement::spin360::smoothness = 10.0f;
		settings::movement::noclip::enabled = false;
		settings::movement::noclip::keybind = 0;
		settings::movement::noclip::activation_mode = 1;
		settings::movement::infjump::enabled = false;
		settings::movement::wallbug::enabled = false;
		settings::movement::wallbug::height = 30.0f;
		settings::movement::bhop::enabled = false;
		settings::movement::bhop::speed = 50.0f;
		settings::movement::bhop::keybind = 0;
		settings::movement::bhop::activation_mode = 1;
		settings::movement::wallslide::enabled = false;
		settings::movement::wallslide::speed = 15.0f;
		settings::movement::wallslide::keybind = 0;
		settings::movement::wallslide::activation_mode = 1;
		settings::movement::pixelsurf::enabled = false;
		settings::movement::pixelsurf::speed = 50.0f;
		settings::movement::pixelsurf::keybind = 0;
		settings::movement::pixelsurf::activation_mode = 1;
		settings::movement::voidhide::enabled = false;
		settings::movement::voidhide::keybind = 0;
		settings::movement::voidhide::activation_mode = 1;
		settings::movement::third_person::enabled = false;
		settings::movement::third_person::distance = 10.0f;
		settings::movement::third_person::keybind = 0;
		settings::movement::third_person::activation_mode = 1;
		settings::movement::fov_changer::enabled = false;
		settings::movement::fov_changer::fov_value = 90.0f;
		settings::movement::fov_changer::keybind = 0;
		settings::movement::fov_changer::activation_mode = 1;

		settings::visuals::neutral.box = false;
		settings::visuals::neutral.box_inline = false;
		settings::visuals::neutral.box_gradient = false;
		settings::visuals::neutral.box_type = 0;
		settings::visuals::neutral.box_fill = false;
		settings::visuals::neutral.box_fill_style = 0;
		settings::visuals::neutral.box_fill_gradient_mask = 1;
		settings::visuals::neutral.colour[0] = 1.f; settings::visuals::neutral.colour[1] = 1.f; settings::visuals::neutral.colour[2] = 1.f; settings::visuals::neutral.colour[3] = 1.f;
		settings::visuals::neutral.box_fill_colour[0] = 1.f; settings::visuals::neutral.box_fill_colour[1] = 1.f; settings::visuals::neutral.box_fill_colour[2] = 1.f; settings::visuals::neutral.box_fill_colour[3] = 0.2f;
		settings::visuals::neutral.box_fill_colour_mid[0] = 1.f; settings::visuals::neutral.box_fill_colour_mid[1] = 1.f; settings::visuals::neutral.box_fill_colour_mid[2] = 1.f; settings::visuals::neutral.box_fill_colour_mid[3] = 0.2f;
		settings::visuals::neutral.box_fill_colour_low[0] = 1.f; settings::visuals::neutral.box_fill_colour_low[1] = 1.f; settings::visuals::neutral.box_fill_colour_low[2] = 1.f; settings::visuals::neutral.box_fill_colour_low[3] = 0.2f;
		settings::visuals::neutral.username = false;
		settings::visuals::neutral.name_bracket_style = 0;
		settings::visuals::neutral.username_colour[0] = 1.f; settings::visuals::neutral.username_colour[1] = 1.f; settings::visuals::neutral.username_colour[2] = 1.f; settings::visuals::neutral.username_colour[3] = 1.f;
		settings::visuals::neutral.distance = false;
		settings::visuals::neutral.distance_colour[0] = 1.f; settings::visuals::neutral.distance_colour[1] = 1.f; settings::visuals::neutral.distance_colour[2] = 1.f; settings::visuals::neutral.distance_colour[3] = 1.f;
		settings::visuals::neutral.tool = false;
		settings::visuals::neutral.tool_colour[0] = 1.f; settings::visuals::neutral.tool_colour[1] = 1.f; settings::visuals::neutral.tool_colour[2] = 1.f; settings::visuals::neutral.tool_colour[3] = 1.f;
		settings::visuals::neutral.healthbar = false;
		settings::visuals::neutral.healthbar_style = 0;
		settings::visuals::neutral.healthbar_padding = 4.f;
		settings::visuals::neutral.healthbar_colour[0] = 0.f; settings::visuals::neutral.healthbar_colour[1] = 1.f; settings::visuals::neutral.healthbar_colour[2] = 0.f; settings::visuals::neutral.healthbar_colour[3] = 1.f;
		settings::visuals::neutral.healthbar_colour_mid[0] = 1.f; settings::visuals::neutral.healthbar_colour_mid[1] = 1.f; settings::visuals::neutral.healthbar_colour_mid[2] = 0.f; settings::visuals::neutral.healthbar_colour_mid[3] = 1.f;
		settings::visuals::neutral.healthbar_colour_low[0] = 1.f; settings::visuals::neutral.healthbar_colour_low[1] = 0.f; settings::visuals::neutral.healthbar_colour_low[2] = 0.f; settings::visuals::neutral.healthbar_colour_low[3] = 1.f;
		settings::visuals::neutral.tracers = false;
		settings::visuals::neutral.tracers_colour[0] = 1.f; settings::visuals::neutral.tracers_colour[1] = 1.f; settings::visuals::neutral.tracers_colour[2] = 1.f; settings::visuals::neutral.tracers_colour[3] = 1.f;
		settings::visuals::neutral.chams = false;
		settings::visuals::neutral.chams_type = 0;
		settings::visuals::neutral.chams_colour[0] = 0.855f; settings::visuals::neutral.chams_colour[1] = 0.278f; settings::visuals::neutral.chams_colour[2] = 0.478f; settings::visuals::neutral.chams_colour[3] = 0.5f;
		settings::visuals::neutral.chams_outline_colour[0] = 0.f; settings::visuals::neutral.chams_outline_colour[1] = 0.f; settings::visuals::neutral.chams_outline_colour[2] = 0.f; settings::visuals::neutral.chams_outline_colour[3] = 1.f;
		settings::visuals::neutral.skeleton = false;
		settings::visuals::neutral.skeleton_colour[0] = 1.f; settings::visuals::neutral.skeleton_colour[1] = 1.f; settings::visuals::neutral.skeleton_colour[2] = 1.f; settings::visuals::neutral.skeleton_colour[3] = 1.f;
		settings::visuals::neutral.skeleton_outline_colour[0] = 0.f; settings::visuals::neutral.skeleton_outline_colour[1] = 0.f; settings::visuals::neutral.skeleton_outline_colour[2] = 0.f; settings::visuals::neutral.skeleton_outline_colour[3] = 1.f;
		settings::visuals::neutral.corpse = false;
		settings::visuals::neutral.corpse_colour[0] = 1.f; settings::visuals::neutral.corpse_colour[1] = 1.f; settings::visuals::neutral.corpse_colour[2] = 1.f; settings::visuals::neutral.corpse_colour[3] = 0.25f;
		settings::visuals::neutral.corpse_outline_colour[0] = 0.f; settings::visuals::neutral.corpse_outline_colour[1] = 0.f; settings::visuals::neutral.corpse_outline_colour[2] = 0.f; settings::visuals::neutral.corpse_outline_colour[3] = 1.f;
		settings::visuals::neutral.corpse_names = false;
		settings::visuals::neutral.corpse_names_colour[0] = 1.f; settings::visuals::neutral.corpse_names_colour[1] = 1.f; settings::visuals::neutral.corpse_names_colour[2] = 1.f; settings::visuals::neutral.corpse_names_colour[3] = 1.f;

		settings::visuals::friendly = settings::visuals::neutral;
		settings::visuals::hostile = settings::visuals::neutral;
		settings::visuals::enabled = false;
		settings::visuals::keybind = 0;
		settings::visuals::activation_mode = 1;
		settings::visuals::teamcheck = false;
		settings::visuals::esp_font_index = 0;
		settings::visuals::esp_font_size = 12.f;
		settings::visuals::bounding_type = 0;
		settings::visuals::render_outlines = { 1, 1, 1, 1, 1, 1, 1, 1 };
		settings::visuals::skeleton_thickness = 1.5f;
		settings::visuals::box_thickness = 1.0f;
		settings::visuals::healthbar_thickness = 2.0f;
		settings::visuals::chams_thickness = 1.0f;
		settings::visuals::chams_outline_thickness = 1.0f;
		settings::visuals::distance_unit = 0;
		settings::visuals::distance_check = false;
		settings::visuals::sort_by_status = false;
		settings::visuals::knock_check = false;
		settings::visuals::local_player = false;
		settings::visuals::tool_check = false;
		settings::visuals::godded_check = false;
		settings::visuals::forcefield_check = false;
		settings::visuals::ignore_full_health = false;
		settings::visuals::static_on_death = false;
		settings::visuals::corpse_shift = 0.0f;
		settings::visuals::enemy_highlight = false;
		settings::visuals::enemy_highlight_colour[0] = 1.f; settings::visuals::enemy_highlight_colour[1] = 0.2f; settings::visuals::enemy_highlight_colour[2] = 0.2f; settings::visuals::enemy_highlight_colour[3] = 0.6f;
		settings::visuals::friendly_highlight = false;
		settings::visuals::friendly_highlight_colour[0] = 0.2f; settings::visuals::friendly_highlight_colour[1] = 1.f; settings::visuals::friendly_highlight_colour[2] = 0.2f; settings::visuals::friendly_highlight_colour[3] = 0.6f;
		settings::visuals::bar_fill = false;
		settings::visuals::bar_fill_colour[0] = 0.1f; settings::visuals::bar_fill_colour[1] = 0.1f; settings::visuals::bar_fill_colour[2] = 0.1f; settings::visuals::bar_fill_colour[3] = 0.5f;
		settings::visuals::health_check_enabled = false;
		settings::visuals::min_health = 0.0f;
		settings::visuals::flags = false;
		settings::visuals::flags_mask = 0;
		settings::visuals::flags_state_colour[0] = 1.f; settings::visuals::flags_state_colour[1] = 1.f; settings::visuals::flags_state_colour[2] = 1.f; settings::visuals::flags_state_colour[3] = 1.f;
		settings::visuals::flags_colour[0] = 1.f; settings::visuals::flags_colour[1] = 1.f; settings::visuals::flags_colour[2] = 1.f; settings::visuals::flags_colour[3] = 1.f;
		settings::visuals::client_korblox = false;
		settings::visuals::client_headless = false;
		settings::visuals::black_avatar = false;
		settings::visuals::target_recolor = false;
		settings::visuals::target_recolor_colour[0] = 1.f; settings::visuals::target_recolor_colour[1] = 0.f; settings::visuals::target_recolor_colour[2] = 0.f; settings::visuals::target_recolor_colour[3] = 1.f;
		settings::visuals::client_remove_hair = false;
		settings::visuals::client_remove_accessories = false;
		settings::visuals::avatar_recolor_custom = false;
		settings::visuals::avatar_recolor_color[0] = 0.f; settings::visuals::avatar_recolor_color[1] = 0.f; settings::visuals::avatar_recolor_color[2] = 0.f; settings::visuals::avatar_recolor_color[3] = 1.f;
		settings::visuals::avatar_recolor = false;
		for (int i = 0; i < 4; ++i) {
			settings::visuals::recolor_head[i] = 1.0f;
			settings::visuals::recolor_torso[i] = 1.0f;
			settings::visuals::recolor_left_arm[i] = 1.0f;
			settings::visuals::recolor_right_arm[i] = 1.0f;
			settings::visuals::recolor_left_leg[i] = 1.0f;
			settings::visuals::recolor_right_leg[i] = 1.0f;
		}

		settings::visuals::engine_chams = false;
		settings::visuals::engine_chams_weapons = false;
		settings::visuals::engine_chams_style = 3;
		settings::visuals::engine_chams_color_index = 0;
		settings::visuals::engine_chams_mode = 0;
		settings::visuals::engine_chams_queue_id = 10;
		settings::visuals::engine_chams_weapons_style = 2;
		for (int i = 0; i < 4; i++) {
			settings::visuals::engine_chams_colour[i] = 1.0f;
			settings::visuals::engine_chams_weapons_colour[i] = 1.0f;
		}
		settings::visuals::engine_chams_black_only = false;

		settings::hitboxexpander::enabled = false;
		settings::hitboxexpander::size_x = 5.0f;
		settings::hitboxexpander::size_y = 5.0f;
		settings::hitboxexpander::size_z = 5.0f;
		settings::hitboxexpander::visualize = false;
		settings::hitboxexpander::hitbox_colour[0] = 0.855f; settings::hitboxexpander::hitbox_colour[1] = 0.278f; settings::hitboxexpander::hitbox_colour[2] = 0.478f; settings::hitboxexpander::hitbox_colour[3] = 1.f;
		settings::hitboxexpander::hitbox_outline_colour[0] = 0.f; settings::hitboxexpander::hitbox_outline_colour[1] = 0.f; settings::hitboxexpander::hitbox_outline_colour[2] = 0.f; settings::hitboxexpander::hitbox_outline_colour[3] = 1.f;

		settings::hit_tracers::enabled = false;
		settings::hit_tracers::origin_type = 0;
		settings::hit_tracers::style = 2;
		settings::hit_tracers::duration = 1.5f;
		settings::hit_tracers::thickness = 2.0f;
		settings::hit_tracers::colour[0] = 1.0f; settings::hit_tracers::colour[1] = 0.25f; settings::hit_tracers::colour[2] = 0.45f; settings::hit_tracers::colour[3] = 1.0f;
		settings::hit_tracers::outline_colour[0] = 0.0f; settings::hit_tracers::outline_colour[1] = 0.0f; settings::hit_tracers::outline_colour[2] = 0.0f; settings::hit_tracers::outline_colour[3] = 0.8f;
		settings::hit_tracers::draw_outline = true;
		settings::hit_tracers::damage_text = true;
		settings::hit_tracers::damage_text_colour[0] = 1.0f; settings::hit_tracers::damage_text_colour[1] = 1.0f; settings::hit_tracers::damage_text_colour[2] = 1.0f; settings::hit_tracers::damage_text_colour[3] = 1.0f;

		settings::hit_chams::enabled = false;
		settings::hit_chams::style = 0;
		settings::hit_chams::fade_easing = 1;
		settings::hit_chams::duration = 1.2f;
		settings::hit_chams::scale = 1.0f;
		settings::hit_chams::fill_colour[0] = 0.2f; settings::hit_chams::fill_colour[1] = 0.6f; settings::hit_chams::fill_colour[2] = 1.0f; settings::hit_chams::fill_colour[3] = 0.45f;
		settings::hit_chams::outline_colour[0] = 0.5f; settings::hit_chams::outline_colour[1] = 0.85f; settings::hit_chams::outline_colour[2] = 1.0f; settings::hit_chams::outline_colour[3] = 0.9f;
		settings::hit_chams::outline_thickness = 1.5f;
		settings::hit_chams::draw_outline = true;

		settings::hit_skeleton::enabled = false;
		settings::hit_skeleton::style = 1;
		settings::hit_skeleton::fade_easing = 1;
		settings::hit_skeleton::duration = 1.5f;
		settings::hit_skeleton::thickness = 2.0f;
		settings::hit_skeleton::joint_radius = 3.5f;
		settings::hit_skeleton::colour[0] = 1.0f; settings::hit_skeleton::colour[1] = 0.3f; settings::hit_skeleton::colour[2] = 0.3f; settings::hit_skeleton::colour[3] = 1.0f;
		settings::hit_skeleton::outline_colour[0] = 0.0f; settings::hit_skeleton::outline_colour[1] = 0.0f; settings::hit_skeleton::outline_colour[2] = 0.0f; settings::hit_skeleton::outline_colour[3] = 0.8f;
		settings::hit_skeleton::joint_colour[0] = 1.0f; settings::hit_skeleton::joint_colour[1] = 0.9f; settings::hit_skeleton::joint_colour[2] = 0.3f; settings::hit_skeleton::joint_colour[3] = 1.0f;
		settings::hit_skeleton::draw_joints = true;
		settings::hit_skeleton::draw_outline = true;

		settings::streamproof = false;
		settings::teamcheck = false;
		settings::vsync = false;
		settings::performance_mode = false;
		settings::misc::unlock_fps = false;
		settings::misc::fps_cap = 180;
		settings::misc::auto_rescan = true;
		settings::misc::keybind_list = true;
		settings::misc::keybind_indicator = false;
		for (int i = 0; i < 4; ++i) settings::misc::keybind_indicator_feature_colour[i] = (i < 3) ? 1.0f : 1.0f;
		for (int i = 0; i < 4; ++i) settings::misc::keybind_indicator_mode_colour[i]    = (i < 3) ? 0.65f : 1.0f;
		settings::misc::watermark = true;
		settings::misc::watermark_pos_x = 10.0f;
		settings::misc::watermark_pos_y = 10.0f;
		settings::misc::keybind_list_pos_x = 10.0f;
		settings::misc::keybind_list_pos_y = 45.0f;
		settings::misc::menu_background_style = 2;
		settings::misc::explorer_window = false;
		settings::misc::spotify_window = false;
		settings::misc::performance_window = false;
		settings::misc::model_viewer_window = false;
		settings::misc::preview_model = 2; // Default to Tung Tung Tung Sahur
		settings::misc::custom_model_path[0] = '\0';
		settings::misc::sticky_preview = false;
		settings::watermark::draggable = false;
		settings::btools::enabled = false;
		settings::btools::tool_type = 0;
		settings::btools::keybind = 0;
		settings::btools::activation_mode = 1;
		settings::notifications::enabled = true;
		settings::notifications::welcome = true;
		settings::notifications::thankyou = true;
		settings::notifications::hit = true;
		settings::notifications::kill = true;
		settings::notifications::duration = 5.0f;
		settings::misc::hide_console = false;
		settings::misc::bot_support = false;

		settings::theme::preset = 0;
		Theme::ResetDefaults();
		settings::theme::accent[0] = clr->accent.Value.x; settings::theme::accent[1] = clr->accent.Value.y; settings::theme::accent[2] = clr->accent.Value.z; settings::theme::accent[3] = 1.f;
		settings::theme::background_one[0] = clr->window.background_one.Value.x; settings::theme::background_one[1] = clr->window.background_one.Value.y; settings::theme::background_one[2] = clr->window.background_one.Value.z; settings::theme::background_one[3] = 1.f;
		settings::theme::background_two[0] = clr->window.background_two.Value.x; settings::theme::background_two[1] = clr->window.background_two.Value.y; settings::theme::background_two[2] = clr->window.background_two.Value.z; settings::theme::background_two[3] = 1.f;
		settings::theme::stroke[0] = clr->window.stroke.Value.x; settings::theme::stroke[1] = clr->window.stroke.Value.y; settings::theme::stroke[2] = clr->window.stroke.Value.z; settings::theme::stroke[3] = 1.f;
		settings::theme::stroke_two[0] = clr->widgets.stroke_two.Value.x; settings::theme::stroke_two[1] = clr->widgets.stroke_two.Value.y; settings::theme::stroke_two[2] = clr->widgets.stroke_two.Value.z; settings::theme::stroke_two[3] = 1.f;
		settings::theme::text[0] = clr->widgets.text.Value.x; settings::theme::text[1] = clr->widgets.text.Value.y; settings::theme::text[2] = clr->widgets.text.Value.z; settings::theme::text[3] = 1.f;
		settings::theme::text_inactive[0] = clr->widgets.text_inactive.Value.x; settings::theme::text_inactive[1] = clr->widgets.text_inactive.Value.y; settings::theme::text_inactive[2] = clr->widgets.text_inactive.Value.z; settings::theme::text_inactive[3] = 1.f;

		settings::movement::gravity::enabled = false;
		settings::movement::gravity::value = 196.2f;
		settings::movement::tickrate::enabled = false;
		settings::movement::tickrate::value = 240.0f;

		settings::lighting::ambient::enabled = false;
		for (int i = 0; i < 4; ++i) {
			settings::lighting::ambient::color[i] = 1.f;
			settings::lighting::ambient::outdoor_color[i] = 1.f;
		}
		settings::lighting::ambient::pulse = false;
		settings::lighting::ambient::pulse_speed = 2.0f;
		settings::lighting::ambient::pulse_intensity = 0.5f;
		settings::lighting::shadows::enabled = false;
		settings::lighting::shadows::value = 0.f;
		settings::lighting::fog::enabled = false;
		settings::lighting::fog::fog_start = 0.f;
		settings::lighting::fog::fog_end = 0.f;
		for (int i = 0; i < 4; ++i) settings::lighting::fog::fog_color[i] = 1.f;
		settings::lighting::fog::pulse = false;
		settings::lighting::fog::pulse_mode = 0;
		settings::lighting::fog::pulse_speed = 2.0f;
		settings::lighting::fog::pulse_intensity = 0.5f;
		settings::lighting::clocktime::enabled = false;
		settings::lighting::clocktime::time = 0.f;
		settings::lighting::atmosphere::enabled = false;
		for (int i = 0; i < 4; ++i) {
			settings::lighting::atmosphere::color[i] = 1.f;
			settings::lighting::atmosphere::decay[i] = 1.f;
		}
		settings::lighting::atmosphere::density = 0.f;
		settings::lighting::atmosphere::glare = 0.f;
		settings::lighting::atmosphere::haze = 0.f;
		settings::lighting::atmosphere::offset = 0.f;
		settings::lighting::atmosphere::pulse = false;
		settings::lighting::atmosphere::pulse_mode = 0;
		settings::lighting::atmosphere::pulse_speed = 2.0f;
		settings::lighting::atmosphere::pulse_intensity = 0.5f;
		settings::lighting::colorshift::enabled = false;
		for (int i = 0; i < 4; ++i) {
			settings::lighting::colorshift::bottom[i] = 1.f;
			settings::lighting::colorshift::top[i] = 1.f;
		}
		settings::lighting::exposure::enabled = false;
		settings::lighting::exposure::exposure = 0.f;
		settings::lighting::starcount::enabled = false;
		settings::lighting::starcount::star_count = 3000;
		settings::lighting::sunmoon::enabled = false;
		settings::lighting::sunmoon::custom_sun_id[0] = '\0';
		settings::lighting::sunmoon::custom_moon_id[0] = '\0';
		settings::lighting::brightness::enabled = false;
		settings::lighting::brightness::value = 1.f;
		settings::lighting::environment::enabled = false;
		settings::lighting::environment::diffuse_scale = 1.f;
		settings::lighting::environment::specular_scale = 1.f;
		settings::lighting::latitude::enabled = false;
		settings::lighting::latitude::value = 0.f;

		settings::freezeplayer::enabled = false;
		settings::freezeplayer::keybind = 0;
		settings::freezeplayer::activation_mode = 1;
		settings::typingcheck::enabled = true;

		

		settings::skinchanger::enabled = false;
		settings::skinchanger::category_index = 0;
		settings::skinchanger::skin_index = 0;
		settings::skinchanger::search_filter[0] = '\0';
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			settings::skinchanger::favorites.clear();
		}

		settings::dh_skinchanger::enabled = false;
		settings::dh_skinchanger::skin_index = 0;

		settings::skyboxchanger::enabled = false;
		settings::skyboxchanger::preset = 0;


	}

	bool load_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;

		std::ifstream file(path);
		if (!file.is_open())
			return false;

		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		reset_to_defaults();
		settings::teamcheck = read_bool(json, "teamcheck", settings::teamcheck);

		std::string aimbot_section = extract_section(json, "aimbot");
		std::string triggerbot_section = extract_section(json, "triggerbot");
		std::string silentaim_section = extract_section(json, "silentaim");
		std::string raycast_silentaim_section = extract_section(json, "raycast_silentaim");
		std::string crosshair_section = extract_section(json, "crosshair");
		std::string animationchanger_section = extract_section(json, "animationchanger");
		std::string materialchanger_section = extract_section(json, "materialchanger");
		std::string movement_section = extract_section(json, "movement");
		std::string visuals_section = extract_section(json, "visuals");
		std::string hitboxexpander_section = extract_section(json, "hitboxexpander");
		std::string hit_tracers_section = extract_section(json, "hit_tracers");
		std::string hit_chams_section = extract_section(json, "hit_chams");
		std::string hit_skeleton_section = extract_section(json, "hit_skeleton");
		std::string theme_section = extract_section(json, "theme");
		std::string misc_section = extract_section(json, "misc");
		std::string lighting_section = extract_section(json, "lighting");
		std::string freezeplayer_section = extract_section(json, "freezeplayer");
		std::string typingcheck_section = extract_section(json, "typingcheck");
		std::string config_system_section = extract_section(json, "config_system");
		
		std::string hitsounds_section = extract_section(json, "hitsounds");
		std::string killsounds_section = extract_section(json, "killsounds");
		std::string godmode_section = extract_section(json, "godmode");
		std::string dh_skinchanger_section = extract_section(json, "dh_skinchanger");
		std::string skinchanger_section = extract_section(json, "skinchanger");

		std::string speedhack_section = extract_section(movement_section, "speedhack");
		std::string flyhack_section = extract_section(movement_section, "flyhack");

		std::string spin360_section = extract_section(movement_section, "spin360");
		std::string gravity_section = extract_section(movement_section, "gravity");
		std::string tickrate_section = extract_section(movement_section, "tickrate");
		std::string wallbug_section = extract_section(movement_section, "wallbug");
		std::string bhop_section = extract_section(movement_section, "bhop");

		std::string ambient_section = extract_section(lighting_section, "ambient");
		std::string shadows_section = extract_section(lighting_section, "shadows");
		std::string fog_section = extract_section(lighting_section, "fog");
		std::string clocktime_section = extract_section(lighting_section, "clocktime");
		std::string atmosphere_section = extract_section(lighting_section, "atmosphere");
		std::string colorshift_section = extract_section(lighting_section, "colorshift");
		std::string exposure_section = extract_section(lighting_section, "exposure");
		std::string starcount_section = extract_section(lighting_section, "starcount");
		std::string sunmoon_section = extract_section(lighting_section, "sunmoon");
		std::string brightness_section = extract_section(lighting_section, "brightness");
		std::string environment_section = extract_section(lighting_section, "environment");
		std::string latitude_section = extract_section(lighting_section, "latitude");
		std::string terrain_section = extract_section(lighting_section, "terrain");
		std::string bloom_section = extract_section(lighting_section, "bloom");
		std::string sunrays_section = extract_section(lighting_section, "sunrays");
		std::string color_correction_section = extract_section(lighting_section, "color_correction");
		std::string depth_of_field_section = extract_section(lighting_section, "depth_of_field");

		if (!aimbot_section.empty())
		{
			settings::aimbot::enabled = read_bool(aimbot_section, "enabled", settings::aimbot::enabled);
			settings::aimbot::keybind = read_int(aimbot_section, "keybind", settings::aimbot::keybind);
			settings::aimbot::activation_mode = read_int(aimbot_section, "activation_mode", settings::aimbot::activation_mode);
			settings::aimbot::mode = read_int(aimbot_section, "mode", settings::aimbot::mode);
			settings::aimbot::mouse_sensitivity = read_float(aimbot_section, "mouse_sensitivity", settings::aimbot::mouse_sensitivity);
			settings::aimbot::target_part = read_int(aimbot_section, "target_part", settings::aimbot::target_part);
			settings::aimbot::air_part = read_int(aimbot_section, "air_part", settings::aimbot::air_part);
			settings::aimbot::fov = read_float(aimbot_section, "fov", settings::aimbot::fov);
			settings::aimbot::use_fov = read_bool(aimbot_section, "use_fov", settings::aimbot::use_fov);
			settings::aimbot::dynamic_fov = read_bool(aimbot_section, "dynamic_fov", settings::aimbot::dynamic_fov);
			settings::aimbot::ring_fov_enabled = read_bool(aimbot_section, "ring_fov_enabled", settings::aimbot::ring_fov_enabled);
			settings::aimbot::ring_fov_inner = read_float(aimbot_section, "ring_fov_inner", settings::aimbot::ring_fov_inner);
			settings::aimbot::ring_fov_outer = read_float(aimbot_section, "ring_fov_outer", settings::aimbot::ring_fov_outer);
			settings::aimbot::smoothing = read_bool(aimbot_section, "smoothing", settings::aimbot::smoothing);
			settings::aimbot::smoothingx = read_float(aimbot_section, "smoothingx", settings::aimbot::smoothingx);
			settings::aimbot::smoothingy = read_float(aimbot_section, "smoothingy", settings::aimbot::smoothingy);
			settings::aimbot::smoothing_style = read_int(aimbot_section, "smoothing_style", settings::aimbot::smoothing_style);
			settings::aimbot::shake = read_bool(aimbot_section, "shake", settings::aimbot::shake);
			settings::aimbot::shake_value = read_float(aimbot_section, "shake_value", settings::aimbot::shake_value);
			settings::aimbot::enable_prediction = read_bool(aimbot_section, "enable_prediction", settings::aimbot::enable_prediction);
			settings::aimbot::prediction_x = read_float(aimbot_section, "prediction_x", settings::aimbot::prediction_x);
			settings::aimbot::prediction_y = read_float(aimbot_section, "prediction_y", settings::aimbot::prediction_y);
			settings::aimbot::jump_prediction = read_bool(aimbot_section, "jump_prediction", settings::aimbot::jump_prediction);
			settings::aimbot::jump_prediction_value = read_float(aimbot_section, "jump_prediction_value", settings::aimbot::jump_prediction_value);
			settings::aimbot::fall_prediction = read_bool(aimbot_section, "fall_prediction", settings::aimbot::fall_prediction);
			settings::aimbot::fall_prediction_value = read_float(aimbot_section, "fall_prediction_value", settings::aimbot::fall_prediction_value);
			settings::aimbot::jump_fall_prediction = read_bool(aimbot_section, "jump_fall_prediction", settings::aimbot::jump_fall_prediction);
			settings::aimbot::teamcheck = read_bool(aimbot_section, "teamcheck", settings::aimbot::teamcheck);
			settings::aimbot::knock_check = read_bool(aimbot_section, "knock_check", settings::aimbot::knock_check);
			settings::aimbot::disable_on_kill = read_bool(aimbot_section, "disable_on_kill", settings::aimbot::disable_on_kill);
			settings::aimbot::priorities = read_int(aimbot_section, "priorities", settings::aimbot::priorities);
			settings::aimbot::health_check_enabled = read_bool(aimbot_section, "health_check_enabled", settings::aimbot::health_check_enabled);
			settings::aimbot::min_health = read_float(aimbot_section, "min_health", settings::aimbot::min_health);
			settings::aimbot::sticky_aim = read_bool(aimbot_section, "sticky_aim", settings::aimbot::sticky_aim);
			settings::aimbot::draw_fov = read_bool(aimbot_section, "draw_fov", settings::aimbot::draw_fov);
			settings::aimbot::fill_fov = read_bool(aimbot_section, "fill_fov", settings::aimbot::fill_fov);
			read_color(aimbot_section, "fov_circle_colour", settings::aimbot::fov_circle_colour);
			read_color(aimbot_section, "fov_outline_colour", settings::aimbot::fov_outline_colour);
			read_color(aimbot_section, "fov_fill_colour", settings::aimbot::fov_fill_colour);
		}

		if (!triggerbot_section.empty())
		{
			settings::triggerbot::enabled = read_bool(triggerbot_section, "enabled", settings::triggerbot::enabled);
			settings::triggerbot::method = read_int(triggerbot_section, "method", settings::triggerbot::method);
			settings::triggerbot::keybind = read_int(triggerbot_section, "keybind", settings::triggerbot::keybind);
			settings::triggerbot::activation_mode = read_int(triggerbot_section, "activation_mode", settings::triggerbot::activation_mode);
			settings::triggerbot::target_part = read_int(triggerbot_section, "target_part", settings::triggerbot::target_part);
			settings::triggerbot::threshold = read_float(triggerbot_section, "threshold", settings::triggerbot::threshold);
			settings::triggerbot::delay_ms = read_float(triggerbot_section, "delay_ms", settings::triggerbot::delay_ms);
			settings::triggerbot::cooldown_ms = read_float(triggerbot_section, "cooldown_ms", settings::triggerbot::cooldown_ms);
			settings::triggerbot::teamcheck = read_bool(triggerbot_section, "teamcheck", settings::triggerbot::teamcheck);
			settings::triggerbot::knock_check = read_bool(triggerbot_section, "knock_check", settings::triggerbot::knock_check);
			settings::triggerbot::guncheck = read_bool(triggerbot_section, "guncheck", settings::triggerbot::guncheck);
			settings::triggerbot::wallcheck = read_bool(triggerbot_section, "wallcheck", settings::triggerbot::wallcheck);
			settings::triggerbot::draw_fov = read_bool(triggerbot_section, "draw_fov", settings::triggerbot::draw_fov);
			settings::triggerbot::fill_fov = read_bool(triggerbot_section, "fill_fov", settings::triggerbot::fill_fov);
			read_color(triggerbot_section, "fov_circle_colour", settings::triggerbot::fov_circle_colour);
			read_color(triggerbot_section, "fov_outline_colour", settings::triggerbot::fov_outline_colour);
			read_color(triggerbot_section, "fov_fill_colour", settings::triggerbot::fov_fill_colour);
		}

		if (!silentaim_section.empty())
		{
			settings::silentaim::enabled = read_bool(silentaim_section, "enabled", settings::silentaim::enabled);
			settings::silentaim::method = read_int(silentaim_section, "method", settings::silentaim::method);
			settings::silentaim::keybind = read_int(silentaim_section, "keybind", settings::silentaim::keybind);
			settings::silentaim::activation_mode = read_int(silentaim_section, "activation_mode", settings::silentaim::activation_mode);
			settings::silentaim::target_part = read_int(silentaim_section, "target_part", settings::silentaim::target_part);
			settings::silentaim::snapline = read_bool(silentaim_section, "snapline", settings::silentaim::snapline);
			settings::silentaim::snapline_lerp = read_bool(silentaim_section, "snapline_lerp", settings::silentaim::snapline_lerp);
			settings::silentaim::snapline_origin = read_int(silentaim_section, "snapline_origin", settings::silentaim::snapline_origin);
			read_color(silentaim_section, "snapline_colour", settings::silentaim::snapline_colour);
			settings::silentaim::fov = read_float(silentaim_section, "fov", settings::silentaim::fov);
			settings::silentaim::mode_360 = read_bool(silentaim_section, "mode_360", settings::silentaim::mode_360);
			settings::silentaim::use_fov = read_bool(silentaim_section, "use_fov", settings::silentaim::use_fov);
			settings::silentaim::dynamic_fov = read_bool(silentaim_section, "dynamic_fov", settings::silentaim::dynamic_fov);
			settings::silentaim::use_aimbot_target = read_bool(silentaim_section, "use_aimbot_target", settings::silentaim::use_aimbot_target);
			settings::silentaim::draw_fov = read_bool(silentaim_section, "draw_fov", settings::silentaim::draw_fov);
			settings::silentaim::fill_fov = read_bool(silentaim_section, "fill_fov", settings::silentaim::fill_fov);
			read_color(silentaim_section, "fov_circle_colour", settings::silentaim::fov_circle_colour);
			read_color(silentaim_section, "fov_outline_colour", settings::silentaim::fov_outline_colour);
			read_color(silentaim_section, "fov_fill_colour", settings::silentaim::fov_fill_colour);
			settings::silentaim::sticky_aim = read_bool(silentaim_section, "sticky_aim", settings::silentaim::sticky_aim);
			settings::silentaim::knock_check = read_bool(silentaim_section, "knock_check", settings::silentaim::knock_check);
			settings::silentaim::auto_switch = read_bool(silentaim_section, "auto_switch", settings::silentaim::auto_switch);
			settings::silentaim::spoof_mouse = read_bool(silentaim_section, "spoof_mouse", settings::silentaim::spoof_mouse);
			settings::silentaim::teamcheck = read_bool(silentaim_section, "teamcheck", settings::silentaim::teamcheck);
			settings::silentaim::guncheck = read_bool(silentaim_section, "guncheck", settings::silentaim::guncheck);
			settings::silentaim::priorities = read_int(silentaim_section, "priorities", settings::silentaim::priorities);
			settings::silentaim::health_check_enabled = read_bool(silentaim_section, "health_check_enabled", settings::silentaim::health_check_enabled);
			settings::silentaim::min_health = read_float(silentaim_section, "min_health", settings::silentaim::min_health);
			settings::silentaim::enable_prediction = read_bool(silentaim_section, "enable_prediction", settings::silentaim::enable_prediction);
			settings::silentaim::prediction_x = read_float(silentaim_section, "prediction_x", settings::silentaim::prediction_x);
			settings::silentaim::prediction_y = read_float(silentaim_section, "prediction_y", settings::silentaim::prediction_y);
			settings::silentaim::forcefield_check = read_bool(silentaim_section, "forcefield_check", settings::silentaim::forcefield_check);
			settings::silentaim::katana_check = read_bool(silentaim_section, "katana_check", settings::silentaim::katana_check);
		}

		if (!raycast_silentaim_section.empty())
		{
			settings::raycast_silentaim::enabled = read_bool(raycast_silentaim_section, "enabled", settings::raycast_silentaim::enabled);
			settings::raycast_silentaim::mode_360 = read_bool(raycast_silentaim_section, "mode_360", settings::raycast_silentaim::mode_360);
			settings::raycast_silentaim::mode_360_keybind = read_int(raycast_silentaim_section, "mode_360_keybind", settings::raycast_silentaim::mode_360_keybind);
			settings::raycast_silentaim::mode_360_activation_mode = read_int(raycast_silentaim_section, "mode_360_activation_mode", settings::raycast_silentaim::mode_360_activation_mode);
			settings::raycast_silentaim::draw_fov = read_bool(raycast_silentaim_section, "draw_fov", settings::raycast_silentaim::draw_fov);
			settings::raycast_silentaim::fill_fov = read_bool(raycast_silentaim_section, "fill_fov", settings::raycast_silentaim::fill_fov);
			settings::raycast_silentaim::enable_prediction = read_bool(raycast_silentaim_section, "enable_prediction", settings::raycast_silentaim::enable_prediction);
			settings::raycast_silentaim::teamcheck = read_bool(raycast_silentaim_section, "teamcheck", settings::raycast_silentaim::teamcheck);
			settings::raycast_silentaim::magic_bullet = read_bool(raycast_silentaim_section, "magic_bullet", settings::raycast_silentaim::magic_bullet);
			settings::raycast_silentaim::forcefield_check = read_bool(raycast_silentaim_section, "forcefield_check", settings::raycast_silentaim::forcefield_check);
			settings::raycast_silentaim::hitmarkers = read_bool(raycast_silentaim_section, "hitmarkers", settings::raycast_silentaim::hitmarkers);
			read_color(raycast_silentaim_section, "hitmarker_colour", settings::raycast_silentaim::hitmarker_colour);
			settings::raycast_silentaim::hitmarker_size = read_float(raycast_silentaim_section, "hitmarker_size", settings::raycast_silentaim::hitmarker_size);
			settings::raycast_silentaim::hitmarker_gap = read_float(raycast_silentaim_section, "hitmarker_gap", settings::raycast_silentaim::hitmarker_gap);
			settings::raycast_silentaim::hitmarker_duration = read_float(raycast_silentaim_section, "hitmarker_duration", settings::raycast_silentaim::hitmarker_duration);
			settings::raycast_silentaim::hit_flash = read_bool(raycast_silentaim_section, "hit_flash", settings::raycast_silentaim::hit_flash);
			read_color(raycast_silentaim_section, "hit_flash_colour", settings::raycast_silentaim::hit_flash_colour);
			settings::raycast_silentaim::hit_flash_duration = read_float(raycast_silentaim_section, "hit_flash_duration", settings::raycast_silentaim::hit_flash_duration);
			settings::raycast_silentaim::magic_bullet_keybind = read_int(raycast_silentaim_section, "magic_bullet_keybind", settings::raycast_silentaim::magic_bullet_keybind);
			settings::raycast_silentaim::magic_bullet_activation_mode = read_int(raycast_silentaim_section, "magic_bullet_activation_mode", settings::raycast_silentaim::magic_bullet_activation_mode);
			settings::raycast_silentaim::magic_bullet_method = read_int(raycast_silentaim_section, "magic_bullet_method", settings::raycast_silentaim::magic_bullet_method);
			settings::raycast_silentaim::use_fov = read_bool(raycast_silentaim_section, "use_fov", settings::raycast_silentaim::use_fov);
			settings::raycast_silentaim::dynamic_fov = read_bool(raycast_silentaim_section, "dynamic_fov", settings::raycast_silentaim::dynamic_fov);
			settings::raycast_silentaim::fov = read_float(raycast_silentaim_section, "fov", settings::raycast_silentaim::fov);
			read_color(raycast_silentaim_section, "fov_circle_colour", settings::raycast_silentaim::fov_circle_colour);
			read_color(raycast_silentaim_section, "fov_outline_colour", settings::raycast_silentaim::fov_outline_colour);
			read_color(raycast_silentaim_section, "fov_fill_colour", settings::raycast_silentaim::fov_fill_colour);
			settings::raycast_silentaim::keybind = read_int(raycast_silentaim_section, "keybind", settings::raycast_silentaim::keybind);
			settings::raycast_silentaim::activation_mode = read_int(raycast_silentaim_section, "activation_mode", settings::raycast_silentaim::activation_mode);
		}

		if (!crosshair_section.empty())
		{
			settings::crosshair::enabled = read_bool(crosshair_section, "enabled", settings::crosshair::enabled);
			settings::crosshair::attach_to_enemy = read_bool(crosshair_section, "attach_to_enemy", settings::crosshair::attach_to_enemy);
			settings::crosshair::lerp = read_bool(crosshair_section, "lerp", settings::crosshair::lerp);
			settings::crosshair::lerp_speed = read_float(crosshair_section, "lerp_speed", settings::crosshair::lerp_speed);
			settings::crosshair::style = read_int(crosshair_section, "style", settings::crosshair::style);
			read_color(crosshair_section, "colour", settings::crosshair::colour);
			read_color(crosshair_section, "outline_colour", settings::crosshair::outline_colour);
			settings::crosshair::outline = read_bool(crosshair_section, "outline", settings::crosshair::outline);
			settings::crosshair::size = read_float(crosshair_section, "size", settings::crosshair::size);
			settings::crosshair::gap = read_float(crosshair_section, "gap", settings::crosshair::gap);
			settings::crosshair::thickness = read_float(crosshair_section, "thickness", settings::crosshair::thickness);
			settings::crosshair::dot = read_bool(crosshair_section, "dot", settings::crosshair::dot);
			settings::crosshair::dot_size = read_float(crosshair_section, "dot_size", settings::crosshair::dot_size);
			settings::crosshair::rotate = read_bool(crosshair_section, "rotate", settings::crosshair::rotate);
			settings::crosshair::rotate_speed = read_float(crosshair_section, "rotate_speed", settings::crosshair::rotate_speed);
			settings::crosshair::pulse = read_bool(crosshair_section, "pulse", settings::crosshair::pulse);
			settings::crosshair::pulse_speed = read_float(crosshair_section, "pulse_speed", settings::crosshair::pulse_speed);
			settings::crosshair::pulse_amount = read_float(crosshair_section, "pulse_amount", settings::crosshair::pulse_amount);
			settings::crosshair::rainbow = read_bool(crosshair_section, "rainbow", settings::crosshair::rainbow);
			settings::crosshair::rainbow_speed = read_float(crosshair_section, "rainbow_speed", settings::crosshair::rainbow_speed);
			settings::crosshair::dynamic_gap = read_bool(crosshair_section, "dynamic_gap", settings::crosshair::dynamic_gap);
			settings::crosshair::expansion_amount = read_float(crosshair_section, "expansion_amount", settings::crosshair::expansion_amount);
			settings::crosshair::expansion_decay = read_float(crosshair_section, "expansion_decay", settings::crosshair::expansion_decay);
		}

		if (!hitsounds_section.empty())
		{
			settings::hitsounds::enabled = read_bool(hitsounds_section, "enabled", settings::hitsounds::enabled);
			settings::hitsounds::type = read_int(hitsounds_section, "type", settings::hitsounds::type);
			settings::hitsounds::method = read_int(hitsounds_section, "method", settings::hitsounds::method);
			std::string path_val = read_string(hitsounds_section, "custom_path", settings::hitsounds::custom_path);
			strcpy_s(settings::hitsounds::custom_path, 260, path_val.c_str());
			settings::hitsounds::volume = read_float(hitsounds_section, "volume", settings::hitsounds::volume);
		}

		if (!killsounds_section.empty())
		{
			settings::killsounds::enabled = read_bool(killsounds_section, "enabled", settings::killsounds::enabled);
			settings::killsounds::type = read_int(killsounds_section, "type", settings::killsounds::type);
			std::string path_val = read_string(killsounds_section, "custom_path", settings::killsounds::custom_path);
			strcpy_s(settings::killsounds::custom_path, 260, path_val.c_str());
			settings::killsounds::volume = read_float(killsounds_section, "volume", settings::killsounds::volume);
		}

		if (!animationchanger_section.empty())
		{
			settings::animationchanger::enabled = read_bool(animationchanger_section, "enabled", settings::animationchanger::enabled);
			settings::animationchanger::type = read_int(animationchanger_section, "type", settings::animationchanger::type);
			settings::animationchanger::mode = read_int(animationchanger_section, "mode", settings::animationchanger::mode);
		}

		if (!materialchanger_section.empty())
		{
			settings::materialchanger::enabled = read_bool(materialchanger_section, "enabled", settings::materialchanger::enabled);
			settings::materialchanger::material_index = read_int(materialchanger_section, "material_index", settings::materialchanger::material_index);
			settings::materialchanger::affect_accessories = read_bool(materialchanger_section, "affect_accessories", settings::materialchanger::affect_accessories);
		}

		if (!godmode_section.empty())
		{
			settings::godmode::enabled = read_bool(godmode_section, "enabled", settings::godmode::enabled);
		}

		if (!speedhack_section.empty())
		{
			settings::movement::speedhack::enabled = read_bool(speedhack_section, "enabled", settings::movement::speedhack::enabled);
			settings::movement::speedhack::mode = read_int(speedhack_section, "mode", settings::movement::speedhack::mode);
			settings::movement::speedhack::speed = read_float(speedhack_section, "speed", settings::movement::speedhack::speed);
			settings::movement::speedhack::keybind = read_int(speedhack_section, "keybind", settings::movement::speedhack::keybind);
			settings::movement::speedhack::activation_mode = read_int(speedhack_section, "activation_mode", settings::movement::speedhack::activation_mode);
		}
		if (!flyhack_section.empty())
		{
			settings::movement::flyhack::enabled = read_bool(flyhack_section, "enabled", settings::movement::flyhack::enabled);
			settings::movement::flyhack::mode = read_int(flyhack_section, "mode", settings::movement::flyhack::mode);
			settings::movement::flyhack::speed = read_float(flyhack_section, "speed", settings::movement::flyhack::speed);
			settings::movement::flyhack::keybind = read_int(flyhack_section, "keybind", settings::movement::flyhack::keybind);
			settings::movement::flyhack::activation_mode = read_int(flyhack_section, "activation_mode", settings::movement::flyhack::activation_mode);
		}

		if (!spin360_section.empty())
		{
			settings::movement::spin360::enabled = read_bool(spin360_section, "enabled", settings::movement::spin360::enabled);
			settings::movement::spin360::keybind = read_int(spin360_section, "keybind", settings::movement::spin360::keybind);
			settings::movement::spin360::speed = read_float(spin360_section, "speed", settings::movement::spin360::speed);
			settings::movement::spin360::smoothness = read_float(spin360_section, "smoothness", settings::movement::spin360::smoothness);
		}
		if (!gravity_section.empty())
		{
			settings::movement::gravity::enabled = read_bool(gravity_section, "enabled", settings::movement::gravity::enabled);
			settings::movement::gravity::value = read_float(gravity_section, "value", settings::movement::gravity::value);
		}
		if (!tickrate_section.empty())
		{
			settings::movement::tickrate::enabled = read_bool(tickrate_section, "enabled", settings::movement::tickrate::enabled);
			settings::movement::tickrate::value = read_float(tickrate_section, "value", settings::movement::tickrate::value);
		}
		if (!wallbug_section.empty())
		{
			settings::movement::wallbug::enabled = read_bool(wallbug_section, "enabled", settings::movement::wallbug::enabled);
			settings::movement::wallbug::height = read_float(wallbug_section, "height", settings::movement::wallbug::height);
		}
		if (!bhop_section.empty())
		{
			settings::movement::bhop::enabled = read_bool(bhop_section, "enabled", settings::movement::bhop::enabled);
			settings::movement::bhop::speed = read_float(bhop_section, "speed", settings::movement::bhop::speed);
			settings::movement::bhop::keybind = read_int(bhop_section, "keybind", settings::movement::bhop::keybind);
			settings::movement::bhop::activation_mode = read_int(bhop_section, "activation_mode", settings::movement::bhop::activation_mode);
		}
		settings::movement::noclip::enabled = read_bool(movement_section, "noclip_enabled", settings::movement::noclip::enabled);
		settings::movement::noclip::keybind = read_int(movement_section, "noclip_keybind", settings::movement::noclip::keybind);
		settings::movement::noclip::activation_mode = read_int(movement_section, "noclip_activation_mode", settings::movement::noclip::activation_mode);
		settings::movement::wallslide::enabled = read_bool(movement_section, "wallslide_enabled", settings::movement::wallslide::enabled);
		settings::movement::wallslide::speed = read_float(movement_section, "wallslide_speed", settings::movement::wallslide::speed);
		settings::movement::wallslide::keybind = read_int(movement_section, "wallslide_keybind", settings::movement::wallslide::keybind);
		settings::movement::wallslide::activation_mode = read_int(movement_section, "wallslide_activation_mode", settings::movement::wallslide::activation_mode);
		settings::movement::pixelsurf::enabled = read_bool(movement_section, "pixelsurf_enabled", settings::movement::pixelsurf::enabled);
		settings::movement::pixelsurf::speed = read_float(movement_section, "pixelsurf_speed", settings::movement::pixelsurf::speed);
		settings::movement::pixelsurf::keybind = read_int(movement_section, "pixelsurf_keybind", settings::movement::pixelsurf::keybind);
		settings::movement::pixelsurf::activation_mode = read_int(movement_section, "pixelsurf_activation_mode", settings::movement::pixelsurf::activation_mode);
		settings::movement::voidhide::enabled = read_bool(movement_section, "voidhide_enabled", settings::movement::voidhide::enabled);
		settings::movement::voidhide::keybind = read_int(movement_section, "voidhide_keybind", settings::movement::voidhide::keybind);
		settings::movement::voidhide::activation_mode = read_int(movement_section, "voidhide_activation_mode", settings::movement::voidhide::activation_mode);
		settings::movement::third_person::enabled = read_bool(movement_section, "third_person_enabled", settings::movement::third_person::enabled);
		settings::movement::third_person::distance = read_float(movement_section, "third_person_distance", settings::movement::third_person::distance);
		settings::movement::third_person::keybind = read_int(movement_section, "third_person_keybind", settings::movement::third_person::keybind);
		settings::movement::third_person::activation_mode = read_int(movement_section, "third_person_activation_mode", settings::movement::third_person::activation_mode);
		settings::movement::fov_changer::enabled = read_bool(movement_section, "fov_changer_enabled", settings::movement::fov_changer::enabled);
		settings::movement::fov_changer::fov_value = read_float(movement_section, "fov_changer_value", settings::movement::fov_changer::fov_value);
		settings::movement::fov_changer::dynamic = read_bool(movement_section, "fov_changer_dynamic", settings::movement::fov_changer::dynamic);
		settings::movement::fov_changer::keybind = read_int(movement_section, "fov_changer_keybind", settings::movement::fov_changer::keybind);
		settings::movement::fov_changer::activation_mode = read_int(movement_section, "fov_changer_activation_mode", settings::movement::fov_changer::activation_mode);
		settings::movement::freecam::enabled = read_bool(movement_section, "freecam_enabled", settings::movement::freecam::enabled);
		settings::movement::freecam::keybind = read_int(movement_section, "freecam_keybind", settings::movement::freecam::keybind);
		settings::movement::freecam::activation_mode = read_int(movement_section, "freecam_activation_mode", settings::movement::freecam::activation_mode);
		settings::movement::freecam::mouse_look_mode = read_int(movement_section, "freecam_mouse_look_mode", settings::movement::freecam::mouse_look_mode);
		settings::movement::freecam::speed = read_float(movement_section, "freecam_speed", settings::movement::freecam::speed);
		settings::movement::freecam::sensitivity = read_float(movement_section, "freecam_sensitivity", settings::movement::freecam::sensitivity);
		settings::movement::freecam::shift_multiplier = read_float(movement_section, "freecam_shift_multiplier", settings::movement::freecam::shift_multiplier);
		settings::movement::freecam::azerty = read_bool(movement_section, "freecam_azerty", settings::movement::freecam::azerty);
		settings::movement::freecam::fov_override = read_bool(movement_section, "freecam_fov_override", settings::movement::freecam::fov_override);
		settings::movement::freecam::fov_value = read_float(movement_section, "freecam_fov_value", settings::movement::freecam::fov_value);
		settings::movement::freecam::lock_character = read_bool(movement_section, "freecam_lock_character", settings::movement::freecam::lock_character);
		settings::movement::infjump::enabled = read_bool(movement_section, "infjump", settings::movement::infjump::enabled);
		settings::movement::hipheight::enabled = read_bool(movement_section, "hipheight_enabled", settings::movement::hipheight::enabled);
		settings::movement::hipheight::value = read_float(movement_section, "hipheight_value", settings::movement::hipheight::value);

		if (!visuals_section.empty())
		{
			settings::visuals::enabled = read_bool(visuals_section, "enabled", settings::visuals::enabled);
			settings::visuals::keybind = read_int(visuals_section, "keybind", settings::visuals::keybind);
			settings::visuals::activation_mode = read_int(visuals_section, "activation_mode", settings::visuals::activation_mode);
			read_priority_settings(visuals_section, "neutral", settings::visuals::neutral);
			read_priority_settings(visuals_section, "friendly", settings::visuals::friendly);
			read_priority_settings(visuals_section, "hostile", settings::visuals::hostile);
			settings::visuals::teamcheck = read_bool(visuals_section, "teamcheck", settings::visuals::teamcheck);
			settings::visuals::esp_font_index = read_int(visuals_section, "esp_font_index", settings::visuals::esp_font_index);
			settings::visuals::esp_font_size = read_float(visuals_section, "esp_font_size", settings::visuals::esp_font_size);
			settings::visuals::bounding_type = read_int(visuals_section, "bounding_type", settings::visuals::bounding_type);
			for (size_t i = 0; i < settings::visuals::render_outlines.size(); i++) {
				std::string name = "render_outline_" + std::to_string(i);
				settings::visuals::render_outlines[i] = read_int(visuals_section, name.c_str(), settings::visuals::render_outlines[i]);
			}
			settings::visuals::skeleton_thickness = read_float(visuals_section, "skeleton_thickness", settings::visuals::skeleton_thickness);
			settings::visuals::box_thickness = read_float(visuals_section, "box_thickness", settings::visuals::box_thickness);
			settings::visuals::healthbar_thickness = read_float(visuals_section, "healthbar_thickness", settings::visuals::healthbar_thickness);
			settings::visuals::chams_thickness = read_float(visuals_section, "chams_thickness", settings::visuals::chams_thickness);
			settings::visuals::chams_outline_thickness = read_float(visuals_section, "chams_outline_thickness", settings::visuals::chams_outline_thickness);
			settings::visuals::text_outline_thickness = read_float(visuals_section, "text_outline_thickness", settings::visuals::text_outline_thickness);
			settings::visuals::distance_unit = read_int(visuals_section, "distance_unit", settings::visuals::distance_unit);
			settings::visuals::distance_check = read_bool(visuals_section, "distance_check", settings::visuals::distance_check);
			settings::visuals::max_distance = read_float(visuals_section, "max_distance", settings::visuals::max_distance);
			settings::visuals::sort_by_status = read_bool(visuals_section, "sort_by_status", settings::visuals::sort_by_status);
			settings::visuals::knock_check = read_bool(visuals_section, "knock_check", settings::visuals::knock_check);
			settings::visuals::local_player = read_bool(visuals_section, "local_player", settings::visuals::local_player);
			settings::visuals::tool_check = read_bool(visuals_section, "tool_check", settings::visuals::tool_check);
			settings::visuals::godded_check = read_bool(visuals_section, "godded_check", settings::visuals::godded_check);
			settings::visuals::forcefield_check = read_bool(visuals_section, "forcefield_check", settings::visuals::forcefield_check);
			settings::visuals::alive_check = read_bool(visuals_section, "alive_check", settings::visuals::alive_check);
			settings::visuals::ragdoll_check = read_bool(visuals_section, "ragdoll_check", settings::visuals::ragdoll_check);
			settings::visuals::visible_check = read_bool(visuals_section, "visible_check", settings::visuals::visible_check);
			settings::visuals::static_on_void = read_bool(visuals_section, "static_on_void", settings::visuals::static_on_void);
			settings::visuals::show_none = read_bool(visuals_section, "show_none", settings::visuals::show_none);
			settings::visuals::use_display_name = read_bool(visuals_section, "use_display_name", settings::visuals::use_display_name);
			settings::visuals::combined_name = read_bool(visuals_section, "combined_name", settings::visuals::combined_name);
			settings::visuals::global_outline_type = read_int(visuals_section, "global_outline_type", settings::visuals::global_outline_type);
			settings::visuals::ignore_full_health = read_bool(visuals_section, "ignore_full_health", settings::visuals::ignore_full_health);
			settings::visuals::static_on_death = read_bool(visuals_section, "static_on_death", settings::visuals::static_on_death);
			settings::visuals::corpse_shift = read_float(visuals_section, "corpse_shift", settings::visuals::corpse_shift);
			settings::visuals::corpse_max_dist = read_float(visuals_section, "corpse_max_dist", settings::visuals::corpse_max_dist);
			settings::visuals::enemy_highlight = read_bool(visuals_section, "enemy_highlight", settings::visuals::enemy_highlight);
			read_color(visuals_section, "enemy_highlight_colour", settings::visuals::enemy_highlight_colour);
			settings::visuals::friendly_highlight = read_bool(visuals_section, "friendly_highlight", settings::visuals::friendly_highlight);
			read_color(visuals_section, "friendly_highlight_colour", settings::visuals::friendly_highlight_colour);
			settings::visuals::bar_fill = read_bool(visuals_section, "bar_fill", settings::visuals::bar_fill);
			read_color(visuals_section, "bar_fill_colour", settings::visuals::bar_fill_colour);
			settings::visuals::health_check_enabled = read_bool(visuals_section, "health_check_enabled", settings::visuals::health_check_enabled);
			settings::visuals::min_health = read_float(visuals_section, "min_health", settings::visuals::min_health);
			settings::visuals::chams_hit_impact = read_bool(visuals_section, "chams_hit_impact", settings::visuals::chams_hit_impact);
			read_color(visuals_section, "chams_hit_impact_colour", settings::visuals::chams_hit_impact_colour);
			settings::visuals::flags = read_bool(visuals_section, "flags", settings::visuals::flags);
			settings::visuals::flags_mask = read_int(visuals_section, "flags_mask", settings::visuals::flags_mask);
			read_color(visuals_section, "flags_state_colour", settings::visuals::flags_state_colour);
			read_color(visuals_section, "flags_colour", settings::visuals::flags_colour);
			settings::visuals::client_korblox = read_bool(visuals_section, "client_korblox", settings::visuals::client_korblox);
			settings::visuals::client_headless = read_bool(visuals_section, "client_headless", settings::visuals::client_headless);
			settings::visuals::black_avatar = read_bool(visuals_section, "black_avatar", settings::visuals::black_avatar);
			settings::visuals::target_recolor = read_bool(visuals_section, "target_recolor", settings::visuals::target_recolor);
			read_color(visuals_section, "target_recolor_colour", settings::visuals::target_recolor_colour);
			settings::visuals::client_remove_hair = read_bool(visuals_section, "client_remove_hair", settings::visuals::client_remove_hair);
			settings::visuals::client_remove_accessories = read_bool(visuals_section, "client_remove_accessories", settings::visuals::client_remove_accessories);
			settings::visuals::avatar_recolor_custom = read_bool(visuals_section, "avatar_recolor_custom", settings::visuals::avatar_recolor_custom);
			read_color(visuals_section, "avatar_recolor_color", settings::visuals::avatar_recolor_color);
			settings::visuals::avatar_recolor = read_bool(visuals_section, "avatar_recolor", settings::visuals::avatar_recolor);
			read_color(visuals_section, "recolor_head", settings::visuals::recolor_head);
			read_color(visuals_section, "recolor_torso", settings::visuals::recolor_torso);
			read_color(visuals_section, "recolor_left_arm", settings::visuals::recolor_left_arm);
			read_color(visuals_section, "recolor_right_arm", settings::visuals::recolor_right_arm);
			read_color(visuals_section, "recolor_left_leg", settings::visuals::recolor_left_leg);
			read_color(visuals_section, "recolor_right_leg", settings::visuals::recolor_right_leg);
			settings::visuals::engine_chams = read_bool(visuals_section, "engine_chams", settings::visuals::engine_chams);
			settings::visuals::engine_chams_weapons = read_bool(visuals_section, "engine_chams_weapons", settings::visuals::engine_chams_weapons);
			settings::visuals::engine_chams_style = read_int(visuals_section, "engine_chams_style", settings::visuals::engine_chams_style);
			settings::visuals::engine_chams_color_index = read_int(visuals_section, "engine_chams_color_index", settings::visuals::engine_chams_color_index);
			settings::visuals::engine_chams_mode = read_int(visuals_section, "engine_chams_mode", settings::visuals::engine_chams_mode);
			settings::visuals::engine_chams_queue_id = read_int(visuals_section, "engine_chams_queue_id", settings::visuals::engine_chams_queue_id);
			settings::visuals::engine_chams_weapons_style = read_int(visuals_section, "engine_chams_weapons_style", settings::visuals::engine_chams_weapons_style);
			read_color(visuals_section, "engine_chams_colour", settings::visuals::engine_chams_colour);
			read_color(visuals_section, "engine_chams_weapons_colour", settings::visuals::engine_chams_weapons_colour);
			settings::visuals::engine_chams_black_only = read_bool(visuals_section, "engine_chams_black_only", settings::visuals::engine_chams_black_only);
		}

		if (!hitboxexpander_section.empty())
		{
			settings::hitboxexpander::enabled = read_bool(hitboxexpander_section, "enabled", settings::hitboxexpander::enabled);
			settings::hitboxexpander::size_x = read_float(hitboxexpander_section, "size_x", settings::hitboxexpander::size_x);
			settings::hitboxexpander::size_y = read_float(hitboxexpander_section, "size_y", settings::hitboxexpander::size_y);
			settings::hitboxexpander::size_z = read_float(hitboxexpander_section, "size_z", settings::hitboxexpander::size_z);
			settings::hitboxexpander::visualize = read_bool(hitboxexpander_section, "visualize", settings::hitboxexpander::visualize);
			read_color(hitboxexpander_section, "hitbox_colour", settings::hitboxexpander::hitbox_colour);
			read_color(hitboxexpander_section, "hitbox_outline_colour", settings::hitboxexpander::hitbox_outline_colour);
		}

		if (!hit_tracers_section.empty())
		{
			settings::hit_tracers::enabled = read_bool(hit_tracers_section, "enabled", settings::hit_tracers::enabled);
			settings::hit_tracers::origin_type = read_int(hit_tracers_section, "origin_type", settings::hit_tracers::origin_type);
			settings::hit_tracers::style = read_int(hit_tracers_section, "style", settings::hit_tracers::style);
			settings::hit_tracers::duration = read_float(hit_tracers_section, "duration", settings::hit_tracers::duration);
			settings::hit_tracers::thickness = read_float(hit_tracers_section, "thickness", settings::hit_tracers::thickness);
			read_color(hit_tracers_section, "colour", settings::hit_tracers::colour);
			read_color(hit_tracers_section, "outline_colour", settings::hit_tracers::outline_colour);
			settings::hit_tracers::draw_outline = read_bool(hit_tracers_section, "draw_outline", settings::hit_tracers::draw_outline);
			settings::hit_tracers::damage_text = read_bool(hit_tracers_section, "damage_text", settings::hit_tracers::damage_text);
			read_color(hit_tracers_section, "damage_text_colour", settings::hit_tracers::damage_text_colour);
		}

		if (!hit_chams_section.empty())
		{
			settings::hit_chams::enabled = read_bool(hit_chams_section, "enabled", settings::hit_chams::enabled);
			settings::hit_chams::style = read_int(hit_chams_section, "style", settings::hit_chams::style);
			settings::hit_chams::fade_easing = read_int(hit_chams_section, "fade_easing", settings::hit_chams::fade_easing);
			settings::hit_chams::duration = read_float(hit_chams_section, "duration", settings::hit_chams::duration);
			settings::hit_chams::scale = read_float(hit_chams_section, "scale", settings::hit_chams::scale);
			read_color(hit_chams_section, "fill_colour", settings::hit_chams::fill_colour);
			read_color(hit_chams_section, "outline_colour", settings::hit_chams::outline_colour);
			settings::hit_chams::outline_thickness = read_float(hit_chams_section, "outline_thickness", settings::hit_chams::outline_thickness);
			settings::hit_chams::draw_outline = read_bool(hit_chams_section, "draw_outline", settings::hit_chams::draw_outline);
		}

		if (!hit_skeleton_section.empty())
		{
			settings::hit_skeleton::enabled = read_bool(hit_skeleton_section, "enabled", settings::hit_skeleton::enabled);
			settings::hit_skeleton::style = read_int(hit_skeleton_section, "style", settings::hit_skeleton::style);
			settings::hit_skeleton::fade_easing = read_int(hit_skeleton_section, "fade_easing", settings::hit_skeleton::fade_easing);
			settings::hit_skeleton::duration = read_float(hit_skeleton_section, "duration", settings::hit_skeleton::duration);
			settings::hit_skeleton::thickness = read_float(hit_skeleton_section, "thickness", settings::hit_skeleton::thickness);
			settings::hit_skeleton::joint_radius = read_float(hit_skeleton_section, "joint_radius", settings::hit_skeleton::joint_radius);
			read_color(hit_skeleton_section, "colour", settings::hit_skeleton::colour);
			read_color(hit_skeleton_section, "outline_colour", settings::hit_skeleton::outline_colour);
			read_color(hit_skeleton_section, "joint_colour", settings::hit_skeleton::joint_colour);
			settings::hit_skeleton::draw_joints = read_bool(hit_skeleton_section, "draw_joints", settings::hit_skeleton::draw_joints);
			settings::hit_skeleton::draw_outline = read_bool(hit_skeleton_section, "draw_outline", settings::hit_skeleton::draw_outline);
		}

		if (!theme_section.empty())
		{
			ImGuiStyle& style = ImGui::GetStyle();
			read_color(theme_section, "WindowBg", &style.Colors[ImGuiCol_WindowBg].x);
			read_color(theme_section, "ChildBg", &style.Colors[ImGuiCol_ChildBg].x);
			read_color(theme_section, "Header", &style.Colors[ImGuiCol_Header].x);
			read_color(theme_section, "PopupBg", &style.Colors[ImGuiCol_PopupBg].x);
			read_color(theme_section, "Text", &style.Colors[ImGuiCol_Text].x);
			read_color(theme_section, "TextDisabled", &style.Colors[ImGuiCol_TextDisabled].x);
			read_color(theme_section, "SliderGrab", &style.Colors[ImGuiCol_SliderGrab].x);
			read_color(theme_section, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
			read_color(theme_section, "Border", &style.Colors[ImGuiCol_Border].x);
			read_color(theme_section, "Button", &style.Colors[ImGuiCol_Button].x);
			read_color(theme_section, "ButtonHovered", &style.Colors[ImGuiCol_ButtonHovered].x);
			read_color(theme_section, "ButtonActive", &style.Colors[ImGuiCol_ButtonActive].x);
			read_color(theme_section, "FrameBg", &style.Colors[ImGuiCol_FrameBg].x);
			read_color(theme_section, "FrameBgHovered", &style.Colors[ImGuiCol_FrameBgHovered].x);
			read_color(theme_section, "FrameBgActive", &style.Colors[ImGuiCol_FrameBgActive].x);
			read_color(theme_section, "ScrollbarBg", &style.Colors[ImGuiCol_ScrollbarBg].x);
			read_color(theme_section, "ScrollbarGrab", &style.Colors[ImGuiCol_ScrollbarGrab].x);
			read_color(theme_section, "menu_color", orok_config.menu_color);
			read_color(theme_section, "menu_color_secondary", orok_config.menu_color_secondary);
			read_color(theme_section, "menu_color_text", orok_config.menu_color_text);
			read_color(theme_section, "menu_color_border", orok_config.menu_color_border);
			read_color(theme_section, "menu_color_child", orok_config.menu_color_child);
			read_color(theme_section, "menu_color_button", orok_config.menu_color_button);
			read_color(theme_section, "menu_color_button_hover", orok_config.menu_color_button_hover);
			read_color(theme_section, "menu_color_button_active", orok_config.menu_color_button_active);
			read_color(theme_section, "menu_color_frame", orok_config.menu_color_frame);
			read_color(theme_section, "menu_color_frame_hover", orok_config.menu_color_frame_hover);
			read_color(theme_section, "menu_color_frame_active", orok_config.menu_color_frame_active);
			read_color(theme_section, "menu_color_popup", orok_config.menu_color_popup);
			read_color(theme_section, "menu_color_header", orok_config.menu_color_header);
			read_color(theme_section, "menu_color_header_hover", orok_config.menu_color_header_hover);
			read_color(theme_section, "menu_color_header_active", orok_config.menu_color_header_active);
			read_color(theme_section, "menu_color_checkmark", orok_config.menu_color_checkmark);
			read_color(theme_section, "menu_color_slider_grab", orok_config.menu_color_slider_grab);
			read_color(theme_section, "menu_color_slider_grab_active", orok_config.menu_color_slider_grab_active);
			read_color(theme_section, "menu_color_scrollbar_bg", orok_config.menu_color_scrollbar_bg);
			read_color(theme_section, "menu_color_scrollbar_grab", orok_config.menu_color_scrollbar_grab);
		}

		if (!misc_section.empty())
		{
			settings::streamproof = read_bool(misc_section, "streamproof", settings::streamproof);
			settings::teamcheck = read_bool(misc_section, "teamcheck", settings::teamcheck);
			settings::vsync = read_bool(misc_section, "vsync", settings::vsync);
			settings::performance_mode = read_bool(misc_section, "performance_mode", settings::performance_mode);
			settings::misc::unlock_fps = read_bool(misc_section, "unlock_fps", settings::misc::unlock_fps);
			settings::misc::fps_cap = read_int(misc_section, "fps_cap", settings::misc::fps_cap);
			settings::misc::auto_rescan = read_bool(misc_section, "auto_rescan", settings::misc::auto_rescan);
			settings::misc::keybind_list = read_bool(misc_section, "keybind_list", settings::misc::keybind_list);
			settings::misc::keybind_indicator = read_bool(misc_section, "keybind_indicator", settings::misc::keybind_indicator);
			read_color(misc_section, "keybind_indicator_feature_colour", settings::misc::keybind_indicator_feature_colour);
			read_color(misc_section, "keybind_indicator_mode_colour", settings::misc::keybind_indicator_mode_colour);
			settings::watermark::enabled = read_bool(misc_section, "watermark_enabled", settings::watermark::enabled);
			settings::watermark::draggable = read_bool(misc_section, "watermark_draggable", settings::watermark::draggable);
			settings::watermark::pos_x = read_float(misc_section, "watermark_pos_x", settings::watermark::pos_x);
			settings::watermark::pos_y = read_float(misc_section, "watermark_pos_y", settings::watermark::pos_y);
			settings::watermark::show_prefix = read_bool(misc_section, "watermark_show_prefix", settings::watermark::show_prefix);
			std::string pfx = read_string(misc_section, "watermark_prefix_text", settings::watermark::prefix_text);
			snprintf(settings::watermark::prefix_text, sizeof(settings::watermark::prefix_text), "%s", pfx.c_str());
			settings::watermark::show_right_badge = read_bool(misc_section, "watermark_show_right_badge", settings::watermark::show_right_badge);
			std::string bdg = read_string(misc_section, "watermark_right_badge_text", settings::watermark::right_badge_text);
			snprintf(settings::watermark::right_badge_text, sizeof(settings::watermark::right_badge_text), "%s", bdg.c_str());
			settings::watermark::show_username = read_bool(misc_section, "watermark_show_username", settings::watermark::show_username);
			settings::watermark::show_fps = read_bool(misc_section, "watermark_show_fps", settings::watermark::show_fps);
			settings::watermark::show_game_id = read_bool(misc_section, "watermark_show_game_id", settings::watermark::show_game_id);
			settings::watermark::show_place_id = read_bool(misc_section, "watermark_show_place_id", settings::watermark::show_place_id);
			settings::watermark::show_job_id = read_bool(misc_section, "watermark_show_job_id", settings::watermark::show_job_id);
			settings::watermark::show_cpu_ram = read_bool(misc_section, "watermark_show_cpu_ram", settings::watermark::show_cpu_ram);
			settings::watermark::show_ip_port = read_bool(misc_section, "watermark_show_ip_port", settings::watermark::show_ip_port);
			settings::watermark::show_client_id = read_bool(misc_section, "watermark_show_client_id", settings::watermark::show_client_id);
			settings::watermark::show_time = read_bool(misc_section, "watermark_show_time", settings::watermark::show_time);
			settings::watermark::custom_accent = read_bool(misc_section, "watermark_custom_accent", settings::watermark::custom_accent);
			read_color(misc_section, "watermark_accent_color", settings::watermark::accent_color);
			settings::misc::keybind_list_pos_x = read_float(misc_section, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
			settings::misc::keybind_list_pos_y = read_float(misc_section, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
			settings::misc::menu_background_style = read_int(misc_section, "menu_background_style", settings::misc::menu_background_style);
			settings::misc::watermark_position = read_int(misc_section, "watermark_position", settings::misc::watermark_position);
			settings::misc::explorer_window = read_bool(misc_section, "explorer_window", settings::misc::explorer_window);
			settings::misc::spotify_window = read_bool(misc_section, "spotify_window", settings::misc::spotify_window);
			settings::misc::performance_window = read_bool(misc_section, "performance_window", settings::misc::performance_window);
			settings::performance::target_thread_sleep = read_int(misc_section, "target_thread_sleep", settings::performance::target_thread_sleep);
			settings::performance::aim_thread_sleep = read_int(misc_section, "aim_thread_sleep", settings::performance::aim_thread_sleep);
			settings::performance::cache_refresh_delay = read_int(misc_section, "cache_refresh_delay", settings::performance::cache_refresh_delay);
			settings::performance::main_loop_delay = read_int(misc_section, "main_loop_delay", settings::performance::main_loop_delay);
			settings::performance::thread_throttling = read_bool(misc_section, "thread_throttling", settings::performance::thread_throttling);
			settings::misc::hide_console = read_bool(misc_section, "hide_console", settings::misc::hide_console);
			settings::misc::bot_support = read_bool(misc_section, "bot_support", settings::misc::bot_support);
			settings::misc::only_workspace_bots = read_bool(misc_section, "only_workspace_bots", settings::misc::only_workspace_bots);
			settings::misc::disable_scrollbars = read_bool(misc_section, "disable_scrollbars", settings::misc::disable_scrollbars);
			settings::misc::preview_model = read_int(misc_section, "preview_model", settings::misc::preview_model);
			std::string loaded_cmp = read_string(misc_section, "custom_model_path", settings::misc::custom_model_path);
			strncpy_s(settings::misc::custom_model_path, loaded_cmp.c_str(), sizeof(settings::misc::custom_model_path) - 1);
			settings::misc::sticky_preview = read_bool(misc_section, "sticky_preview", settings::misc::sticky_preview);
			settings::config_system::disable_confirmations = read_bool(misc_section, "disable_confirmations", settings::config_system::disable_confirmations);
			settings::general::confirm_unload = read_bool(misc_section, "confirm_unload", settings::general::confirm_unload);
			settings::misc::theme_index = read_int(misc_section, "theme_index", settings::misc::theme_index);
			settings::misc::custom_colors_enabled = read_bool(misc_section, "custom_colors_enabled", settings::misc::custom_colors_enabled);
			for (int i = 0; i < 14; i++)
				read_color(misc_section, ("custom_color_" + std::to_string(i)).c_str(), settings::misc::custom_colors[i]);

			if (misc_section.find("theme_preset") != std::string::npos)
			{
				settings::theme::preset = read_int(misc_section, "theme_preset", settings::theme::preset);
				Theme::ApplyTheme(settings::theme::preset);
			}
			if (misc_section.find("theme_menu_accent") != std::string::npos)
			{
				read_color(misc_section, "theme_menu_accent", settings::theme::accent);
				clr->accent = ImColor(settings::theme::accent[0], settings::theme::accent[1], settings::theme::accent[2], 1.f);
			}
			if (misc_section.find("theme_background_one") != std::string::npos)
			{
				read_color(misc_section, "theme_background_one", settings::theme::background_one);
				clr->window.background_one = ImColor(settings::theme::background_one[0], settings::theme::background_one[1], settings::theme::background_one[2], 1.f);
			}
			if (misc_section.find("theme_background_two") != std::string::npos)
			{
				read_color(misc_section, "theme_background_two", settings::theme::background_two);
				clr->window.background_two = ImColor(settings::theme::background_two[0], settings::theme::background_two[1], settings::theme::background_two[2], 1.f);
			}
			if (misc_section.find("theme_stroke") != std::string::npos)
			{
				read_color(misc_section, "theme_stroke", settings::theme::stroke);
				clr->window.stroke = ImColor(settings::theme::stroke[0], settings::theme::stroke[1], settings::theme::stroke[2], 1.f);
			}
			if (misc_section.find("theme_stroke_two") != std::string::npos)
			{
				read_color(misc_section, "theme_stroke_two", settings::theme::stroke_two);
				clr->widgets.stroke_two = ImColor(settings::theme::stroke_two[0], settings::theme::stroke_two[1], settings::theme::stroke_two[2], 1.f);
			}
			if (misc_section.find("theme_text") != std::string::npos)
			{
				read_color(misc_section, "theme_text", settings::theme::text);
				clr->widgets.text = ImColor(settings::theme::text[0], settings::theme::text[1], settings::theme::text[2], 1.f);
			}
			if (misc_section.find("theme_text_inactive") != std::string::npos)
			{
				read_color(misc_section, "theme_text_inactive", settings::theme::text_inactive);
				clr->widgets.text_inactive = ImColor(settings::theme::text_inactive[0], settings::theme::text_inactive[1], settings::theme::text_inactive[2], 1.f);
			}
		}

		std::string notifications_section = extract_section(json, "notifications");
		if (!notifications_section.empty())
		{
			settings::notifications::enabled = read_bool(notifications_section, "enabled", settings::notifications::enabled);
			settings::notifications::welcome = read_bool(notifications_section, "welcome", settings::notifications::welcome);

			std::string w_str = read_string(notifications_section, "welcome_msg", settings::notifications::welcome_msg);
			snprintf(settings::notifications::welcome_msg, sizeof(settings::notifications::welcome_msg), "%s", w_str.c_str());

			settings::notifications::thankyou = read_bool(notifications_section, "thankyou", settings::notifications::thankyou);

			std::string t_str = read_string(notifications_section, "thankyou_msg", settings::notifications::thankyou_msg);
			snprintf(settings::notifications::thankyou_msg, sizeof(settings::notifications::thankyou_msg), "%s", t_str.c_str());

			settings::notifications::hit = read_bool(notifications_section, "hit", settings::notifications::hit);

			std::string h_str = read_string(notifications_section, "hit_msg", settings::notifications::hit_msg);
			snprintf(settings::notifications::hit_msg, sizeof(settings::notifications::hit_msg), "%s", h_str.c_str());

			settings::notifications::kill = read_bool(notifications_section, "kill", settings::notifications::kill);

			std::string k_str = read_string(notifications_section, "kill_msg", settings::notifications::kill_msg);
			snprintf(settings::notifications::kill_msg, sizeof(settings::notifications::kill_msg), "%s", k_str.c_str());

			settings::notifications::duration = read_float(notifications_section, "duration", settings::notifications::duration);
			settings::notifications::position = read_int(notifications_section, "position", settings::notifications::position);
		}

		std::string btools_section = extract_section(json, "btools");
		if (!btools_section.empty())
		{
			settings::btools::enabled = read_bool(btools_section, "enabled", settings::btools::enabled);
			settings::btools::tool_type = read_int(btools_section, "tool_type", settings::btools::tool_type);
			settings::btools::keybind = read_int(btools_section, "keybind", settings::btools::keybind);
			settings::btools::activation_mode = read_int(btools_section, "activation_mode", settings::btools::activation_mode);
		}

		if (!lighting_section.empty())
		{
			if (!ambient_section.empty())
			{
				settings::lighting::ambient::enabled = read_bool(ambient_section, "enabled", settings::lighting::ambient::enabled);
				read_color(ambient_section, "color", settings::lighting::ambient::color);
				read_color(ambient_section, "outdoor_color", settings::lighting::ambient::outdoor_color);
				settings::lighting::ambient::pulse = read_bool(ambient_section, "pulse", settings::lighting::ambient::pulse);
				settings::lighting::ambient::pulse_speed = read_float(ambient_section, "pulse_speed", settings::lighting::ambient::pulse_speed);
				settings::lighting::ambient::pulse_intensity = read_float(ambient_section, "pulse_intensity", settings::lighting::ambient::pulse_intensity);
			}
			if (!shadows_section.empty())
			{
				settings::lighting::shadows::enabled = read_bool(shadows_section, "enabled", settings::lighting::shadows::enabled);
				settings::lighting::shadows::value = read_float(shadows_section, "value", settings::lighting::shadows::value);
			}
			if (!fog_section.empty())
			{
				settings::lighting::fog::enabled = read_bool(fog_section, "enabled", settings::lighting::fog::enabled);
				settings::lighting::fog::fog_start = read_float(fog_section, "fog_start", settings::lighting::fog::fog_start);
				settings::lighting::fog::fog_end = read_float(fog_section, "fog_end", settings::lighting::fog::fog_end);
				read_color(fog_section, "fog_color", settings::lighting::fog::fog_color);
				settings::lighting::fog::pulse = read_bool(fog_section, "pulse", settings::lighting::fog::pulse);
				settings::lighting::fog::pulse_mode = read_int(fog_section, "pulse_mode", settings::lighting::fog::pulse_mode);
				settings::lighting::fog::pulse_speed = read_float(fog_section, "pulse_speed", settings::lighting::fog::pulse_speed);
				settings::lighting::fog::pulse_intensity = read_float(fog_section, "pulse_intensity", settings::lighting::fog::pulse_intensity);
			}
			if (!clocktime_section.empty())
			{
				settings::lighting::clocktime::enabled = read_bool(clocktime_section, "enabled", settings::lighting::clocktime::enabled);
				settings::lighting::clocktime::time = read_float(clocktime_section, "time", settings::lighting::clocktime::time);
			}
			if (!atmosphere_section.empty())
			{
				settings::lighting::atmosphere::enabled = read_bool(atmosphere_section, "enabled", settings::lighting::atmosphere::enabled);
				read_color(atmosphere_section, "color", settings::lighting::atmosphere::color);
				read_color(atmosphere_section, "decay", settings::lighting::atmosphere::decay);
				settings::lighting::atmosphere::density = read_float(atmosphere_section, "density", settings::lighting::atmosphere::density);
				settings::lighting::atmosphere::glare = read_float(atmosphere_section, "glare", settings::lighting::atmosphere::glare);
				settings::lighting::atmosphere::haze = read_float(atmosphere_section, "haze", settings::lighting::atmosphere::haze);
				settings::lighting::atmosphere::offset = read_float(atmosphere_section, "offset", settings::lighting::atmosphere::offset);
				settings::lighting::atmosphere::pulse = read_bool(atmosphere_section, "pulse", settings::lighting::atmosphere::pulse);
				settings::lighting::atmosphere::pulse_mode = read_int(atmosphere_section, "pulse_mode", settings::lighting::atmosphere::pulse_mode);
				settings::lighting::atmosphere::pulse_speed = read_float(atmosphere_section, "pulse_speed", settings::lighting::atmosphere::pulse_speed);
				settings::lighting::atmosphere::pulse_intensity = read_float(atmosphere_section, "pulse_intensity", settings::lighting::atmosphere::pulse_intensity);
			}
			if (!colorshift_section.empty())
			{
				settings::lighting::colorshift::enabled = read_bool(colorshift_section, "enabled", settings::lighting::colorshift::enabled);
				read_color(colorshift_section, "bottom", settings::lighting::colorshift::bottom);
				read_color(colorshift_section, "top", settings::lighting::colorshift::top);
			}
			if (!exposure_section.empty())
			{
				settings::lighting::exposure::enabled = read_bool(exposure_section, "enabled", settings::lighting::exposure::enabled);
				settings::lighting::exposure::exposure = read_float(exposure_section, "exposure", settings::lighting::exposure::exposure);
			}
			if (!starcount_section.empty())
			{
				settings::lighting::starcount::enabled = read_bool(starcount_section, "enabled", settings::lighting::starcount::enabled);
				settings::lighting::starcount::star_count = read_int(starcount_section, "star_count", settings::lighting::starcount::star_count);
			}
			if (!sunmoon_section.empty())
			{
				settings::lighting::sunmoon::enabled = read_bool(sunmoon_section, "enabled", settings::lighting::sunmoon::enabled);
				std::string sun_id = read_string(sunmoon_section, "custom_sun_id", settings::lighting::sunmoon::custom_sun_id);
				strcpy_s(settings::lighting::sunmoon::custom_sun_id, sun_id.c_str());
				std::string moon_id = read_string(sunmoon_section, "custom_moon_id", settings::lighting::sunmoon::custom_moon_id);
				strcpy_s(settings::lighting::sunmoon::custom_moon_id, moon_id.c_str());
			}
			if (!brightness_section.empty())
			{
				settings::lighting::brightness::enabled = read_bool(brightness_section, "enabled", settings::lighting::brightness::enabled);
				settings::lighting::brightness::value = read_float(brightness_section, "value", settings::lighting::brightness::value);
			}
			if (!environment_section.empty())
			{
				settings::lighting::environment::enabled = read_bool(environment_section, "enabled", settings::lighting::environment::enabled);
				settings::lighting::environment::diffuse_scale = read_float(environment_section, "diffuse_scale", settings::lighting::environment::diffuse_scale);
				settings::lighting::environment::specular_scale = read_float(environment_section, "specular_scale", settings::lighting::environment::specular_scale);
			}
			if (!latitude_section.empty())
			{
				settings::lighting::latitude::enabled = read_bool(latitude_section, "enabled", settings::lighting::latitude::enabled);
				settings::lighting::latitude::value = read_float(latitude_section, "value", settings::lighting::latitude::value);
			}
			if (!terrain_section.empty())
			{
				settings::lighting::terrain::enabled = read_bool(terrain_section, "enabled", settings::lighting::terrain::enabled);
				settings::lighting::terrain::grass_length = read_float(terrain_section, "grass_length", settings::lighting::terrain::grass_length);
				read_color(terrain_section, "water_color", settings::lighting::terrain::water_color);
				settings::lighting::terrain::water_reflectance = read_float(terrain_section, "water_reflectance", settings::lighting::terrain::water_reflectance);
				settings::lighting::terrain::water_transparency = read_float(terrain_section, "water_transparency", settings::lighting::terrain::water_transparency);
				settings::lighting::terrain::water_wave_size = read_float(terrain_section, "water_wave_size", settings::lighting::terrain::water_wave_size);
				settings::lighting::terrain::water_wave_speed = read_float(terrain_section, "water_wave_speed", settings::lighting::terrain::water_wave_speed);
			}
			if (!bloom_section.empty())
			{
				settings::lighting::bloom::enabled = read_bool(bloom_section, "enabled", settings::lighting::bloom::enabled);
				settings::lighting::bloom::intensity = read_float(bloom_section, "intensity", settings::lighting::bloom::intensity);
				settings::lighting::bloom::size = read_float(bloom_section, "size", settings::lighting::bloom::size);
				settings::lighting::bloom::threshold = read_float(bloom_section, "threshold", settings::lighting::bloom::threshold);
			}
			if (!sunrays_section.empty())
			{
				settings::lighting::sunrays::enabled = read_bool(sunrays_section, "enabled", settings::lighting::sunrays::enabled);
				settings::lighting::sunrays::intensity = read_float(sunrays_section, "intensity", settings::lighting::sunrays::intensity);
				settings::lighting::sunrays::spread = read_float(sunrays_section, "spread", settings::lighting::sunrays::spread);
			}
			if (!color_correction_section.empty())
			{
				settings::lighting::color_correction::enabled = read_bool(color_correction_section, "enabled", settings::lighting::color_correction::enabled);
				settings::lighting::color_correction::brightness = read_float(color_correction_section, "brightness", settings::lighting::color_correction::brightness);
				settings::lighting::color_correction::contrast = read_float(color_correction_section, "contrast", settings::lighting::color_correction::contrast);
				settings::lighting::color_correction::saturation = read_float(color_correction_section, "saturation", settings::lighting::color_correction::saturation);
				read_color(color_correction_section, "tint_color", settings::lighting::color_correction::tint_color);
			}
			if (!depth_of_field_section.empty())
			{
				settings::lighting::depth_of_field::enabled = read_bool(depth_of_field_section, "enabled", settings::lighting::depth_of_field::enabled);
				settings::lighting::depth_of_field::density = read_float(depth_of_field_section, "density", settings::lighting::depth_of_field::density);
				settings::lighting::depth_of_field::focus_distance = read_float(depth_of_field_section, "focus_distance", settings::lighting::depth_of_field::focus_distance);
				settings::lighting::depth_of_field::in_focus_radius = read_float(depth_of_field_section, "in_focus_radius", settings::lighting::depth_of_field::in_focus_radius);
				settings::lighting::depth_of_field::near_intensity = read_float(depth_of_field_section, "near_intensity", settings::lighting::depth_of_field::near_intensity);
			}
		}

		if (!freezeplayer_section.empty())
		{
			settings::freezeplayer::enabled = read_bool(freezeplayer_section, "enabled", settings::freezeplayer::enabled);
			settings::freezeplayer::keybind = read_int(freezeplayer_section, "keybind", settings::freezeplayer::keybind);
			settings::freezeplayer::activation_mode = read_int(freezeplayer_section, "activation_mode", settings::freezeplayer::activation_mode);
		}

		if (!typingcheck_section.empty())
		{
			settings::typingcheck::enabled = read_bool(typingcheck_section, "enabled", settings::typingcheck::enabled);
		}

		if (!config_system_section.empty())
		{
			settings::config_system::disable_confirmations = read_bool(config_system_section, "disable_confirmations", settings::config_system::disable_confirmations);
		}

		

		if (!skinchanger_section.empty())
		{
			settings::skinchanger::enabled = read_bool(skinchanger_section, "enabled", settings::skinchanger::enabled);
			settings::skinchanger::category_index = read_int(skinchanger_section, "category_index", settings::skinchanger::category_index);
			settings::skinchanger::skin_index = read_int(skinchanger_section, "skin_index", settings::skinchanger::skin_index);
			std::string filter = read_string(skinchanger_section, "search_filter", settings::skinchanger::search_filter);
			strcpy_s(settings::skinchanger::search_filter, filter.c_str());

			std::vector<std::string> loaded_favs = read_string_vector(skinchanger_section, "favorites");
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				settings::skinchanger::favorites = loaded_favs;
			}
		}

		if (!dh_skinchanger_section.empty())
		{
			settings::dh_skinchanger::enabled = read_bool(dh_skinchanger_section, "enabled", settings::dh_skinchanger::enabled);
			settings::dh_skinchanger::skin_index = read_int(dh_skinchanger_section, "skin_index", settings::dh_skinchanger::skin_index);
		}

		std::string skyboxchanger_section = extract_section(json, "skyboxchanger");
		if (!skyboxchanger_section.empty())
		{
			settings::skyboxchanger::enabled = read_bool(skyboxchanger_section, "enabled", settings::skyboxchanger::enabled);
			settings::skyboxchanger::preset = read_int(skyboxchanger_section, "preset", settings::skyboxchanger::preset);
		}

		std::string win_section = extract_section(json, "windows");
		if (!win_section.empty())
		{
			float mx = read_float(win_section, "menu_pos_x", -1.f);
			float my = read_float(win_section, "menu_pos_y", -1.f);
			float mw = read_float(win_section, "menu_size_w", -1.f);
			float mh = read_float(win_section, "menu_size_h", -1.f);
			if (mx >= 0.f && my >= 0.f) g_saved_main_menu_pos = ImVec2(mx, my);
			if (mw > 0.f && mh > 0.f) {
				g_saved_main_menu_size = ImVec2(mw, mh);
				g_main_menu_size = ImVec2(mw, mh);
			}

			float sx = read_float(win_section, "settings_pos_x", -1.f);
			float sy = read_float(win_section, "settings_pos_y", -1.f);
			float sw = read_float(win_section, "settings_size_w", -1.f);
			float sh = read_float(win_section, "settings_size_h", -1.f);
			if (sx >= 0.f && sy >= 0.f) g_saved_settings_pos = ImVec2(sx, sy);
			if (sw > 0.f && sh > 0.f) g_saved_settings_size = ImVec2(sw, sh);

			float lx = read_float(win_section, "luau_pos_x", -1.f);
			float ly = read_float(win_section, "luau_pos_y", -1.f);
			float lw = read_float(win_section, "luau_size_w", -1.f);
			float lh = read_float(win_section, "luau_size_h", -1.f);
			if (lx >= 0.f && ly >= 0.f) g_saved_luau_pos = ImVec2(lx, ly);
			if (lw > 0.f && lh > 0.f) g_saved_luau_size = ImVec2(lw, lh);

			float px = read_float(win_section, "preview_pos_x", -1.f);
			float py = read_float(win_section, "preview_pos_y", -1.f);
			float pw = read_float(win_section, "preview_size_w", -1.f);
			float ph = read_float(win_section, "preview_size_h", -1.f);
			if (px >= 0.f && py >= 0.f) g_saved_preview_pos = ImVec2(px, py);
			if (pw > 0.f && ph > 0.f) g_saved_preview_size = ImVec2(pw, ph);

			float pfx = read_float(win_section, "perf_pos_x", -1.f);
			float pfy = read_float(win_section, "perf_pos_y", -1.f);
			float pfw = read_float(win_section, "perf_size_w", -1.f);
			float pfh = read_float(win_section, "perf_size_h", -1.f);
			if (pfx >= 0.f && pfy >= 0.f) g_saved_perf_pos = ImVec2(pfx, pfy);
			if (pfw > 0.f && pfh > 0.f) g_saved_perf_size = ImVec2(pfw, pfh);

			float spx = read_float(win_section, "spotify_pos_x", -1.f);
			float spy = read_float(win_section, "spotify_pos_y", -1.f);
			float spw = read_float(win_section, "spotify_size_w", -1.f);
			float sph = read_float(win_section, "spotify_size_h", -1.f);
			if (spx >= 0.f && spy >= 0.f) g_saved_spotify_pos = ImVec2(spx, spy);
			if (spw > 0.f && sph > 0.f) g_saved_spotify_size = ImVec2(spw, sph);

			float exx = read_float(win_section, "explorer_pos_x", -1.f);
			float exy = read_float(win_section, "explorer_pos_y", -1.f);
			float exw = read_float(win_section, "explorer_size_w", -1.f);
			float exh = read_float(win_section, "explorer_size_h", -1.f);
			if (exx >= 0.f && exy >= 0.f) g_saved_explorer_pos = ImVec2(exx, exy);
			if (exw > 0.f && exh > 0.f) g_saved_explorer_size = ImVec2(exw, exh);
		}


		return true;
	}

	bool delete_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;

		try
		{
			return std::filesystem::remove(path);
		}
		catch (...)
		{
			return false;
		}
	}

	bool config_exists(const std::string& name)
	{
		std::string path = get_config_path(name);
		return !path.empty() && std::filesystem::exists(path);
	}

	static std::string get_autoload_path()
	{
		std::string dir = get_config_directory();
		if (dir.empty()) return "";
		return dir + "\\autoload.txt";
	}

	bool set_autoload(const std::string& name)
	{
		if (!ensure_config_directory()) return false;
		std::string path = get_autoload_path();
		if (path.empty()) return false;
		std::ofstream f(path, std::ios::trunc);
		if (!f.is_open()) return false;
		f << name;
		return true;
	}

	std::string get_autoload()
	{
		// Read once per second max — the settings tab queries this every frame.
		static std::mutex cache_mu;
		static std::string cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::string result;
		std::string path = get_autoload_path();
		if (!path.empty() && std::filesystem::exists(path))
		{
			std::ifstream f(path);
			if (f.is_open())
				std::getline(f, result);
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = result;
		has_scanned = true;
		return cached;
	}

	void clear_autoload()
	{
		std::string path = get_autoload_path();
		if (!path.empty() && std::filesystem::exists(path))
			std::filesystem::remove(path);
	}

	bool load_autoload()
	{
		std::string name = get_autoload();
		if (name.empty()) return false;
		if (!config_exists(name)) return false;
		return load_config(name);
	}

	bool save_theme()
	{
		if (!ensure_config_directory()) return false;
		std::string path = get_config_directory() + "\\theme.json";
		if (path.empty()) return false;

		ImGuiStyle& style = ImGui::GetStyle();
		std::ostringstream ss;
		ss << "{\n";
		write_color(ss, "WindowBg",          &style.Colors[ImGuiCol_WindowBg].x);
		write_color(ss, "ChildBg",           &style.Colors[ImGuiCol_ChildBg].x);
		write_color(ss, "Header",            &style.Colors[ImGuiCol_Header].x);
		write_color(ss, "PopupBg",           &style.Colors[ImGuiCol_PopupBg].x);
		write_color(ss, "Text",              &style.Colors[ImGuiCol_Text].x);
		write_color(ss, "TextDisabled",      &style.Colors[ImGuiCol_TextDisabled].x);
		write_color(ss, "SliderGrab",        &style.Colors[ImGuiCol_SliderGrab].x);
		write_color(ss, "SliderGrabActive",  &style.Colors[ImGuiCol_SliderGrabActive].x);
		write_color(ss, "Border",            &style.Colors[ImGuiCol_Border].x);
		write_color(ss, "Button",            &style.Colors[ImGuiCol_Button].x);
		write_color(ss, "ButtonHovered",     &style.Colors[ImGuiCol_ButtonHovered].x);
		write_color(ss, "ButtonActive",      &style.Colors[ImGuiCol_ButtonActive].x);
		write_color(ss, "FrameBg",           &style.Colors[ImGuiCol_FrameBg].x);
		write_color(ss, "FrameBgHovered",    &style.Colors[ImGuiCol_FrameBgHovered].x);
		write_color(ss, "FrameBgActive",     &style.Colors[ImGuiCol_FrameBgActive].x);
		write_color(ss, "ScrollbarBg",       &style.Colors[ImGuiCol_ScrollbarBg].x);
		write_color(ss, "ScrollbarGrab",     &style.Colors[ImGuiCol_ScrollbarGrab].x);
		ss << "}\n";

		std::ofstream file(path);
		if (!file.is_open()) return false;
		file << ss.str();
		file.close();
		return true;
	}

	bool load_theme()
	{
		std::string path = get_config_directory() + "\\theme.json";
		if (path.empty() || !std::filesystem::exists(path)) return false;

		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		ImGuiStyle& style = ImGui::GetStyle();
		read_color(json, "WindowBg",         &style.Colors[ImGuiCol_WindowBg].x);
		read_color(json, "ChildBg",          &style.Colors[ImGuiCol_ChildBg].x);
		read_color(json, "Header",           &style.Colors[ImGuiCol_Header].x);
		read_color(json, "PopupBg",          &style.Colors[ImGuiCol_PopupBg].x);
		read_color(json, "Text",             &style.Colors[ImGuiCol_Text].x);
		read_color(json, "TextDisabled",     &style.Colors[ImGuiCol_TextDisabled].x);
		read_color(json, "SliderGrab",       &style.Colors[ImGuiCol_SliderGrab].x);
		read_color(json, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
		read_color(json, "Border",           &style.Colors[ImGuiCol_Border].x);
		read_color(json, "Button",           &style.Colors[ImGuiCol_Button].x);
		read_color(json, "ButtonHovered",    &style.Colors[ImGuiCol_ButtonHovered].x);
		read_color(json, "ButtonActive",     &style.Colors[ImGuiCol_ButtonActive].x);
		read_color(json, "FrameBg",          &style.Colors[ImGuiCol_FrameBg].x);
		read_color(json, "FrameBgHovered",   &style.Colors[ImGuiCol_FrameBgHovered].x);
		read_color(json, "FrameBgActive",    &style.Colors[ImGuiCol_FrameBgActive].x);
		read_color(json, "ScrollbarBg",      &style.Colors[ImGuiCol_ScrollbarBg].x);
		read_color(json, "ScrollbarGrab",    &style.Colors[ImGuiCol_ScrollbarGrab].x);

		style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_ScrollbarGrab];
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = style.Colors[ImGuiCol_ScrollbarGrab];
		style.Colors[ImGuiCol_Separator]             = style.Colors[ImGuiCol_Border];
		style.Colors[ImGuiCol_CheckMark]             = style.Colors[ImGuiCol_ChildBg];
		style.Colors[ImGuiCol_FrameBgHovered]        = style.Colors[ImGuiCol_ButtonHovered];
		style.Colors[ImGuiCol_FrameBgActive]         = style.Colors[ImGuiCol_ButtonActive];
		return true;
	}

	std::string get_theme_directory()
	{
		return "C:\\Ivory\\Themes";
	}

	bool ensure_theme_directory()
	{
		std::string dir = get_theme_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<std::string> get_theme_list()
	{
		// Cached with a short TTL — the theme tab iterates this every frame.
		static std::mutex cache_mu;
		static std::vector<std::string> cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::vector<std::string> themes;
		std::string dir = get_theme_directory();
		if (!dir.empty() && std::filesystem::exists(dir))
		{
			try
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file() && entry.path().extension() == ".json")
					{
						std::string stem = entry.path().stem().string();
						if (stem != "autoload_theme")
						{
							themes.push_back(stem);
						}
					}
				}
			}
			catch (...)
			{
			}
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = themes;
		has_scanned = true;
		return cached;
	}

	bool save_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_theme_directory();
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		std::ostringstream ss;
		ss << "{\n";
		write_color(ss, "accent", settings::theme::accent);
		write_color(ss, "background_one", settings::theme::background_one);
		write_color(ss, "background_two", settings::theme::background_two);
		write_color(ss, "stroke", settings::theme::stroke);
		write_color(ss, "stroke_two", settings::theme::stroke_two);
		write_color(ss, "text", settings::theme::text);
		write_color(ss, "text_inactive", settings::theme::text_inactive);
		write_int(ss, "preset", settings::theme::preset);

		write_color(ss, "menu_color", orok_config.menu_color);
		write_color(ss, "menu_color_secondary", orok_config.menu_color_secondary);
		write_color(ss, "menu_color_text", orok_config.menu_color_text);
		write_color(ss, "menu_color_border", orok_config.menu_color_border);
		write_float(ss, "menu_window_rounding", orok_config.menu_window_rounding);
		write_float(ss, "menu_frame_rounding", orok_config.menu_frame_rounding);
		write_float(ss, "menu_scrollbar_size", orok_config.menu_scrollbar_size);
		write_float(ss, "menu_size_width", orok_config.menu_size[0]);
		write_float(ss, "menu_size_height", orok_config.menu_size[1]);
		write_bool(ss, "watermark", settings::misc::watermark);
		write_bool(ss, "keybind_list", settings::misc::keybind_list);
		write_float(ss, "watermark_pos_x", settings::misc::watermark_pos_x);
		write_float(ss, "watermark_pos_y", settings::misc::watermark_pos_y);
		write_float(ss, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		write_float(ss, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		write_int(ss, "theme_index", settings::misc::theme_index);
		write_bool(ss, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
		{
			std::string key = "custom_color_" + std::to_string(i);
			write_color(ss, key.c_str(), settings::misc::custom_colors[i]);
		}
		ss << "}\n";

		std::ofstream file(path);
		if (!file.is_open()) return false;
		file << ss.str();
		file.close();
		return true;
	}

	bool load_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_theme_directory();
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		if (!std::filesystem::exists(path)) return false;

		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		read_color(json, "accent", settings::theme::accent);
		read_color(json, "background_one", settings::theme::background_one);
		read_color(json, "background_two", settings::theme::background_two);
		read_color(json, "stroke", settings::theme::stroke);
		read_color(json, "stroke_two", settings::theme::stroke_two);
		read_color(json, "text", settings::theme::text);
		read_color(json, "text_inactive", settings::theme::text_inactive);
		settings::theme::preset = read_int(json, "preset", settings::theme::preset);

		clr->accent = ImColor(settings::theme::accent[0], settings::theme::accent[1], settings::theme::accent[2], 1.f);
		clr->window.background_one = ImColor(settings::theme::background_one[0], settings::theme::background_one[1], settings::theme::background_one[2], 1.f);
		clr->window.background_two = ImColor(settings::theme::background_two[0], settings::theme::background_two[1], settings::theme::background_two[2], 1.f);
		clr->window.stroke = ImColor(settings::theme::stroke[0], settings::theme::stroke[1], settings::theme::stroke[2], 1.f);
		clr->widgets.stroke_two = ImColor(settings::theme::stroke_two[0], settings::theme::stroke_two[1], settings::theme::stroke_two[2], 1.f);
		clr->widgets.text = ImColor(settings::theme::text[0], settings::theme::text[1], settings::theme::text[2], 1.f);
		clr->widgets.text_inactive = ImColor(settings::theme::text_inactive[0], settings::theme::text_inactive[1], settings::theme::text_inactive[2], 1.f);

		read_color(json, "menu_color", orok_config.menu_color);
		read_color(json, "menu_color_secondary", orok_config.menu_color_secondary);
		read_color(json, "menu_color_text", orok_config.menu_color_text);
		read_color(json, "menu_color_border", orok_config.menu_color_border);
		orok_config.menu_window_rounding = read_float(json, "menu_window_rounding", 0.0f);
		orok_config.menu_frame_rounding = read_float(json, "menu_frame_rounding", 0.0f);
		orok_config.menu_scrollbar_size = read_float(json, "menu_scrollbar_size", 6.0f);
		orok_config.menu_size[0] = read_float(json, "menu_size_width", 516.0f);
		orok_config.menu_size[1] = read_float(json, "menu_size_height", 563.0f);
		settings::misc::watermark = read_bool(json, "watermark", settings::misc::watermark);
		settings::misc::keybind_list = read_bool(json, "keybind_list", settings::misc::keybind_list);
		settings::misc::watermark_pos_x = read_float(json, "watermark_pos_x", settings::misc::watermark_pos_x);
		settings::misc::watermark_pos_y = read_float(json, "watermark_pos_y", settings::misc::watermark_pos_y);
		settings::misc::keybind_list_pos_x = read_float(json, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		settings::misc::keybind_list_pos_y = read_float(json, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		settings::misc::theme_index = read_int(json, "theme_index", settings::misc::theme_index);
		settings::misc::custom_colors_enabled = read_bool(json, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
		{
			std::string key = "custom_color_" + std::to_string(i);
			read_color(json, key.c_str(), settings::misc::custom_colors[i]);
		}

		return true;
	}

	bool delete_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		if (std::filesystem::exists(path))
		{
			return std::filesystem::remove(path);
		}
		return false;
	}

	bool set_theme_autoload(const std::string& name)
	{
		ensure_theme_directory();
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		std::ofstream file(path);
		if (!file.is_open()) return false;
		file << name;
		file.close();
		return true;
	}

	std::string get_theme_autoload()
	{
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		if (!std::filesystem::exists(path)) return "";
		std::ifstream file(path);
		if (!file.is_open()) return "";
		std::string name;
		file >> name;
		file.close();
		return name;
	}

	void clear_theme_autoload()
	{
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		if (std::filesystem::exists(path))
		{
			std::filesystem::remove(path);
		}
	}

	bool auto_load_theme()
	{
		ensure_theme_directory();
		std::string name = get_theme_autoload();
		if (!name.empty())
		{
			return load_theme_custom(name);
		}
		if (std::filesystem::exists(get_theme_directory() + "\\default.json"))
		{
			return load_theme_custom("default");
		}
		return false;
	}

	static const std::string b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	static std::string base64_encode(const std::string& in)
	{
		std::string out;
		int val = 0, valb = -6;
		for (unsigned char c : in)
		{
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0)
			{
				out.push_back(b64_table[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		if (valb > -6) out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
		while (out.size() % 4) out.push_back('=');
		return out;
	}

	static std::string base64_decode(const std::string& in)
	{
		std::string out;
		std::vector<int> T(256, -1);
		for (int i = 0; i < 64; i++) T[b64_table[i]] = i;
		int val = 0, valb = -8;
		for (unsigned char c : in)
		{
			if (T[c] == -1) break;
			val = (val << 6) + T[c];
			valb += 6;
			if (valb >= 0)
			{
				out.push_back(char((val >> valb) & 0xFF));
				valb -= 8;
			}
		}
		return out;
	}

	static bool copy_to_clipboard(const std::string& text)
	{
		if (!OpenClipboard(nullptr)) return false;
		EmptyClipboard();
		HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
		if (!hGlob) { CloseClipboard(); return false; }
		char* pBuf = static_cast<char*>(GlobalLock(hGlob));
		if (pBuf)
		{
			memcpy(pBuf, text.c_str(), text.size() + 1);
			GlobalUnlock(hGlob);
			SetClipboardData(CF_TEXT, hGlob);
		}
		CloseClipboard();
		return true;
	}

	static std::string read_from_clipboard()
	{
		if (!OpenClipboard(nullptr)) return "";
		HANDLE hData = GetClipboardData(CF_TEXT);
		if (!hData) { CloseClipboard(); return ""; }
		char* pBuf = static_cast<char*>(GlobalLock(hData));
		std::string text = pBuf ? pBuf : "";
		GlobalUnlock(hData);
		CloseClipboard();
		return text;
	}

	bool export_config_to_clipboard()
	{
		save_config("temp_export");
		std::string path = get_config_path("temp_export");
		if (!std::filesystem::exists(path)) return false;
		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		delete_config("temp_export");

		std::string encoded = base64_encode(json);
		return copy_to_clipboard(encoded);
	}

	bool import_config_from_clipboard()
	{
		std::string clipboard = read_from_clipboard();
		if (clipboard.empty()) return false;

		std::string encoded = clipboard;
		size_t pos = clipboard.find("Ivory_CFG_CODE:");
		if (pos != std::string::npos)
		{
			encoded = clipboard.substr(pos + 20);
		}

		while (!encoded.empty() && (encoded.back() == '\r' || encoded.back() == '\n' || encoded.back() == ' ' || encoded.back() == '\t'))
			encoded.pop_back();

		std::string json = base64_decode(encoded);
		if (json.empty() || json.find("{") == std::string::npos) return false;

		auto now = std::chrono::system_clock::now().time_since_epoch().count() % 10000;
		std::string name = "Imported_" + std::to_string(now);

		ensure_config_directory();
		std::string path = get_config_path(name);
		std::ofstream file(path);
		if (!file.is_open()) return false;
		file << json;
		file.close();

		return load_config(name);
	}

	bool export_theme_to_clipboard()
	{
		save_theme_custom("temp_theme_export");
		std::string path = get_theme_directory() + "\\temp_theme_export.json";
		if (!std::filesystem::exists(path)) return false;
		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		delete_theme_custom("temp_theme_export");

		std::string encoded = base64_encode(json);
		return copy_to_clipboard(encoded);
	}

	bool import_theme_from_clipboard()
	{
		std::string clipboard = read_from_clipboard();
		if (clipboard.empty()) return false;

		if (clipboard.find("Menu Accent:") != std::string::npos || clipboard.find("Contrast One:") != std::string::npos)
		{
			auto parse_hex = [](const std::string& text, const std::string& key, float* out_rgb) {
				size_t p = text.find(key);
				if (p == std::string::npos) return;
				size_t hash_pos = text.find('#', p);
				if (hash_pos != std::string::npos && hash_pos + 6 < text.length())
				{
					std::string hex = text.substr(hash_pos + 1, 6);
					int r = 0, g = 0, b = 0;
					if (sscanf_s(hex.c_str(), "%02x%02x%02x", &r, &g, &b) == 3 || sscanf_s(hex.c_str(), "%02X%02X%02X", &r, &g, &b) == 3)
					{
						out_rgb[0] = r / 255.0f;
						out_rgb[1] = g / 255.0f;
						out_rgb[2] = b / 255.0f;
					}
				}
			};

			parse_hex(clipboard, "Menu Accent:", settings::theme::accent);
			parse_hex(clipboard, "Contrast One:", settings::theme::background_one);
			parse_hex(clipboard, "Contrast Two:", settings::theme::background_two);
			parse_hex(clipboard, "Inline:", settings::theme::stroke);
			parse_hex(clipboard, "Outline:", settings::theme::stroke_two);
			parse_hex(clipboard, "Text Active:", settings::theme::text);
			parse_hex(clipboard, "Text Inactive:", settings::theme::text_inactive);

			clr->accent = ImColor(settings::theme::accent[0], settings::theme::accent[1], settings::theme::accent[2], 1.f);
			clr->window.background_one = ImColor(settings::theme::background_one[0], settings::theme::background_one[1], settings::theme::background_one[2], 1.f);
			clr->window.background_two = ImColor(settings::theme::background_two[0], settings::theme::background_two[1], settings::theme::background_two[2], 1.f);
			clr->window.stroke = ImColor(settings::theme::stroke[0], settings::theme::stroke[1], settings::theme::stroke[2], 1.f);
			clr->widgets.stroke_two = ImColor(settings::theme::stroke_two[0], settings::theme::stroke_two[1], settings::theme::stroke_two[2], 1.f);
			clr->widgets.text = ImColor(settings::theme::text[0], settings::theme::text[1], settings::theme::text[2], 1.f);
			clr->widgets.text_inactive = ImColor(settings::theme::text_inactive[0], settings::theme::text_inactive[1], settings::theme::text_inactive[2], 1.f);

			return true;
		}

		std::string encoded = clipboard;
		size_t pos = clipboard.find("Ivory_THEME_CODE:");
		if (pos != std::string::npos)
		{
			encoded = clipboard.substr(pos + 22);
		}

		while (!encoded.empty() && (encoded.back() == '\r' || encoded.back() == '\n' || encoded.back() == ' ' || encoded.back() == '\t'))
			encoded.pop_back();

		std::string json = base64_decode(encoded);
		if (json.empty() || json.find("{") == std::string::npos) return false;

		auto now = std::chrono::system_clock::now().time_since_epoch().count() % 10000;
		std::string name = "ImportedTheme_" + std::to_string(now);

		ensure_theme_directory();
		std::string path = get_theme_directory() + "\\" + name + ".json";
		std::ofstream file(path);
		if (!file.is_open()) return false;
		file << json;
		file.close();

		return load_theme_custom(name);
	}

	std::string get_scripts_directory()
	{
		return "C:\\Ivory\\Scripts";
	}

	bool ensure_scripts_directory()
	{
		std::string dir = get_scripts_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<std::string> get_script_list()
	{
		ensure_scripts_directory();
		std::vector<std::string> list;
		std::string dir = get_scripts_directory();
		try
		{
			if (std::filesystem::exists(dir))
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file())
					{
						auto ext = entry.path().extension().string();
						if (ext == ".lua" || ext == ".luau" || ext == ".txt")
						{
							list.push_back(entry.path().stem().string());
						}
					}
				}
			}
		}
		catch (...) {}
		std::sort(list.begin(), list.end());
		return list;
	}

	bool save_script(const std::string& name, const std::string& content)
	{
		if (name.empty()) return false;
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		try
		{
			std::ofstream f(path);
			if (!f.is_open()) return false;
			f << content;
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	std::string load_script(const std::string& name)
	{
		if (name.empty()) return "";
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		if (!std::filesystem::exists(path))
		{
			path = get_scripts_directory() + "\\" + name + ".luau";
			if (!std::filesystem::exists(path))
			{
				path = get_scripts_directory() + "\\" + name + ".txt";
				if (!std::filesystem::exists(path)) return "";
			}
		}
		try
		{
			std::ifstream f(path);
			if (!f.is_open()) return "";
			std::stringstream buffer;
			buffer << f.rdbuf();
			return buffer.str();
		}
		catch (...) { return ""; }
	}

	bool delete_script(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		try
		{
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			path = get_scripts_directory() + "\\" + name + ".luau";
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			path = get_scripts_directory() + "\\" + name + ".txt";
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			return false;
		}
		catch (...) { return false; }
	}

	std::string get_debug_directory()
	{
		return "C:\\Ivory\\Debug";
	}

	bool ensure_debug_directories()
	{
		try
		{
			std::string base = get_debug_directory();
			std::filesystem::create_directories(base);
			std::filesystem::create_directories(base + "\\Players");
			std::filesystem::create_directories(base + "\\Explorer");
			std::filesystem::create_directories(base + "\\Memory");
			std::filesystem::create_directories(base + "\\Bones");
			return true;
		}
		catch (...) { return false; }
	}

	static std::string get_timestamp_str()
	{
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		std::tm tm;
		localtime_s(&tm, &time_t);
		char buf[64];
		std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
		return std::string(buf);
	}

	bool dump_debug_players(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Players\\players_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - PLAYERS\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			std::lock_guard<std::mutex> lock(cache::mtx);
			f << "Cached Players Count: " << cache::players.size() << "\n\n";

			for (size_t i = 0; i < cache::players.size(); i++)
			{
				const auto& p = cache::players[i];
				f << "[" << i << "] Player: " << p.name << " (Display: " << p.display_name << ")\n";
				f << "  Address: 0x" << std::hex << p.instance.address << std::dec << "\n";
				f << "  User ID: " << p.user_id << "\n";
				f << "  Health: " << p.health << " / " << p.max_health << "\n";
				f << "  Team: " << p.team << "\n";
				f << "  Priority: " << (int)p.priority << "\n";
				f << "  Position: (" << p.position.x << ", " << p.position.y << ", " << p.position.z << ")\n";
				f << "  HRP Prim: 0x" << std::hex << p.hrp_prim_addr << std::dec << "\n";
				f << "  Head Prim: 0x" << std::hex << p.head_prim_addr << std::dec << "\n";
				f << "  Torso Prim: 0x" << std::hex << p.torso_prim_addr << std::dec << "\n";
				f << "  Left Arm Prim: 0x" << std::hex << p.left_arm_prim_addr << std::dec << "\n";
				f << "  Right Arm Prim: 0x" << std::hex << p.right_arm_prim_addr << std::dec << "\n";
				f << "  Left Leg Prim: 0x" << std::hex << p.left_leg_prim_addr << std::dec << "\n";
				f << "  Right Leg Prim: 0x" << std::hex << p.right_leg_prim_addr << std::dec << "\n";
				f << "  Tool: " << p.tool_name << "\n";
				f << "  Parts map count: " << p.parts.size() << "\n";
				for (const auto& [part_name, part_inst] : p.parts)
				{
					f << "    - " << part_name << " (Addr: 0x" << std::hex << part_inst.address << std::dec << ")\n";
				}
				f << "-------------------------------------------------------\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_explorer(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Explorer\\datamodel_tree_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - DATAMODEL / EXPLORER HIERARCHY\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			if (game::datamodel && game::datamodel->address)
			{
				f << "DataModel Address: 0x" << std::hex << game::datamodel->address << std::dec << "\n";
				f << "Game Place ID: " << game::datamodel->get_place_id() << "\n";
				f << "Game Job ID: " << game::datamodel->get_job_id() << "\n\n";

				std::vector<rbx::c_instance> services = game::datamodel->get_children<rbx::c_instance>();
				f << "Top-Level Services (" << services.size() << "):\n";
				for (auto& s : services)
				{
					if (!s.address) continue;
					f << "+ [" << s.get_class_name() << "] " << s.get_name() << " (0x" << std::hex << s.address << std::dec << ")\n";
					std::vector<rbx::c_instance> children = s.get_children<rbx::c_instance>();
					for (auto& c : children)
					{
						if (!c.address) continue;
						f << "  |- [" << c.get_class_name() << "] " << c.get_name() << " (0x" << std::hex << c.address << std::dec << ")\n";
						std::vector<rbx::c_instance> sub_children = c.get_children<rbx::c_instance>();
						for (auto& sc : sub_children)
						{
							if (!sc.address) continue;
							f << "     |-- [" << sc.get_class_name() << "] " << sc.get_name() << " (0x" << std::hex << sc.address << std::dec << ")\n";
						}
					}
				}
			}
			else
			{
				f << "DataModel is inactive / nullptr.\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_bones(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Bones\\local_player_bones_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - LOCAL PLAYER & BONES\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			std::lock_guard<std::mutex> lock(cache::local_player_mtx);
			f << "Local Player Name: " << cache::local_player.name << " (" << cache::local_player.display_name << ")\n";
			f << "Address: 0x" << std::hex << cache::local_player.instance.address << std::dec << "\n";
			f << "Position: (" << cache::local_player.position.x << ", " << cache::local_player.position.y << ", " << cache::local_player.position.z << ")\n";
			f << "Head: (" << cache::local_player.part_positions.head.x << ", " << cache::local_player.part_positions.head.y << ", " << cache::local_player.part_positions.head.z << ")\n";
			f << "Torso: (" << cache::local_player.part_positions.torso.x << ", " << cache::local_player.part_positions.torso.y << ", " << cache::local_player.part_positions.torso.z << ")\n";
			f << "Left Arm: (" << cache::local_player.part_positions.left_arm.x << ", " << cache::local_player.part_positions.left_arm.y << ", " << cache::local_player.part_positions.left_arm.z << ")\n";
			f << "Right Arm: (" << cache::local_player.part_positions.right_arm.x << ", " << cache::local_player.part_positions.right_arm.y << ", " << cache::local_player.part_positions.right_arm.z << ")\n";
			f << "Left Leg: (" << cache::local_player.part_positions.left_leg.x << ", " << cache::local_player.part_positions.left_leg.y << ", " << cache::local_player.part_positions.left_leg.z << ")\n";
			f << "Right Leg: (" << cache::local_player.part_positions.right_leg.x << ", " << cache::local_player.part_positions.right_leg.y << ", " << cache::local_player.part_positions.right_leg.z << ")\n";
			f << "Health: " << cache::local_player.health << " / " << cache::local_player.max_health << "\n";

			if (game::visualengine && game::visualengine->address)
			{
				f << "\nVisualEngine View Dimensions: " << game::visualengine->get_dimensions().x << " x " << game::visualengine->get_dimensions().y << "\n";
				f << "VisualEngine Address: 0x" << std::hex << game::visualengine->address << std::dec << "\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_memory(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Memory\\game_memory_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - GAME & MEMORY STATUS\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			if (game::datamodel && game::datamodel->address)
			{
				f << "DataModel: 0x" << std::hex << game::datamodel->address << std::dec << "\n";
				f << "Place ID: " << game::datamodel->get_place_id() << "\n";
				f << "Job ID: " << game::datamodel->get_job_id() << "\n";
			}
			if (game::visualengine && game::visualengine->address)
			{
				f << "VisualEngine: 0x" << std::hex << game::visualengine->address << std::dec << "\n";
				f << "View Dimensions: " << game::visualengine->get_dimensions().x << " x " << game::visualengine->get_dimensions().y << "\n";
			}

			MEMORYSTATUSEX mem_info;
			mem_info.dwLength = sizeof(MEMORYSTATUSEX);
			if (GlobalMemoryStatusEx(&mem_info))
			{
				f << "\nSystem Memory Usage: " << mem_info.dwMemoryLoad << "%\n";
				f << "Total Physical RAM: " << (mem_info.ullTotalPhys / (1024 * 1024)) << " MB\n";
				f << "Available RAM: " << (mem_info.ullAvailPhys / (1024 * 1024)) << " MB\n";
			}

			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool clear_debug_files()
	{
		try
		{
			std::string base = get_debug_directory();
			if (std::filesystem::exists(base))
			{
				std::filesystem::remove_all(base);
				ensure_debug_directories();
				return true;
			}
			return false;
		}
		catch (...) { return false; }
	}
}

