#pragma once
#include <atomic>

namespace rescan
{
	inline std::atomic<bool> require_rescan{ false };
	inline std::atomic<bool> require_cache_reset{ false };
	inline std::atomic<bool> is_rescanning{ false };

	void rescan_game();
	void rescan_process();
}