#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <core/memory/memory.h>

#include "offsets/offsets.h"
#include "math/math.h"

namespace rbx
{
	struct c_addressable;
	struct c_nameable;
	struct c_node;
	struct c_instance;
	struct c_player;
	struct c_model_instance;
	struct c_humanoid;
	struct c_humanoid_root_part;
	struct c_part;
	struct c_primitive;
	struct c_datamodel;
	struct c_workspace;
	struct c_visualengine;
	struct c_silent_help;

	struct c_addressable
	{
		std::uint64_t address = 0;

		c_addressable() = default;
		c_addressable(std::uint64_t address) : address(address) {}
		bool is_valid() const { return address != 0; }
	};

	struct c_nameable : public c_addressable
	{
		using c_addressable::c_addressable;

		std::string get_name() const;
		std::string get_class_name() const;
	};

	struct c_node
	{
		std::uint64_t find_first_child(std::string_view name);
		std::uint64_t find_first_child_by_class(std::string_view name);

		template <typename type>
		std::vector<type> get_children();

		std::vector<std::uint64_t> get_children();

		std::uint64_t get_parent();
		void set_parent(const std::uint64_t& parent);
	};

	struct c_instance : public c_nameable, public c_node
	{
		using c_nameable::c_nameable;

		c_primitive get_primitive();
	};

	struct c_player final : public c_instance
	{
		using c_instance::c_instance;

		c_model_instance get_model_instance();
		std::uint64_t get_team();
		std::string get_display_name();
		std::uint64_t get_user_id();
		int get_account_age();
	};

	struct c_model_instance final : public c_addressable, public c_node
	{
		using c_addressable::c_addressable;
	};

	struct c_humanoid final : public c_nameable
	{
		using c_nameable::c_nameable;

		float get_health() const;
		float get_max_health() const;

		float get_jump_power() const;
		void set_jump_power(const float& jump);
		void set_jump_power(const float& jump) const;

		float get_walk_speed() const;
		void set_walk_speed(const float& speed);
		void set_walk_speed(const float& speed) const;

		float get_hip_height() const;
		void set_hip_height(const float& height);

		std::uint8_t get_rig_type() const;
		std::uint16_t get_state() const;

		static void write_gravity(float gravity);
		static void write_tickrate(float tickrate);
	};

	using humanoid_t = c_humanoid;

	struct c_animation_track final : public c_instance
	{
		using c_instance::c_instance;

		bool is_playing();
		float get_time_position();
		std::uint64_t get_animation_id();
		std::string get_animation_name();
	};

	struct c_animator final : public c_instance
	{
		using c_instance::c_instance;

		std::vector<c_animation_track> get_active_animations();
	};

	struct c_humanoid_root_part final : public c_nameable
	{
		using c_nameable::c_nameable;

	};

	struct c_part final : public c_nameable
	{
		using c_nameable::c_nameable;

		c_primitive get_primitive() const;
	};

	struct c_primitive final : public c_addressable
	{
		using c_addressable::c_addressable;

		math::vector3 get_position() const;
		void set_position(const math::vector3& position);

		math::matrix3 get_rotation() const;
		void set_rotation(const math::matrix3& rotation);

		math::vector3 get_size() const;
		void set_size(const math::vector3& size);

		math::cframe get_cframe() const;

		void set_can_collide(bool enable);
	};

	struct c_datamodel final : public c_instance
	{
		using c_instance::c_instance;

