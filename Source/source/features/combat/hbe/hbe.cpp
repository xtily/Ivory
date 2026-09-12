#include "hbe.h"

#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <sdk/math/math.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <features/system/settings/settings.h>

#include <thread>
#include <chrono>
#include <unordered_map>

namespace hitboxexpander
{
	struct original_size_t
	{
		math::vector3 size;
		bool can_collide;
	};

	inline std::unordered_map<std::uint64_t, original_size_t> original_sizes;

	static void apply_hitbox_expander()
	{
		if (!game::datamodel || game::datamodel->address == 0)
			return;

		static std::vector<cache::entity_t> entities_snapshot;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			entities_snapshot = cache::players;
		}

		for (const auto& entity : entities_snapshot)
		{
			if (entity.instance.address == 0)
				continue;

			if (entity.instance.address == cache::get_local_player().instance.address)
				continue;

			if (settings::teamcheck && cache::get_local_player().team != 0 && entity.team == cache::get_local_player().team)
				continue;

			if (settings::hitboxexpander::knock_check && entity.knocked)
				continue;

			auto hrp_it = entity.parts.find("HumanoidRootPart");
			if (hrp_it == entity.parts.end() || !hrp_it->second.address)
				continue;

			rbx::c_part hrp = hrp_it->second;
			rbx::c_primitive prim = hrp.get_primitive();
			if (prim.address == 0)
				continue;

			if (original_sizes.find(prim.address) == original_sizes.end())
			{
				original_size_t original;
				original.size = prim.get_size();
				std::uint8_t flags = memory->read<std::uint8_t>(prim.address + Offsets::Primitive::Flags);
				original.can_collide = (flags & static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide)) != 0;
				original_sizes[prim.address] = original;
			}

			math::vector3 new_size(settings::hitboxexpander::size_x, settings::hitboxexpander::size_y, settings::hitboxexpander::size_z);

			prim.set_size(new_size);
			prim.set_can_collide(false);
		}

		for (auto it = original_sizes.begin(); it != original_sizes.end();)
		{
			bool found = false;
			for (const auto& entity : entities_snapshot)
			{
				if (entity.instance.address == 0)
					continue;

				if (settings::teamcheck && cache::get_local_player().team != 0 && entity.team == cache::get_local_player().team)
					continue;

				if (settings::hitboxexpander::knock_check && entity.knocked)
					continue;

				auto hrp_it = entity.parts.find("HumanoidRootPart");
				if (hrp_it == entity.parts.end() || !hrp_it->second.address)
					continue;

				rbx::c_part hrp = hrp_it->second;
				rbx::c_primitive prim = hrp.get_primitive();
				if (prim.address == it->first)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{

				rbx::c_primitive prim(it->first);
				if (prim.address != 0)
				{
					prim.set_size(it->second.size);
					prim.set_can_collide(it->second.can_collide);
				}
				it = original_sizes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	static void restore_hitboxes()
	{
		for (auto& [address, original] : original_sizes)
		{
			rbx::c_primitive prim(address);
			if (prim.address == 0)
				continue;

			prim.set_size(original.size);
			prim.set_can_collide(original.can_collide);
		}
		original_sizes.clear();
	}

	void run()
	{
		using namespace std::chrono_literals;

		for (;;)
		{
			std::this_thread::sleep_for(100ms);

			if (settings::hitboxexpander::enabled)
			{
				apply_hitbox_expander();
			}
			else
			{
				if (!original_sizes.empty())
				{
					restore_hitboxes();
				}
			}
		}
	}
}