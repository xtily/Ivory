#include "sdk.h"
#include <unordered_map>
#include <Windows.h>

#include <core/memory/memory.h>
#include <sdk/game/game.h>
#include <xmmintrin.h>

namespace {
	constexpr std::uint64_t k_name_cache_ttl_ms = 2000;

	inline bool name_cache_fetch(void* cache_ptr, std::uint64_t key, std::string& out)
	{
		auto& cache = *static_cast<std::unordered_map<std::uint64_t, std::pair<std::string, std::uint64_t>>*>(cache_ptr);
		auto it = cache.find(key);
		if (it == cache.end()) return false;
		if (GetTickCount64() - it->second.second > k_name_cache_ttl_ms)
		{
			cache.erase(it);
			return false;
		}
		out = it->second.first;
		return true;
	}

	inline void name_cache_store(void* cache_ptr, std::uint64_t key, const std::string& value)
	{
		auto& cache = *static_cast<std::unordered_map<std::uint64_t, std::pair<std::string, std::uint64_t>>*>(cache_ptr);
		if (cache.size() > 8192) cache.clear();
		cache[key] = { value, GetTickCount64() };
	}
}

std::string rbx::c_nameable::get_name() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull)
		return "unknown";

	static thread_local std::unordered_map<std::uint64_t, std::pair<std::string, std::uint64_t>> s_cache;
	{
		std::string cached;
		if (name_cache_fetch(&s_cache, address, cached))
			return cached;
	}

	auto name_container{ memory->read<std::uint64_t>(address + Offsets::Instance::NameContainer) };
	if (name_container < 0x10000 || name_container > 0x7FFFFFFFFFFFull)
		return "unknown";

	std::string result = memory->read_string(name_container + Offsets::Instance::Name);
	if (result != "str_error")
		name_cache_store(&s_cache, address, result);
	return result;
}

std::string rbx::c_nameable::get_class_name() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull)
		return "unknown";

	static thread_local std::unordered_map<std::uint64_t, std::pair<std::string, std::uint64_t>> s_cache;
	{
		std::string cached;
		if (name_cache_fetch(&s_cache, address, cached))
			return cached;
	}

	auto class_descriptor{ memory->read<std::uint64_t>(address + Offsets::Instance::ClassDescriptor) };
	if (class_descriptor < 0x10000 || class_descriptor > 0x7FFFFFFFFFFFull)
		return "unknown";

	auto class_name{ memory->read<std::uint64_t>(class_descriptor + Offsets::Instance::ClassName) };
	if (class_name < 0x10000 || class_name > 0x7FFFFFFFFFFFull)
		return "unknown";

	std::string result = memory->read_string(class_name);
	if (result != "str_error")
		name_cache_store(&s_cache, address, result);
	return result;
}

std::uint64_t rbx::c_node::find_first_child(std::string_view name)
{
	std::vector<std::uint64_t> children{ get_children() };

	for (rbx::c_nameable child : children)
	{
		if (child.get_name() == name)
		{
			return child.address;
		}
	}

	return {};
}

std::uint64_t rbx::c_node::find_first_child_by_class(std::string_view name)
{
	std::vector<std::uint64_t> children{ get_children() };

	for (rbx::c_nameable child : children)
	{
		if (child.get_class_name() == name)
		{
			return child.address;
		}
	}

	return {};
}

