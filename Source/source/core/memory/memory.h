#pragma once
#include <windows.h>
#include <TlHelp32.h>
#include <vector>
#include <memory>
#include <string>
#include <mutex>

using fnNtReadVirtualMemory = LONG(NTAPI*)(HANDLE, PVOID, PVOID, ULONG_PTR, PULONG_PTR);
using fnNtWriteVirtualMemory = LONG(NTAPI*)(HANDLE, PVOID, PVOID, ULONG_PTR, PULONG_PTR);
inline fnNtReadVirtualMemory pNtReadVirtualMemory = nullptr;
inline fnNtWriteVirtualMemory pNtWriteVirtualMemory = nullptr;

inline void init_ntdll_ptrs() {
	static bool inited = []() {
		HMODULE ntdll = GetModuleHandleA("ntdll.dll");
		if (ntdll) {
			pNtReadVirtualMemory = (fnNtReadVirtualMemory)GetProcAddress(ntdll, "NtReadVirtualMemory");
			pNtWriteVirtualMemory = (fnNtWriteVirtualMemory)GetProcAddress(ntdll, "NtWriteVirtualMemory");
		}
		return true;
	}();
	(void)inited;
}

inline std::uintptr_t Luck_ReadVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG NumberOfBytesToRead, PULONG NumberOfBytesRead)
{
	init_ntdll_ptrs();
	if (pNtReadVirtualMemory) {
		ULONG_PTR bytes_read = 0;
		LONG status = pNtReadVirtualMemory(ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, &bytes_read);
		if (NumberOfBytesRead) *NumberOfBytesRead = (ULONG)bytes_read;
		return (status == 0) ? 0 : 1;
	}
	SIZE_T bytes_read = 0;
	BOOL ok = ReadProcessMemory(ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, &bytes_read);
	if (NumberOfBytesRead) *NumberOfBytesRead = (ULONG)bytes_read;
	return ok ? 0 : 1;
}

inline std::uintptr_t Luck_WriteVirtualMemory(HANDLE Processhandle, PVOID BaseAddress, PVOID Buffer, ULONG NumberOfBytesToWrite, PULONG NumberOfBytesWritten)
{
	init_ntdll_ptrs();
	if (pNtWriteVirtualMemory) {
		ULONG_PTR bytes_written = 0;
		LONG status = pNtWriteVirtualMemory(Processhandle, BaseAddress, Buffer, NumberOfBytesToWrite, &bytes_written);
		if (NumberOfBytesWritten) *NumberOfBytesWritten = (ULONG)bytes_written;
		return (status == 0) ? 0 : 1;
	}
	SIZE_T bytes_written = 0;
	BOOL ok = WriteProcessMemory(Processhandle, BaseAddress, Buffer, NumberOfBytesToWrite, &bytes_written);
	if (NumberOfBytesWritten) *NumberOfBytesWritten = (ULONG)bytes_written;
	return ok ? 0 : 1;
}

class c_memory final
{
public:
	std::uint32_t m_process_id{};
	std::uint64_t m_base_address{};
	HANDLE m_process_handle{};
	std::mutex m_handle_mtx{};

	c_memory() = default;
	~c_memory() = default;

	std::uint32_t find_process_id(std::string_view process_name);
	std::uint64_t find_module_address(std::string_view module_name);

	bool attach_to_process(std::string_view process_name);
	bool is_alive();
	bool is_attached();
	void detach();
	bool get_exit_code(DWORD* code);

	std::uint64_t get_base() const { return m_base_address; }
	std::uint64_t get_module_base() const { return m_base_address; }
	std::uint64_t get_module_address() const { return m_base_address; }
	std::uint32_t get_pid() const { return m_process_id; }
	std::uint32_t get_process_id() const { return m_process_id; }
	HANDLE get_process_handle() const { return m_process_handle; }

	template <typename T>
	T read(const std::uint64_t& address);

