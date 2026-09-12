#include "creator.h"
#include <sdk/offsets/offsets.h>
#include <sdk/offsets/rva.h>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <core/memory/memory.h>

#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
	constexpr std::size_t k_stub_needed = 0x280;
	constexpr std::size_t k_rtti_pad = 0x40;

#pragma pack(push, 1)
	struct hook_ctrl_t {
		std::uint64_t real_step_impl;
		std::uint64_t fn_canonical_lookup;
		std::uint64_t fn_desc_lookup;
		std::uint64_t fn_set_parent;
		std::uint8_t cmd_flag;
		std::uint8_t pad0[3];
		std::uint8_t done_flag;
		std::uint8_t stage;
		std::uint8_t pad1[2];
		std::uint64_t parent_ptr;
		std::uint64_t out_instance;
		std::uint64_t out_refcount;
		std::uint64_t scratch_a;
		std::uint64_t scratch_b;
		char class_name_buf[64];
		std::uint64_t fn_set_creator;
		std::uint64_t creator_a;
		std::uint64_t creator_b;
		std::uint64_t tick_counter;
		std::uint8_t call_flag;
		std::uint8_t call_done;
		std::uint8_t pad2[6];
		std::uint64_t call_fn;
		std::uint64_t call_arg1;
		std::uint64_t call_arg2;
	};
