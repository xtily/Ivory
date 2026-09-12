#pragma once
#include <string>
#include <sdk/cache/core/cache.h>

namespace playerlist
{
	void render();
	void set_priority(const std::string& player_name, cache::player_priority priority);
	cache::player_priority get_priority(const std::string& player_name);
	void start_spectating(const std::string& player_name);
	void stop_spectating();
	bool is_spectating();
	std::string get_spectating_name();
	void tick();
}