	template <typename T>
	void write(const std::uint64_t& address, T value);

	std::string read_string(const std::uint64_t& address);
	void write_string(const std::uint64_t& address, const std::string& str);
	bool read_raw(const std::uint64_t& address, void* buffer, size_t size);
	bool write_raw(const std::uint64_t& address, const void* buffer, size_t size);
	bool protect_raw(const std::uint64_t& address, size_t size, DWORD new_protect, DWORD* old_protect);

};

inline bool c_memory::read_raw(const std::uint64_t& address, void* buffer, size_t size)
{
	if (!buffer || size == 0 || address < 0x10000 || address > 0x7FFFFFFFFFFFull)
		return false;

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return false;

	init_ntdll_ptrs();
	if (pNtReadVirtualMemory) {
		ULONG_PTR bytes_read = 0;
		LONG status = pNtReadVirtualMemory(handle, reinterpret_cast<void*>(address), buffer, static_cast<ULONG>(size), &bytes_read);
		return (status == 0 && bytes_read == size);
	}
	SIZE_T bytes_read = 0;
	return ReadProcessMemory(handle, reinterpret_cast<void*>(address), buffer, size, &bytes_read) && (bytes_read == size);
}

inline bool c_memory::write_raw(const std::uint64_t& address, const void* buffer, size_t size)
{
	if (!buffer || size == 0 || address < 0x10000 || address > 0x7FFFFFFFFFFFull)
		return false;

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return false;

	init_ntdll_ptrs();
	if (pNtWriteVirtualMemory) {
		ULONG_PTR bytes_written = 0;
		LONG status = pNtWriteVirtualMemory(handle, reinterpret_cast<void*>(address), const_cast<void*>(buffer), static_cast<ULONG>(size), &bytes_written);
		return (status == 0 && bytes_written == size);
	}
	SIZE_T bytes_written = 0;
	return WriteProcessMemory(handle, reinterpret_cast<void*>(address), buffer, size, &bytes_written) && (bytes_written == size);
}

inline bool c_memory::protect_raw(const std::uint64_t& address, size_t size, DWORD new_protect, DWORD* old_protect)
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull || size == 0)
		return false;

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return false;

	DWORD old = 0;
	BOOL ok = VirtualProtectEx(handle, reinterpret_cast<LPVOID>(address), size, new_protect, &old);
	if (old_protect)
		*old_protect = old;
	return ok != FALSE;
}

template <typename T>
T c_memory::read(const std::uint64_t& address)
{
	T buffer{};
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull)
	{
		return buffer;
	}

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
	{
		return buffer;
	}

	init_ntdll_ptrs();
	if (pNtReadVirtualMemory) {
		ULONG_PTR bytes_read = 0;
		pNtReadVirtualMemory(handle, reinterpret_cast<void*>(address), &buffer, sizeof(T), &bytes_read);
	} else {
		SIZE_T bytes_read = 0;
		ReadProcessMemory(handle, reinterpret_cast<void*>(address), &buffer, sizeof(T), &bytes_read);
	}

	return buffer;
}

template <typename T>
void c_memory::write(const std::uint64_t& address, T value)
{
	if (address < 0x10000 || address > 0x7FFFFFFFFFFFull)
	{
		return;
	}

	const HANDLE handle = m_process_handle;
	if (!handle || handle == INVALID_HANDLE_VALUE)
	{
		return;
	}

	init_ntdll_ptrs();
	if (pNtWriteVirtualMemory) {
		ULONG_PTR bytes_written = 0;
		pNtWriteVirtualMemory(handle, reinterpret_cast<void*>(address), &value, sizeof(T), &bytes_written);
	} else {
		SIZE_T bytes_written = 0;
		WriteProcessMemory(handle, reinterpret_cast<void*>(address), &value, sizeof(T), &bytes_written);
	}
}

inline std::unique_ptr<c_memory> memory = std::make_unique<c_memory>();