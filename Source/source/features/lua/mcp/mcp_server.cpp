#include "mcp_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include <nlohmann/json.hpp>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <core/memory/memory.h>
#include <core/logger/logger.h>
#include <features/system/settings/settings.h>
#include <features/system/config/config.h>
#include <features/system/notifications/notifications.h>
#include <features/lua/vm/LuaVM.h>
#include <features/lua/vm/ScriptBytecode.h>
#include <features/exploits/environment/explorer/explorer.h>
#include <features/combat/aimbot/aimbot.h>
#include <features/combat/silent/mouse/mouse.h>
#include <features/combat/silent/raycast/raycast.h>
#include <ui/editor/editor.h>

using json = nlohmann::json;

namespace mcp
{
	static std::atomic<bool> s_running{ false };
	static std::atomic<int> s_port{ 28888 };
	static std::atomic<int> s_request_count{ 0 };
	static SOCKET s_listen_socket = INVALID_SOCKET;

	bool is_running() { return s_running.load(); }
	int get_port() { return s_port.load(); }
	void set_port(int port) { s_port.store(port); }
	int get_request_count() { return s_request_count.load(); }

	static std::string hex_str(std::uint64_t v)
	{
		std::stringstream ss;
		ss << "0x" << std::hex << v;
		return ss.str();
	}

