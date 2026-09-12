#pragma once
#include <string>
#include <vector>
#include <atomic>

namespace mcp
{
	void run();
	void stop();
	bool is_running();
	int get_port();
	void set_port(int port);
	int get_request_count();
}