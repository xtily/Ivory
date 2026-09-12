#include "cache.h"

#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <fstream>

#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <core/scanner/rescan.h>
#include <core/logger/logger.h>
#include <features/system/settings/settings.h>

static rbx::c_instance find_instance_by_path(rbx::c_instance start, const std::string& path)
{
	if (path.empty() || !start.address)
	{
		return rbx::c_instance(0);
	}

	std::istringstream iss(path);
	std::string segment;
	rbx::c_instance current = start;

	while (std::getline(iss, segment, '.'))
	{
		if (segment.empty())
		{
			continue;
		}

		std::uint64_t child_addr = current.find_first_child(segment);
		if (child_addr == 0)
		{
			return rbx::c_instance(0);
		}

		current = rbx::c_instance(child_addr);
	}

	return current;
}

#include <unordered_set>
#include <unordered_map>

static inline bool is_valid_addr(std::uint64_t a) { return a >= 0x10000 && a < 0x7FFFFFFFFFFFull; }
#define valid_addr is_valid_addr

static bool is_mesh_part_name(const std::string& name) {
	return name == "Head" ||
		name == "UpperTorso" ||
		name == "LowerTorso" ||
		name == "Torso" ||
		name == "LeftHand" ||
		name == "RightHand" ||
		name == "LeftFoot" ||
		name == "RightFoot" ||
		name == "Left Arm" ||
		name == "Right Arm" ||
		name == "Left Leg" ||
		name == "Right Leg" ||
		name == "LeftUpperArm" ||
		name == "RightUpperArm" ||
		name == "LeftLowerArm" ||
		name == "RightLowerArm" ||
		name == "LeftUpperLeg" ||
		name == "RightUpperLeg" ||
		name == "LeftLowerLeg" ||
		name == "RightLowerLeg" ||
		name == "pfLimbs1" ||
		name == "pfLimbs2" ||
		name == "pfLimbs3" ||
		name == "pfLimbs4" ||
		name == "pfLimbs5";
}

struct cached_player_info_t {
	std::uint64_t model_instance_address = 0;
	std::uint64_t team = 0;
	std::uint64_t user_id = 0;
	std::string name;
	std::string display_name;
	std::string tool_name;
	rbx::c_humanoid humanoid;
	rbx::c_part humanoid_root_part;
	rbx::c_part head_part;
	std::unordered_map<std::string, rbx::c_part> parts;
	std::uint64_t ko_address = 0;
	std::chrono::steady_clock::time_point last_rescan_time{};
	std::uint64_t hrp_prim_addr = 0;
	std::uint64_t head_prim_addr = 0;
	std::uint64_t torso_prim_addr = 0;
	std::uint64_t left_arm_prim_addr = 0;
	std::uint64_t right_arm_prim_addr = 0;
	std::uint64_t left_leg_prim_addr = 0;
	std::uint64_t right_leg_prim_addr = 0;
	bool is_corpse = false;
};

static std::string find_tool_name(rbx::c_model_instance& model)
{
	if (model.address == 0)
		return {};

	std::uint64_t tool_address = model.find_first_child_by_class("Tool");
	if (tool_address != 0)
	{
		return rbx::c_instance(tool_address).get_name();
	}

	std::vector<std::uint64_t> children = model.get_children();
	for (std::uint64_t child_addr : children)
	{
		rbx::c_instance child(child_addr);
		if (!child.address) continue;
		std::string cls = child.get_class_name();
		if (cls == "Model")
		{
			std::string name = child.get_name();
			if (name != "HumanoidRootPart" && name != "Head" && name != "Torso" &&
				name != "UpperTorso" && name != "LowerTorso" && name != "LeftArm" &&
				name != "RightArm" && name != "LeftLeg" && name != "RightLeg" &&
				name != "LeftHand" && name != "RightHand" && name != "LeftUpperArm" &&
				name != "RightUpperArm" && name != "LeftLowerArm" && name != "RightLowerArm" &&
				name != "LeftUpperLeg" && name != "RightUpperLeg" && name != "LeftLowerLeg" &&
				name != "RightLowerLeg" && name != "LeftFoot" && name != "RightFoot" &&
				name != "Animations" && name != "AnimationsV2" && name != "Body" &&
				name.find("Injury") == std::string::npos)
			{
				if (child.find_first_child("Handle") || child.find_first_child("Muzzle") ||
					child.find_first_child("Grip") || child.find_first_child("Primary") ||
					child.find_first_child("Blade") || child.find_first_child("_center") ||
					child.find_first_child("_grip"))
				{
					return name;
				}
			}
		}
	}

	return {};
}

void cache::run()
{
	auto spawn_guarded = [](const char* name, void(*fn)()) {
		std::thread([name, fn]() {
			try { fn(); }
			catch (const std::exception& e) { logger->log<ERR>("cache worker '{}' died: {}", name, e.what()); }
			catch (...) { logger->log<ERR>("cache worker '{}' died: unknown exception", name); }
		}).detach();
	};

	spawn_guarded("run_part_cache", cache::run_part_cache);
	spawn_guarded("run_transform_cache", cache::run_transform_cache);
	spawn_guarded("run_position_cache", cache::run_position_cache);
}

static std::uint64_t get_prim_address_from_part(std::uint64_t part_addr)
{
	if (!part_addr) return 0;
	return memory->read<std::uint64_t>(part_addr + Offsets::BasePart::Primitive);
}

