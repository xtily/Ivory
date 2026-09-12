#include <core/scanner/rescan.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <windows.h>

#include <core/globals.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>
#include <core/memory/memory.h>
#include <features/system/config/config.h>

void rescan::rescan_process()
{
	using namespace std::chrono_literals;

	while (true)
	{
		HANDLE current_handle = nullptr;
		{
			std::lock_guard<std::mutex> lock(memory->m_handle_mtx);
			current_handle = memory->m_process_handle;
		}

		bool process_dead = false;
		if (current_handle != nullptr && current_handle != INVALID_HANDLE_VALUE)
		{
			DWORD exit_code = 0;
			if (GetExitCodeProcess(current_handle, &exit_code))
			{
				if (exit_code != STILL_ACTIVE)
				{
					process_dead = true;
				}
			}
			else
			{
				process_dead = true;
			}

			if (!process_dead && WaitForSingleObject(current_handle, 0) == WAIT_OBJECT_0)
			{
				process_dead = true;
			}
		}
		else
		{
			process_dead = true;
		}

		if (process_dead)
		{
			logger->log<WARN>("[Process Monitor] Target game process closed or crashed. Terminating Lullaby...");
			ExitProcess(0);
		}

		std::this_thread::sleep_for(100ms);
	}
}