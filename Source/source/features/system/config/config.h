#pragma once
#include <string>
#include <vector>

namespace config
{
	struct config_info_t
	{
		std::string name;
		std::string path;
	};

	std::string get_base_directory();
	std::string get_config_directory();
	std::string get_config_path(const std::string& name);
	bool ensure_config_directory();

	std::string get_dumps_directory();
	bool ensure_dumps_directory();

	std::string get_resources_directory();
	bool ensure_resources_directory();

	std::vector<config_info_t> get_config_list();
	bool save_config(const std::string& name);
	bool load_config(const std::string& name);
	bool delete_config(const std::string& name);
	bool config_exists(const std::string& name);

	bool set_autoload(const std::string& name);
	std::string get_autoload();
	void clear_autoload();
	bool load_autoload();

	bool save_theme();
	bool load_theme();

	std::string get_theme_directory();
	bool ensure_theme_directory();
	std::vector<std::string> get_theme_list();
	bool save_theme_custom(const std::string& name);
	bool load_theme_custom(const std::string& name);
	bool delete_theme_custom(const std::string& name);
	bool set_theme_autoload(const std::string& name);
	std::string get_theme_autoload();
	void clear_theme_autoload();
	bool auto_load_theme();
	bool save_theme_default();
	bool load_theme_default();

	bool export_config_to_clipboard();
	bool import_config_from_clipboard();
	bool export_theme_to_clipboard();
	bool import_theme_from_clipboard();

	std::string get_scripts_directory();
	bool ensure_scripts_directory();
	std::vector<std::string> get_script_list();
	bool save_script(const std::string& name, const std::string& content);
	std::string load_script(const std::string& name);
	bool delete_script(const std::string& name);

	std::string get_debug_directory();
	bool ensure_debug_directories();
	bool dump_debug_players(std::string& out_path);
	bool dump_debug_explorer(std::string& out_path);
	bool dump_debug_bones(std::string& out_path);
	bool dump_debug_memory(std::string& out_path);
	bool clear_debug_files();
}
