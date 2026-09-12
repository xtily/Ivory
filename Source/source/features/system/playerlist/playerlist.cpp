#include "playerlist.h"
#include <imgui/imgui.h>
#include <imgui/addons/imgui_addons.h>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <sdk/cache/core/cache.h>
#include <features/system/settings/settings.h>
#include <ui/menu/settings/theme.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <sdk/game/game.h>
#include <features/system/notifications/notifications.h>

namespace playerlist
{
	static std::unordered_map<std::string, cache::player_priority> priority_cache;
	static std::mutex priority_mtx;
	static std::unordered_map<std::string, int> combo_states;

	void set_priority(const std::string& player_name, cache::player_priority priority)
	{
		std::lock_guard<std::mutex> lock(priority_mtx);
		priority_cache[player_name] = priority;
	}

	cache::player_priority get_priority(const std::string& player_name)
	{
		std::lock_guard<std::mutex> lock(priority_mtx);
		auto it = priority_cache.find(player_name);
		if (it != priority_cache.end())
		{
			return it->second;
		}
		return cache::player_priority::neutral;
	}

	void refresh_priorities()
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		std::lock_guard<std::mutex> priority_lock(priority_mtx);

		for (auto& player : cache::players)
		{
			if (player.instance.address == cache::get_local_player().instance.address)
			{
				player.priority = cache::player_priority::neutral;
				priority_cache[player.name] = cache::player_priority::neutral;
			}
			else
			{
				auto it = priority_cache.find(player.name);
				if (it != priority_cache.end())
				{
					player.priority = it->second;
				}
				else
				{
					player.priority = cache::player_priority::neutral;
					priority_cache[player.name] = cache::player_priority::neutral;
				}
			}
		}
	}

	void render()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		static std::string selected_player_name = "";

		std::vector<cache::entity_t> players_copy;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			players_copy = cache::players;
		}

		refresh_priorities();

		if (ImGui::BeginChild("Player List", ImVec2(0, ImGui::GetContentRegionAvail().y - 130)))
		{
			if (players_copy.empty())
			{
				ImGui::TextDisabled("No players found");
			}
			else
			{
				for (const auto& player : players_copy)
				{
					bool is_selected = (selected_player_name == player.name);
					bool is_local = (player.instance.address == cache::get_local_player().instance.address);

					ImVec4 text_main = Theme::GetTextMain();
					ImVec4 text_misc = Theme::GetTextMisc();

					cache::player_priority priority = is_local ? cache::player_priority::neutral : get_priority(player.name);
					ImVec4 priority_color = text_misc;
					const char* priority_text = "Neutral";

					if (is_local)
					{
						priority_text = "Client";
						priority_color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
					}
					else
					{
						switch (priority)
						{
						case cache::player_priority::neutral:
							priority_color = text_misc;
							priority_text = "Neutral";
							break;
						case cache::player_priority::friendly:
							priority_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
							priority_text = "Friendly";
							break;
						case cache::player_priority::hostile:
							priority_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
							priority_text = "Hostile";
							break;
						}
					}

					std::string display_text;
					if (player.display_name.empty())
					{
						display_text = player.name;
					}
					else
					{
						display_text = player.display_name + " @" + player.name;
					}

					ImVec4 name_color = is_selected ? style.Colors[ImGuiCol_SliderGrab] : text_main;

					ImGui::PushStyleColor(ImGuiCol_Text, name_color);
					if (ImGui::Selectable(display_text.c_str(), is_selected))
					{
						selected_player_name = player.name;
						if (combo_states.find(player.name) == combo_states.end())
						{
							combo_states[player.name] = static_cast<int>(priority);
						}
					}
				ImGui::PopStyleColor();

				float text_width = ImGui::CalcTextSize(priority_text).x;
				if (player.is_custom)
				{
					text_width += ImGui::CalcTextSize("Custom").x + style.ItemSpacing.x;
				}

				ImGui::SameLine(ImGui::GetContentRegionAvail().x + style.WindowPadding.x - text_width);

				if (player.is_custom)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Custom");
					ImGui::SameLine();
					ImGui::TextColored(priority_color, "%s", priority_text);
				}
				else
				{
					ImGui::TextColored(priority_color, "%s", priority_text);
				}
				}
			}

			ImGui::EndChild();
		}

		auto content = ImGui::GetContentRegionAvail();

		if (ImGui::BeginChild("Actions", ImVec2(content.x, ImGui::GetContentRegionAvail().y)))
		{
			std::string current_selected = selected_player_name;
			auto it = std::find_if(players_copy.begin(), players_copy.end(),
				[&current_selected](const cache::entity_t& player) {
					return player.name == current_selected;
				});

			if (it != players_copy.end())
			{
				const auto& selected = *it;
				bool is_local_player = (selected.instance.address == cache::get_local_player().instance.address);

				if (is_local_player)
				{
					set_priority(selected.name, cache::player_priority::neutral);
					refresh_priorities();
					combo_states[selected.name] = static_cast<int>(cache::player_priority::neutral);
				}
				else if (combo_states.find(selected.name) == combo_states.end())
				{
					cache::player_priority current_priority = get_priority(selected.name);
					combo_states[selected.name] = static_cast<int>(current_priority);
				}

				int& priority_index = combo_states[selected.name];

				if (is_local_player)
				{
					priority_index = static_cast<int>(cache::player_priority::neutral);
				}

				static std::vector<const char*> priority_options = { "Neutral", "Friendly", "Hostile" };

				int previous_priority_index = priority_index;

				ImGui::PushID(selected.name.c_str());
				if (is_local_player)
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::BeginCombo("Priority", priority_options[priority_index]))
				{
					for (int i = 0; i < priority_options.size(); i++)
					{
						bool is_selected_opt = (priority_index == i);
						if (ImGui::Selectable(priority_options[i], is_selected_opt))
						{
							priority_index = i;
						}
						if (is_selected_opt)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}

				if (is_local_player)
				{
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::TextDisabled("(Local Player)");
				}
				ImGui::PopID();

				if (!is_local_player && priority_index != previous_priority_index)
				{
					cache::player_priority new_priority = static_cast<cache::player_priority>(priority_index);
					set_priority(selected.name, new_priority);
					refresh_priorities();
				}
			}
			else
			{
				ImGui::TextDisabled("No player selected");
				if (!selected_player_name.empty())
				{
					selected_player_name = "";
				}
			}

			ImGui::EndChild();
		}
	}

	static std::string s_spectating_player = "";
	static bool s_is_spectating = false;

	void start_spectating(const std::string& player_name)
	{
		s_spectating_player = player_name;
		s_is_spectating = true;
	}

	void stop_spectating()
	{
		s_is_spectating = false;
		s_spectating_player = "";

		uint64_t cam = game::camera;
		if (cam >= 0x10000)
		{
			auto local_snap = cache::get_local_snap();
			uint64_t local_hum = (local_snap && local_snap->humanoid.address) ? local_snap->humanoid.address : 0;
			if (!local_hum)
			{
				auto lp = cache::get_local_player();
				if (lp.humanoid.address) local_hum = lp.humanoid.address;
			}
			if (local_hum)
			{
				memory->write<uint64_t>(cam + Offsets::Camera::CameraSubject, local_hum);
			}
		}
	}

	bool is_spectating()
	{
		return s_is_spectating;
	}

	std::string get_spectating_name()
	{
		return s_spectating_player;
	}

	void tick()
	{
		if (!s_is_spectating || s_spectating_player.empty())
			return;

		uint64_t cam = game::camera;
		if (cam < 0x10000)
			return;

		uint64_t target_subject = 0;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			for (const auto& p : cache::players)
			{
				if (p.name == s_spectating_player)
				{
					if (p.humanoid.address >= 0x10000)
					{
						target_subject = p.humanoid.address;
					}
					else if (p.humanoid_root_part.address >= 0x10000)
					{
						target_subject = p.humanoid_root_part.address;
					}
					else
					{
						auto it = p.parts.find("Head");
						if (it != p.parts.end() && it->second.address >= 0x10000)
							target_subject = it->second.address;
					}
					break;
				}
			}
		}

		if (target_subject != 0)
		{
			memory->write<uint64_t>(cam + Offsets::Camera::CameraSubject, target_subject);
		}
		else
		{
			stop_spectating();
			notifications::add("Spectate target lost");
		}
	}
}