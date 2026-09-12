#include "antidump.h"

#ifndef _DEBUG

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace anti_dump
{
	void erase_pe_header()
	{
		DWORD oldProtect = 0;
		char* baseAddress = reinterpret_cast<char*>(GetModuleHandleA(nullptr));
		if (baseAddress)
		{
			VirtualProtect(baseAddress, 4096, PAGE_READWRITE, &oldProtect);
			SecureZeroMemory(baseAddress, 4096);
			VirtualProtect(baseAddress, 4096, oldProtect, &oldProtect);
		}
	}

	void protect_sections()
	{
		DWORD oldProtect = 0;
		char* baseAddress = reinterpret_cast<char*>(GetModuleHandleA(nullptr));
		if (baseAddress)
		{
			VirtualProtect(baseAddress, 4096, PAGE_NOACCESS, &oldProtect);
		}
	}
}

#else

namespace anti_dump
{
	void erase_pe_header() {}
	void protect_sections() {}
}

#endif
