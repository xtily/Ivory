#include "antiattach.h"

#ifndef _DEBUG

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <cstdlib>
#include <cstdint>
#include <string>

using NtSetInfoProcess_t = NTSTATUS(NTAPI*)(
    HANDLE ProcessHandle,
    UINT   ProcessInformationClass,
    PVOID  ProcessInformation,
    ULONG  ProcessInformationLength
);

[[noreturn]] static void die() {
    TerminateProcess(GetCurrentProcess(), 0);
    __assume(0);
}

static NtSetInfoProcess_t get_NtSIP() {
    static auto fn = reinterpret_cast<NtSetInfoProcess_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess")
    );
    return fn;
}

using NtQueryInfoProcess_t = NTSTATUS(NTAPI*)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG
);

static NtQueryInfoProcess_t get_NtQIP() {
    static auto fn = reinterpret_cast<NtQueryInfoProcess_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess")
    );
    return fn;
}

namespace anti_attach {

    void set_critical_process() {
        auto NtSIP = get_NtSIP();
        if (!NtSIP) return;

        ULONG is_critical = 1;
        NtSIP(
            GetCurrentProcess(),
            29,
            &is_critical,
            sizeof(is_critical)
        );
    }

    bool check_debug_object() {
        auto NtQIP = get_NtQIP();
        if (!NtQIP) return false;

        HANDLE dbg_obj = nullptr;
        NTSTATUS st = NtQIP(
            GetCurrentProcess(),
            static_cast<PROCESSINFOCLASS>(10),
            &dbg_obj,
            sizeof(dbg_obj),
            nullptr
        );

        if (NT_SUCCESS(st) && dbg_obj != nullptr) {
            CloseHandle(dbg_obj);
            return true;
        }
        return false;
    }

    bool check_hardware_breakpoints() {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        if (!GetThreadContext(GetCurrentThread(), &ctx))
            return false;

        if (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3)
            return true;

        if (ctx.Dr7 & 0x55)
            return true;

        return false;
    }

    bool check_api_hooks() {
        const char* apis[] = {
            "NtReadVirtualMemory",
            "NtWriteVirtualMemory",
            "NtProtectVirtualMemory",
            "NtQueryInformationProcess",
            "NtQuerySystemInformation",
            "NtSetInformationThread",
            "NtOpenProcess"
        };

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) return false;

        for (const char* api_name : apis) {
            auto* pFunc = reinterpret_cast<const uint8_t*>(GetProcAddress(ntdll, api_name));
            if (!pFunc) continue;

            if (pFunc[0] == 0xE9 || (pFunc[0] == 0xFF && pFunc[1] == 0x25) || pFunc[0] == 0xCC || pFunc[0] == 0xC3) {
                return true;
            }
        }
        return false;
    }

    bool check_untrusted_modules() {
        HMODULE hMods[1024];
        DWORD cbNeeded = 0;
        HANDLE hProcess = GetCurrentProcess();

        typedef BOOL(WINAPI* EnumProcessModules_t)(HANDLE, HMODULE*, DWORD, LPDWORD);
        static auto fnEnum = reinterpret_cast<EnumProcessModules_t>(
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules")
        );
        if (!fnEnum) return false;

        if (fnEnum(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
            DWORD count = cbNeeded / sizeof(HMODULE);
            char modName[MAX_PATH];
            for (DWORD i = 0; i < count; i++) {
                if (GetModuleFileNameA(hMods[i], modName, sizeof(modName))) {
                    std::string path(modName);
                    if (path.find("speedhack") != std::string::npos ||
                        path.find("vehdebug") != std::string::npos ||
                        path.find("minhook") != std::string::npos ||
                        path.find("scylla") != std::string::npos ||
                        path.find("x64dbg") != std::string::npos ||
                        path.find("cheatengine") != std::string::npos) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void check() {
        if (check_debug_object())         { die(); }
        if (check_hardware_breakpoints()) { die(); }
        if (check_api_hooks())            { die(); }
        if (check_untrusted_modules())    { die(); }
    }

}

#else

namespace anti_attach {
    void set_critical_process()        {}
    bool check_debug_object()          { return false; }
    bool check_hardware_breakpoints()  { return false; }
    bool check_api_hooks()             { return false; }
    bool check_untrusted_modules()     { return false; }
    void check()                       {}
}

#endif