#pragma pack(pop)

	std::mutex g_mutex;
	std::string g_last_error;
	std::uint64_t g_stub_addr = 0;
	std::uint64_t g_ctrl_addr = 0;
	std::uint64_t g_vtbl_addr = 0;
	std::uint64_t g_real_vtbl = 0;
	std::uint64_t g_hb_task = 0;
	std::uint64_t g_last_base = 0;
	bool g_hook_installed = false;
	bool g_caves_ready = false;

	auto set_error(const char* msg) -> void {
		g_last_error = msg ? msg : "";
	}

	auto looks_valid(std::uint64_t address) -> bool {
		return address > 0x10000 && address < 0x7FFFFFFEFFFFULL;
	}

	auto push_bytes(std::vector<std::uint8_t>& out, std::initializer_list<std::uint8_t> bytes) -> void {
		for (auto b : bytes)
			out.push_back(b);
	}

	auto push_u64(std::vector<std::uint8_t>& out, std::uint64_t value) -> void {
		for (int i = 0; i < 8; i++)
			out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
	}

	auto push_u32(std::vector<std::uint8_t>& out, std::uint32_t value) -> void {
		for (int i = 0; i < 4; i++)
			out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
	}

	auto mov_rax_imm64(std::vector<std::uint8_t>& out, std::uint64_t value) -> void {
		push_bytes(out, { 0x48, 0xB8 });
		push_u64(out, value);
	}

	auto mov_rcx_imm64(std::vector<std::uint8_t>& out, std::uint64_t value) -> void {
		push_bytes(out, { 0x48, 0xB9 });
		push_u64(out, value);
	}

	auto mov_rdx_imm64(std::vector<std::uint8_t>& out, std::uint64_t value) -> void {
		push_bytes(out, { 0x48, 0xBA });
		push_u64(out, value);
	}

	auto mov_r9_imm64(std::vector<std::uint8_t>& out, std::uint64_t value) -> void {
		push_bytes(out, { 0x49, 0xB9 });
		push_u64(out, value);
	}

	auto call_rax(std::vector<std::uint8_t>& out) -> void {
		push_bytes(out, { 0xFF, 0xD0 });
	}

	auto mov_r64_ptr_rax_disp32(std::vector<std::uint8_t>& out, std::uint8_t reg_field, std::uint32_t disp) -> void {
		push_bytes(out, { 0x48, 0x8B, static_cast<std::uint8_t>(0x80 | (reg_field << 3)) });
		push_u32(out, disp);
	}

	auto patch_rel32(std::vector<std::uint8_t>& code, std::size_t field_pos, std::size_t target) -> void {
		auto delta = static_cast<std::int32_t>(target - (field_pos + 4));
		std::memcpy(code.data() + field_pos, &delta, sizeof(delta));
	}

	auto mov_stage(std::vector<std::uint8_t>& code, std::uint64_t ctrl, std::uint32_t off_stage, std::uint8_t value) -> void {
		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0xC6, 0x80 });
		push_u32(code, off_stage);
		push_bytes(code, { value });
	}

	auto build_stub(std::uint64_t ctrl, std::uint64_t fn_canonical, std::uint64_t fn_desc, std::uint64_t fn_set_creator, std::uint64_t fn_set_parent) -> std::vector<std::uint8_t> {
		std::vector<std::uint8_t> code;
		code.reserve(k_stub_needed);

		auto off_cmd = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, cmd_flag));
		auto off_done = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, done_flag));
		auto off_out_instance = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, out_instance));
		auto off_out_refcount = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, out_refcount));
		auto off_parent = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, parent_ptr));
		auto off_name = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, class_name_buf));
		auto off_creator_a = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, creator_a));
		auto off_call_flag = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, call_flag));
		auto off_call_done = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, call_done));
		auto off_call_fn = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, call_fn));
		auto off_call_arg1 = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, call_arg1));
		auto off_call_arg2 = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, call_arg2));
		auto off_real_step = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, real_step_impl));
		auto off_stage = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, stage));
		auto off_tick = static_cast<std::uint32_t>(offsetof(hook_ctrl_t, tick_counter));

		push_bytes(code, { 0x51 });
		push_bytes(code, { 0x52 });
		push_bytes(code, { 0x41, 0x50 });
		push_bytes(code, { 0x41, 0x51 });

		push_bytes(code, { 0x48, 0x83, 0xEC, 0x28 });

		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0x48, 0xFF, 0x80 });
		push_u32(code, off_tick);

		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0x44, 0x0F, 0xB6, 0x90 });
		push_u32(code, off_cmd);
		push_bytes(code, { 0x45, 0x85, 0xD2 });

		auto jz_check_call = code.size();
		push_bytes(code, { 0x0F, 0x84 });
		push_u32(code, 0);

		mov_stage(code, ctrl, off_stage, 1);

		mov_rcx_imm64(code, ctrl);
		push_bytes(code, { 0x48, 0x81, 0xC1 });
		push_u32(code, off_name);
		mov_rax_imm64(code, fn_canonical);
		call_rax(code);

		push_bytes(code, { 0x48, 0x89, 0xC1 });
		mov_stage(code, ctrl, off_stage, 2);
		mov_rax_imm64(code, fn_desc);
		call_rax(code);

		push_bytes(code, { 0x48, 0x85, 0xC0 });
		auto jz_create_fail_1 = code.size();
		push_bytes(code, { 0x0F, 0x84 });
		push_u32(code, 0);

		push_bytes(code, { 0x48, 0x89, 0xC1 });
		mov_stage(code, ctrl, off_stage, 3);
		mov_rdx_imm64(code, ctrl);
		push_bytes(code, { 0x48, 0x81, 0xC2 });
		push_u32(code, off_out_instance);
		push_bytes(code, { 0x4D, 0x31, 0xC0 });
		mov_r9_imm64(code, 2);
		push_bytes(code, { 0x48, 0x8B, 0x01 });
		push_bytes(code, { 0x48, 0x8B, 0x00 });
		call_rax(code);

		mov_rax_imm64(code, ctrl);
		mov_r64_ptr_rax_disp32(code, 0, off_out_instance);
		push_bytes(code, { 0x48, 0x85, 0xC0 });
		auto jz_create_fail_2 = code.size();
		push_bytes(code, { 0x0F, 0x84 });
		push_u32(code, 0);

		push_bytes(code, { 0x48, 0x89, 0xC1 });
		mov_stage(code, ctrl, off_stage, 4);
		mov_rdx_imm64(code, ctrl);
		push_bytes(code, { 0x48, 0x81, 0xC2 });
		push_u32(code, off_creator_a);
		mov_rax_imm64(code, fn_set_creator);
		call_rax(code);

		mov_stage(code, ctrl, off_stage, 5);

		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0x4C, 0x8B, 0x90 });
		push_u32(code, off_parent);
		push_bytes(code, { 0x4D, 0x85, 0xD2 });
		auto jz_skip_parent = code.size();
		push_bytes(code, { 0x0F, 0x84 });
		push_u32(code, 0);

		mov_rax_imm64(code, ctrl);
		mov_r64_ptr_rax_disp32(code, 1, off_out_instance);
		push_bytes(code, { 0x4C, 0x89, 0xD2 });
		mov_rax_imm64(code, fn_set_parent);
		call_rax(code);

		auto jmp_create_done_1 = code.size();
		push_bytes(code, { 0xE9 });
		push_u32(code, 0);

		patch_rel32(code, jz_skip_parent + 2, jmp_create_done_1);

		auto create_fail_pos = code.size();
		mov_stage(code, ctrl, off_stage, 0xFE);
		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0x48, 0xC7, 0x80 });
		push_u32(code, off_out_instance);
		push_u32(code, 0);
		push_bytes(code, { 0x48, 0xC7, 0x80 });
		push_u32(code, off_out_refcount);
		push_u32(code, 0);

		auto create_done_pos = code.size();
		mov_stage(code, ctrl, off_stage, 0xFF);
		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0xC6, 0x80 });
		push_u32(code, off_cmd);
		push_bytes(code, { 0x00 });
		push_bytes(code, { 0xC6, 0x80 });
		push_u32(code, off_done);
		push_bytes(code, { 0x01 });

		auto jmp_tail_1 = code.size();
		push_bytes(code, { 0xE9 });
		push_u32(code, 0);

		auto check_call_pos = code.size();
		patch_rel32(code, jz_check_call + 2, check_call_pos);

		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0x44, 0x0F, 0xB6, 0x90 });
		push_u32(code, off_call_flag);
		push_bytes(code, { 0x45, 0x85, 0xD2 });
		auto jz_tail = code.size();
		push_bytes(code, { 0x0F, 0x84 });
		push_u32(code, 0);

		mov_r64_ptr_rax_disp32(code, 1, off_call_arg1);
		mov_r64_ptr_rax_disp32(code, 2, off_call_arg2);
		push_bytes(code, { 0x4C, 0x8B, 0x98 });
		push_u32(code, off_call_fn);
		push_bytes(code, { 0x41, 0xFF, 0xD3 });

		mov_rax_imm64(code, ctrl);
		push_bytes(code, { 0xC6, 0x80 });
		push_u32(code, off_call_flag);
		push_bytes(code, { 0x00 });
		push_bytes(code, { 0xC6, 0x80 });
		push_u32(code, off_call_done);
		push_bytes(code, { 0x01 });

		auto tail_pos = code.size();
		push_bytes(code, { 0x48, 0x83, 0xC4, 0x28 });
		push_bytes(code, { 0x41, 0x59 });
		push_bytes(code, { 0x41, 0x58 });
		push_bytes(code, { 0x5A });
		push_bytes(code, { 0x59 });
		mov_rax_imm64(code, ctrl);
		mov_r64_ptr_rax_disp32(code, 0, off_real_step);
		push_bytes(code, { 0xFF, 0xE0 });

		patch_rel32(code, jz_create_fail_1 + 2, create_fail_pos);
		patch_rel32(code, jz_create_fail_2 + 2, create_fail_pos);
		patch_rel32(code, jmp_create_done_1 + 1, create_done_pos);
		patch_rel32(code, jmp_tail_1 + 1, tail_pos);
		patch_rel32(code, jz_tail + 2, tail_pos);

		return code;
	}

	auto dig_module_cave(const wchar_t* module_name, std::size_t needed) -> std::uint64_t {
		auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, memory->m_process_id);
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);

		std::uint64_t base = 0;
		DWORD size = 0;

		if (Module32FirstW(snapshot, &entry)) {
			do {
				if (_wcsicmp(entry.szModule, module_name) == 0) {
					base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
					size = entry.modBaseSize;
					break;
				}
			} while (Module32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);

		if (!base || !size)
			return 0;

		constexpr std::size_t chunk_size = 0x1000;
		std::vector<std::uint8_t> chunk(chunk_size);

		for (auto scan = base + chunk_size; scan + chunk_size < base + size; scan += chunk_size - needed) {
			if (!memory->read_raw(scan, chunk.data(), static_cast<std::uint32_t>(chunk_size)))
				continue;

			std::size_t run = 0;
			for (std::size_t i = 0; i < chunk_size; i++) {
				if (chunk[i] == 0xCC || chunk[i] == 0x00) {
					if (++run >= needed)
						return scan + i - run + 1;
				} else {
					run = 0;
				}
			}
		}

		return 0;
	}

	auto dig_section_cave(const wchar_t* module_name, const char* section_name, std::size_t needed) -> std::uint64_t {
		auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, memory->m_process_id);
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);

		std::uint64_t base = 0;

		if (Module32FirstW(snapshot, &entry)) {
			do {
				if (_wcsicmp(entry.szModule, module_name) == 0) {
					base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
					break;
				}
			} while (Module32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);

		if (!base)
			return 0;

		IMAGE_DOS_HEADER dos{};
		if (!memory->read_raw(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
			return 0;

		IMAGE_NT_HEADERS64 nt{};
		if (!memory->read_raw(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
			return 0;

		auto section_table = base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;

		for (WORD i = 0; i < nt.FileHeader.NumberOfSections; i++) {
			IMAGE_SECTION_HEADER section{};
			if (!memory->read_raw(section_table + i * sizeof(section), &section, sizeof(section)))
				continue;

			char name[9]{};
			std::memcpy(name, section.Name, 8);
			if (_stricmp(name, section_name) != 0)
				continue;

			auto section_base = base + section.VirtualAddress;
			auto section_size = section.Misc.VirtualSize;
			constexpr std::size_t chunk_size = 0x1000;
			std::vector<std::uint8_t> chunk(chunk_size);

			for (auto scan = section_base; scan + chunk_size < section_base + section_size; scan += chunk_size - needed) {
				if (!memory->read_raw(scan, chunk.data(), static_cast<std::uint32_t>(chunk_size)))
					continue;

				std::size_t run = 0;
				for (std::size_t j = 0; j < chunk_size; j++) {
					if (chunk[j] == 0x00) {
						if (++run >= needed)
							return scan + j - run + 1;
					} else {
						run = 0;
					}
				}
			}
		}

		return 0;
	}

	auto roblox_module_range(std::uint64_t& out_base, std::uint64_t& out_end) -> bool {
		auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, memory->m_process_id);
		if (snapshot == INVALID_HANDLE_VALUE)
			return false;

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);

		bool found = false;
		if (Module32FirstW(snapshot, &entry)) {
			do {
				if (_wcsicmp(entry.szModule, L"RobloxPlayerBeta.exe") == 0) {
					out_base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
					out_end = out_base + entry.modBaseSize;
					found = true;
					break;
				}
			} while (Module32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);
		return found && out_base && out_end > out_base;
	}

	auto in_roblox_module(std::uint64_t address) -> bool {
		std::uint64_t base = 0, end = 0;
		if (!roblox_module_range(base, end))
			return false;
		return address >= base && address < end;
	}

	auto recover_real_step_from_hijacked(std::uint64_t hijacked_vtbl, std::size_t vtbl_slots) -> std::uint64_t {
		auto vtbl_bytes = vtbl_slots * sizeof(std::uint64_t);
		auto ctrl_addr = (hijacked_vtbl + vtbl_bytes + 0xF) & ~static_cast<std::uint64_t>(0xF);
		auto real_step = memory->read<std::uint64_t>(ctrl_addr + offsetof(hook_ctrl_t, real_step_impl));
		if (!looks_valid(real_step) || !in_roblox_module(real_step))
			return 0;
		return real_step;
	}

	auto live_datamodel() -> std::uint64_t {
		auto mod_base = memory->m_base_address;
		if (mod_base) {
			auto base = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
			if (!base || base < 0x10000 || base > 0x7FFFFFFFFFFFull)
				base = mod_base;

			auto fake_dm = memory->read<std::uint64_t>(base + RVA::FakeDataModel::Pointer);
			if (looks_valid(fake_dm)) {
				auto real_dm = memory->read<std::uint64_t>(fake_dm + Offsets::FakeDataModel::RealDataModel);
				if (looks_valid(real_dm))
					return real_dm;
			}
		}

		if (game::datamodel && looks_valid(game::datamodel->address))
			return game::datamodel->address;

		return 0;
	}

	auto instance_in_current_datamodel(std::uint64_t instance) -> bool {
		if (!looks_valid(instance))
			return false;

		auto target = live_datamodel();
		if (!target)
			return false;

		auto current = instance;
		for (int i = 0; i < 32; i++) {
			if (current == target)
				return true;
			if (!looks_valid(current))
				return false;

			auto parent = memory->read<std::uint64_t>(current + Offsets::Instance::Parent);
			if (!parent || parent == current)
				return false;
			current = parent;
		}

		return false;
	}

	auto reset_state() -> void {
		g_stub_addr = 0;
		g_ctrl_addr = 0;
		g_vtbl_addr = 0;
		g_real_vtbl = 0;
		g_hb_task = 0;
		g_hook_installed = false;
		g_caves_ready = false;
	}

	auto install_hook() -> bool {
		if constexpr (!RVA::Creator::Resolved) {
			set_error("creator RVAs unresolved for this build");
			return false;
		}

		auto mod_base = memory->m_base_address;
		if (!mod_base) {
			set_error("no module base");
			return false;
		}

		auto base = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
		if (!base || base < 0x10000 || base > 0x7FFFFFFFFFFFull)
			base = mod_base;

		if (g_last_base != base) {
			reset_state();
			g_last_base = base;
		}

		auto data_model = live_datamodel();
		if (!data_model) {
			set_error("no datamodel");
			return false;
		}

		rbx::c_instance dm(data_model);
		auto run_service = dm.find_first_child_by_class("RunService");
		if (!looks_valid(run_service)) {
			set_error("no RunService");
			return false;
		}

		auto hb_task = memory->read<std::uint64_t>(run_service + Offsets::RunService::HeartbeatTask);
		if (!looks_valid(hb_task)) {
			set_error("no heartbeat task");
			return false;
		}

		if (g_hook_installed && g_hb_task == hb_task && g_vtbl_addr) {
			if (memory->read<std::uint64_t>(g_hb_task) == g_vtbl_addr)
				return true;
			g_hook_installed = false;
		}

		g_hb_task = hb_task;

		auto vtbl_slots = RVA::Creator::Pending::HeartbeatVtableSlots;
		auto step_index = RVA::Creator::Pending::JobStepVtableIndex;
		auto vtbl_bytes = vtbl_slots * sizeof(std::uint64_t);

		auto current_vtbl = memory->read<std::uint64_t>(g_hb_task);
		if (!looks_valid(current_vtbl)) {
			set_error("invalid heartbeat vtable");
			return false;
		}

		if (current_vtbl == g_vtbl_addr) {
			g_hook_installed = true;
			return true;
		}

		auto live_step = memory->read<std::uint64_t>(current_vtbl + step_index * sizeof(std::uint64_t));
		if (looks_valid(live_step) && !in_roblox_module(live_step)) {
			auto real_step = recover_real_step_from_hijacked(current_vtbl, vtbl_slots);
			if (real_step) {
				memory->write<std::uint64_t>(current_vtbl + step_index * sizeof(std::uint64_t), real_step);
			} else {
				set_error("stale hook from prior session could not be recovered");
				return false;
			}
		}

		auto data_needed = k_rtti_pad + vtbl_bytes + sizeof(hook_ctrl_t) + 0x40;

		if (!g_caves_ready) {
			auto stub_cave = dig_module_cave(L"dxgi.dll", k_stub_needed);
			if (!stub_cave)
				stub_cave = dig_module_cave(L"combase.dll", k_stub_needed);
			if (!stub_cave) {
				set_error("no stub cave");
				return false;
			}

			auto data_cave = dig_section_cave(L"dxgi.dll", ".data", data_needed);
			if (!data_cave)
				data_cave = dig_section_cave(L"combase.dll", ".data", data_needed);
			if (!data_cave)
				data_cave = dig_module_cave(L"dxgi.dll", data_needed);
			if (!data_cave)
				data_cave = dig_module_cave(L"combase.dll", data_needed);
			if (!data_cave) {
				set_error("no data cave");
				return false;
			}

			DWORD old_protect = 0;
			if (!memory->protect_raw(stub_cave, k_stub_needed, PAGE_EXECUTE_READWRITE, &old_protect)) {
				set_error("stub protect failed");
				return false;
			}
			if (!memory->protect_raw(data_cave, data_needed, PAGE_READWRITE, &old_protect)) {
				if (!memory->protect_raw(data_cave, data_needed, PAGE_EXECUTE_READWRITE, &old_protect)) {
					set_error("data protect failed");
					return false;
				}
			}

			g_stub_addr = stub_cave;
			g_vtbl_addr = (data_cave + k_rtti_pad + 7) & ~static_cast<std::uint64_t>(7);
			g_ctrl_addr = (g_vtbl_addr + vtbl_bytes + 0xF) & ~static_cast<std::uint64_t>(0xF);

			hook_ctrl_t ctrl{};
			ctrl.fn_canonical_lookup = base + RVA::Creator::Pending::CanonicalNameLookup;
			ctrl.fn_desc_lookup = base + RVA::Creator::Pending::ClassDescLookup;
			ctrl.fn_set_parent = base + RVA::Creator::Pending::InstanceSetParent;
			ctrl.fn_set_creator = base + RVA::Creator::Pending::SetCreator;

			if (!memory->write_raw(g_ctrl_addr, &ctrl, sizeof(ctrl))) {
				set_error("ctrl write failed");
				return false;
			}

			auto stub_bytes = build_stub(
				g_ctrl_addr,
				base + RVA::Creator::Pending::CanonicalNameLookup,
				base + RVA::Creator::Pending::ClassDescLookup,
				base + RVA::Creator::Pending::SetCreator,
				base + RVA::Creator::Pending::InstanceSetParent);
			if (stub_bytes.size() > k_stub_needed || !memory->write_raw(g_stub_addr, stub_bytes.data(), static_cast<std::uint32_t>(stub_bytes.size()))) {
				set_error("stub write failed");
				return false;
			}

			g_caves_ready = true;
		}

		std::vector<std::uint8_t> vtbl_copy(vtbl_bytes);
		if (!memory->read_raw(current_vtbl, vtbl_copy.data(), static_cast<std::uint32_t>(vtbl_bytes))) {
			set_error("vtable read failed");
			return false;
		}

		std::vector<std::uint8_t> rtti_buf(k_rtti_pad);
		if (memory->read_raw(current_vtbl - k_rtti_pad, rtti_buf.data(), static_cast<std::uint32_t>(k_rtti_pad)))
			memory->write_raw(g_vtbl_addr - k_rtti_pad, rtti_buf.data(), static_cast<std::uint32_t>(k_rtti_pad));

		std::uint64_t real_step = 0;
		std::memcpy(&real_step, vtbl_copy.data() + step_index * sizeof(std::uint64_t), sizeof(real_step));
		if (!looks_valid(real_step) || !in_roblox_module(real_step)) {
			set_error("heartbeat step not in roblox module");
			return false;
		}
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, real_step_impl), real_step);

		std::memcpy(vtbl_copy.data() + step_index * sizeof(std::uint64_t), &g_stub_addr, sizeof(g_stub_addr));

		if (!memory->write_raw(g_vtbl_addr, vtbl_copy.data(), static_cast<std::uint32_t>(vtbl_bytes))) {
			set_error("vtable write failed");
			return false;
		}

		g_real_vtbl = current_vtbl;

		auto hb_task_check = memory->read<std::uint64_t>(run_service + Offsets::RunService::HeartbeatTask);
		if (hb_task_check != g_hb_task || memory->read<std::uint64_t>(g_hb_task) != current_vtbl) {
			set_error("heartbeat task changed during install");
			return false;
		}

		if (!memory->write_raw(g_hb_task, &g_vtbl_addr, sizeof(g_vtbl_addr))) {
			set_error("heartbeat swap failed");
			return false;
		}

		g_hook_installed = true;
		g_last_error.clear();
		return true;
	}

	auto uninstall_hook_locked() -> void {
		if (!g_hook_installed)
			return;

		auto hb_task = g_hb_task;
		if (!looks_valid(hb_task) || !looks_valid(g_real_vtbl)) {
			reset_state();
			return;
		}

		auto live_vtbl = memory->read<std::uint64_t>(hb_task);
		if (live_vtbl == g_vtbl_addr)
			memory->write_raw(hb_task, &g_real_vtbl, sizeof(g_real_vtbl));

		reset_state();
	}

	constexpr std::size_t k_diag_slots = 8;
	constexpr std::size_t k_diag_stride = 32;

#pragma pack(push, 1)
	struct diag_ctrl_t {
		std::uint64_t counters[k_diag_slots];
		std::uint64_t real_impls[k_diag_slots];
	};
#pragma pack(pop)

	auto build_diag_stub(std::uint64_t ctrl) -> std::vector<std::uint8_t> {
		std::vector<std::uint8_t> code(k_diag_slots * k_diag_stride, 0x90);

		for (std::size_t i = 0; i < k_diag_slots; i++) {
			std::vector<std::uint8_t> piece;
			auto counter_off = static_cast<std::uint32_t>(offsetof(diag_ctrl_t, counters) + i * sizeof(std::uint64_t));
			auto impl_off = static_cast<std::uint32_t>(offsetof(diag_ctrl_t, real_impls) + i * sizeof(std::uint64_t));

			mov_rax_imm64(piece, ctrl);
			push_bytes(piece, { 0x48, 0xFF, 0x80 });
			push_u32(piece, counter_off);
			mov_r64_ptr_rax_disp32(piece, 0, impl_off);
			push_bytes(piece, { 0xFF, 0xE0 });

			std::memcpy(code.data() + i * k_diag_stride, piece.data(), piece.size());
		}

		return code;
	}

	auto scan_job_slots_locked(std::uint32_t duration_ms) -> std::string {
		auto mod_base = memory->m_base_address;
		if (!mod_base)
			return "no module base";

		auto base = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
		if (!base || base < 0x10000 || base > 0x7FFFFFFFFFFFull)
			base = mod_base;

		auto data_model = live_datamodel();
		if (!data_model)
			return "no datamodel";

		rbx::c_instance dm(data_model);
		auto run_service = dm.find_first_child_by_class("RunService");
		if (!looks_valid(run_service))
			return "no RunService";

		auto hb_task = memory->read<std::uint64_t>(run_service + Offsets::RunService::HeartbeatTask);
		if (!looks_valid(hb_task))
			return "no heartbeat task";

		auto restore_vtbl = memory->read<std::uint64_t>(hb_task);
		if (!looks_valid(restore_vtbl))
			return "invalid live vtable";

		auto vtbl_slots = RVA::Creator::Pending::HeartbeatVtableSlots;
		auto step_index = RVA::Creator::Pending::JobStepVtableIndex;
		auto vtbl_bytes = vtbl_slots * sizeof(std::uint64_t);

		std::vector<std::uint8_t> vtbl_copy(vtbl_bytes);
		if (!memory->read_raw(restore_vtbl, vtbl_copy.data(), static_cast<std::uint32_t>(vtbl_bytes)))
			return "vtable read failed";

		std::vector<std::uint64_t> true_original(vtbl_slots);
		std::memcpy(true_original.data(), vtbl_copy.data(), vtbl_bytes);

		if (g_hook_installed && restore_vtbl == g_vtbl_addr && step_index < vtbl_slots) {
			auto real_step = memory->read<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, real_step_impl));
			if (looks_valid(real_step))
				true_original[step_index] = real_step;
		}

		auto diag_data_needed = k_rtti_pad + vtbl_bytes + sizeof(diag_ctrl_t) + 0x40;
		auto diag_data_cave = dig_section_cave(L"RobloxPlayerBeta.exe", ".data", diag_data_needed);
		if (!diag_data_cave)
			diag_data_cave = dig_section_cave(L"RobloxPlayerBeta.exe", ".bss", diag_data_needed);
		if (!diag_data_cave)
			return "no diag data cave";

		auto diag_stub_cave = dig_module_cave(L"dxgi.dll", k_diag_slots * k_diag_stride);
		if (!diag_stub_cave)
			diag_stub_cave = dig_module_cave(L"combase.dll", k_diag_slots * k_diag_stride);
		if (!diag_stub_cave)
			return "no diag stub cave";

		DWORD old_protect = 0;
		if (!memory->protect_raw(diag_stub_cave, k_diag_slots * k_diag_stride, PAGE_EXECUTE_READWRITE, &old_protect))
			return "diag stub protect failed";

		auto diag_vtbl_addr = (diag_data_cave + k_rtti_pad + 7) & ~static_cast<std::uint64_t>(7);
		auto diag_ctrl_addr = (diag_vtbl_addr + vtbl_bytes + 0xF) & ~static_cast<std::uint64_t>(0xF);

		diag_ctrl_t diag{};
		for (std::size_t i = 0; i < vtbl_slots && i < k_diag_slots; i++)
			diag.real_impls[i] = true_original[i];

		if (!memory->write_raw(diag_ctrl_addr, &diag, sizeof(diag)))
			return "diag ctrl write failed";

		auto diag_stub_bytes = build_diag_stub(diag_ctrl_addr);
		if (!memory->write_raw(diag_stub_cave, diag_stub_bytes.data(), static_cast<std::uint32_t>(diag_stub_bytes.size())))
			return "diag stub write failed";

		std::vector<std::uint8_t> rtti_buf(k_rtti_pad);
		if (memory->read_raw(restore_vtbl - k_rtti_pad, rtti_buf.data(), static_cast<std::uint32_t>(k_rtti_pad)))
			memory->write_raw(diag_vtbl_addr - k_rtti_pad, rtti_buf.data(), static_cast<std::uint32_t>(k_rtti_pad));

		std::vector<std::uint8_t> diag_vtbl(vtbl_bytes);
		for (std::size_t i = 0; i < vtbl_slots; i++) {
			std::uint64_t slot_fn = (i < k_diag_slots) ? (diag_stub_cave + i * k_diag_stride) : true_original[i];
			std::memcpy(diag_vtbl.data() + i * sizeof(std::uint64_t), &slot_fn, sizeof(slot_fn));
		}

		if (!memory->write_raw(diag_vtbl_addr, diag_vtbl.data(), static_cast<std::uint32_t>(vtbl_bytes)))
			return "diag vtable write failed";

		if (!memory->write_raw(hb_task, &diag_vtbl_addr, sizeof(diag_vtbl_addr)))
			return "diag heartbeat swap failed";

		std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));

		memory->write_raw(hb_task, &restore_vtbl, sizeof(restore_vtbl));

		diag_ctrl_t result{};
		memory->read_raw(diag_ctrl_addr, &result, sizeof(result));

		std::string report = "job slot ticks over " + std::to_string(duration_ms) + "ms: ";
		for (std::size_t i = 0; i < vtbl_slots; i++) {
			report += "[" + std::to_string(i) + "]=" + std::to_string(result.counters[i]);
			if (i + 1 < vtbl_slots)
				report += " ";
		}

		return report;
	}

	auto create_locked(const char* class_name, std::uint64_t parent, std::uint32_t timeout_ms) -> std::uint64_t {
		if (!g_hook_installed || !parent) {
			set_error("hook not ready or parent missing");
			return 0;
		}

		if (!instance_in_current_datamodel(parent)) {
			set_error("parent not in datamodel");
			return 0;
		}

		auto name_len = class_name ? std::strlen(class_name) : 0;
		if (!name_len || name_len >= sizeof(hook_ctrl_t::class_name_buf)) {
			set_error("invalid class name");
			return 0;
		}

		std::uint8_t zero8 = 0;
		std::uint64_t zero64 = 0;

		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, done_flag), zero8);
		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, stage), zero8);
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, parent_ptr), parent);
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, out_instance), zero64);
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, out_refcount), zero64);

		char name_buf[sizeof(hook_ctrl_t::class_name_buf)]{};
		std::memcpy(name_buf, class_name, name_len);
		memory->write_raw(g_ctrl_addr + offsetof(hook_ctrl_t, class_name_buf), name_buf, sizeof(name_buf));

		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, cmd_flag), static_cast<std::uint8_t>(1));

		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < deadline) {
			if (memory->read<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, done_flag))) {
				auto result = memory->read<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, out_instance));
				if (!result)
					set_error("create returned null");
				else
					g_last_error.clear();
				return result;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, cmd_flag), zero8);
		set_error("create timed out");

		return 0;
	}

	auto call_locked(std::uint64_t fn, std::uint64_t arg1, std::uint64_t arg2, std::uint32_t timeout_ms) -> bool {
		if (!g_hook_installed || !g_ctrl_addr || !fn)
			return false;

		std::uint8_t zero8 = 0;

		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_fn), fn);
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_arg1), arg1);
		memory->write<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_arg2), arg2);
		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_done), zero8);
		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_flag), static_cast<std::uint8_t>(1));

		auto completed = false;
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < deadline) {
			if (memory->read<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_done))) {
				completed = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		memory->write<std::uint8_t>(g_ctrl_addr + offsetof(hook_ctrl_t, call_flag), zero8);
		if (!completed)
			set_error("call timed out");
		return completed;
	}
}

