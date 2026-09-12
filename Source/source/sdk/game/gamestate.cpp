#include "game.h"
#include <core/globals.h>
#include <core/memory/memory.h>
#include <chrono>

HWND game::get_roblox_window()
{
	static std::chrono::steady_clock::time_point s_last_check{};
	static HWND s_cached = nullptr;

	const auto now = std::chrono::steady_clock::now();
	const bool cache_valid = (s_cached != nullptr) &&
		std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_check).count() < 250;

	if (cache_valid)
	{
		return s_cached;
	}
	s_last_check = now;

	if (game::roblox_window && IsWindow(game::roblox_window))
	{
		s_cached = game::roblox_window;
		return s_cached;
	}

	HWND hwnd = FindWindowA("WINDOWSCLIENT", nullptr);
	if (hwnd == nullptr)
	{
		hwnd = FindWindowA(nullptr, "Roblox");
	}

	if (hwnd == nullptr && memory->m_process_id != 0)
	{
		EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
		{
			DWORD process_id = 0;
			GetWindowThreadProcessId(hwnd, &process_id);
			if (process_id == memory->m_process_id && IsWindowVisible(hwnd))
			{
				*reinterpret_cast<HWND*>(lParam) = hwnd;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&hwnd));
	}

	if (hwnd)
	{
		game::roblox_window = hwnd;
	}

	s_cached = hwnd;
	return hwnd;
}