std::vector<std::uint64_t> rbx::c_node::get_children()
{
	auto* self{ static_cast<rbx::c_instance*>(this) };
	if (!self || self->address < 0x10000 || self->address > 0x7FFFFFFFFFFFull)
		return {};

	std::vector<std::uint64_t> container;

	std::uint64_t kids = memory->read<std::uint64_t>(self->address + Offsets::Instance::ChildrenStart);
	if (!kids || kids < 0x10000 || kids > 0x7FFFFFFFFFFFull)
		return {};

	std::uint64_t start = memory->read<std::uint64_t>(kids);
	std::uint64_t end = memory->read<std::uint64_t>(kids + Offsets::Instance::ChildrenEnd);

	if (!start || start < 0x10000 || start > 0x7FFFFFFFFFFFull ||
		!end || end < 0x10000 || end > 0x7FFFFFFFFFFFull ||
		start >= end || (end - start) > 0x100000)
	{
		return {};
	}

	const std::size_t span = static_cast<std::size_t>(end - start);
	const std::size_t count = span / 16;

	static thread_local std::vector<std::uint64_t> raw_span;
	raw_span.resize(span / 8);

	if (memory->read_raw(start, raw_span.data(), (span / 8) * sizeof(std::uint64_t)))
	{
		container.resize(count);
		std::size_t write_idx = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			std::uint64_t child = raw_span[i * 2];
			if (child >= 0x10000 && child < 0x7FFFFFFFFFFFull)
				container[write_idx++] = child;
		}
		container.resize(write_idx);
		return container;
	}

	container.reserve(count);
	for (std::uint64_t ptr = start; ptr < end; ptr += 16)
	{
		std::uint64_t child = memory->read<std::uint64_t>(ptr);
		if (child >= 0x10000 && child < 0x7FFFFFFFFFFFull)
			container.push_back(child);
	}

	return container;
}

std::uint64_t rbx::c_node::get_parent()
{
	auto* base{ static_cast<rbx::c_instance*>(this) };
	if (!base || base->address < 0x10000 || base->address > 0x7FFFFFFFFFFFull)
		return 0;

	return memory->read<std::uint64_t>(base->address + Offsets::Instance::Parent);
}

namespace {
	struct sp_entry_t
	{
		std::uint64_t ptr = 0;
		std::uint64_t ctrl = 0;
	};

	static bool is_addr_valid(std::uint64_t a)
	{
		return a >= 0x10000 && a < 0x000FFFFFFFFF0000ULL;
	}

	static bool kids_remove(std::uint64_t parent, std::uint64_t child)
	{
		if (!is_addr_valid(parent) || !is_addr_valid(child))
			return false;

		std::uint64_t cow = memory->read<std::uint64_t>(parent + Offsets::Instance::ChildrenStart);
		if (!is_addr_valid(cow))
			return true;

		std::uint64_t first = memory->read<std::uint64_t>(cow);
		std::uint64_t last = memory->read<std::uint64_t>(cow + Offsets::Instance::ChildrenEnd);
		if (!is_addr_valid(first) || last <= first)
			return true;

		const std::size_t stride = sizeof(sp_entry_t);
		std::size_t n = (std::size_t)(last - first) / stride;
		if (n > 10000)
			return false;

		for (std::size_t i = 0; i < n; ++i)
		{
			sp_entry_t e = memory->read<sp_entry_t>(first + i * stride);
			if (e.ptr != child)
				continue;

			if (i + 1 < n)
			{
				sp_entry_t tail = memory->read<sp_entry_t>(first + (n - 1) * stride);
				memory->write<sp_entry_t>(first + i * stride, tail);
			}

			memory->write<std::uint64_t>(cow + Offsets::Instance::ChildrenEnd, last - stride);
			return true;
		}
		return true;
	}