	static std::uint64_t parse_hex(const std::string& str)
	{
		if (str.empty()) return 0;
		try
		{
			if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0)
				return std::stoull(str.substr(2), nullptr, 16);
			return std::stoull(str, nullptr, 10);
		}
		catch (...)
		{
			return 0;
		}
	}

	// Tool: Ivory_status
	static json tool_Ivory_status()
	{
		bool attached = memory && memory->is_attached() && memory->is_alive();
		std::string game_name = "Unknown";
		std::uint64_t place_id = 0;
		std::uint64_t datamodel_addr = (game::datamodel && game::datamodel->address != 0) ? game::datamodel->address : 0;

		if (game::datamodel && game::datamodel->address != 0)
		{
			place_id = game::datamodel->get_place_id();
			game_name = game::datamodel->get_name();
		}

		cache::entity_t local_ent = cache::get_local_player();
		size_t player_count = 0;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			player_count = cache::players.size();
		}

		json res = {
			{ "attached", attached },
			{ "process_id", memory ? memory->get_pid() : 0 },
			{ "datamodel_address", hex_str(datamodel_addr) },
			{ "place_id", place_id },
			{ "game_name", game_name },
			{ "entity_count", player_count },
			{ "local_player", {
				{ "address", hex_str(local_ent.instance.address) },
				{ "name", local_ent.name },
				{ "display_name", local_ent.display_name },
				{ "health", local_ent.health },
				{ "max_health", local_ent.max_health },
				{ "position", { local_ent.position.x, local_ent.position.y, local_ent.position.z } },
				{ "tool", local_ent.tool_name }
			}},
			{ "active_features", {
				{ "esp", settings::visuals::enabled },
				{ "aimbot", settings::aimbot::enabled },
				{ "silentaim", settings::silentaim::enabled || settings::raycast_silentaim::enabled },
				{ "speedhack", settings::movement::speedhack::enabled },
				{ "flyhack", settings::movement::flyhack::enabled },
				{ "noclip", settings::movement::noclip::enabled },
				{ "corpse_chams", settings::visuals::neutral.corpse }
			}}
		};
		return res;
	}

	// Tool: Ivory_list_players
	static json tool_list_players()
	{
		json players_arr = json::array();
		cache::entity_t local_ent = cache::get_local_player();

		std::lock_guard<std::mutex> lock(cache::mtx);
		for (const auto& ent : cache::players)
		{
			float dist = (local_ent.position.x != 0.f || local_ent.position.y != 0.f || local_ent.position.z != 0.f)
				? ent.position.distance(local_ent.position) : 0.f;

			std::string priority_str = "neutral";
			if (ent.priority == cache::player_priority::friendly) priority_str = "friendly";
			else if (ent.priority == cache::player_priority::hostile) priority_str = "hostile";

			players_arr.push_back({
				{ "address", hex_str(ent.instance.address) },
				{ "name", ent.name },
				{ "display_name", ent.display_name },
				{ "team", hex_str(ent.team) },
				{ "health", ent.health },
				{ "max_health", ent.max_health },
				{ "knocked", ent.knocked },
				{ "tool", ent.tool_name },
				{ "priority", priority_str },
				{ "distance_studs", std::round(dist * 10.f) / 10.f },
				{ "position", { std::round(ent.position.x * 10.f) / 10.f, std::round(ent.position.y * 10.f) / 10.f, std::round(ent.position.z * 10.f) / 10.f } }
			});
		}
		return { { "players", players_arr }, { "count", players_arr.size() } };
	}

	// Recursive tree builder
	static json build_instance_json(rbx::c_instance inst, int current_depth, int max_depth)
	{
		if (inst.address == 0) return nullptr;

		std::string name = inst.get_name();
		std::string class_name = inst.get_class_name();

		json node = {
			{ "name", name },
			{ "class_name", class_name },
			{ "address", hex_str(inst.address) }
		};

		auto children = inst.get_children<rbx::c_instance>();
		node["child_count"] = children.size();

		if (current_depth < max_depth)
		{
			json children_arr = json::array();
			for (auto& child : children)
			{
				if (child.address != 0)
				{
					children_arr.push_back(build_instance_json(child, current_depth + 1, max_depth));
				}
			}
			node["children"] = children_arr;
		}

		return node;
	}

	// Tool: Ivory_explorer_tree
	static json tool_explorer_tree(const std::string& root_path, int max_depth)
	{
		if (!game::datamodel || game::datamodel->address == 0)
		{
			return { { "error", "DataModel is not available. Please make sure Roblox is open and Ivory is attached." } };
		}

		if (max_depth <= 0) max_depth = 2;
		if (max_depth > 5) max_depth = 5;

		rbx::c_instance root_inst(game::datamodel->address);

		if (!root_path.empty() && root_path != "game" && root_path != "DataModel")
		{
			// Resolve path (e.g. "Workspace.Camera" or "ReplicatedStorage")
			std::stringstream ss(root_path);
			std::string token;
			rbx::c_instance current = root_inst;

			while (std::getline(ss, token, '.'))
			{
				if (token.empty() || token == "game") continue;
				auto ch_list = current.get_children<rbx::c_instance>();
				bool found = false;
				for (auto& ch : ch_list)
				{
					if (ch.get_name() == token)
					{
						current = ch;
						found = true;
						break;
					}
				}
				if (!found)
				{
					return { { "error", "Path segment not found: " + token + " in " + root_path } };
				}
			}
			root_inst = current;
		}

		return build_instance_json(root_inst, 0, max_depth);
	}

	// Tool: Ivory_inspect_instance
	static json tool_inspect_instance(const std::string& addr_or_path)
	{
		std::uint64_t target_addr = parse_hex(addr_or_path);
		rbx::c_instance inst(target_addr);

		if (target_addr == 0 && game::datamodel && game::datamodel->address != 0)
		{
			// Try path lookup
			std::stringstream ss(addr_or_path);
			std::string token;
			rbx::c_instance current(game::datamodel->address);
			bool found_path = true;

			while (std::getline(ss, token, '.'))
			{
				if (token.empty() || token == "game") continue;
				auto ch_list = current.get_children<rbx::c_instance>();
				bool step_found = false;
				for (auto& ch : ch_list)
				{
					if (ch.get_name() == token)
					{
						current = ch;
						step_found = true;
						break;
					}
				}
				if (!step_found) { found_path = false; break; }
			}
			if (found_path) inst = current;
		}

		if (inst.address == 0)
		{
			return { { "error", "Invalid instance address or path not found: " + addr_or_path } };
		}

		std::string name = inst.get_name();
		std::string class_name = inst.get_class_name();
		std::uint64_t parent_addr = memory->read<std::uint64_t>(inst.address + Offsets::Instance::Parent);
		std::string parent_name = parent_addr ? rbx::c_instance(parent_addr).get_name() : "None";

		auto children = inst.get_children<rbx::c_instance>();
		json children_summary = json::array();
		for (auto& ch : children)
		{
			if (ch.address != 0)
			{
				children_summary.push_back({
					{ "name", ch.get_name() },
					{ "class_name", ch.get_class_name() },
					{ "address", hex_str(ch.address) }
				});
			}
		}

		json res = {
			{ "address", hex_str(inst.address) },
			{ "name", name },
			{ "class_name", class_name },
			{ "parent", {
				{ "name", parent_name },
				{ "address", hex_str(parent_addr) }
			}},
			{ "child_count", children.size() },
			{ "children", children_summary }
		};

		// Check if it's a BasePart to extract CFrame / Position / Size
		if (class_name == "Part" || class_name == "MeshPart" || class_name == "BasePart" || class_name == "WedgePart" || class_name == "TrussPart")
		{
			std::uint64_t prim_addr = memory->read<std::uint64_t>(inst.address + Offsets::BasePart::Primitive);
			if (prim_addr != 0)
			{
				rbx::c_primitive prim(prim_addr);
				math::cframe cf = prim.get_cframe();
				math::vector3 sz = prim.get_size();
				res["position"] = { cf.position.x, cf.position.y, cf.position.z };
				res["size"] = { sz.x, sz.y, sz.z };
			}
		}

		return res;
	}

	// Tool: Ivory_read_script
	static json tool_read_script(const std::string& addr_or_path)
	{
		std::uint64_t script_addr = parse_hex(addr_or_path);
		rbx::c_instance inst(script_addr);

		if (script_addr == 0 && game::datamodel && game::datamodel->address != 0)
		{
			std::stringstream ss(addr_or_path);
			std::string token;
			rbx::c_instance current(game::datamodel->address);
			bool found_path = true;

			while (std::getline(ss, token, '.'))
			{
				if (token.empty() || token == "game") continue;
				auto ch_list = current.get_children<rbx::c_instance>();
				bool step_found = false;
				for (auto& ch : ch_list)
				{
					if (ch.get_name() == token)
					{
						current = ch;
						step_found = true;
						break;
					}
				}
				if (!step_found) { found_path = false; break; }
			}
			if (found_path) inst = current;
		}

		if (inst.address == 0)
		{
			return { { "error", "Script instance not found for: " + addr_or_path } };
		}

		std::string class_name = inst.get_class_name();
		std::string script_name = inst.get_name();

		std::vector<std::uint8_t> raw_or_luau;
		if (!Cheat::Features::ScriptBytecode::ReadDecompressed(inst.address, class_name, raw_or_luau))
		{
			return {
				{ "success", false },
				{ "name", script_name },
				{ "class_name", class_name },
				{ "address", hex_str(inst.address) },
				{ "error", "Failed to extract bytecode for this script. The script may be core-protected, empty, or bytecode was cleared." }
			};
		}

		std::string decompiled_src = Cheat::Features::ScriptBytecode::Decompile(raw_or_luau, script_name.c_str());

		return {
			{ "success", true },
			{ "name", script_name },
			{ "class_name", class_name },
			{ "address", hex_str(inst.address) },
			{ "bytecode_size", raw_or_luau.size() },
			{ "source_code", decompiled_src }
		};
	}

	// Tool: Ivory_execute_lua
	static json tool_execute_lua(const std::string& code)
	{
		if (code.empty())
		{
			return { { "error", "No Lua code provided." } };
		}

		bool success = LuaVM::Execute(code);
		return {
			{ "success", success },
			{ "code_length", code.length() },
			{ "message", success ? "Lua script executed successfully." : "Lua execution returned false / failed to launch." }
		};
	}

	// Tool: Ivory_get_decompiled_scripts
	static json tool_get_decompiled_scripts()
	{
		json scripts_arr = json::array();
		if (explorer::explorer)
		{
			std::lock_guard<std::mutex> lock(explorer::explorer->decompiled_mutex);
			for (const auto& s : explorer::explorer->decompiled_scripts)
			{
				scripts_arr.push_back({
					{ "name", s.name },
					{ "code", s.code },
					{ "open", s.open }
				});
			}
		}
		return { { "scripts", scripts_arr }, { "count", scripts_arr.size() } };
	}

	// Tool: Ivory_toggle_feature
	static json tool_toggle_feature(const std::string& feature, const json& state_opt)
	{
		std::string feat = feature;
		std::transform(feat.begin(), feat.end(), feat.begin(), ::tolower);

		auto apply_toggle = [](bool& var, const json& opt) -> bool {
			if (!opt.is_null() && opt.is_boolean()) {
				var = opt.get<bool>();
			} else {
				var = !var;
			}
			return var;
		};

		bool new_state = false;
		std::string resolved_name = feature;

		if (feat == "esp" || feat == "visuals") {
			new_state = apply_toggle(settings::visuals::enabled, state_opt);
			resolved_name = "Visuals ESP";
		}
		else if (feat == "box") {
			new_state = apply_toggle(settings::visuals::neutral.box, state_opt);
			resolved_name = "ESP Box";
		}
		else if (feat == "chams") {
			new_state = apply_toggle(settings::visuals::neutral.chams, state_opt);
			resolved_name = "Player Chams";
		}
		else if (feat == "corpse" || feat == "corpse_chams" || feat == "corpsechams") {
			new_state = apply_toggle(settings::visuals::neutral.corpse, state_opt);
			resolved_name = "Corpse Chams";
		}
		else if (feat == "aimbot") {
			new_state = apply_toggle(settings::aimbot::enabled, state_opt);
			resolved_name = "Aimbot";
		}
		else if (feat == "silentaim" || feat == "silent_aim") {
			new_state = apply_toggle(settings::silentaim::enabled, state_opt);
			resolved_name = "Silent Aim";
		}
		else if (feat == "raycast_silentaim") {
			new_state = apply_toggle(settings::raycast_silentaim::enabled, state_opt);
			resolved_name = "Raycast Silent Aim";
		}
		else if (feat == "triggerbot") {
			new_state = apply_toggle(settings::triggerbot::enabled, state_opt);
			resolved_name = "Triggerbot";
		}
		else if (feat == "speed" || feat == "speedhack") {
			new_state = apply_toggle(settings::movement::speedhack::enabled, state_opt);
			resolved_name = "Speedhack";
		}
		else if (feat == "fly" || feat == "flyhack") {
			new_state = apply_toggle(settings::movement::flyhack::enabled, state_opt);
			resolved_name = "Flyhack";
		}
		else if (feat == "noclip") {
			new_state = apply_toggle(settings::movement::noclip::enabled, state_opt);
			resolved_name = "Noclip";
		}
		else if (feat == "btools") {
			new_state = apply_toggle(settings::btools::enabled, state_opt);
			resolved_name = "BTools";
		}
		else if (feat == "enemy_highlight") {
			new_state = apply_toggle(settings::visuals::enemy_highlight, state_opt);
			resolved_name = "Enemy Highlight";
		}
		else if (feat == "friendly_highlight") {
			new_state = apply_toggle(settings::visuals::friendly_highlight, state_opt);
			resolved_name = "Friendly Highlight";
		}
		else {
			return { { "error", "Unknown feature: " + feature + ". Supported: esp, box, chams, corpse_chams, aimbot, silentaim, triggerbot, speedhack, flyhack, noclip, btools, enemy_highlight, friendly_highlight." } };
		}

		notifications::add("MCP: " + resolved_name + (new_state ? " Enabled" : " Disabled"));

		return {
			{ "success", true },
			{ "feature", resolved_name },
			{ "state", new_state }
		};
	}

	// Dispatch JSON-RPC tool calls
	static json handle_tool_call(const std::string& name, const json& args)
	{
		if (name == "Ivory_status")
		{
			return tool_Ivory_status();
		}
		else if (name == "Ivory_list_players")
		{
			return tool_list_players();
		}
		else if (name == "Ivory_explorer_tree")
		{
			std::string root_path = args.value("root_path", "game");
			int depth = args.value("max_depth", 2);
			return tool_explorer_tree(root_path, depth);
		}
		else if (name == "Ivory_inspect_instance")
		{
			std::string target = args.value("address_or_path", "");
			return tool_inspect_instance(target);
		}
		else if (name == "Ivory_read_script")
		{
			std::string target = args.value("address_or_path", "");
			return tool_read_script(target);
		}
		else if (name == "Ivory_execute_lua")
		{
			std::string code = args.value("code", "");
			return tool_execute_lua(code);
		}
		else if (name == "Ivory_get_decompiled_scripts")
		{
			return tool_get_decompiled_scripts();
		}
		else if (name == "Ivory_toggle_feature")
		{
			std::string feat = args.value("feature", "");
			json state = args.contains("state") ? args["state"] : json(nullptr);
			return tool_toggle_feature(feat, state);
		}
		else if (name == "Ivory_write_editor")
		{
			std::string code = args.value("code", "");
			editor::set_active_editor_text(code);
			LuaVM::LogOutput(("[AI MCP] Injected " + std::to_string(code.length()) + " bytes into editor.").c_str());
			notifications::add("AI updated code editor!");
			return { { "success", true }, { "message", "Code written to editor." } };
		}
		else if (name == "Ivory_console_log")
		{
			std::string msg = args.value("message", "");
			bool is_err = args.value("is_error", false);
			LuaVM::LogOutput(msg.c_str(), is_err);
			return { { "success", true } };
		}

		return { { "error", "Tool not found: " + name } };
	}

	// Returns the standard MCP tools list
	static json get_mcp_tools_list()
	{
		json tools = json::array();

		{
			json t = json::object();
			t["name"] = "Ivory_status";
			t["description"] = "Get the real-time status of Ivory and Roblox game attachment, Place ID, local player health, position, and active features.";
			t["inputSchema"] = json{ { "type", "object" }, { "properties", json::object() } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_list_players";
			t["description"] = "List all active players in the current game session with their names, display names, health, teams, distances, tools, and world coordinates.";
			t["inputSchema"] = json{ { "type", "object" }, { "properties", json::object() } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_explorer_tree";
			t["description"] = "Explore the Roblox DataModel hierarchy tree (e.g. Workspace, Players, ReplicatedStorage, Lighting, StarterGui) to analyze game objects.";
			json props = json::object();
			props["root_path"] = json{ { "type", "string" }, { "description", "Root path to start browsing from. Defaults to 'game'." } };
			props["max_depth"] = json{ { "type", "integer" }, { "description", "Maximum child depth to traverse (1 to 5). Defaults to 2." } };
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_inspect_instance";
			t["description"] = "Get detailed properties, ClassName, parent, children list, and world coordinates of a specific instance by address (0x...) or path.";
			json props = json::object();
			props["address_or_path"] = json{ { "type", "string" }, { "description", "Hex address (e.g. '0x1c84f00') or dot-separated path (e.g. 'Workspace.Baseplate')." } };
			json req = json::array({ "address_or_path" });
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props }, { "required", req } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_read_script";
			t["description"] = "Decompiles and returns the Lua/Luau source code of any LocalScript or ModuleScript in the game given its address or path.";
			json props = json::object();
			props["address_or_path"] = json{ { "type", "string" }, { "description", "Hex address or dot-separated path." } };
			json req = json::array({ "address_or_path" });
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props }, { "required", req } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_execute_lua";
			t["description"] = "Execute custom Lua/Luau code directly inside the game's Lua VM.";
			json props = json::object();
			props["code"] = json{ { "type", "string" }, { "description", "The Lua code snippet to execute." } };
			json req = json::array({ "code" });
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props }, { "required", req } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_write_editor";
			t["description"] = "Writes Lua code directly into the cheat's visual Luau Script Editor where the user can see and type.";
			json props = json::object();
			props["code"] = json{ { "type", "string" }, { "description", "The Lua source code to place in the editor." } };
			json req = json::array({ "code" });
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props }, { "required", req } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_get_decompiled_scripts";
			t["description"] = "Retrieves all scripts that have been decompiled or opened in the Explorer Script Viewer.";
			t["inputSchema"] = json{ { "type", "object" }, { "properties", json::object() } };
			tools.push_back(t);
		}

		{
			json t = json::object();
			t["name"] = "Ivory_toggle_feature";
			t["description"] = "Toggle or configure cheat and visual features in real-time (esp, box, chams, corpse_chams, aimbot, silentaim, speedhack, flyhack, noclip, btools, enemy_highlight, friendly_highlight).";
			json props = json::object();
			props["feature"] = json{ { "type", "string" }, { "description", "Feature name (e.g. 'esp', 'aimbot', 'corpse_chams', 'speedhack', 'noclip', 'flyhack', 'btools')." } };
			props["state"] = json{ { "type", "boolean" }, { "description", "Optional state." } };
			json req = json::array({ "feature" });
			t["inputSchema"] = json{ { "type", "object" }, { "properties", props }, { "required", req } };
			tools.push_back(t);
		}

		return tools;
	}

	// Handle MCP JSON-RPC 2.0 requests
	static json handle_mcp_jsonrpc(const json& req)
	{
		std::string method = req.value("method", "");
		auto id = req.contains("id") ? req["id"] : json(nullptr);

		json resp = {
			{ "jsonrpc", "2.0" },
			{ "id", id }
		};

		if (method == "initialize")
		{
			resp["result"] = {
				{ "protocolVersion", "2024-11-05" },
				{ "capabilities", {
					{ "tools", json::object() }
				}},
				{ "serverInfo", {
					{ "name", "Ivory-mcp-bridge" },
					{ "version", "1.0.0" }
				}}
			};
		}
		else if (method == "notifications/initialized")
		{
			return json(nullptr);
		}
		else if (method == "ping")
		{
			resp["result"] = json::object();
		}
		else if (method == "tools/list")
		{
			resp["result"] = {
				{ "tools", get_mcp_tools_list() }
			};
		}
		else if (method == "tools/call")
		{
			json params = req.value("params", json::object());
			std::string tool_name = params.value("name", "");
			json args = params.value("arguments", json::object());

			json tool_result = handle_tool_call(tool_name, args);
			resp["result"] = {
				{ "content", {
					{
						{ "type", "text" },
						{ "text", tool_result.dump(2) }
					}
				}}
			};
		}
		else
		{
			resp["error"] = {
				{ "code", -32601 },
				{ "message", "Method not found: " + method }
			};
		}

		return resp;
	}

	// Client connection handler
	static void handle_client(SOCKET client_sock)
	{
		char buffer[65536];
		int bytes_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
		if (bytes_read <= 0)
		{
			closesocket(client_sock);
			return;
		}
		buffer[bytes_read] = '\0';
		s_request_count++;

		std::string req_str(buffer, bytes_read);

		size_t header_end = req_str.find("\r\n\r\n");
		size_t content_length = 0;
		size_t cl_pos = req_str.find("Content-Length:");
		if (cl_pos == std::string::npos) cl_pos = req_str.find("content-length:");
		if (cl_pos != std::string::npos)
		{
			size_t cl_end = req_str.find("\r\n", cl_pos);
			std::string cl_val = req_str.substr(cl_pos + 15, cl_end - (cl_pos + 15));
			try { content_length = std::stoul(cl_val); } catch (...) {}
		}

		while (header_end == std::string::npos || (content_length > 0 && req_str.length() - (header_end + 4) < content_length))
		{
			char extra_buf[4096];
			int extra = recv(client_sock, extra_buf, sizeof(extra_buf), 0);
			if (extra <= 0) break;
			req_str.append(extra_buf, extra);
			if (header_end == std::string::npos)
				header_end = req_str.find("\r\n\r\n");
		}

		std::string method, path;
		std::istringstream stream(req_str);
		stream >> method >> path;

		// Extract body if POST
		std::string body;
		if (header_end != std::string::npos)
		{
			body = req_str.substr(header_end + 4);
		}

		auto send_response = [&](int code, const std::string& status, const std::string& content_type, const std::string& content) {
			std::stringstream resp;
			resp << "HTTP/1.1 " << code << " " << status << "\r\n"
				 << "Content-Type: " << content_type << "\r\n"
				 << "Content-Length: " << content.length() << "\r\n"
				 << "Access-Control-Allow-Origin: *\r\n"
				 << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
				 << "Access-Control-Allow-Headers: *\r\n"
				 << "Connection: close\r\n\r\n"
				 << content;
			std::string s = resp.str();
			send(client_sock, s.c_str(), static_cast<int>(s.length()), 0);
		};

		if (method == "OPTIONS")
		{
			send_response(200, "OK", "text/plain", "");
			closesocket(client_sock);
			return;
		}

		// MCP JSON-RPC Endpoint (POST /mcp or POST /)
		if (method == "POST" && (path == "/mcp" || path == "/" || path == "/rpc"))
		{
			try
			{
				json req_json = json::parse(body);
				json resp_json = handle_mcp_jsonrpc(req_json);
				if (!resp_json.is_null())
				{
					send_response(200, "OK", "application/json", resp_json.dump());
				}
				else
				{
					send_response(204, "No Content", "text/plain", "");
				}
			}
			catch (const std::exception& ex)
			{
				json err = {
					{ "jsonrpc", "2.0" },
					{ "error", { { "code", -32700 }, { "message", std::string("Parse error: ") + ex.what() } } }
				};
				send_response(400, "Bad Request", "application/json", err.dump());
			}
			closesocket(client_sock);
			return;
		}

		// REST API: GET /api/status
		if (method == "GET" && path == "/api/status")
		{
			send_response(200, "OK", "application/json", tool_Ivory_status().dump(2));
			closesocket(client_sock);
			return;
		}

		// REST API: GET /api/players
		if (method == "GET" && path == "/api/players")
		{
			send_response(200, "OK", "application/json", tool_list_players().dump(2));
			closesocket(client_sock);
			return;
		}

		// REST API: GET /api/explorer
		if (method == "GET" && path.rfind("/api/explorer", 0) == 0)
		{
			std::string root_path = "game";
			int depth = 2;
			size_t q = path.find('?');
			if (q != std::string::npos)
			{
				std::string query = path.substr(q + 1);
				size_t ppos = query.find("path=");
				if (ppos != std::string::npos) {
					size_t pend = query.find('&', ppos);
					root_path = query.substr(ppos + 5, pend == std::string::npos ? std::string::npos : pend - (ppos + 5));
				}
				size_t dpos = query.find("depth=");
				if (dpos != std::string::npos) {
					depth = std::atoi(query.substr(dpos + 6).c_str());
				}
			}
			send_response(200, "OK", "application/json", tool_explorer_tree(root_path, depth).dump(2));
			closesocket(client_sock);
			return;
		}

		// REST API: POST /api/lua/execute
		if (method == "POST" && path == "/api/lua/execute")
		{
			try
			{
				json j = json::parse(body);
				std::string code = j.value("code", "");
				send_response(200, "OK", "application/json", tool_execute_lua(code).dump(2));
			}
			catch (const std::exception& ex)
			{
				send_response(400, "Bad Request", "application/json", std::string("{\"error\":\"") + ex.what() + "\"}");
			}
			closesocket(client_sock);
			return;
		}

		// REST API: POST /api/lua/write (Write into UI Code Editor text box)
		if (method == "POST" && (path == "/api/lua/write" || path == "/api/lua/editor"))
		{
			try
			{
				json j = json::parse(body);
				std::string code = j.value("code", j.value("text", ""));
				editor::set_active_editor_text(code);
				LuaVM::LogOutput(("[AI MCP] Injected " + std::to_string(code.length()) + " bytes into editor.").c_str());
				notifications::add("AI updated code editor!");
				send_response(200, "OK", "application/json", "{\"success\":true,\"message\":\"Code written to editor\"}");
			}
			catch (const std::exception& ex)
			{
				send_response(400, "Bad Request", "application/json", std::string("{\"error\":\"") + ex.what() + "\"}");
			}
			closesocket(client_sock);
			return;
		}

		// REST API: POST /api/lua/console
		if (method == "POST" && path == "/api/lua/console")
		{
			try
			{
				json j = json::parse(body);
				std::string msg = j.value("message", j.value("text", ""));
				bool is_err = j.value("is_error", false);
				LuaVM::LogOutput(msg.c_str(), is_err);
				send_response(200, "OK", "application/json", "{\"success\":true,\"message\":\"Logged to console\"}");
			}
			catch (const std::exception& ex)
			{
				send_response(400, "Bad Request", "application/json", std::string("{\"error\":\"") + ex.what() + "\"}");
			}
			closesocket(client_sock);
			return;
		}

		// Default dashboard / status
		json welcome = {
			{ "service", "Ivory MCP AI Bridge" },
			{ "version", "1.0.0" },
			{ "mcp_endpoint", "/mcp" },
			{ "status", tool_Ivory_status() },
			{ "available_tools", get_mcp_tools_list() }
		};
		send_response(200, "OK", "application/json", welcome.dump(2));
		closesocket(client_sock);
	}

	void run()
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		{
			logger->log<ERR>("[MCP] WSAStartup failed");
			return;
		}

		while (true)
		{
			if (!settings::mcp::enabled)
			{
				if (s_running.load())
				{
					s_running.store(false);
					if (s_listen_socket != INVALID_SOCKET)
					{
						closesocket(s_listen_socket);
						s_listen_socket = INVALID_SOCKET;
					}
					logger->log<INFO>("[MCP] AI Bridge server stopped");
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				continue;
			}

			if (!s_running.load())
			{
				s_port.store(settings::mcp::port);
				s_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (s_listen_socket == INVALID_SOCKET)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				BOOL opt = TRUE;
				setsockopt(s_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

				sockaddr_in addr{};
				addr.sin_family = AF_INET;
				addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 (safe local loopback)
				addr.sin_port = htons(static_cast<u_short>(s_port.load()));

				if (bind(s_listen_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
				{
					closesocket(s_listen_socket);
					s_listen_socket = INVALID_SOCKET;
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				if (listen(s_listen_socket, SOMAXCONN) == SOCKET_ERROR)
				{
					closesocket(s_listen_socket);
					s_listen_socket = INVALID_SOCKET;
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				s_running.store(true);
				logger->log<INFO>("[MCP] AI Bridge server active on http://127.0.0.1:{}", s_port.load());
			}

			sockaddr_in client_addr{};
			int client_len = sizeof(client_addr);
			SOCKET client_sock = accept(s_listen_socket, (sockaddr*)&client_addr, &client_len);

			if (client_sock == INVALID_SOCKET)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}

			std::thread([client_sock]() {
				handle_client(client_sock);
			}).detach();
		}

		if (s_listen_socket != INVALID_SOCKET)
		{
			closesocket(s_listen_socket);
			s_listen_socket = INVALID_SOCKET;
		}
		WSACleanup();
	}

	void stop()
	{
		s_running.store(false);
		if (s_listen_socket != INVALID_SOCKET)
		{
			closesocket(s_listen_socket);
			s_listen_socket = INVALID_SOCKET;
		}
	}
}