void cache::run_part_cache()
{
	using namespace std::chrono_literals;

	std::vector<cache::entity_t> temp_cache;
	temp_cache.reserve(64);

	std::unordered_map<std::uint64_t, cached_player_info_t> player_cache;
	std::unordered_map<std::uint64_t, cached_player_info_t> npc_cache;

	std::unordered_set<std::uint64_t> seen_players;
	std::unordered_set<std::uint64_t> seen_npcs;

	std::uint64_t last_datamodel_addr = 0;
	rbx::c_instance cached_players_container(0);

	auto is_part_class = [](const std::string& class_name) -> bool
	{
		return class_name == "BasePart" || class_name == "Part" ||
			   class_name == "MeshPart" || class_name == "WedgePart" ||
			   class_name == "CornerWedgePart" || class_name == "Ball" ||
			   class_name == "Cylinder" || class_name == "UnionOperation" ||
			   class_name == "TrussPart";
	};

	// Tracks whether we already cleared the cache for the current rescan session.
	bool s_cache_cleared_for_rescan = false;

	while (true)
	{
		temp_cache.clear();
		seen_players.clear();
		seen_npcs.clear();

		const std::uint64_t current_datamodel_addr = (game::datamodel && !rescan::is_rescanning) ? game::datamodel->address : 0;
		if (current_datamodel_addr == 0 || rescan::is_rescanning)
		{
			// Only clear once per rescan — not every 20ms tick — so chams don't flicker.
			if (!s_cache_cleared_for_rescan)
			{
				{
					std::lock_guard<std::mutex> lock(cache::mtx);
					cache::players.clear();
					cache::local_character = rbx::c_model_instance(0);
					cache::player_gui = rbx::c_instance(0);
				}
				{
					std::lock_guard<std::mutex> lock(cache::local_player_mtx);
					cache::local_player = {};
				}
				s_cache_cleared_for_rescan = true;
			}
			std::this_thread::sleep_for(20ms);
			continue;
		}
		// Rescan finished — allow clearing again on next rescan.
		s_cache_cleared_for_rescan = false;

		static size_t last_players_count = 0;
		static std::uint64_t last_place_id = 0;
		std::uint64_t current_place_id = 0;
		if (game::datamodel && game::datamodel->address != 0)
		{
			current_place_id = game::datamodel->get_place_id();
		}

		bool place_teleported = (last_place_id != 0 && current_place_id != 0 && current_place_id != last_place_id);
		bool datamodel_changed = (last_datamodel_addr != 0 && current_datamodel_addr != last_datamodel_addr);

		if (place_teleported || datamodel_changed || cached_players_container.address == 0 || rescan::require_cache_reset)
		{
			if (place_teleported || datamodel_changed)
			{
				rescan::require_rescan = true;
			}

			if (current_place_id != 0)
				last_place_id = current_place_id;
			last_datamodel_addr = current_datamodel_addr;
			cached_players_container = game::datamodel->find_first_child_by_class("Players");
			if (cached_players_container.address == 0)
				cached_players_container = game::datamodel->find_first_child("Players");

			player_cache.clear();
			npc_cache.clear();
			rescan::require_cache_reset = false;
		}

		if (cached_players_container.address == 0)
		{
			cached_players_container = game::datamodel->find_first_child_by_class("Players");
			if (cached_players_container.address == 0)
				cached_players_container = game::datamodel->find_first_child("Players");
			if (cached_players_container.address == 0)
			{
				if (settings::misc::auto_rescan)
				{
					rescan::require_rescan.store(true);
				}
				std::this_thread::sleep_for(50ms);
				continue;
			}
		}

		rbx::c_instance players = cached_players_container;
		std::vector<rbx::c_player> current_players_list = players.get_children<rbx::c_player>();
		static int empty_player_cycles = 0;
		if (current_players_list.empty())
		{
			++empty_player_cycles;
			if (empty_player_cycles >= 15 && settings::misc::auto_rescan)
			{
				empty_player_cycles = 0;
				cached_players_container = rbx::c_instance(0);
				rescan::require_rescan.store(true);
				rescan::require_cache_reset.store(true);
			}
		}
		else
		{
			empty_player_cycles = 0;
		}

		if (current_players_list.size() != last_players_count)
		{
			last_players_count = current_players_list.size();
		}
		rbx::c_player local_player = memory->read<std::uint64_t>(players.address + Offsets::Player::LocalPlayer);

		auto workspace = game::datamodel->get_workspace();
		if (workspace.address != 0)
		{
			game::camera = memory->read<std::uint64_t>(workspace.address + Offsets::Workspace::CurrentCamera);
		}
		else
		{
			game::camera = 0;
		}

		if (local_player.address != 0)
		{
			cache::local_character = local_player.get_model_instance();
			cache::player_gui = local_player.find_first_child("PlayerGui");

			std::string lp_name = local_player.get_name();
			std::string lp_display = local_player.get_display_name();

			rbx::c_humanoid hum(0);
			rbx::c_part hrp(0);
			uint64_t hrp_prim = 0;

			if (cache::local_character.address != 0)
			{
				hum = cache::local_character.find_first_child_by_class("Humanoid");
				hrp = cache::local_character.find_first_child("HumanoidRootPart");
				if (hrp.address == 0) hrp = cache::local_character.find_first_child("Torso");
				if (hrp.address == 0) hrp = cache::local_character.find_first_child("UpperTorso");

				if (hrp.address != 0)
				{
					hrp_prim = memory->read<uint64_t>(hrp.address + Offsets::BasePart::Primitive);
				}
			}

			{
				std::lock_guard<std::mutex> lock(cache::local_player_mtx);
				cache::local_player.instance = local_player;
				cache::local_player.name = lp_name;
				cache::local_player.display_name = lp_display;
				cache::local_player.user_id = local_player.get_user_id();
				if (hum.address != 0) cache::local_player.humanoid = hum;
				if (hrp.address != 0) cache::local_player.humanoid_root_part = hrp;
				if (hrp_prim != 0) cache::local_player.hrp_prim_addr = hrp_prim;
			}
		}
		else
		{
			cache::local_character = rbx::c_model_instance(0);
			cache::player_gui = rbx::c_instance(0);
		}

		rbx::c_instance characters_folder(0);
		if (workspace.address != 0)
		{
			characters_folder = workspace.find_first_child("Characters");
		}

		bool is_bloxstrike = (current_place_id == 114234929420007ull || (game::datamodel && game::datamodel->get_place_id() == 114234929420007ull));
		bool is_pf = is_bloxstrike || (current_place_id == 113491250 || current_place_id == 292439477 || (game::datamodel && (game::datamodel->get_game_id() == 113491250 || game::datamodel->get_game_id() == 292439477 || game::datamodel->get_place_id() == 292439477 || game::datamodel->get_place_id() == 113491250)));
		if (!is_pf && workspace.address != 0)
		{
			rbx::c_instance ws_p = workspace.find_first_child("Players");
			if (ws_p.address == 0)
				ws_p = workspace.find_first_child("Characters");
			if (ws_p.address != 0)
			{
				std::vector<rbx::c_instance> children = ws_p.get_children<rbx::c_instance>();
				if (children.size() >= 2)
				{
					bool has_humanoid_in_children = false;
					for (auto& child : children)
					{
						if (child.address && child.find_first_child_by_class("Humanoid") != 0)
						{
							has_humanoid_in_children = true;
							break;
						}
					}
					if (!has_humanoid_in_children)
					{
						is_pf = true;
					}
				}
			}
		}

		if (is_pf)
		{
			static auto last_pf_scan = std::chrono::steady_clock::now();
			auto now_pf = std::chrono::steady_clock::now();
			bool pf_scan_due = std::chrono::duration_cast<std::chrono::milliseconds>(now_pf - last_pf_scan).count() >= 30;
			if (!pf_scan_due)
			{
				std::this_thread::sleep_for(5ms);
				continue;
			}
			last_pf_scan = now_pf;
			if (workspace.address == 0)
			{
				std::this_thread::sleep_for(50ms);
				continue;
			}
			std::unordered_map<std::string, std::uint32_t> playerserviceTeamColors;
			for (rbx::c_player& p : current_players_list)
			{
				if (!p.address) continue;
				std::string pname = p.get_name();
				std::string dname = p.get_display_name();
				std::uint32_t teamColor = memory->read<std::uint32_t>(p.address + Offsets::Player::TeamColor);
				if (!pname.empty())
					playerserviceTeamColors[pname] = teamColor;
				if (!dname.empty() && dname != pname)
					playerserviceTeamColors[dname] = teamColor;
			}

			rbx::c_instance ws_players = workspace.find_first_child("Players");
			if (ws_players.address == 0)
				ws_players = workspace.find_first_child("Characters");
			if (ws_players.address == 0)
			{
				std::this_thread::sleep_for(20ms);
				continue;
			}

			std::vector<rbx::c_instance> team_folders = ws_players.get_children<rbx::c_instance>();
			if (team_folders.size() < 1)
			{
				std::this_thread::sleep_for(20ms);
				continue;
			}

			std::unordered_map<std::uint64_t, std::uint32_t> modelAddrTeamColor;
			modelAddrTeamColor.reserve(24);

			std::unordered_map<std::uint64_t, std::uint64_t> modelTeamFolderMap;
			modelTeamFolderMap.reserve(24);

			std::vector<std::uint64_t> model_addresses;
			model_addresses.reserve(24);

			std::uint64_t local_team_folder_addr = 0;
			std::uint32_t folder0_teamColor = 0;
			std::uint32_t folder1_teamColor = 0;

			std::string lp_name = (local_player.address != 0) ? local_player.get_name() : "";
			std::string lp_display = (local_player.address != 0) ? local_player.get_display_name() : "";

			for (size_t t_idx = 0; t_idx < team_folders.size(); ++t_idx)
			{
				auto& team = team_folders[t_idx];
				if (!team.address) continue;
				std::vector<rbx::c_instance> player_models = team.get_children<rbx::c_instance>();
				for (auto& model : player_models)
				{
					if (!model.address || model.address < 0x10000 || model.address > 0x7FFFFFFFFFFFull) continue;
					modelTeamFolderMap[model.address] = team.address;

					std::string model_name = model.get_name();
					auto tc_it = playerserviceTeamColors.find(model_name);
					if (tc_it != playerserviceTeamColors.end() && tc_it->second != 0)
					{
						modelAddrTeamColor[model.address] = tc_it->second;
						if (t_idx == 0 && folder0_teamColor == 0) folder0_teamColor = tc_it->second;
						if (t_idx == 1 && folder1_teamColor == 0) folder1_teamColor = tc_it->second;
					}

					bool is_lp = (cache::local_character.address != 0 && model.address == cache::local_character.address) ||
					             (!lp_name.empty() && model_name == lp_name) ||
					             (!lp_display.empty() && model_name == lp_display);

					if (is_lp)
					{
						local_team_folder_addr = team.address;
						cache::local_character = rbx::c_model_instance(model.address);
						continue;
					}

					model_addresses.push_back(model.address);
					seen_players.insert(model.address);
				}
			}

			// Scan for player corpses in PF
			std::unordered_set<std::uint64_t> pf_corpses_found;
			for (rbx::c_player& p : current_players_list)
			{
				if (!p.address) continue;
				if (p.address == local_player.address) continue;

				std::string pname = p.get_name();
				std::string dname = p.get_display_name();

				std::uint64_t corpse_addr = 0;
				static const char* s_corpse_folders[] = {
					"Ignore", "Ignored", "Debris", "DebrisFolder", "Bodies", "bodies", 
					"Ragdolls", "ragdolls", "Corpses", "corpses", "Dead", "dead"
				};
				for (const char* folder_name : s_corpse_folders)
				{
					rbx::c_instance corpse_f = workspace.find_first_child(folder_name);
					if (corpse_f.address != 0)
					{
						std::vector<rbx::c_instance> folder_children = corpse_f.get_children<rbx::c_instance>();
						for (auto& child : folder_children)
						{
							if (!child.address || !valid_addr(child.address)) continue;
							
							std::string child_name = child.get_name();
							if (child_name == pname || (!dname.empty() && child_name == dname))
							{
								corpse_addr = child.address;
								break;
							}
							
							std::vector<rbx::c_instance> sub_children = child.get_children<rbx::c_instance>();
							for (auto& sub_child : sub_children)
							{
								if (!sub_child.address || !valid_addr(sub_child.address)) continue;
								
								std::uint64_t billboard_addr = sub_child.find_first_child_by_class("BillboardGui");
								if (billboard_addr != 0)
								{
									rbx::c_instance billboard(billboard_addr);
									std::uint64_t textlabel_addr = billboard.find_first_child_by_class("TextLabel");
									if (textlabel_addr != 0)
									{
										std::string fetched_name = memory->read_string(textlabel_addr + Offsets::GuiObject::Text);
										if (fetched_name == "str_error" || fetched_name.empty())
											fetched_name = memory->read_string(textlabel_addr + 0xe40);
										if (fetched_name != "str_error" && !fetched_name.empty())
										{
											if (fetched_name == pname || (!dname.empty() && fetched_name == dname))
											{
												corpse_addr = child.address;
												break;
											}
										}
									}
								}
							}
							if (corpse_addr != 0) break;
						}
					}
					if (corpse_addr != 0) break;
				}

				if (corpse_addr != 0)
				{
					model_addresses.push_back(corpse_addr);
					seen_players.insert(corpse_addr);
					pf_corpses_found.insert(corpse_addr);
					
					std::uint32_t team_color = memory->read<std::uint32_t>(p.address + Offsets::Player::TeamColor);
					if (team_color != 0)
					{
						modelAddrTeamColor[corpse_addr] = team_color;
					}
					else
					{
						auto tc_it = playerserviceTeamColors.find(pname);
						if (tc_it != playerserviceTeamColors.end())
						{
							modelAddrTeamColor[corpse_addr] = tc_it->second;
						}
					}
				}
			}

			std::uint32_t lp_team_val = (local_player.address != 0) ? memory->read<std::uint32_t>(local_player.address + Offsets::Player::TeamColor) : 0;
			if (lp_team_val == 0 || lp_team_val == 194)
			{
				if (local_team_folder_addr != 0)
				{
					if (local_team_folder_addr == team_folders[0].address)
						lp_team_val = (folder0_teamColor != 0) ? folder0_teamColor : 1001;
					else if (team_folders.size() > 1 && local_team_folder_addr == team_folders[1].address)
						lp_team_val = (folder1_teamColor != 0) ? folder1_teamColor : 1002;
					else
						lp_team_val = 1001;
				}
				else if (folder0_teamColor != 0)
				{
					lp_team_val = folder0_teamColor;
				}
				else
				{
					lp_team_val = 1001;
				}
			}

			{
				std::lock_guard<std::mutex> lock(cache::local_player_mtx);
				cache::local_player.team = lp_team_val;
			}

			auto resolve_pf_team = [&](std::uint64_t m_addr, std::uint32_t extracted_tc) -> std::uint64_t {
				if (extracted_tc != 0 && extracted_tc != 194) return extracted_tc;
				auto ma = modelAddrTeamColor.find(m_addr);
				if (ma != modelAddrTeamColor.end() && ma->second != 0 && ma->second != 194) return ma->second;

				auto f_it = modelTeamFolderMap.find(m_addr);
				if (f_it != modelTeamFolderMap.end())
				{
					std::uint64_t f_addr = f_it->second;
					if (team_folders.size() > 0 && f_addr == team_folders[0].address)
						return (folder0_teamColor != 0) ? folder0_teamColor : 1001;
					if (team_folders.size() > 1 && f_addr == team_folders[1].address)
						return (folder1_teamColor != 0) ? folder1_teamColor : 1002;
				}
				return 0;
			};

			int new_scan_budget = 8;
			int refresh_budget = 2;

			auto valid_addr = [](std::uint64_t a) { return a >= 0x10000 && a < 0x7FFFFFFFFFFFull; };

			for (std::uint64_t model_addr : model_addresses)
			{
				auto cache_it = player_cache.find(model_addr);
				bool needs_rescan = (cache_it == player_cache.end());

				if (!needs_rescan)
				{
					auto& cached = cache_it->second;
					auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - cached.last_rescan_time).count();

					if (age_ms >= 2000)
					{
						needs_rescan = true;
					}
					else if (valid_addr(cached.hrp_prim_addr))
					{
						math::vector3 prim_pos = memory->read<math::vector3>(cached.hrp_prim_addr + Offsets::Primitive::Position);
						if (prim_pos.x == 0.0f && prim_pos.y == 0.0f && prim_pos.z == 0.0f)
							needs_rescan = true;
					}
					else
					{
						needs_rescan = true;
					}
				}
				if (needs_rescan)
				{
					const bool is_new_model = (cache_it == player_cache.end());
					int& budget = is_new_model ? new_scan_budget : refresh_budget;
					if (budget <= 0)
					{
						if (cache_it != player_cache.end())
						{
							auto& cached = cache_it->second;
							entity_t entity{
								.instance = rbx::c_instance(model_addr),
								.team = cached.team,
								.name = cached.name,
								.display_name = cached.display_name,
								.tool_name = {},
								.health = cached.is_corpse ? 0.0f : 100.0f,
								.max_health = 100.0f,
								.knocked = cached.is_corpse,
								.humanoid_root_part = cached.humanoid_root_part,
								.head_part = cached.head_part,
								.humanoid = cached.humanoid,
								.parts = cached.parts,
								.priority = player_priority::neutral,
								.hrp_prim_addr = cached.hrp_prim_addr,
								.head_prim_addr = cached.head_prim_addr,
								.torso_prim_addr = cached.torso_prim_addr,
								.left_arm_prim_addr = cached.left_arm_prim_addr,
								.right_arm_prim_addr = cached.right_arm_prim_addr,
								.left_leg_prim_addr = cached.left_leg_prim_addr,
								.right_leg_prim_addr = cached.right_leg_prim_addr
							};
							temp_cache.push_back(std::move(entity));
						}
							continue;
					}
					--budget;

					rbx::c_instance PlayerModel(model_addr);
					std::string plr_name;
					std::uint32_t plr_teamColor = 0;
					rbx::c_part head_part(0);
					rbx::c_part upper_torso_part(0);
					rbx::c_part hrp(0);
					math::cframe torso_cframe{};
					bool has_torso_cframe = false;
					std::unordered_map<std::string, rbx::c_part> parts;
					std::vector<rbx::c_part> mesh_limbs;
					bool found_limbs_by_name = false;

					// Recursive helper to gather all body parts under the player model
					auto gather_parts = [&](auto& self, std::uint64_t parent_addr, bool is_root) -> void {
						if (!parent_addr || !valid_addr(parent_addr)) return;
						rbx::c_instance parent_inst(parent_addr);
						std::string parent_name = parent_inst.get_name();
						std::string lower_parent = parent_name;
						std::transform(lower_parent.begin(), lower_parent.end(), lower_parent.begin(), ::tolower);

						std::vector<rbx::c_instance> children = parent_inst.get_children<rbx::c_instance>();
						for (auto& child : children)
						{
							if (!child.address || !valid_addr(child.address)) continue;
							std::string child_class = child.get_class_name();
							std::string child_name = child.get_name();

							if (child_class.find("Part") != std::string::npos || child_class == "UnionOperation" || child_class == "BasePart" || child_class == "MeshPart")
							{
								rbx::c_part current_p(child.address);

								static const std::unordered_set<std::string> s_weapon_keywords = {
									"Handle", "Muzzle", "Barrel", "Grip", "Blade", "Primary",
									"Flash", "Sight", "Stock", "Mag", "Magazine", "Trigger",
									"_center", "_grip", "FrontSight", "BackSight", "Suppressor",
									"Shield", "Bullet", "Projectile", "Trail", "Effect",
									"Laser", "Scope", "Bolt", "Slide", "Hammer", "Clip",
									"Gun", "Weapon", "Attachment", "SightFront", "SightBack"
								};
								
								bool is_weapon = false;
								for (const auto& wname : s_weapon_keywords)
								{
									if (child_name.find(wname) != std::string::npos)
									{
										is_weapon = true;
										break;
									}
								}
								if (is_weapon) continue;

								std::string lower_name = child_name;
								std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

								bool matches_left_arm = (
									(lower_name.find("left") != std::string::npos && lower_name.find("arm") != std::string::npos) ||
									lower_name.rfind("l_arm", 0) == 0 || lower_name == "l arm" ||
									(lower_name.find("left") != std::string::npos && lower_name.find("sleeve") != std::string::npos) ||
									(lower_name.find("left") != std::string::npos && lower_name.find("glove") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("arm") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("sleeve") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("glove") != std::string::npos)
								);

								if (matches_left_arm)
								{
									if (parts.find("LeftArm") == parts.end()) {
										parts["LeftArm"] = parts["Left Arm"] = current_p;
									} else if (parts.find("LeftUpperArm") == parts.end()) {
										parts["LeftUpperArm"] = current_p;
									} else if (parts.find("LeftLowerArm") == parts.end()) {
										parts["LeftLowerArm"] = current_p;
									} else if (parts.find("LeftHand") == parts.end()) {
										parts["LeftHand"] = current_p;
									}
									found_limbs_by_name = true;
									continue;
								}

								bool matches_right_arm = (
									(lower_name.find("right") != std::string::npos && lower_name.find("arm") != std::string::npos) ||
									lower_name.rfind("r_arm", 0) == 0 || lower_name == "r arm" ||
									(lower_name.find("right") != std::string::npos && lower_name.find("sleeve") != std::string::npos) ||
									(lower_name.find("right") != std::string::npos && lower_name.find("glove") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("arm") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("sleeve") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("glove") != std::string::npos)
								);

								if (matches_right_arm)
								{
									if (parts.find("RightArm") == parts.end()) {
										parts["RightArm"] = parts["Right Arm"] = current_p;
									} else if (parts.find("RightUpperArm") == parts.end()) {
										parts["RightUpperArm"] = current_p;
									} else if (parts.find("RightLowerArm") == parts.end()) {
										parts["RightLowerArm"] = current_p;
									} else if (parts.find("RightHand") == parts.end()) {
										parts["RightHand"] = current_p;
									}
									found_limbs_by_name = true;
									continue;
								}

								bool matches_left_leg = (
									(lower_name.find("left") != std::string::npos && lower_name.find("leg") != std::string::npos) ||
									lower_name.rfind("l_leg", 0) == 0 || lower_name == "l leg" ||
									(lower_name.find("left") != std::string::npos && lower_name.find("pants") != std::string::npos) ||
									(lower_name.find("left") != std::string::npos && lower_name.find("foot") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("leg") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("pants") != std::string::npos) ||
									(lower_parent.find("left") != std::string::npos && lower_parent.find("foot") != std::string::npos)
								);

								if (matches_left_leg)
								{
									if (parts.find("LeftLeg") == parts.end()) {
										parts["LeftLeg"] = parts["Left Leg"] = current_p;
									} else if (parts.find("LeftUpperLeg") == parts.end()) {
										parts["LeftUpperLeg"] = current_p;
									} else if (parts.find("LeftLowerLeg") == parts.end()) {
										parts["LeftLowerLeg"] = current_p;
									} else if (parts.find("LeftFoot") == parts.end()) {
										parts["LeftFoot"] = current_p;
									}
									found_limbs_by_name = true;
									continue;
								}

								bool matches_right_leg = (
									(lower_name.find("right") != std::string::npos && lower_name.find("leg") != std::string::npos) ||
									lower_name.rfind("r_leg", 0) == 0 || lower_name == "r leg" ||
									(lower_name.find("right") != std::string::npos && lower_name.find("pants") != std::string::npos) ||
									(lower_name.find("right") != std::string::npos && lower_name.find("foot") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("leg") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("pants") != std::string::npos) ||
									(lower_parent.find("right") != std::string::npos && lower_parent.find("foot") != std::string::npos)
								);

								if (matches_right_leg)
								{
									if (parts.find("RightLeg") == parts.end()) {
										parts["RightLeg"] = parts["Right Leg"] = current_p;
									} else if (parts.find("RightUpperLeg") == parts.end()) {
										parts["RightUpperLeg"] = current_p;
									} else if (parts.find("RightLowerLeg") == parts.end()) {
										parts["RightLowerLeg"] = current_p;
									} else if (parts.find("RightFoot") == parts.end()) {
										parts["RightFoot"] = current_p;
									}
									found_limbs_by_name = true;
									continue;
								}

								bool matches_head = (
									lower_name == "head" || lower_name == "helmet" || lower_name == "face" ||
									lower_parent == "head" || lower_parent == "helmet"
								);

								if (matches_head)
								{
									head_part = current_p;
									parts["Head"] = head_part;
									continue;
								}

								bool is_torso = (
									lower_name == "torso" || lower_name == "uppertorso" || lower_name == "lowertorso" ||
									lower_parent == "torso" || lower_parent == "uppertorso" || lower_parent == "lowertorso" ||
									child.find_first_child_by_class("SpotLight") != 0
								);
								if (is_torso)
								{
									upper_torso_part = current_p;
									if (parts.find("Torso") == parts.end()) {
										parts["Torso"] = upper_torso_part;
									}
									if (parts.find("UpperTorso") == parts.end()) {
										parts["UpperTorso"] = upper_torso_part;
									}
									if (parts.find("LowerTorso") == parts.end()) {
										parts["LowerTorso"] = upper_torso_part;
									}

									rbx::c_primitive prim = upper_torso_part.get_primitive();
									if (prim.address != 0)
									{
										torso_cframe = prim.get_cframe();
										has_torso_cframe = true;
									}
									continue;
								}

								// Check if this part is a BillboardGui container or head with billboard
								std::uint64_t billboard_addr = child.find_first_child_by_class("BillboardGui");
								if (billboard_addr == 0)
								{
									std::vector<rbx::c_instance> part_kids = child.get_children<rbx::c_instance>();
									for (auto& pk : part_kids)
									{
										if (pk.address && pk.get_class_name() == "BillboardGui")
										{
											billboard_addr = pk.address;
											break;
										}
									}
								}

								if (billboard_addr != 0)
								{
									rbx::c_instance billboard(billboard_addr);
									std::uint64_t textlabel_addr = billboard.find_first_child_by_class("TextLabel");
									if (textlabel_addr == 0)
									{
										std::vector<rbx::c_instance> bb_kids = billboard.get_children<rbx::c_instance>();
										for (auto& bk : bb_kids)
										{
											if (bk.address && bk.get_class_name() == "TextLabel")
											{
												textlabel_addr = bk.address;
												break;
											}
										}
									}

									if (textlabel_addr != 0)
									{
										std::string fetched_name = memory->read_string(textlabel_addr + Offsets::TextLabel::Text);
										if (fetched_name == "str_error" || fetched_name.empty())
											fetched_name = memory->read_string(textlabel_addr + Offsets::GuiObject::Text);
										if (fetched_name == "str_error" || fetched_name.empty())
											fetched_name = memory->read_string(textlabel_addr + 0xe40);
										if (fetched_name != "str_error" && !fetched_name.empty())
										{
											auto line_end = fetched_name.find_first_of("\r\n");
											if (line_end != std::string::npos)
												fetched_name.resize(line_end);
											plr_name = fetched_name;
										}
									}
									if (!plr_name.empty())
									{
										auto tcit = playerserviceTeamColors.find(plr_name);
										if (tcit != playerserviceTeamColors.end())
											plr_teamColor = tcit->second;
									}
									if (plr_teamColor == 0)
									{
										auto ma_it = modelAddrTeamColor.find(model_addr);
										if (ma_it != modelAddrTeamColor.end())
											plr_teamColor = ma_it->second;
									}
									head_part = current_p;
									parts["Head"] = head_part;
									continue;
								}

								rbx::c_primitive prim = current_p.get_primitive();
								if (prim.address != 0)
								{
									math::vector3 sz = prim.get_size();
									if (sz.x < 0.1f || sz.y < 0.1f || sz.z < 0.1f) continue;
								}

								mesh_limbs.push_back(current_p);
							}
							else if (child_class == "Model" || child_class == "Folder" || child_class == "Tool")
							{
								std::string c_lower = child_name;
								std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::tolower);

								if (is_root)
								{
									static const std::unordered_set<std::string> s_char_containers = {
										"character", "body", "left arm", "right arm", "left leg", "right leg", "head", "torso",
										"leftarm", "rightarm", "leftleg", "rightleg", "bodies"
									};
									if (s_char_containers.count(c_lower) > 0)
									{
										self(self, child.address, false);
									}
								}
								else
								{
									self(self, child.address, false);
								}
							}
						}
					};

					gather_parts(gather_parts, PlayerModel.address, true);

					if (upper_torso_part.address == 0 && !mesh_limbs.empty())
					{
						// In PF, if torso wasn't explicitly found by SpotLight, find the largest part
						for (auto it = mesh_limbs.begin(); it != mesh_limbs.end(); ++it)
						{
							rbx::c_primitive prim = it->get_primitive();
							if (prim.address != 0)
							{
								math::vector3 sz = prim.get_size();
								if (sz.x >= 1.5f && sz.y >= 1.5f)
								{
									upper_torso_part = *it;
									torso_cframe = prim.get_cframe();
									has_torso_cframe = true;
									parts["Torso"] = parts["UpperTorso"] = parts["LowerTorso"] = upper_torso_part;
									mesh_limbs.erase(it);
									break;
								}
							}
						}
						if (upper_torso_part.address == 0 && !mesh_limbs.empty())
						{
							upper_torso_part = mesh_limbs.front();
							rbx::c_primitive prim = upper_torso_part.get_primitive();
							if (prim.address != 0)
							{
								torso_cframe = prim.get_cframe();
								has_torso_cframe = true;
							}
							parts["Torso"] = parts["UpperTorso"] = parts["LowerTorso"] = upper_torso_part;
							mesh_limbs.erase(mesh_limbs.begin());
						}
					}

					{
						int idx = 1;
						for (auto& limb : mesh_limbs)
							parts["pfLimb" + std::to_string(idx++)] = limb;
					}

					if (!found_limbs_by_name || is_pf)
					{
						if (has_torso_cframe && !mesh_limbs.empty())
						{
							math::matrix3 inv_rot = torso_cframe.rotation.inverse();
							struct LimbInfo { rbx::c_part part; math::vector3 rel; };
							std::vector<LimbInfo> limbs_info;
							for (auto& limb : mesh_limbs)
							{
								rbx::c_primitive prim = limb.get_primitive();
								if (prim.address == 0) continue;
								limbs_info.push_back({ limb, inv_rot * (prim.get_position() - torso_cframe.position) });
							}

							std::vector<LimbInfo> arms, legs;
							for (auto& li : limbs_info)
								(li.rel.y > -1.0f ? arms : legs).push_back(li);

							std::sort(arms.begin(), arms.end(), [](const LimbInfo& a, const LimbInfo& b) { return a.rel.x < b.rel.x; });
							std::sort(legs.begin(), legs.end(), [](const LimbInfo& a, const LimbInfo& b) { return a.rel.x < b.rel.x; });

							if (arms.size() >= 2) { parts["LeftUpperArm"] = parts["LeftArm"] = parts["Left Arm"] = arms[0].part; parts["RightUpperArm"] = parts["RightArm"] = parts["Right Arm"] = arms.back().part; }
							else if (arms.size() == 1) { if (arms[0].rel.x < 0.0f) { parts["LeftUpperArm"] = parts["Left Arm"] = arms[0].part; } else { parts["RightUpperArm"] = parts["Right Arm"] = arms[0].part; } }
							if (legs.size() >= 2) { parts["LeftUpperLeg"] = parts["LeftLeg"] = parts["Left Leg"] = legs[0].part; parts["RightUpperLeg"] = parts["RightLeg"] = parts["Right Leg"] = legs.back().part; }
							else if (legs.size() == 1) { if (legs[0].rel.x < 0.0f) { parts["LeftUpperLeg"] = parts["Left Leg"] = legs[0].part; } else { parts["RightUpperLeg"] = parts["Right Leg"] = legs[0].part; } }
						}
						else
						{
							if (mesh_limbs.size() >= 1) { parts["LeftUpperArm"] = parts["LeftLowerArm"] = parts["LeftHand"] = parts["LeftArm"] = parts["Left Arm"] = mesh_limbs[0]; }
							if (mesh_limbs.size() >= 2) { parts["RightUpperArm"] = parts["RightLowerArm"] = parts["RightHand"] = parts["RightArm"] = parts["Right Arm"] = mesh_limbs[1]; }
							if (mesh_limbs.size() >= 3) { parts["LeftUpperLeg"] = parts["LeftLowerLeg"] = parts["LeftFoot"] = parts["LeftLeg"] = parts["Left Leg"] = mesh_limbs[2]; }
							if (mesh_limbs.size() >= 4) { parts["RightUpperLeg"] = parts["RightLowerLeg"] = parts["RightFoot"] = parts["RightLeg"] = parts["Right Leg"] = mesh_limbs[3]; }
						}
					}

					if (upper_torso_part.address != 0) hrp = upper_torso_part;
					else if (head_part.address != 0) hrp = head_part;
					else if (!mesh_limbs.empty()) hrp = mesh_limbs.front();

					if (!plr_name.empty() && (plr_name == cache::local_player.name || (!cache::local_player.display_name.empty() && plr_name == cache::local_player.display_name)))
					{
						std::lock_guard<std::mutex> lock(cache::local_player_mtx);
						cache::local_player.parts = parts;
						if (hrp.address != 0) cache::local_player.humanoid_root_part = hrp;
						cache::local_player.team = plr_teamColor;

						cached_player_info_t lp_cached{};
						lp_cached.name = plr_name;
						lp_cached.display_name = plr_name;
						lp_cached.team = plr_teamColor;
						lp_cached.parts = parts;
						lp_cached.humanoid_root_part = hrp;
						lp_cached.head_part = head_part;
						lp_cached.hrp_prim_addr = get_prim_address_from_part(hrp.address);
						lp_cached.last_rescan_time = std::chrono::steady_clock::now();
						player_cache[model_addr] = std::move(lp_cached);
						continue;
					}

					bool is_pf_corpse = (pf_corpses_found.count(model_addr) > 0);
					entity_t entity{
						.instance = rbx::c_instance(model_addr),
						.team = resolve_pf_team(model_addr, plr_teamColor),
						.name = plr_name.empty() ? PlayerModel.get_name() : plr_name,
						.display_name = plr_name.empty() ? PlayerModel.get_name() : plr_name,
						.tool_name = {},
						.health = is_pf_corpse ? 0.0f : 100.0f,
						.max_health = 100.0f,
						.knocked = is_pf_corpse,
						.humanoid_root_part = hrp,
						.humanoid = rbx::c_humanoid(0),
						.parts = parts,
						.priority = player_priority::neutral
					};

					entity.hrp_prim_addr = get_prim_address_from_part(hrp.address);
					if (valid_addr(entity.hrp_prim_addr))
						entity.position = memory->read<math::vector3>(entity.hrp_prim_addr + Offsets::Primitive::Position);

					auto find_part_pf = [&](const std::initializer_list<const char*>& names) -> std::uint64_t {
						for (const char* name : names) {
							auto it = parts.find(name);
							if (it != parts.end()) {
								std::uint64_t prim = get_prim_address_from_part(it->second.address);
								if (valid_addr(prim)) return prim;
							}
						}
						return 0;
					};

					entity.head_prim_addr = find_part_pf({ "Head" });
					if (valid_addr(entity.head_prim_addr))
					{
						if (entity.position.x == 0.0f && entity.position.y == 0.0f && entity.position.z == 0.0f)
							entity.position = memory->read<math::vector3>(entity.head_prim_addr + Offsets::Primitive::Position);
						entity.part_positions.head = memory->read<math::vector3>(entity.head_prim_addr + Offsets::Primitive::Position);
					}

					entity.torso_prim_addr = find_part_pf({ "Torso", "UpperTorso", "LowerTorso" });
					if (valid_addr(entity.torso_prim_addr)) entity.part_positions.torso = memory->read<math::vector3>(entity.torso_prim_addr + Offsets::Primitive::Position);

					entity.left_arm_prim_addr = find_part_pf({ "Left Arm", "LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand" });
					if (valid_addr(entity.left_arm_prim_addr)) entity.part_positions.left_arm = memory->read<math::vector3>(entity.left_arm_prim_addr + Offsets::Primitive::Position);

					entity.right_arm_prim_addr = find_part_pf({ "Right Arm", "RightArm", "RightUpperArm", "RightLowerArm", "RightHand" });
					if (valid_addr(entity.right_arm_prim_addr)) entity.part_positions.right_arm = memory->read<math::vector3>(entity.right_arm_prim_addr + Offsets::Primitive::Position);

					entity.left_leg_prim_addr = find_part_pf({ "Left Leg", "LeftLeg", "LeftFoot", "LeftLowerLeg", "LeftUpperLeg" });
					if (valid_addr(entity.left_leg_prim_addr)) entity.part_positions.left_leg = memory->read<math::vector3>(entity.left_leg_prim_addr + Offsets::Primitive::Position);

					entity.right_leg_prim_addr = find_part_pf({ "Right Leg", "RightLeg", "RightFoot", "RightLowerLeg", "RightUpperLeg" });
					if (valid_addr(entity.right_leg_prim_addr)) entity.part_positions.right_leg = memory->read<math::vector3>(entity.right_leg_prim_addr + Offsets::Primitive::Position);

					cached_player_info_t new_cached{};
					new_cached.name = entity.name;
					new_cached.display_name = entity.display_name;
					new_cached.team = entity.team;
					new_cached.parts = entity.parts;
					new_cached.humanoid_root_part = entity.humanoid_root_part;
					new_cached.head_part = head_part;
					new_cached.hrp_prim_addr = entity.hrp_prim_addr;
					new_cached.head_prim_addr = entity.head_prim_addr;
					new_cached.torso_prim_addr = entity.torso_prim_addr;
					new_cached.left_arm_prim_addr = entity.left_arm_prim_addr;
					new_cached.right_arm_prim_addr = entity.right_arm_prim_addr;
					new_cached.left_leg_prim_addr = entity.left_leg_prim_addr;
					new_cached.right_leg_prim_addr = entity.right_leg_prim_addr;
					new_cached.is_corpse = is_pf_corpse;
					new_cached.last_rescan_time = std::chrono::steady_clock::now();
					player_cache[model_addr] = std::move(new_cached);

					temp_cache.push_back(std::move(entity));
					continue;
				}
				{
					auto& cached = cache_it->second;

					cached.team = resolve_pf_team(model_addr, 0);

					entity_t entity{
						.instance = rbx::c_instance(model_addr),
						.team = cached.team,
						.name = cached.name,
						.display_name = cached.display_name,
						.tool_name = {},
						.health = cached.is_corpse ? 0.0f : 100.0f,
						.max_health = 100.0f,
						.knocked = cached.is_corpse,
						.humanoid_root_part = cached.humanoid_root_part,
						.head_part = cached.head_part,
						.humanoid = cached.humanoid,
						.parts = cached.parts,
						.priority = player_priority::neutral,
						.hrp_prim_addr = cached.hrp_prim_addr,
						.head_prim_addr = cached.head_prim_addr,
						.torso_prim_addr = cached.torso_prim_addr,
						.left_arm_prim_addr = cached.left_arm_prim_addr,
						.right_arm_prim_addr = cached.right_arm_prim_addr,
						.left_leg_prim_addr = cached.left_leg_prim_addr,
						.right_leg_prim_addr = cached.right_leg_prim_addr
					};

					if (valid_addr(cached.hrp_prim_addr))       entity.position                  = memory->read<math::vector3>(cached.hrp_prim_addr      + Offsets::Primitive::Position);
					if (valid_addr(cached.head_prim_addr))      entity.part_positions.head       = memory->read<math::vector3>(cached.head_prim_addr     + Offsets::Primitive::Position);
					if (valid_addr(cached.torso_prim_addr))     entity.part_positions.torso      = memory->read<math::vector3>(cached.torso_prim_addr    + Offsets::Primitive::Position);
					if (valid_addr(cached.left_arm_prim_addr))  entity.part_positions.left_arm   = memory->read<math::vector3>(cached.left_arm_prim_addr  + Offsets::Primitive::Position);
					if (valid_addr(cached.right_arm_prim_addr)) entity.part_positions.right_arm  = memory->read<math::vector3>(cached.right_arm_prim_addr + Offsets::Primitive::Position);
					if (valid_addr(cached.left_leg_prim_addr))  entity.part_positions.left_leg   = memory->read<math::vector3>(cached.left_leg_prim_addr  + Offsets::Primitive::Position);
					if (valid_addr(cached.right_leg_prim_addr)) entity.part_positions.right_leg  = memory->read<math::vector3>(cached.right_leg_prim_addr + Offsets::Primitive::Position);

					temp_cache.push_back(std::move(entity));
				}
			}

			for (auto it = player_cache.begin(); it != player_cache.end();)
			{
				if (seen_players.find(it->first) == seen_players.end())
					it = player_cache.erase(it);
				else
					++it;
			}

			{
				std::lock_guard<std::mutex> rlock(cache::mtx);
				for (auto& ne : temp_cache)
				{
					for (const auto& oe : cache::players)
					{
						if (ne.instance.address != oe.instance.address) continue;
						if (ne.position.x == 0.0f && ne.position.y == 0.0f && ne.position.z == 0.0f)
							ne.position = oe.position;
						ne.rotation = oe.rotation;
						ne.velocity = oe.velocity;
						break;
					}
				}
			}

			// Dynamic Team Validation:
			// If all players in the server have the same non-zero team, team check is invalid.
			// Reset teams to 0 so everyone is treated as enemies/hostiles.
			std::unordered_map<std::uint64_t, int> team_counts;
			for (const auto& entity : temp_cache)
			{
				if (entity.team != 0)
				{
					team_counts[entity.team]++;
				}
			}
			if (team_counts.size() == 1 && temp_cache.size() >= 2)
			{
				for (auto& entity : temp_cache)
				{
					entity.team = 0;
				}
				std::lock_guard<std::mutex> lock_local(cache::local_player_mtx);
				cache::local_player.team = 0;
			}

			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				cache::players = std::move(temp_cache);
			}
			cache::publish_snaps();

			settings::performance::cache_loop_count++;
			std::this_thread::sleep_for(20ms);
			continue;
		}

		if (!is_pf)
		{
			std::vector<rbx::c_instance> team_folders;
			bool searched_team_folders = false;

			for (rbx::c_player& player : current_players_list)
			{
				auto cache_it = player_cache.find(player.address);
				bool cache_valid = true;

				if (cache_it == player_cache.end())
				{
					cache_valid = false;
				}
				else
				{
					auto& cached = cache_it->second;
					rbx::c_model_instance current_model = player.get_model_instance();
					if (current_model.address == 0 || current_model.address != cached.model_instance_address)
					{
						cache_valid = false;
					}
					else if (cached.model_instance_address == 0 || cached.humanoid.address == 0 || cached.humanoid_root_part.address == 0)
					{
						cache_valid = false;
					}
					else if (memory->read<std::uint64_t>(cached.humanoid.address + Offsets::Instance::Parent) != cached.model_instance_address)
					{
						cache_valid = false;
					}
					else if (memory->read<std::uint64_t>(cached.humanoid_root_part.address + Offsets::Instance::Parent) != cached.model_instance_address)
					{
						cache_valid = false;
					}
					else
					{
						bool has_r15 = (cached.parts.count("UpperTorso") > 0 || cached.parts.count("LowerTorso") > 0 ||
										cached.parts.count("RightUpperArm") > 0 || cached.parts.count("LeftUpperArm") > 0 ||
										cached.parts.count("RightUpperLeg") > 0 || cached.parts.count("LeftUpperLeg") > 0);

						bool has_head = (cached.parts.count("Head") > 0);
						bool has_torso = has_r15 ? (cached.parts.count("UpperTorso") > 0 && cached.parts.count("LowerTorso") > 0)
												 : (cached.parts.count("Torso") > 0);
						bool has_left_arm = has_r15 ? (cached.parts.count("LeftUpperArm") > 0)
													: (cached.parts.count("Left Arm") > 0 || cached.parts.count("LeftArm") > 0);
						bool has_right_arm = has_r15 ? (cached.parts.count("RightUpperArm") > 0)
													 : (cached.parts.count("Right Arm") > 0 || cached.parts.count("RightArm") > 0);
						bool has_left_leg = has_r15 ? (cached.parts.count("LeftUpperLeg") > 0)
													: (cached.parts.count("Left Leg") > 0 || cached.parts.count("LeftLeg") > 0);
						bool has_right_leg = has_r15 ? (cached.parts.count("RightUpperLeg") > 0)
													 : (cached.parts.count("Right Leg") > 0 || cached.parts.count("RightLeg") > 0);

						bool is_complete = has_head && has_torso && has_left_arm && has_right_arm && has_left_leg && has_right_leg;

						auto now = std::chrono::steady_clock::now();
						auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.last_rescan_time).count();

						if (!is_complete)
						{
							if (elapsed_ms > 30)
							{
								cache_valid = false;
							}
						}
						else if (elapsed_ms > 2000)
						{
							cache_valid = false;
						}
					}
				}

				if (cache_valid)
				{
					auto& cached = cache_it->second;
					float health = (cached.is_corpse || !cached.humanoid.address) ? 0.0f : cached.humanoid.get_health();
					float max_health = (cached.is_corpse || !cached.humanoid.address) ? 100.0f : cached.humanoid.get_max_health();
					bool knocked = false;
					if (cached.ko_address != 0)
					{
						knocked = memory->read<bool>(cached.ko_address + Offsets::Misc::Value);
					}
					if (cached.is_corpse) knocked = true;

					int h_state = cached.humanoid.address ? static_cast<int>(cached.humanoid.get_state()) : 0;

					std::uint64_t current_team = player.get_team();
					if (current_team == 0)
					{
						rbx::c_model_instance char_model = player.get_model_instance();
						if (char_model.address == 0 && workspace.address != 0)
						{
							rbx::c_instance bodies_f = workspace.find_first_child("bodies");
							if (bodies_f.address != 0)
							{
								char_model = rbx::c_model_instance(bodies_f.find_first_child(cached.name));
							}
						}
						if (char_model.address != 0)
						{
							if (char_model.find_first_child("blue-shirt") != 0 || char_model.find_first_child("blue-pants") != 0 || char_model.find_first_child("blue_shirt") != 0 || char_model.find_first_child("blue_pants") != 0 || char_model.find_first_child("defenders") != 0)
								current_team = 1;
							else if (char_model.find_first_child("red-shirt") != 0 || char_model.find_first_child("red-pants") != 0 || char_model.find_first_child("red_shirt") != 0 || char_model.find_first_child("red_pants") != 0 || char_model.find_first_child("attackers") != 0)
								current_team = 2;
						}
					}
					if (current_team == 0) current_team = cached.team;
					if (current_team == 0)
					{
						std::uint32_t team_color = memory->read<std::uint32_t>(player.address + Offsets::Player::TeamColor);
						if (team_color != 0 && team_color != 194)
						{
							current_team = static_cast<std::uint64_t>(team_color);
						}
					}

					temp_cache.emplace_back(entity_t{
						.instance = player,
						.team = current_team,
						.user_id = cached.user_id,
						.name = cached.name,
						.display_name = cached.display_name,
						.tool_name = cached.tool_name,
						.health = health,
						.max_health = max_health,
						.knocked = knocked,
						.humanoid_root_part = cached.humanoid_root_part,
						.head_part = cached.head_part,
						.humanoid = cached.humanoid,
						.parts = cached.parts,
						.priority = player_priority::neutral,
						.humanoid_state = h_state,
						.hrp_prim_addr = cached.hrp_prim_addr,
						.head_prim_addr = cached.head_prim_addr,
						.torso_prim_addr = cached.torso_prim_addr,
						.left_arm_prim_addr = cached.left_arm_prim_addr,
						.right_arm_prim_addr = cached.right_arm_prim_addr,
						.left_leg_prim_addr = cached.left_leg_prim_addr,
						.right_leg_prim_addr = cached.right_leg_prim_addr
					});

					if (valid_addr(cached.hrp_prim_addr))       temp_cache.back().position                  = memory->read<math::vector3>(cached.hrp_prim_addr      + Offsets::Primitive::Position);
					if (valid_addr(cached.head_prim_addr))      temp_cache.back().part_positions.head       = memory->read<math::vector3>(cached.head_prim_addr     + Offsets::Primitive::Position);
					if (valid_addr(cached.torso_prim_addr))     temp_cache.back().part_positions.torso      = memory->read<math::vector3>(cached.torso_prim_addr    + Offsets::Primitive::Position);
					if (valid_addr(cached.left_arm_prim_addr))  temp_cache.back().part_positions.left_arm   = memory->read<math::vector3>(cached.left_arm_prim_addr  + Offsets::Primitive::Position);
					if (valid_addr(cached.right_arm_prim_addr)) temp_cache.back().part_positions.right_arm  = memory->read<math::vector3>(cached.right_arm_prim_addr + Offsets::Primitive::Position);
					if (valid_addr(cached.left_leg_prim_addr))  temp_cache.back().part_positions.left_leg   = memory->read<math::vector3>(cached.left_leg_prim_addr  + Offsets::Primitive::Position);
					if (valid_addr(cached.right_leg_prim_addr)) temp_cache.back().part_positions.right_leg  = memory->read<math::vector3>(cached.right_leg_prim_addr + Offsets::Primitive::Position);
					if (temp_cache.back().position.x == 0.0f && temp_cache.back().position.y == 0.0f && temp_cache.back().position.z == 0.0f)
					{
						if (valid_addr(cached.head_prim_addr)) temp_cache.back().position = temp_cache.back().part_positions.head;
						else if (valid_addr(cached.torso_prim_addr)) temp_cache.back().position = temp_cache.back().part_positions.torso;
					}

					if (player.address == local_player.address)
					{
						cache::local_character = rbx::c_model_instance(cached.model_instance_address);
						std::lock_guard<std::mutex> lock(cache::local_player_mtx);
						temp_cache.back().position = cache::local_player.position;
						temp_cache.back().rotation = cache::local_player.rotation;
						temp_cache.back().part_positions = cache::local_player.part_positions;
						cache::local_player = temp_cache.back();
					}

					seen_players.insert(player.address);
					continue;
				}

				std::string player_name = (cache_it != player_cache.end()) ? cache_it->second.name : player.get_name();
				std::string display_name = (cache_it != player_cache.end()) ? cache_it->second.display_name : player.get_display_name();
				rbx::c_model_instance model_instance = player.get_model_instance();
				std::uint64_t model_addr = model_instance.address;
				std::uint64_t player_team = player.get_team();

				if (!searched_team_folders && characters_folder.address != 0)
				{
					team_folders = characters_folder.get_children<rbx::c_instance>();
					searched_team_folders = true;
				}

				if (!team_folders.empty())
				{
					for (rbx::c_instance& team_folder : team_folders)
					{
						if (!team_folder.address) continue;
						std::uint64_t found_char = team_folder.find_first_child(player_name);
						if (found_char != 0)
						{
							if (model_addr == 0)
							{
								model_addr = found_char;
								model_instance = rbx::c_model_instance(model_addr);
							}
							if (player_team == 0)
							{
								std::string folder_name = team_folder.get_name();
								if (folder_name == "Terrorists" || folder_name == "T") player_team = 1;
								else if (folder_name == "Counter-Terrorists" || folder_name == "CT") player_team = 2;
								else if (!folder_name.empty())
								{
									static const std::unordered_set<std::string> s_skip_folders = {
										"Workspace", "Characters", "bodies", "Bodies", "Players", "players",
										"Live", "live", "Ingame", "ingame", "Map", "map", "NPCs", "npcs",
										"Bots", "bots", "Entities", "entities", "Units", "units", "Mobs", "mobs",
										"Zombies", "zombies", "Monsters", "monsters", "Ignore", "Ignored",
										"Debris", "debris", "Ragdolls", "ragdolls", "Corpses", "corpses",
										"Dead", "dead", "Arena", "arena", "Match", "match"
									};
									if (s_skip_folders.count(folder_name) == 0)
									{
										player_team = std::hash<std::string>{}(folder_name);
									}
								}
							}
							break;
						}
					}
				}

				bool is_standard_corpse = false;
				if (model_addr == 0 && workspace.address != 0)
				{
					static const char* s_corpse_folders[] = {
						"bodies", "Bodies", "Ragdolls", "ragdolls", "Corpses", "corpses",
						"Dead", "dead", "Debris", "DebrisFolder", "Characters", "characters",
						"Players", "players", "Ignore", "Ignored"
					};
					for (const char* folder_name : s_corpse_folders)
					{
						rbx::c_instance corpse_f = workspace.find_first_child(folder_name);
						if (corpse_f.address != 0)
						{
							std::uint64_t found_char = corpse_f.find_first_child(player_name);
							if (found_char == 0 && !display_name.empty()) found_char = corpse_f.find_first_child(display_name);
							if (found_char != 0)
							{
								model_addr = found_char;
								model_instance = rbx::c_model_instance(model_addr);
								is_standard_corpse = true;
								break;
							}
						}
					}
					if (model_addr == 0)
					{
						std::uint64_t found_char = workspace.find_first_child(player_name);
						if (found_char == 0 && !display_name.empty()) found_char = workspace.find_first_child(display_name);
						if (found_char != 0)
						{
							model_addr = found_char;
							model_instance = rbx::c_model_instance(model_addr);
						}
					}
				}

				model_instance = rbx::c_model_instance(model_addr);

				if (player_team == 0 && model_addr != 0)
				{
					std::vector<rbx::c_instance> temp_children = model_instance.get_children<rbx::c_instance>();
					for (rbx::c_instance& child : temp_children)
					{
						if (!child.address) continue;
						std::string child_name = child.get_name();
						if (child_name == "blue-shirt" || child_name == "blue-pants" || child_name == "blue_shirt" || child_name == "blue_pants" || child_name == "defenders")
						{
							player_team = 1;
							break;
						}
						else if (child_name == "red-shirt" || child_name == "red-pants" || child_name == "red_shirt" || child_name == "red_pants" || child_name == "attackers")
						{
							player_team = 2;
							break;
						}
					}
				}

				if (player_team == 0 && model_addr != 0)
				{
					std::uint64_t parent_addr = model_instance.get_parent();
					if (parent_addr != 0)
					{
						rbx::c_nameable parent_nameable(parent_addr);
						std::string parent_name = parent_nameable.get_name();
						if (parent_name == "Terrorists" || parent_name == "T") player_team = 1;
						else if (parent_name == "Counter-Terrorists" || parent_name == "CT") player_team = 2;
						else if (!parent_name.empty())
						{
							static const std::unordered_set<std::string> s_skip_parents = {
								"Workspace", "Characters", "bodies", "Bodies", "Players", "players",
								"Live", "live", "Ingame", "ingame", "Map", "map", "NPCs", "npcs",
								"Bots", "bots", "Entities", "entities", "Units", "units", "Mobs", "mobs",
								"Zombies", "zombies", "Monsters", "monsters", "Ignore", "Ignored",
								"Debris", "debris", "Ragdolls", "ragdolls", "Corpses", "corpses",
								"Dead", "dead", "Arena", "arena", "Match", "match"
							};
							if (s_skip_parents.count(parent_name) == 0)
							{
								player_team = std::hash<std::string>{}(parent_name);
							}
						}
					}
				}

				if (player_team == 0)
				{
					std::uint32_t team_color = memory->read<std::uint32_t>(player.address + Offsets::Player::TeamColor);
					if (team_color != 0 && team_color != 194)
					{
						player_team = static_cast<std::uint64_t>(team_color);
					}
				}

				model_instance = rbx::c_model_instance(model_addr);
				rbx::c_humanoid humanoid(0);
				rbx::c_part humanoid_root_part(0);
				std::unordered_map<std::string, rbx::c_part> parts;
				std::uint64_t ko_address = 0;

				if (model_addr != 0)
				{
					std::vector<rbx::c_instance> children = model_instance.get_children<rbx::c_instance>();

					for (rbx::c_instance& child : children)
					{
						if (!child.address) continue;

						std::string child_name = child.get_name();
						std::string child_class = child.get_class_name();

						if (child_name == "blue-shirt" || child_name == "blue-pants" || child_name == "blue_shirt" || child_name == "blue_pants" || child_name == "defenders")
						{
							player_team = 1;
						}
						else if (child_name == "red-shirt" || child_name == "red-pants" || child_name == "red_shirt" || child_name == "red_pants" || child_name == "attackers")
						{
							player_team = 2;
						}

						if (child_name == "Humanoid")
						{
							humanoid = rbx::c_humanoid(child.address);
						}
						else if (child_name == "HumanoidRootPart")
						{
							humanoid_root_part = rbx::c_part(child.address);
						}
						else if (child_name == "BodyEffects")
						{
							rbx::c_instance ko = child.find_first_child("K.O");
							if (ko.address != 0)
							{
								ko_address = ko.address;
							}
						}

						bool is_acc = (child_class.find("Accessory") != std::string::npos || child_class.find("Accoutrement") != std::string::npos || child_class.find("Hat") != std::string::npos);
						if (is_acc)
						{
							std::uint64_t handle_addr = child.find_first_child("Handle");
							if (handle_addr != 0)
							{
								parts["Accessory_" + child_name] = rbx::c_part(handle_addr);
							}
							int sub_idx = 0;
							for (auto acc_child : child.get_children<rbx::c_instance>())
							{
								if (!acc_child.address) continue;
								if (handle_addr == 0 && (acc_child.get_class_name().find("Part") != std::string::npos || acc_child.get_class_name() == "MeshPart"))
								{
									handle_addr = acc_child.address;
									parts["Accessory_" + child_name] = rbx::c_part(handle_addr);
									continue;
								}
								if (acc_child.address == handle_addr) continue;

								std::string acc_cls = acc_child.get_class_name();
								if (acc_cls.find("Accessory") == std::string::npos && (acc_cls.find("Part") != std::string::npos || acc_cls == "UnionOperation" || acc_cls == "BasePart" || acc_cls == "MeshPart"))
								{
									std::string key = "Accessory_" + child_name + "_" + acc_child.get_name() + (sub_idx > 0 ? ("_" + std::to_string(sub_idx)) : "");
									parts[key] = rbx::c_part(acc_child.address);
									sub_idx++;
								}
								else if (acc_cls == "Model" || acc_cls == "Folder")
								{
									for (auto nested_child : acc_child.get_children<rbx::c_instance>())
									{
										if (!nested_child.address) continue;
										std::string n_cls = nested_child.get_class_name();
										if (n_cls.find("Accessory") == std::string::npos && (n_cls.find("Part") != std::string::npos || n_cls == "UnionOperation" || n_cls == "BasePart" || n_cls == "MeshPart"))
										{
											std::string key = "Accessory_" + child_name + "_" + nested_child.get_name() + "_" + std::to_string(sub_idx++);
											parts[key] = rbx::c_part(nested_child.address);
										}
									}
								}
							}
						}
						else if (child_class.find("Part") != std::string::npos || child_class == "UnionOperation" || child_class == "BasePart" || child_class == "MeshPart")
						{
							// Skip weapon/tool parts that get welded directly into the character
							static const std::unordered_set<std::string> s_weapon_part_names = {
								"Handle", "Muzzle", "Barrel", "Grip", "Blade", "Primary",
								"Flash", "Sight", "Stock", "Mag", "Magazine", "Trigger",
								"_center", "_grip", "FrontSight", "BackSight", "Suppressor",
								"Shield", "Bullet", "Projectile", "Trail", "Effect"
							};
							if (s_weapon_part_names.count(child_name) == 0)
							{
								parts[child_name] = rbx::c_part(child.address);
							}
						}
						else if (child_class == "Model" || child_class == "Folder")
						{
							// Skip model/folder children that look like weapon/tool models
							static const std::unordered_set<std::string> s_weapon_child_indicators = {
								"Handle", "Muzzle", "Barrel", "Grip", "Blade", "Primary", "Flash", "_center", "_grip"
							};
							static const std::unordered_set<std::string> s_safe_model_names = {
								"R15ArtistIntent", "Character", "BodyParts", "Animations", "AnimationsV2",
								"Body", "HumanoidDescription"
							};

							bool is_weapon_model = false;
							if (s_safe_model_names.count(child_name) == 0)
							{
								// Check if this sub-model has any weapon-indicator children
								for (const auto& indicator : s_weapon_child_indicators)
								{
									if (child.find_first_child(indicator) != 0)
									{
										is_weapon_model = true;
										break;
									}
								}
							}

							if (is_weapon_model) { /* skip – weapon model, not body part */ }
							else
							{
							for (auto m_child : child.get_children<rbx::c_instance>())
							{
								if (!m_child.address) continue;
								std::string m_cls = m_child.get_class_name();
								if (m_cls.find("Accessory") != std::string::npos || m_cls.find("Accoutrement") != std::string::npos || m_cls.find("Hat") != std::string::npos)
								{
									std::uint64_t sub_h = m_child.find_first_child("Handle");
									if (sub_h != 0) parts["Accessory_" + m_child.get_name()] = rbx::c_part(sub_h);
									for (auto sub_part : m_child.get_children<rbx::c_instance>())
									{
										if (!sub_part.address || sub_part.address == sub_h) continue;
										std::string sp_cls = sub_part.get_class_name();
										if (sp_cls.find("Accessory") == std::string::npos && (sp_cls.find("Part") != std::string::npos || sp_cls == "UnionOperation" || sp_cls == "BasePart" || sp_cls == "MeshPart"))
										{
											parts["Accessory_" + m_child.get_name() + "_" + sub_part.get_name()] = rbx::c_part(sub_part.address);
										}
									}
								}
								else if (m_cls.find("Part") != std::string::npos || m_cls == "UnionOperation" || m_cls == "BasePart" || m_cls == "MeshPart")
								{
									std::string m_name = m_child.get_name();
									if (((child_name == "R15ArtistIntent" || child_name == "Character" || child_name == "BodyParts") || is_mesh_part_name(m_name)) && parts.find(m_name) == parts.end())
									{
										parts[m_name] = rbx::c_part(m_child.address);
									}
									else
									{
										parts["Accessory_" + child_name + "_" + m_name] = rbx::c_part(m_child.address);
									}
								}
							}
							} // end is_weapon_model else
						}
					}
				}

				if (workspace.address != 0)
				{
					std::string p_name = player.get_name();
					rbx::c_instance merc_folder = workspace.find_first_child("MercPlayers");
					rbx::c_instance search_scope = (merc_folder.address != 0) ? merc_folder : workspace;

					std::uint64_t merc_hb_addr = search_scope.find_first_child("MercHitboxes_" + p_name);
					if (merc_hb_addr != 0)
					{
						rbx::c_model_instance merc_hb_model(merc_hb_addr);
						std::vector<rbx::c_instance> hb_children = merc_hb_model.get_children<rbx::c_instance>();
						for (rbx::c_instance& child : hb_children)
						{
							if (!child.address) continue;
							std::string child_name = child.get_name();
							std::string child_class = child.get_class_name();
							if (child_class.find("Part") != std::string::npos || child_class == "UnionOperation" || child_class == "BasePart")
							{
								parts[child_name] = rbx::c_part(child.address);
							}
						}
					}
				}

				if (humanoid_root_part.address == 0)
				{
					if (parts.count("LowerTorso") > 0) humanoid_root_part = parts["LowerTorso"];
					else if (parts.count("UpperTorso") > 0) humanoid_root_part = parts["UpperTorso"];
					else if (parts.count("Head") > 0) humanoid_root_part = parts["Head"];
				}

				bool knocked = false;
				if (ko_address != 0)
				{
					knocked = memory->read<bool>(ko_address + Offsets::Misc::Value);
				}

				std::string tool_name = find_tool_name(model_instance);
				if (tool_name.empty() && workspace.address != 0)
				{
					std::string p_name = player.get_name();
					rbx::c_instance merc_folder = workspace.find_first_child("MercPlayers");
					rbx::c_instance search_scope = (merc_folder.address != 0) ? merc_folder : workspace;
					std::uint64_t merc_vis_addr = search_scope.find_first_child("MercVisual_" + p_name);
					if (merc_vis_addr != 0)
					{
						rbx::c_model_instance merc_vis_model(merc_vis_addr);
						std::vector<rbx::c_instance> vis_children = merc_vis_model.get_children<rbx::c_instance>();
						for (rbx::c_instance& child : vis_children)
						{
							if (!child.address) continue;
							if (child.get_class_name() == "Model")
							{
								std::string mname = child.get_name();
								if (mname != "Animations" && mname != "AnimationsV2" && mname.find("Injury") == std::string::npos)
								{
									tool_name = mname;
									break;
								}
							}
						}
					}
				}

				std::uint64_t hrp_prim = get_prim_address_from_part(humanoid_root_part.address);
				
				auto find_part_addr = [&](const std::initializer_list<const char*>& names) -> std::uint64_t {
					for (const char* name : names) {
						auto it = parts.find(name);
						if (it != parts.end()) {
							std::uint64_t prim = get_prim_address_from_part(it->second.address);
							if (valid_addr(prim)) return prim;
						}
					}
					return 0;
				};

				auto head_it = parts.find("Head");
				std::uint64_t head_prim = find_part_addr({ "Head" });
				std::uint64_t torso_prim = find_part_addr({ "Torso", "UpperTorso", "LowerTorso" });
				std::uint64_t left_arm_prim = find_part_addr({ "Left Arm", "LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand" });
				std::uint64_t right_arm_prim = find_part_addr({ "Right Arm", "RightArm", "RightUpperArm", "RightLowerArm", "RightHand" });
				std::uint64_t left_leg_prim = find_part_addr({ "Left Leg", "LeftLeg", "LeftFoot", "LeftLowerLeg", "LeftUpperLeg" });
				std::uint64_t right_leg_prim = find_part_addr({ "Right Leg", "RightLeg", "RightFoot", "RightLowerLeg", "RightUpperLeg" });

				std::uint64_t player_user_id = memory->read<std::uint64_t>(player.address + Offsets::Player::UserId);

				cached_player_info_t new_cached
				{
					.model_instance_address = model_addr,
					.team = player_team,
					.user_id = player_user_id,
					.name = player.get_name(),
					.display_name = player.get_display_name(),
					.tool_name = tool_name,
					.humanoid = humanoid,
					.humanoid_root_part = humanoid_root_part,
					.parts = parts,
					.ko_address = ko_address,
					.last_rescan_time = std::chrono::steady_clock::now(),
					.hrp_prim_addr = hrp_prim,
					.head_prim_addr = head_prim,
					.torso_prim_addr = torso_prim,
					.left_arm_prim_addr = left_arm_prim,
					.right_arm_prim_addr = right_arm_prim,
					.left_leg_prim_addr = left_leg_prim,
					.right_leg_prim_addr = right_leg_prim,
					.is_corpse = is_standard_corpse
				};

				rbx::c_part head_part(0);
				if (head_it != parts.end()) head_part = head_it->second;

				new_cached.head_part = head_part;

				std::string mm2_role = "Innocent";
				if (game::datamodel && game::datamodel->address != 0 && game::datamodel->is_mm2()) {
					auto is_murderer_or_sheriff = [](uint64_t instance_addr, const std::string& name_to_find) -> bool {
						if (!instance_addr) return false;
						rbx::c_instance inst(instance_addr);
						if (inst.find_first_child(name_to_find)) return true;
						for (auto child : inst.get_children<rbx::c_instance>()) {
							if (!child.address) continue;
							std::string cls = child.get_class_name();
							if (cls == "Model" || cls == "Folder" || cls == "Accessory" || cls == "Tool") {
								if (child.get_name() == name_to_find) return true;
								if (child.find_first_child(name_to_find)) return true;
								for (auto grandchild : child.get_children<rbx::c_instance>()) {
									if (!grandchild.address) continue;
									if (grandchild.get_name() == name_to_find) return true;
								}
							}
						}
						return false;
					};

					uint64_t backpack = player.find_first_child("Backpack");
					if (backpack) {
						if (is_murderer_or_sheriff(backpack, "Knife")) {
							mm2_role = "Murderer";
						} else if (is_murderer_or_sheriff(backpack, "Gun")) {
							mm2_role = "Sheriff";
						}
					}
					if (mm2_role == "Innocent" && model_addr) {
						if (is_murderer_or_sheriff(model_addr, "Knife")) {
							mm2_role = "Murderer";
						} else if (is_murderer_or_sheriff(model_addr, "Gun")) {
							mm2_role = "Sheriff";
						}
					}
				}

				entity_t entity{
					.instance = player,
					.team = new_cached.team,
					.user_id = player_user_id,
					.name = new_cached.name,
					.display_name = new_cached.display_name,
					.tool_name = std::move(tool_name),
					.health = (is_standard_corpse || !humanoid.address) ? 0.0f : humanoid.get_health(),
					.max_health = (is_standard_corpse || !humanoid.address) ? 100.0f : humanoid.get_max_health(),
					.knocked = is_standard_corpse ? true : knocked,
					.humanoid_root_part = humanoid_root_part,
					.mm2_role = mm2_role,
					.head_part = head_part,
					.humanoid = humanoid,
					.parts = parts,
					.priority = player_priority::neutral,
					.hrp_prim_addr = hrp_prim,
					.head_prim_addr = head_prim,
					.torso_prim_addr = torso_prim,
					.left_arm_prim_addr = left_arm_prim,
					.right_arm_prim_addr = right_arm_prim,
					.left_leg_prim_addr = left_leg_prim,
					.right_leg_prim_addr = right_leg_prim
				};

				if (valid_addr(hrp_prim))       entity.position                  = memory->read<math::vector3>(hrp_prim      + Offsets::Primitive::Position);
				if (valid_addr(head_prim))      entity.part_positions.head       = memory->read<math::vector3>(head_prim     + Offsets::Primitive::Position);
				if (valid_addr(torso_prim))     entity.part_positions.torso      = memory->read<math::vector3>(torso_prim    + Offsets::Primitive::Position);
				if (valid_addr(left_arm_prim))  entity.part_positions.left_arm   = memory->read<math::vector3>(left_arm_prim  + Offsets::Primitive::Position);
				if (valid_addr(right_arm_prim)) entity.part_positions.right_arm  = memory->read<math::vector3>(right_arm_prim + Offsets::Primitive::Position);
				if (valid_addr(left_leg_prim))  entity.part_positions.left_leg   = memory->read<math::vector3>(left_leg_prim  + Offsets::Primitive::Position);
				if (valid_addr(right_leg_prim)) entity.part_positions.right_leg  = memory->read<math::vector3>(right_leg_prim + Offsets::Primitive::Position);
				if (entity.position.x == 0.0f && entity.position.y == 0.0f && entity.position.z == 0.0f)
				{
					if (valid_addr(head_prim)) entity.position = entity.part_positions.head;
					else if (valid_addr(torso_prim)) entity.position = entity.part_positions.torso;
				}

				player_cache[player.address] = std::move(new_cached);
				seen_players.insert(player.address);

				if (player.address == local_player.address)
				{
					std::lock_guard<std::mutex> lock(cache::local_player_mtx);
					entity.position = cache::local_player.position;
					entity.rotation = cache::local_player.rotation;
					entity.part_positions = cache::local_player.part_positions;
					cache::local_player = entity;
				}

				temp_cache.push_back(std::move(entity));
			}
		}

		if (!is_pf)
		{
			std::unordered_set<std::uint64_t> player_addresses;
			std::uint64_t local_char_addr = cache::local_character.address;
			for (auto& e : temp_cache)
				player_addresses.insert(e.instance.address);

			std::vector<cache::entity_t> npc_temp;

			std::vector<std::string> paths_to_process(settings::playerlist::active_npc_paths.begin(), settings::playerlist::active_npc_paths.end());
			if (settings::misc::bot_support)
			{
				if (settings::misc::only_workspace_bots)
				{
					if (std::find(paths_to_process.begin(), paths_to_process.end(), "Workspace.Bots") == paths_to_process.end())
					{
						paths_to_process.push_back("Workspace.Bots");
					}
				}
				else
				{
					static const std::vector<std::string> default_bot_containers = {
						"Workspace",
						"Workspace.Live",
						"Workspace.Bots",
						"Workspace.Mobs",
						"Workspace.NPCs",
						"Workspace.Enemies",
						"Workspace.Zombies",
						"Workspace.Monsters",
						"Workspace.Entities",
						"Workspace.Units",
						"Workspace.Characters",
						"Workspace.Map",
						"Workspace.Map.NPCs",
						"Workspace.Map.Bots",
						"Workspace.Ingame"
					};
					for (const auto& path : default_bot_containers)
					{
						if (std::find(paths_to_process.begin(), paths_to_process.end(), path) == paths_to_process.end())
						{
							paths_to_process.push_back(path);
						}
					}
				}
			}

			if (!paths_to_process.empty())
			{
				for (const std::string& npc_path_str : paths_to_process)
				{
					if (npc_path_str.empty()) continue;

					rbx::c_instance container(0);
					if (game::datamodel && game::datamodel->address != 0)
					{
						std::string path_to_use = npc_path_str;
						std::string datamodel_name = game::datamodel->get_name();
						if (!datamodel_name.empty() && path_to_use.find(datamodel_name + ".") == 0)
						{
							path_to_use = path_to_use.substr(datamodel_name.length() + 1);
						}
						container = find_instance_by_path(*game::datamodel, path_to_use);
					}

					if (container.address != 0)
					{
						std::vector<rbx::c_instance> children = container.get_children<rbx::c_instance>();

						for (rbx::c_instance& child : children)
						{
							if (!child.address || player_addresses.count(child.address) > 0 || child.address == local_char_addr)
								continue;

							auto npc_cache_it = npc_cache.find(child.address);
							bool npc_cache_valid = true;
							if (npc_cache_it == npc_cache.end() || npc_cache_it->second.model_instance_address != child.address)
							{
								npc_cache_valid = false;
							}
							else
							{
								auto& cached = npc_cache_it->second;
								if (cached.humanoid.address != 0 && memory->read<std::uint64_t>(cached.humanoid.address + Offsets::Instance::Parent) != child.address)
									npc_cache_valid = false;
								else if (cached.humanoid_root_part.address != 0 && memory->read<std::uint64_t>(cached.humanoid_root_part.address + Offsets::Instance::Parent) != child.address)
									npc_cache_valid = false;
								else
								{
									auto head_it = cached.parts.find("Head");
									if (head_it != cached.parts.end() && head_it->second.address != 0 && memory->read<std::uint64_t>(head_it->second.address + Offsets::Instance::Parent) != child.address)
										npc_cache_valid = false;
								}

								if (npc_cache_valid && cached.humanoid.address != 0)
								{
									bool is_r15 = (cached.parts.count("UpperTorso") > 0);
									size_t expected_parts = is_r15 ? 16 : 7;
									if (cached.parts.size() < expected_parts)
									{
										auto now = std::chrono::steady_clock::now();
										if (std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.last_rescan_time).count() > 1000)
										{
											npc_cache_valid = false;
										}
									}
								}
							}

							if (npc_cache_valid)
							{
								auto& cached = npc_cache_it->second;
								float health = cached.humanoid.address ? cached.humanoid.get_health() : 0.0f;
								float max_health = cached.humanoid.address ? cached.humanoid.get_max_health() : 0.0f;
								bool knocked = false;
								if (cached.ko_address != 0)
								{
									knocked = memory->read<bool>(cached.ko_address + Offsets::Misc::Value);
								}

								npc_temp.emplace_back(entity_t{
									.instance = child,
									.team = 0,
									.name = cached.name,
									.display_name = cached.display_name,
									.tool_name = {},
									.health = health,
									.max_health = max_health,
									.knocked = knocked,
									.is_custom = true,
									.custom_path = npc_path_str,
									.humanoid_root_part = cached.humanoid_root_part,
									.humanoid = cached.humanoid,
									.parts = cached.parts,
									.priority = player_priority::neutral,
									.hrp_prim_addr = cached.hrp_prim_addr,
									.head_prim_addr = cached.head_prim_addr,
									.torso_prim_addr = cached.torso_prim_addr,
									.left_arm_prim_addr = cached.left_arm_prim_addr,
									.right_arm_prim_addr = cached.right_arm_prim_addr,
									.left_leg_prim_addr = cached.left_leg_prim_addr,
									.right_leg_prim_addr = cached.right_leg_prim_addr
								});

								seen_npcs.insert(child.address);
								continue;
							}

							std::string child_name = child.get_name();
							std::string child_class = child.get_class_name();

							if (child_name.empty()) child_name = "Unknown";

							std::vector<rbx::c_part> parts_to_cache;

							if (is_part_class(child_class))
							{
								parts_to_cache.push_back(rbx::c_part(child.address));
							}
							else
							{
								std::vector<rbx::c_part> child_parts = child.get_children<rbx::c_part>();
								if (!child_parts.empty())
								{
									for (rbx::c_part& part : child_parts)
									{
										if (part.address && is_part_class(part.get_class_name()))
										{
											parts_to_cache.push_back(part);
										}
									}
								}
								else
								{
									if (is_part_class(child_class))
									{
										parts_to_cache.push_back(rbx::c_part(child.address));
									}
								}
							}

							if (parts_to_cache.empty()) continue;

							rbx::c_humanoid humanoid = child.find_first_child("Humanoid");
							bool has_humanoid = (humanoid.address != 0);

							rbx::c_part humanoid_root_part(0);
							std::unordered_map<std::string, rbx::c_part> parts;
							std::uint64_t ko_address = 0;

							if (has_humanoid)
							{
								rbx::c_instance body_effects = child.find_first_child("BodyEffects");
								if (body_effects.address != 0)
								{
									rbx::c_instance ko = body_effects.find_first_child("K.O");
									if (ko.address != 0) ko_address = ko.address;
								}
							}

							for (rbx::c_part& part : parts_to_cache)
							{
								if (!part.address) continue;
								std::string part_name = part.get_name();
								std::string part_class_name = part.get_class_name();

								if (part_name == "Humanoid" && has_humanoid) humanoid = rbx::c_humanoid(part.address);
								if (part_name == "HumanoidRootPart") humanoid_root_part = rbx::c_part(part.address);
								if (is_part_class(part_class_name))
								{
									if (part_name.empty()) part_name = "Part";
									parts[part_name] = part;
								}
							}

							if (humanoid_root_part.address == 0 && !parts.empty())
							{
								if (parts.count("Head") > 0) humanoid_root_part = parts["Head"];
								else if (parts.count("Torso") > 0) humanoid_root_part = parts["Torso"];
								else if (parts.count("UpperTorso") > 0) humanoid_root_part = parts["UpperTorso"];
								else humanoid_root_part = parts.begin()->second;
							}

							bool knocked = false;
							if (ko_address != 0) knocked = memory->read<bool>(ko_address + Offsets::Misc::Value);

							std::uint64_t hrp_prim = get_prim_address_from_part(humanoid_root_part.address);
							
							std::uint64_t head_prim = 0, torso_prim = 0, left_arm_prim = 0, right_arm_prim = 0, left_leg_prim = 0, right_leg_prim = 0;
							
							auto head_it = parts.find("Head");
							if (head_it != parts.end()) head_prim = get_prim_address_from_part(head_it->second.address);

							auto torso_it = parts.find("Torso");
							if (torso_it != parts.end()) torso_prim = get_prim_address_from_part(torso_it->second.address);
							else {
								auto ut_it = parts.find("UpperTorso");
								if (ut_it != parts.end()) torso_prim = get_prim_address_from_part(ut_it->second.address);
							}

							auto la_it = parts.find("LeftArm");
							if (la_it != parts.end()) left_arm_prim = get_prim_address_from_part(la_it->second.address);
							else {
								auto lua_it = parts.find("LeftUpperArm");
								if (lua_it != parts.end()) left_arm_prim = get_prim_address_from_part(lua_it->second.address);
							}

							auto ra_it = parts.find("RightArm");
							if (ra_it != parts.end()) right_arm_prim = get_prim_address_from_part(ra_it->second.address);
							else {
								auto rua_it = parts.find("RightUpperArm");
								if (rua_it != parts.end()) right_arm_prim = get_prim_address_from_part(rua_it->second.address);
							}

							auto ll_it = parts.find("LeftLeg");
							if (ll_it != parts.end()) left_leg_prim = get_prim_address_from_part(ll_it->second.address);
							else {
								auto lul_it = parts.find("LeftUpperLeg");
								if (lul_it != parts.end()) left_leg_prim = get_prim_address_from_part(lul_it->second.address);
							}

							auto rl_it = parts.find("RightLeg");
							if (rl_it != parts.end()) right_leg_prim = get_prim_address_from_part(rl_it->second.address);
							else {
								auto rul_it = parts.find("RightUpperLeg");
								if (rul_it != parts.end()) right_leg_prim = get_prim_address_from_part(rul_it->second.address);
							}

							cached_player_info_t new_npc
							{
								.model_instance_address = child.address,
								.team = 0,
								.name = child_name,
								.display_name = child_name,
								.tool_name = {},
								.humanoid = humanoid,
								.humanoid_root_part = humanoid_root_part,
								.parts = parts,
								.ko_address = ko_address,
								.last_rescan_time = std::chrono::steady_clock::now(),
								.hrp_prim_addr = hrp_prim,
								.head_prim_addr = head_prim,
								.torso_prim_addr = torso_prim,
								.left_arm_prim_addr = left_arm_prim,
								.right_arm_prim_addr = right_arm_prim,
								.left_leg_prim_addr = left_leg_prim,
								.right_leg_prim_addr = right_leg_prim
							};

							entity_t entity
							{
								.instance = child,
								.team = 0,
								.name = child_name,
								.display_name = child_name,
								.tool_name = {},
								.health = has_humanoid ? humanoid.get_health() : 0.0f,
								.max_health = has_humanoid ? humanoid.get_max_health() : 0.0f,
								.knocked = knocked,
								.is_custom = true,
								.custom_path = npc_path_str,
								.humanoid_root_part = humanoid_root_part,
								.humanoid = humanoid,
								.parts = parts,
								.priority = player_priority::neutral,
								.hrp_prim_addr = hrp_prim,
								.head_prim_addr = head_prim,
								.torso_prim_addr = torso_prim,
								.left_arm_prim_addr = left_arm_prim,
								.right_arm_prim_addr = right_arm_prim,
								.left_leg_prim_addr = left_leg_prim,
								.right_leg_prim_addr = right_leg_prim
							};

							npc_cache[child.address] = std::move(new_npc);
							seen_npcs.insert(child.address);
							npc_temp.push_back(std::move(entity));
						}
					}
				}
			}

			for (auto& npc : npc_temp)
			{
				temp_cache.push_back(std::move(npc));
			}
		}

		for (auto it = player_cache.begin(); it != player_cache.end();)
		{
			if (seen_players.find(it->first) == seen_players.end())
				it = player_cache.erase(it);
			else
				++it;
		}

		for (auto it = npc_cache.begin(); it != npc_cache.end();)
		{
			if (seen_npcs.find(it->first) == seen_npcs.end())
				it = npc_cache.erase(it);
			else
				++it;
		}

			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				if (!temp_cache.empty() && !cache::players.empty())
				{
					static std::unordered_map<std::uint64_t, std::size_t> old_index;
					old_index.clear();
					old_index.reserve(cache::players.size() * 2);
					for (std::size_t i = 0; i < cache::players.size(); ++i)
						old_index.emplace(cache::players[i].instance.address, i);

					for (auto& new_entity : temp_cache)
					{
						auto it = old_index.find(new_entity.instance.address);
						if (it != old_index.end())
						{
							const auto& old_entity = cache::players[it->second];
							if (new_entity.position.x == 0.0f && new_entity.position.y == 0.0f && new_entity.position.z == 0.0f)
								new_entity.position = old_entity.position;
							new_entity.rotation = old_entity.rotation;
							new_entity.velocity = old_entity.velocity;
							if (new_entity.part_positions.head.x == 0.0f && new_entity.part_positions.head.y == 0.0f && new_entity.part_positions.head.z == 0.0f)
								new_entity.part_positions = old_entity.part_positions;
						}
					}
				}

				// Dynamic Team Validation:
				// If all players in the server have the same non-zero team, team check is invalid.
				// Reset teams to 0 so everyone is treated as enemies/hostiles.
				std::unordered_map<std::uint64_t, int> team_counts;
				for (const auto& entity : temp_cache)
				{
					if (entity.team != 0)
					{
						team_counts[entity.team]++;
					}
				}
				if (team_counts.size() == 1 && temp_cache.size() >= 2)
				{
					for (auto& entity : temp_cache)
					{
						entity.team = 0;
					}
					std::lock_guard<std::mutex> lock_local(cache::local_player_mtx);
					cache::local_player.team = 0;
				}

				cache::players = std::move(temp_cache);
				temp_cache.clear();
			}
			cache::publish_snaps();

		settings::performance::cache_loop_count++;
		std::this_thread::sleep_for(100ms);
	}
}

