#pragma once
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>

#include <sdk/sdk.h>
#include <sdk/math/math.h>

namespace cache
{
	inline std::mutex mtx;

	void run();
	void run_part_cache();
	void run_transform_cache();
	void run_position_cache();

	enum class player_priority
	{
		neutral,
		friendly,
		hostile
	};

	struct shared_parts_map {
		std::shared_ptr<std::unordered_map<std::string, rbx::c_part>> m_map;

		shared_parts_map() : m_map(std::make_shared<std::unordered_map<std::string, rbx::c_part>>()) {}

		shared_parts_map(std::unordered_map<std::string, rbx::c_part> map)
			: m_map(std::make_shared<std::unordered_map<std::string, rbx::c_part>>(std::move(map))) {}

		shared_parts_map& operator=(std::unordered_map<std::string, rbx::c_part> map) {
			m_map = std::make_shared<std::unordered_map<std::string, rbx::c_part>>(std::move(map));
			return *this;
		}

		auto begin() const { return m_map->begin(); }
		auto end() const { return m_map->end(); }
		auto begin() { return m_map->begin(); }
		auto end() { return m_map->end(); }
		auto find(const std::string& key) const { return m_map->find(key); }
		auto find(const std::string& key) { return m_map->find(key); }
		auto count(const std::string& key) const { return m_map->count(key); }
		auto size() const { return m_map->size(); }
		auto empty() const { return m_map->empty(); }
		void clear() { m_map->clear(); }
		void reserve(size_t n) { m_map->reserve(n); }

		rbx::c_part& operator[](const std::string& key) {
			return (*m_map)[key];
		}

		operator const std::unordered_map<std::string, rbx::c_part>&() const {
			return *m_map;
		}

		operator std::unordered_map<std::string, rbx::c_part>&() {
			return *m_map;
		}
	};

	struct entity_t final
	{
		rbx::c_instance instance;
		std::uint64_t team;
		std::uint64_t user_id = 0;

		std::string name;
		std::string display_name;
		std::string tool_name;

		float health;
		float max_health;

		bool knocked = false;
		bool is_custom = false;
		std::string custom_path = "";

		rbx::c_part humanoid_root_part;
		std::string mm2_role = "Innocent";
		rbx::c_part head_part;
		rbx::c_humanoid humanoid;
		shared_parts_map parts;
		
		player_priority priority = player_priority::neutral;
		int humanoid_state = 0;

		std::uint64_t hrp_prim_addr = 0;
		std::uint64_t head_prim_addr = 0;
		std::uint64_t torso_prim_addr = 0;
		std::uint64_t left_arm_prim_addr = 0;
		std::uint64_t right_arm_prim_addr = 0;
		std::uint64_t left_leg_prim_addr = 0;
		std::uint64_t right_leg_prim_addr = 0;

		struct part_positions_t {
			math::vector3 head{};
			math::vector3 torso{};
			math::vector3 left_arm{};
			math::vector3 right_arm{};
			math::vector3 left_leg{};
			math::vector3 right_leg{};
		} part_positions;

		math::vector3 velocity{0.0f, 0.0f, 0.0f};

		math::vector3 position{};
		math::cframe  rotation{};
	};

	using EspEntity = entity_t;

	inline entity_t local_player;
	inline rbx::c_model_instance local_character;
	inline rbx::c_instance player_gui;
	inline std::vector<entity_t> players;
	inline const std::vector<entity_t>& GetEspEntities() { return players; }

	inline std::mutex local_player_mtx;

	inline entity_t get_local_player()
	{
		std::lock_guard<std::mutex> lock(local_player_mtx);
		return local_player;
	}

	inline void reset_local_player()
	{
		std::lock_guard<std::mutex> lock(local_player_mtx);
		local_player = {};
	}

	inline std::atomic<std::shared_ptr<const std::vector<entity_t>>> players_snap{};
	inline std::atomic<std::shared_ptr<const entity_t>>              local_snap{};

	inline void publish_snaps()
	{
		std::shared_ptr<std::vector<entity_t>> psnap;
		{
			std::lock_guard<std::mutex> lk(mtx);
			psnap = std::make_shared<std::vector<entity_t>>(players);
		}
		players_snap.store(psnap, std::memory_order_release);

		std::shared_ptr<entity_t> lsnap;
		{
			std::lock_guard<std::mutex> lk(local_player_mtx);
			lsnap = std::make_shared<entity_t>(local_player);
		}
		local_snap.store(lsnap, std::memory_order_release);
	}

	inline std::shared_ptr<const std::vector<entity_t>> get_players_snap()
	{
		return players_snap.load(std::memory_order_acquire);
	}

	inline std::shared_ptr<const entity_t> get_local_snap()
	{
		return local_snap.load(std::memory_order_acquire);
	}
}