	static bool kids_add(std::uint64_t parent, std::uint64_t child)
	{
		if (!is_addr_valid(parent) || !is_addr_valid(child))
			return false;

		std::uint64_t cow = memory->read<std::uint64_t>(parent + Offsets::Instance::ChildrenStart);
		if (!is_addr_valid(cow))
			return false;

		sp_entry_t entry{};
		entry.ptr = child;
		entry.ctrl = memory->read<std::uint64_t>(child + 0x10);

		std::uint64_t first = memory->read<std::uint64_t>(cow);
		std::uint64_t last = memory->read<std::uint64_t>(cow + Offsets::Instance::ChildrenEnd);
		std::uint64_t cap = memory->read<std::uint64_t>(cow + 0x10);

		const std::size_t stride = sizeof(sp_entry_t);

		if (is_addr_valid(first) && last > first)
		{
			std::size_t n = (std::size_t)(last - first) / stride;
			if (n > 10000)
				return false;

			for (std::size_t i = 0; i < n; ++i)
			{
				sp_entry_t e = memory->read<sp_entry_t>(first + i * stride);
				if (e.ptr == child)
					return true;
			}
		}

		bool room = is_addr_valid(first) && is_addr_valid(cap) &&
			(last + stride) <= cap && last >= first;

		if (room)
		{
			memory->write<sp_entry_t>(last, entry);
			memory->write<std::uint64_t>(cow + Offsets::Instance::ChildrenEnd, last + stride);
			return true;
		}

		std::size_t old_n = 0;
		if (is_addr_valid(first) && last > first)
			old_n = (std::size_t)(last - first) / stride;

		std::size_t new_n = old_n + 1;
		std::size_t new_cap = new_n * 2;
		if (new_cap < 4)
			new_cap = 4;

		std::uint64_t nf = (std::uint64_t)VirtualAllocEx(memory->m_process_handle, nullptr, new_cap * stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!nf)
			return false;

		if (old_n > 0)
		{
			std::vector<sp_entry_t> old(old_n);
			{
				std::lock_guard<std::mutex> lock(memory->m_handle_mtx);
				Luck_ReadVirtualMemory(memory->m_process_handle, (void*)first, old.data(), old_n * stride, nullptr);
				Luck_WriteVirtualMemory(memory->m_process_handle, (void*)nf, old.data(), old_n * stride, nullptr);
			}
		}

		memory->write<sp_entry_t>(nf + old_n * stride, entry);
		memory->write<std::uint64_t>(cow, nf);
		memory->write<std::uint64_t>(cow + Offsets::Instance::ChildrenEnd, nf + new_n * stride);
		memory->write<std::uint64_t>(cow + 0x10, nf + new_cap * stride);
		return true;
	}
}

void rbx::c_node::set_parent(const std::uint64_t& parent)
{
	auto* base{ static_cast<rbx::c_instance*>(this) };
	if (!base || !is_addr_valid(base->address))
		return;

	if (parent == base->address)
		return;

	std::uint64_t cur = memory->read<std::uint64_t>(base->address + Offsets::Instance::Parent);
	if (cur == parent)
		return;

	if (is_addr_valid(cur))
		kids_remove(cur, base->address);

	if (is_addr_valid(parent))
		kids_add(parent, base->address);

	memory->write<std::uint64_t>(base->address + Offsets::Instance::Parent, parent);
}

rbx::c_model_instance rbx::c_player::get_model_instance()
{
	return { memory->read<std::uint64_t>(address + Offsets::Player::ModelInstance) };
}

std::uint64_t rbx::c_player::get_team()
{
	return { memory->read<std::uint64_t>(address + Offsets::Player::Team) };
}

std::string rbx::c_player::get_display_name()
{
	return memory->read_string(address + Offsets::Player::DisplayName);
}

std::uint64_t rbx::c_player::get_user_id()
{
	return memory->read<std::uint64_t>(address + Offsets::Player::UserId);
}

int rbx::c_player::get_account_age()
{
	return memory->read<int>(address + Offsets::Player::AccountAge);
}

float rbx::c_humanoid::get_health() const
{
	return { memory->read<float>(address + Offsets::Humanoid::Health) };
}

float rbx::c_humanoid::get_max_health() const
{
	return { memory->read<float>(address + Offsets::Humanoid::MaxHealth) };
}

float rbx::c_humanoid::get_jump_power() const
{
	return { memory->read<float>(address + Offsets::Humanoid::JumpPower) };
}

void rbx::c_humanoid::set_jump_power(const float& jump)
{
	memory->write<float>(address + Offsets::Humanoid::JumpPower, jump);
}

void rbx::c_humanoid::set_jump_power(const float& jump) const
{
	memory->write<float>(address + Offsets::Humanoid::JumpPower, jump);
}

float rbx::c_humanoid::get_walk_speed() const
{
	return { memory->read<float>(address + Offsets::Humanoid::Walkspeed) };
}

void rbx::c_humanoid::set_walk_speed(const float& speed)
{
	memory->write<float>(address + Offsets::Humanoid::Walkspeed, speed);
	memory->write<float>(address + Offsets::Humanoid::WalkspeedCheck, speed);
}