BOOL WINAPI console_ctrl_handler(DWORD ctrl) {
	switch (ctrl) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		creator::shutdown();
		return TRUE;
	default:
		return FALSE;
	}
}

auto creator::init() -> bool {
	std::lock_guard<std::mutex> lock(g_mutex);
	return install_hook();
}

auto creator::shutdown() -> void {
	std::lock_guard<std::mutex> lock(g_mutex);
	uninstall_hook_locked();
}

auto creator::notify_datamodel_lost() -> void {
	std::lock_guard<std::mutex> lock(g_mutex);
	uninstall_hook_locked();
}

auto creator::install_console_handler() -> void {
	SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}

auto creator::is_available() -> bool {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_hook_installed && g_caves_ready;
}

auto creator::create_instance(const char* class_name, std::uint64_t parent, std::uint32_t timeout_ms) -> std::uint64_t {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!install_hook())
		return 0;
	return create_locked(class_name, parent, timeout_ms);
}

auto creator::set_parent(std::uint64_t instance, std::uint64_t parent, std::uint32_t timeout_ms) -> bool {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!install_hook() || !instance)
		return false;

	auto fn = memory->read<std::uint64_t>(g_ctrl_addr + offsetof(hook_ctrl_t, fn_set_parent));
	return call_locked(fn, instance, parent, timeout_ms);
}

auto creator::destroy(std::uint64_t instance, std::uint32_t timeout_ms) -> bool {
	return set_parent(instance, 0, timeout_ms);
}

auto creator::debug_scan_job_slots(std::uint32_t duration_ms) -> std::string {
	std::lock_guard<std::mutex> lock(g_mutex);
	return scan_job_slots_locked(duration_ms);
}

auto creator::call(std::uint64_t fn, std::uint64_t arg1, std::uint64_t arg2, std::uint64_t arg3, std::uint32_t timeout_ms) -> bool {
	(void)arg3;
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!install_hook())
		return false;
	return call_locked(fn, arg1, arg2, timeout_ms);
}

auto creator::last_error() -> const char* {
	return g_last_error.c_str();
}