		static std::unique_ptr<c_datamodel> get()
		{
			uintptr_t base = memory->m_base_address;
			if (!base) return std::make_unique<c_datamodel>(0);

			auto is_dm = [](uintptr_t addr) -> bool {
				if (!addr || addr < 0x10000 || addr > 0x7FFFFFFFFFFFull) return false;
				
				uintptr_t ws = memory->read<uintptr_t>(addr + Offsets::DataModel::Workspace);
				if (ws >= 0x10000 && ws < 0x7FFFFFFFFFFFull) {
					// workspace pointer is valid — that's sufficient; camera may not be ready yet
					return true;
				}

				std::string cls = rbx::c_nameable(addr).get_class_name();
				if (cls == "DataModel") return true;
				std::string nm = rbx::c_nameable(addr).get_name();
				return nm == "Game" || nm == "Ugc" || nm == "LuaApp" || nm == "DataModel";
			};

			uintptr_t front = memory->read<uintptr_t>(base + Offsets::FakeDataModel::Pointer);
			if (front && front >= 0x10000 && front < 0x7FFFFFFFFFFFull) {
				uintptr_t dm = memory->read<uintptr_t>(front + Offsets::FakeDataModel::RealDataModel);
				if (is_dm(dm)) return std::make_unique<c_datamodel>(dm);
				if (is_dm(front)) return std::make_unique<c_datamodel>(front);
			}

			uintptr_t ve = memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
			if (ve && ve >= 0x10000 && ve < 0x7FFFFFFFFFFFull) {
				uintptr_t fake = memory->read<uintptr_t>(ve + Offsets::VisualEngine::FakeDataModel);
				if (fake && fake >= 0x10000 && fake < 0x7FFFFFFFFFFFull) {
					uintptr_t dm = memory->read<uintptr_t>(fake + Offsets::FakeDataModel::RealDataModel);
					if (is_dm(dm)) return std::make_unique<c_datamodel>(dm);
					if (is_dm(fake)) return std::make_unique<c_datamodel>(fake);
				}
			}

			uintptr_t ts = memory->read<uintptr_t>(base + Offsets::TaskScheduler::Pointer);
			if (ts && ts >= 0x10000 && ts < 0x7FFFFFFFFFFFull) {
				uintptr_t job_start = memory->read<uintptr_t>(ts + Offsets::TaskScheduler::JobStart);
				uintptr_t job_end = memory->read<uintptr_t>(ts + Offsets::TaskScheduler::JobEnd);
				if (job_start >= 0x10000 && job_end > job_start && (job_end - job_start) < 0x10000) {
					size_t count = (job_end - job_start) / sizeof(uintptr_t);
					for (size_t i = 0; i < count && i < 128; ++i) {
						uintptr_t job = memory->read<uintptr_t>(job_start + i * sizeof(uintptr_t));
						if (!job || job < 0x10000 || job > 0x7FFFFFFFFFFFull) continue;
						std::string job_name = memory->read_string(job + Offsets::TaskScheduler::JobName);
						if (job_name.find("Render") != std::string::npos) {
							uintptr_t fake_dm = memory->read<uintptr_t>(job + Offsets::RenderJob::FakeDataModel);
							if (fake_dm && fake_dm >= 0x10000 && fake_dm < 0x7FFFFFFFFFFFull) {
								uintptr_t dm = memory->read<uintptr_t>(fake_dm + Offsets::FakeDataModel::RealDataModel);
								if (is_dm(dm)) return std::make_unique<c_datamodel>(dm);
								if (is_dm(fake_dm)) return std::make_unique<c_datamodel>(fake_dm);
							}
							uintptr_t real_dm = memory->read<uintptr_t>(job + Offsets::RenderJob::RealDataModel);
							if (is_dm(real_dm)) return std::make_unique<c_datamodel>(real_dm);
						}
					}
				}
			}

			return std::make_unique<c_datamodel>(0);
		}

		c_workspace get_workspace();

		std::uint64_t get_game_id();
		std::uint64_t get_place_id();
		std::uint64_t get_creator_id();

		bool is_mm2();
		bool is_arsenal();

		std::string get_job_id();
		std::string get_server_ip();
	};

	struct c_workspace final : public c_instance
	{
		using c_instance::c_instance;
	};

	struct c_visualengine final : public c_addressable
	{
		using c_addressable::c_addressable;

		static std::unique_ptr<c_visualengine> get()
		{
			uintptr_t base = memory->m_base_address;
			if (!base) return std::make_unique<c_visualengine>(0);

			auto visualengine{ memory->read<std::uint64_t>(base + Offsets::VisualEngine::Pointer) };
			if (visualengine && visualengine >= 0x10000 && visualengine < 0x7FFFFFFFFFFFull) {
				return std::make_unique<c_visualengine>(visualengine);
			}

			uintptr_t ts = memory->read<uintptr_t>(base + Offsets::TaskScheduler::Pointer);
			if (ts && ts >= 0x10000 && ts < 0x7FFFFFFFFFFFull) {
				uintptr_t job_start = memory->read<uintptr_t>(ts + Offsets::TaskScheduler::JobStart);
				uintptr_t job_end = memory->read<uintptr_t>(ts + Offsets::TaskScheduler::JobEnd);
				if (job_start >= 0x10000 && job_end > job_start && (job_end - job_start) < 0x10000) {
					size_t count = (job_end - job_start) / sizeof(uintptr_t);
					for (size_t i = 0; i < count && i < 128; ++i) {
						uintptr_t job = memory->read<uintptr_t>(job_start + i * sizeof(uintptr_t));
						if (!job || job < 0x10000 || job > 0x7FFFFFFFFFFFull) continue;
						std::string job_name = memory->read_string(job + Offsets::TaskScheduler::JobName);
						if (job_name.find("Render") != std::string::npos) {
							uintptr_t rv = memory->read<uintptr_t>(job + Offsets::RenderJob::RenderView);
							if (rv && rv >= 0x10000 && rv < 0x7FFFFFFFFFFFull) {
								return std::make_unique<c_visualengine>(rv);
							}
						}
					}
				}
			}

			return std::make_unique<c_visualengine>(0);
		}

		math::vector2 get_dimensions();
		math::matrix4 get_viewmatrix();

		bool world_to_screen(
			const math::matrix4& view,
			const math::vector2& dims,
			const math::vector3& world,
			math::vector2& out
		);
	};

	inline thread_local bool g_in_render_context = false;

	struct c_silent_help final : public c_addressable
	{
		using c_addressable::c_addressable;

		static std::uint64_t cached_input_object;
		void initialize_mouse_service(std::uint64_t address);
		void write_mouse_position(std::uint64_t address, float x, float y);
	};

	struct astika_t
	{
		std::uint64_t x;
		std::uint64_t y;
	};
}

template <typename type>
std::vector<type> rbx::c_node::get_children()
{
	std::vector<std::uint64_t> kids = get_children();
	std::vector<type> container;
	container.reserve(kids.size());
	for (std::uint64_t kid : kids)
	{
		container.push_back(type(kid));
	}
	return container;
}