void rbx::c_humanoid::set_walk_speed(const float& speed) const
{
	memory->write<float>(address + Offsets::Humanoid::Walkspeed, speed);
	memory->write<float>(address + Offsets::Humanoid::WalkspeedCheck, speed);
}

float rbx::c_humanoid::get_hip_height() const
{
	return { memory->read<float>(address + Offsets::Humanoid::HipHeight) };
}

void rbx::c_humanoid::set_hip_height(const float& height)
{
	memory->write<float>(address + Offsets::Humanoid::HipHeight, height);
}

std::uint8_t rbx::c_humanoid::get_rig_type() const
{
	return { memory->read<std::uint8_t>(address + Offsets::Humanoid::RigType) };
}

std::uint16_t rbx::c_humanoid::get_state() const
{
	auto humanoid_state{ memory->read<std::uint64_t>(address + Offsets::Humanoid::HumanoidState) };
	return { memory->read<std::uint16_t>(humanoid_state + Offsets::Humanoid::HumanoidStateID) };
}

void rbx::c_humanoid::write_gravity(float gravity)
{
	auto datamodel = c_datamodel::get();
	if (!datamodel || datamodel->address == 0)
		return;

	uintptr_t workspace = memory->read<uintptr_t>(datamodel->address + Offsets::DataModel::Workspace);
	if (workspace == 0)
		return;

	uintptr_t container = memory->read<uintptr_t>(workspace + Offsets::Workspace::World);
	if (container == 0)
		return;

	memory->write<float>(container + Offsets::World::Gravity, gravity);
}

void rbx::c_humanoid::write_tickrate(float tickrate)
{
	auto datamodel = c_datamodel::get();
	if (!datamodel || datamodel->address == 0)
		return;

	uintptr_t workspace = memory->read<uintptr_t>(datamodel->address + Offsets::DataModel::Workspace);
	if (workspace == 0)
		return;

	uintptr_t container = memory->read<uintptr_t>(workspace + Offsets::Workspace::World);
	if (container == 0)
		return;

	memory->write<float>(container + Offsets::World::worldStepsPerSec, tickrate);
}

bool rbx::c_animation_track::is_playing()
{
	return memory->read<bool>(address + Offsets::AnimationTrack::IsPlaying);
}

float rbx::c_animation_track::get_time_position()
{
	return memory->read<float>(address + Offsets::AnimationTrack::TimePosition);
}

std::uint64_t rbx::c_animation_track::get_animation_id()
{
	auto animation_instance = memory->read<std::uint64_t>(address + Offsets::AnimationTrack::Animation);
	if (animation_instance) {
		return animation_instance;
	}
	return 0;
}

std::string rbx::c_animation_track::get_animation_name()
{
	auto animation_instance = memory->read<std::uint64_t>(address + Offsets::AnimationTrack::Animation);
	if (animation_instance) {
		rbx::c_instance anim(animation_instance);
		return anim.get_name();
	}
	return "";
}

std::vector<rbx::c_animation_track> rbx::c_animator::get_active_animations()
{
	std::vector<rbx::c_animation_track> tracks;
	
	uintptr_t active_animations = memory->read<uintptr_t>(address + Offsets::Animator::ActiveAnimations);
	if (!active_animations) return tracks;
	
	
	auto start = memory->read<uint64_t>(active_animations);
	auto end = memory->read<uint64_t>(active_animations + 8);
	if (start && end > start && (end - start) < 0x1000) {
		for (auto curr = start; curr < end; curr += 8) {
			auto track_ptr = memory->read<uint64_t>(curr);
			if (track_ptr) {
				tracks.push_back(rbx::c_animation_track(track_ptr));
			}
		}
	} else {
		uintptr_t root = memory->read<uintptr_t>(active_animations + 8);
		if (root && root != active_animations) {
			std::vector<uintptr_t> nodes;
			nodes.push_back(root);
			int count = 0;
			while (!nodes.empty() && count < 64) {
				uintptr_t curr = nodes.back();
				nodes.pop_back();
				if (!curr) continue;
				count++;
				
				uintptr_t left = memory->read<uintptr_t>(curr + 0x0);
				uintptr_t right = memory->read<uintptr_t>(curr + 0x10);
				
				uintptr_t track_ptr = memory->read<uintptr_t>(curr + 0x28);
				if (track_ptr) {
					tracks.push_back(rbx::c_animation_track(track_ptr));
				}
				
				if (left && left != active_animations && left != curr) nodes.push_back(left);
				if (right && right != active_animations && right != curr) nodes.push_back(right);
			}
		}
	}
	
	return tracks;
}

