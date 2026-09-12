#include "memory.h"

std::uint32_t c_memory::find_process_id(std::string_view process_name)
{
	std::uint32_t process_id{};
	HANDLE snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL) };

	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return process_id;
	}

	PROCESSENTRY32 process_entry{};
	process_entry.dwSize = { sizeof(PROCESSENTRY32) };

	if (Process32First(snapshot, &process_entry))
	{
		do
		{
			if (_stricmp(process_name.data(), process_entry.szExeFile) == 0)
			{
				process_id = { process_entry.th32ProcessID };
				m_process_id = { process_id };
				break;
			}
		} while (Process32Next(snapshot, &process_entry));
	}

	CloseHandle(snapshot);
	return process_id;
}

std::uint64_t c_memory::find_module_address(std::string_view module_name)
{
	std::uint64_t module_address{};

	HANDLE handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_handle_mtx);
		handle = m_process_handle;
	}
	if (!handle || handle == INVALID_HANDLE_VALUE)
	{
		return module_address;
	}

	DWORD pid = GetProcessId(handle);
	if (pid == 0)
	{
		pid = m_process_id;
	}
	if (pid == 0)
	{
		return module_address;
	}

	for (int retry = 0; retry < 5; ++retry)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (snapshot != INVALID_HANDLE_VALUE)
		{
			MODULEENTRY32 module_entry{};
			module_entry.dwSize = sizeof(MODULEENTRY32);

			if (Module32First(snapshot, &module_entry))
			{
				do
				{
					if (_stricmp(module_name.data(), module_entry.szModule) == 0)
					{
						module_address = reinterpret_cast<std::uint64_t>(module_entry.modBaseAddr);
						m_base_address = module_address;
						break;
					}
				} while (Module32Next(snapshot, &module_entry));
			}

			CloseHandle(snapshot);
			if (module_address != 0)
			{
				break;
			}
		}
		Sleep(50);
	}

	return module_address;
}

bool c_memory::attach_to_process(std::string_view process_name)
{
	if (m_process_id == 0)
	{
		m_process_id = find_process_id(process_name);
		if (m_process_id == 0)
		{
			return false;
		}
	}

	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, m_process_id);
	if (!process || process == INVALID_HANDLE_VALUE)
	{
		process = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, m_process_id);
	}

	if (!process || process == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_handle_mtx);
		if (m_process_handle && m_process_handle != INVALID_HANDLE_VALUE && m_process_handle != process)
		{
			CloseHandle(m_process_handle);
		}
		m_process_handle = process;
	}

	return true;
}

std::string c_memory::read_string(const std::uint64_t& address)
{
	if (address < 0x10000 || address >= 0x7FFFFFFFFFFFull)
		return "str_error";

	auto string_length = read<std::uint64_t>(address + 0x10);
	auto capacity = read<std::uint64_t>(address + 0x18);

	if (string_length == 0 || string_length > 2048)
		return "str_error";

	auto string_address = (capacity >= 16 || string_length >= 16) ? read<std::uint64_t>(address) : address;
	if (string_address < 0x10000 || string_address >= 0x7FFFFFFFFFFFull)
		return "str_error";

	std::string buffer;
	buffer.resize(static_cast<std::size_t>(string_length));

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return "str_error";

	init_ntdll_ptrs();
	if (pNtReadVirtualMemory) {
		ULONG_PTR bytes_read = 0;
		pNtReadVirtualMemory(handle, reinterpret_cast<void*>(string_address), buffer.data(), (ULONG)string_length, &bytes_read);
		if (bytes_read != string_length) return "str_error";
	} else {
		SIZE_T bytes_read = 0;
		if (!ReadProcessMemory(handle, reinterpret_cast<void*>(string_address), buffer.data(), string_length, &bytes_read) || bytes_read != string_length)
			return "str_error";
	}

	return buffer;
}

void c_memory::write_string(const std::uint64_t& address, const std::string& str)
{
	int new_len = static_cast<int>(str.length());
	int old_len = read<std::int32_t>(address + 0x10);
	std::uint64_t target_cap = read<std::uint64_t>(address + 0x18);

	std::uint64_t dest_address = address;
	bool is_heap = (target_cap >= 16 || old_len >= 16);
	if (is_heap)
	{
		dest_address = read<std::uint64_t>(address);
	}

	if (dest_address == 0)
		return;

	int max_cap = is_heap ? static_cast<int>(target_cap) : 15;
	if (new_len > max_cap)
		return;

	write<std::int32_t>(address + 0x10, new_len);

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return;

	init_ntdll_ptrs();
	if (pNtWriteVirtualMemory) {
		ULONG_PTR bytes_written = 0;
		pNtWriteVirtualMemory(handle, reinterpret_cast<void*>(dest_address), (void*)str.c_str(), new_len + 1, &bytes_written);
	} else {
		SIZE_T bytes_written = 0;
		WriteProcessMemory(handle, reinterpret_cast<void*>(dest_address), (void*)str.c_str(), new_len + 1, &bytes_written);
	}
}

bool c_memory::is_alive()
{
	std::lock_guard<std::mutex> lock(m_handle_mtx);
	if (!m_process_handle || m_process_handle == INVALID_HANDLE_VALUE)
		return false;
	DWORD exit_code = 0;
	if (GetExitCodeProcess(m_process_handle, &exit_code))
	{
		return (exit_code == STILL_ACTIVE);
	}
	return false;
}

bool c_memory::is_attached()
{
	std::lock_guard<std::mutex> lock(m_handle_mtx);
	return (m_process_handle != nullptr && m_process_handle != INVALID_HANDLE_VALUE);
}

void c_memory::detach()
{
	std::lock_guard<std::mutex> lock(m_handle_mtx);
	if (m_process_handle && m_process_handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_process_handle);
		m_process_handle = nullptr;
	}
	m_process_id = 0;
	m_base_address = 0;
}

bool c_memory::get_exit_code(DWORD* code)
{
	std::lock_guard<std::mutex> lock(m_handle_mtx);
	if (!m_process_handle || m_process_handle == INVALID_HANDLE_VALUE)
		return false;
	return GetExitCodeProcess(m_process_handle, code) != FALSE;
}