void cache::run_transform_cache()
{
	using namespace std::chrono_literals;

	while (true)
	{
		if (rescan::is_rescanning)
		{
			std::this_thread::sleep_for(50ms);
			continue;
		}

		struct transform_entry_t { std::uint64_t player_addr; std::uint64_t hrp_prim_addr; };
		std::vector<transform_entry_t> hrp_list;

		{
			std::lock_guard<std::mutex> lk(cache::mtx);
			hrp_list.reserve(cache::players.size());
			for (auto& p : cache::players)
			{
				if (p.hrp_prim_addr != 0)
					hrp_list.push_back({ p.instance.address, p.hrp_prim_addr });
			}
		}

		struct transform_result_t { std::uint64_t player_addr; math::cframe cf; };
		std::vector<transform_result_t> results;
		results.reserve(hrp_list.size());

		for (auto& entry : hrp_list)
		{
			math::cframe cf = memory->read<math::cframe>(entry.hrp_prim_addr + Offsets::Primitive::Rotation);
			results.push_back({ entry.player_addr, cf });
		}

		{
			std::lock_guard<std::mutex> lk(cache::mtx);
			for (size_t i = 0; i < results.size() && i < cache::players.size(); ++i)
			{
				auto& r = results[i];
				auto& p = cache::players[i];
				if (p.instance.address == r.player_addr)
				{
					p.rotation = r.cf;
				}
				else
				{
					for (auto& pl : cache::players)
					{
						if (pl.instance.address == r.player_addr)
						{
							pl.rotation = r.cf;
							break;
						}
					}
				}
			}
		}

		{
			std::uint64_t lp_hrp_prim = 0;
			{
				std::lock_guard<std::mutex> lk(cache::local_player_mtx);
				lp_hrp_prim = cache::local_player.hrp_prim_addr;
			}
			if (lp_hrp_prim != 0)
			{
				math::cframe cf = memory->read<math::cframe>(lp_hrp_prim + Offsets::Primitive::Rotation);
				std::lock_guard<std::mutex> lk(cache::local_player_mtx);
				cache::local_player.rotation = cf;
			}
		}

		std::this_thread::sleep_for(30ms);
	}
}