rbx::c_primitive rbx::c_instance::get_primitive()
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull) return { 0 };
	return { memory->read<std::uint64_t>(address + Offsets::BasePart::Primitive) };
}

rbx::c_primitive rbx::c_part::get_primitive() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull) return { 0 };
	return { memory->read<std::uint64_t>(address + Offsets::BasePart::Primitive) };
}

math::vector3 rbx::c_primitive::get_position() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull) return { 0.f, 0.f, 0.f };
	return memory->read<math::vector3>(address + Offsets::Primitive::Position);
}

void rbx::c_primitive::set_position(const math::vector3& position)
{
	memory->write<math::vector3>(address + Offsets::Primitive::Position, position);
}

math::matrix3 rbx::c_primitive::get_rotation() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull) return {};
	return memory->read<math::matrix3>(address + Offsets::Primitive::Rotation);
}

void rbx::c_primitive::set_rotation(const math::matrix3& rotation)
{
	memory->write<math::matrix3>(address + Offsets::Primitive::Rotation, rotation);
}

math::vector3 rbx::c_primitive::get_size() const
{
	if (!address) return { 0.f, 0.f, 0.f };
	return { memory->read<math::vector3>(address + Offsets::Primitive::Size) };
}

void rbx::c_primitive::set_size(const math::vector3& size)
{
	memory->write<math::vector3>(address + Offsets::Primitive::Size, size);
}

math::cframe rbx::c_primitive::get_cframe() const
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull) return {};
	return memory->read<math::cframe>(address + Offsets::Primitive::Rotation);
}

void rbx::c_primitive::set_can_collide(bool enable)
{
	if (!address)
		return;

	std::uint8_t val = memory->read<std::uint8_t>(address + Offsets::Primitive::Flags);
	if (enable)
		val |= static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide);
	else
		val &= ~static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide);

	memory->write<std::uint8_t>(address + Offsets::Primitive::Flags, val);
}

rbx::c_workspace rbx::c_datamodel::get_workspace()
{
	return { memory->read<std::uint64_t>(address + Offsets::DataModel::Workspace) };
}

std::uint64_t rbx::c_datamodel::get_game_id()
{
	return memory->read<std::uint64_t>(address + Offsets::DataModel::GameId);
}

std::uint64_t rbx::c_datamodel::get_place_id()
{
	uint64_t pid = memory->read<std::uint64_t>(address + Offsets::DataModel::PlaceId);
	if (pid > 0)
		return pid;

	return memory->read<std::uint64_t>(address + Offsets::DataModel::GameId);
}

bool rbx::c_datamodel::is_mm2()
{
	if (address == 0)
		return false;

	uint64_t pid = get_place_id();
	uint64_t gid = get_game_id();
	if (pid == 142823291ULL || gid == 5829288ULL || pid == 5829288ULL)
		return true;

	auto ws = get_workspace();
	if (ws.address != 0)
	{
		if (ws.find_first_child("CoinContainer") != 0 || ws.find_first_child("Lobby") != 0)
			return true;
	}
	return false;
}

bool rbx::c_datamodel::is_arsenal()
{
	if (address == 0)
		return false;

	uint64_t pid = get_place_id();
	uint64_t gid = get_game_id();
	if (pid == 286090429ULL || gid == 111958650ULL || pid == 111958650ULL)
		return true;
	return false;
}

std::uint64_t rbx::c_datamodel::get_creator_id()
{
	return memory->read<std::uint64_t>(address + Offsets::DataModel::CreatorId);
}

