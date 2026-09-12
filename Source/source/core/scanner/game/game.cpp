#include <core/scanner/rescan.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>

#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <sdk/wallcheck/wallcheck.h>
#include <core/logger/logger.h>
#include <sdk/sdk.h>
#include <core/memory/memory.h>
#include <features/system/settings/settings.h>
#include <features/system/notifications/notifications.h>

void rescan::rescan_game()
{
	using namespace std::chrono_literals;

	uint64_t last_known_datamodel = 0;
	uint64_t last_known_place_id = 0;
	auto last_check_time = std::chrono::steady_clock::now();

	static int consecutive_invalid = 0;
	constexpr int kInvalidThreshold = 3;

	while (true)
	{
		bool needs_rescan = require_rescan.load();

		if (!needs_rescan && settings::misc::auto_rescan)
		{
			bool invalid = false;
			uint64_t current_dm_addr = 0;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				if (!game::datamodel || game::datamodel->address < 0x10000 || game::datamodel->address >= 0x7FFFFFFFFFFFull ||
					!game::visualengine || game::visualengine->address < 0x10000 || game::visualengine->address >= 0x7FFFFFFFFFFFull)
				{
					invalid = true;
				}
				else
				{
					current_dm_addr = game::datamodel->address;
					uint64_t ws_addr = memory->read<uint64_t>(current_dm_addr + Offsets::DataModel::Workspace);
					if (ws_addr < 0x10000 || ws_addr >= 0x7FFFFFFFFFFFull)
					{
						invalid = true;
					}
				}
			}

			if (invalid)
			{
				++consecutive_invalid;
				if (consecutive_invalid >= kInvalidThreshold)
				{
					consecutive_invalid = 0;
					needs_rescan = true;
				}
			}
			else
			{
				consecutive_invalid = 0;
				auto now = std::chrono::steady_clock::now();
				if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check_time).count() >= 500)
				{
					last_check_time = now;
					auto live_dm = rbx::c_datamodel::get();
					if (live_dm && live_dm->address >= 0x10000 && live_dm->address < 0x7FFFFFFFFFFFull)
					{
						if (current_dm_addr != 0 && live_dm->address != current_dm_addr)
						{
							needs_rescan = true;
						}
						else
						{
							uint64_t place_id = live_dm->get_place_id();
							if (last_known_place_id != 0 && place_id != 0 && place_id != last_known_place_id)
							{
								last_known_place_id = place_id;
								needs_rescan = true;
							}
							else if (place_id != 0)
							{
								last_known_place_id = place_id;
							}
						}
					}
				}
			}
		}

		if (needs_rescan)
		{
			is_rescanning.store(true);

			while (true)
			{
				HANDLE current_handle = nullptr;
				{
					std::lock_guard<std::mutex> lock(memory->m_handle_mtx);
					current_handle = memory->m_process_handle;
				}

				if (!current_handle || memory->m_base_address == 0)
				{
					std::this_thread::sleep_for(250ms);
					continue;
				}

				auto dm = rbx::c_datamodel::get();
				auto ve = rbx::c_visualengine::get();

				if (dm && dm->address >= 0x10000 && dm->address < 0x7FFFFFFFFFFFull &&
					ve && ve->address >= 0x10000 && ve->address < 0x7FFFFFFFFFFFull)
				{
					uint64_t ws = memory->read<uint64_t>(dm->address + Offsets::DataModel::Workspace);
					if (ws >= 0x10000 && ws < 0x7FFFFFFFFFFFull)
					{
						// camera is optional — may not be loaded yet at attach time
						uint64_t cam = memory->read<uint64_t>(ws + Offsets::Workspace::CurrentCamera);
						if (cam < 0x10000 || cam >= 0x7FFFFFFFFFFFull)
							cam = 0;

						cache::reset_local_player();
						{
							std::lock_guard<std::mutex> lock(cache::mtx);
							cache::players.clear();
							cache::local_character = rbx::c_model_instance(0);
							cache::player_gui = rbx::c_instance(0);

							static std::vector<std::pair<std::chrono::steady_clock::time_point,
								std::unique_ptr<rbx::c_datamodel>>> retired_datamodels;
							static std::vector<std::pair<std::chrono::steady_clock::time_point,
								std::unique_ptr<rbx::c_visualengine>>> retired_visualengines;
							constexpr auto retire_grace = std::chrono::seconds(5);
							auto now_ts = std::chrono::steady_clock::now();

							if (game::datamodel)
								retired_datamodels.push_back({ now_ts, std::move(game::datamodel) });
							if (game::visualengine)
								retired_visualengines.push_back({ now_ts, std::move(game::visualengine) });

							game::datamodel = std::move(dm);
							game::visualengine = std::move(ve);
							game::camera = cam;

							retired_datamodels.erase(
								std::remove_if(retired_datamodels.begin(), retired_datamodels.end(),
									[&](const auto& e) { return (now_ts - e.first) > retire_grace; }),
								retired_datamodels.end());
							retired_visualengines.erase(
								std::remove_if(retired_visualengines.begin(), retired_visualengines.end(),
									[&](const auto& e) { return (now_ts - e.first) > retire_grace; }),
								retired_visualengines.end());
						}

						last_known_datamodel = game::datamodel->address;
						last_known_place_id = game::datamodel->get_place_id();

						static std::atomic<bool> is_caching_wallcheck{ false };
						if (!is_caching_wallcheck.exchange(true))
						{
							std::thread([]() {
								std::this_thread::sleep_for(1000ms);
								wallcheck->cache_workspace();
								is_caching_wallcheck.store(false);
							}).detach();
						}

						require_rescan.store(false);
						require_cache_reset.store(true);
						is_rescanning.store(false);
						break;
					}
				}

				std::this_thread::sleep_for(200ms);
			}
		}

		std::this_thread::sleep_for(150ms);
	}
}