void cache::run_position_cache()
{
	using namespace std::chrono_literals;

	static int loop_counter = 0;

	while (true)
	{
		if (rescan::is_rescanning)
		{
			std::this_thread::sleep_for(50ms);
			continue;
		}

		loop_counter++;
		const bool update_limbs = true;

		struct pos_entry_t {
			std::uint64_t player_addr;
			std::uint64_t hrp_prim;
			std::uint64_t head_prim;
			std::uint64_t torso_prim;
			std::uint64_t left_arm_prim;
			std::uint64_t right_arm_prim;
			std::uint64_t left_leg_prim;
			std::uint64_t right_leg_prim;
		};
		static thread_local std::vector<pos_entry_t> prim_list;
		prim_list.clear();

		{
			std::lock_guard<std::mutex> lk(cache::mtx);
			prim_list.reserve(cache::players.size());
			for (auto& p : cache::players)
			{
				prim_list.push_back({
					p.instance.address,
					p.hrp_prim_addr,
					p.head_prim_addr,
					p.torso_prim_addr,
					p.left_arm_prim_addr,
					p.right_arm_prim_addr,
					p.left_leg_prim_addr,
					p.right_leg_prim_addr
				});
			}
		}

		struct pos_result_t {
			std::uint64_t player_addr;
			math::vector3 hrp_pos{};
			math::vector3 velocity{};
			math::vector3 head_pos{};
			math::vector3 torso_pos{};
			math::vector3 left_arm_pos{};
			math::vector3 right_arm_pos{};
			math::vector3 left_leg_pos{};
			math::vector3 right_leg_pos{};
		};
		static thread_local std::vector<pos_result_t> results;
		results.clear();
		results.reserve(prim_list.size());

		auto is_valid_vec = [](const math::vector3& v) -> bool {
			return (v.x != 0.0f || v.y != 0.0f || v.z != 0.0f) &&
				   !std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z) &&
				   !std::isinf(v.x) && !std::isinf(v.y) && !std::isinf(v.z) &&
				   (std::abs(v.x) < 500000.0f && std::abs(v.y) < 500000.0f && std::abs(v.z) < 500000.0f);
		};

		for (auto& entry : prim_list)
		{
			pos_result_t res{ entry.player_addr };

			struct pos_vel_pair_t { math::vector3 pos; math::vector3 vel; };

			if (entry.hrp_prim >= 0x10000 && entry.hrp_prim < 0x7FFFFFFFFFFFull) {
				pos_vel_pair_t pv = memory->read<pos_vel_pair_t>(entry.hrp_prim + Offsets::Primitive::Position);
				res.hrp_pos = pv.pos;
				res.velocity = pv.vel;
			}
			if (update_limbs) {
				if (entry.head_prim >= 0x10000 && entry.head_prim < 0x7FFFFFFFFFFFull) {
					res.head_pos = memory->read<math::vector3>(entry.head_prim + Offsets::Primitive::Position);
				}
				if (entry.torso_prim >= 0x10000 && entry.torso_prim < 0x7FFFFFFFFFFFull) {
					res.torso_pos = memory->read<math::vector3>(entry.torso_prim + Offsets::Primitive::Position);
				}
				if (entry.left_arm_prim >= 0x10000 && entry.left_arm_prim < 0x7FFFFFFFFFFFull) {
					res.left_arm_pos = memory->read<math::vector3>(entry.left_arm_prim + Offsets::Primitive::Position);
				}
				if (entry.right_arm_prim >= 0x10000 && entry.right_arm_prim < 0x7FFFFFFFFFFFull) {
					res.right_arm_pos = memory->read<math::vector3>(entry.right_arm_prim + Offsets::Primitive::Position);
				}
				if (entry.left_leg_prim >= 0x10000 && entry.left_leg_prim < 0x7FFFFFFFFFFFull) {
					res.left_leg_pos = memory->read<math::vector3>(entry.left_leg_prim + Offsets::Primitive::Position);
				}
				if (entry.right_leg_prim >= 0x10000 && entry.right_leg_prim < 0x7FFFFFFFFFFFull) {
					res.right_leg_pos = memory->read<math::vector3>(entry.right_leg_prim + Offsets::Primitive::Position);
				}
			}
			
			results.push_back(res);
		}

		{
			std::lock_guard<std::mutex> lk(cache::mtx);
			static std::unordered_map<std::uint64_t, std::size_t> player_index;
			player_index.clear();
			player_index.reserve(cache::players.size() * 2);
			for (std::size_t i = 0; i < cache::players.size(); ++i)
				player_index.emplace(cache::players[i].instance.address, i);

			for (size_t i = 0; i < results.size(); ++i)
			{
				auto& r = results[i];
				auto idx_it = player_index.find(r.player_addr);
				if (idx_it == player_index.end())
					continue;
				auto& p = cache::players[idx_it->second];

				if (is_valid_vec(r.hrp_pos)) p.position = r.hrp_pos;
				if (is_valid_vec(r.velocity)) p.velocity = r.velocity;
				if (is_valid_vec(r.head_pos)) p.part_positions.head = r.head_pos;
				if (is_valid_vec(r.torso_pos)) p.part_positions.torso = r.torso_pos;
				if (is_valid_vec(r.left_arm_pos)) p.part_positions.left_arm = r.left_arm_pos;
				if (is_valid_vec(r.right_arm_pos)) p.part_positions.right_arm = r.right_arm_pos;
				if (is_valid_vec(r.left_leg_pos)) p.part_positions.left_leg = r.left_leg_pos;
				if (is_valid_vec(r.right_leg_pos)) p.part_positions.right_leg = r.right_leg_pos;
			}
		}
		cache::publish_snaps();

		{
			std::uint64_t lp_hrp_prim = 0;
			{
				std::lock_guard<std::mutex> lk(cache::local_player_mtx);
				lp_hrp_prim = cache::local_player.hrp_prim_addr;
			}
			if (lp_hrp_prim != 0)
			{
				struct pos_vel_pair_t { math::vector3 pos; math::vector3 vel; };
				pos_vel_pair_t pv = memory->read<pos_vel_pair_t>(lp_hrp_prim + Offsets::Primitive::Position);
				std::lock_guard<std::mutex> lk(cache::local_player_mtx);
				cache::local_player.position = pv.pos;
				cache::local_player.velocity = pv.vel;
			}
		}

		std::this_thread::sleep_for(16ms);
	}
}