std::string rbx::c_datamodel::get_job_id()
{
	return memory->read_string(address + Offsets::DataModel::JobId);
}

std::string rbx::c_datamodel::get_server_ip()
{
	return memory->read_string(address + Offsets::DataModel::ServerIP);
}

math::vector2 rbx::c_visualengine::get_dimensions()
{
	static thread_local math::vector2 s_cached_dims{ 0.f, 0.f };
	static thread_local std::chrono::steady_clock::time_point s_last_fetch{};
	auto now = std::chrono::steady_clock::now();
	if (s_cached_dims.x > 0.f && std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_fetch).count() < 100)
	{
		return s_cached_dims;
	}
	s_last_fetch = now;

	HWND roblox_window = game::get_roblox_window();
	if (roblox_window)
	{
		RECT client_rect{};
		if (GetClientRect(roblox_window, &client_rect))
		{
			s_cached_dims = { (float)(client_rect.right - client_rect.left), (float)(client_rect.bottom - client_rect.top) };
			return s_cached_dims;
		}
	}
	s_cached_dims = { (float)GetSystemMetrics(SM_CXSCREEN), (float)GetSystemMetrics(SM_CYSCREEN) };
	return s_cached_dims;
}

math::matrix4 rbx::c_visualengine::get_viewmatrix()
{
	return { memory->read<math::matrix4>(address + Offsets::VisualEngine::ViewMatrix) };
}

bool rbx::c_visualengine::world_to_screen(const math::matrix4& view, const math::vector2& dims, const math::vector3& world, math::vector2& out)
{
	math::vector4 clip = view.multiply({ world.x, world.y, world.z, 1.0f });

	if (clip.w < 0.1f)
	{
		return false;
	}

	clip.x /= clip.w;
	clip.y /= clip.w;

	out.x = (dims.x * 0.5f * clip.x) + (dims.x * 0.5f);
	out.y = -(dims.y * 0.5f * clip.y) + (dims.y * 0.5f);

	if (g_in_render_context)
	{
		static thread_local POINT s_cached_origin{ 0, 0 };
		static thread_local bool s_has_origin = false;
		static thread_local std::chrono::steady_clock::time_point s_last_fetch{};

		auto now = std::chrono::steady_clock::now();
		if (!s_has_origin || std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_fetch).count() >= 100)
		{
			s_last_fetch = now;
			HWND roblox_window = game::get_roblox_window();
			POINT pt{ 0, 0 };
			if (roblox_window && ClientToScreen(roblox_window, &pt))
			{
				s_cached_origin = pt;
				s_has_origin = true;
			}
		}

		if (s_has_origin)
		{
			out.x += static_cast<float>(s_cached_origin.x);
			out.y += static_cast<float>(s_cached_origin.y);
		}
	}

	return true;
}

std::uint64_t rbx::c_silent_help::cached_input_object = 0;

static std::uint64_t get_current_input_object(std::uint64_t base_address)
{
	std::uint64_t object_address = memory->read<std::uint64_t>(base_address + Offsets::MouseService::InputObject + sizeof(std::shared_ptr<void*>));
	return object_address;
}

void rbx::c_silent_help::initialize_mouse_service(std::uint64_t address)
{
	cached_input_object = get_current_input_object(address);

	if (cached_input_object && cached_input_object != 0xFFFFFFFFFFFFFFFF)
	{
		const char* base_pointer = reinterpret_cast<const char*>(cached_input_object);
		_mm_prefetch(base_pointer + Offsets::MouseService::MousePosition, _MM_HINT_T0);
		_mm_prefetch(base_pointer + Offsets::MouseService::MousePosition + sizeof(math::vector2), _MM_HINT_T0);
	}
}

void rbx::c_silent_help::write_mouse_position(std::uint64_t address, float x, float y)
{
	cached_input_object = get_current_input_object(address);
	if (cached_input_object != 0 && cached_input_object != 0xFFFFFFFFFFFFFFFF)
	{
		math::vector2 new_position = { x, y };
		memory->write<math::vector2>(cached_input_object + Offsets::MouseService::MousePosition, new_position);
	}
}
