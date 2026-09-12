#include "antidebug.h"
#include <core/protection/antiattach/antiattach.h>

#ifndef _DEBUG

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <thread>
#include <chrono>
#include <cstdlib>

using NtQueryInfoProcess_t = NTSTATUS(NTAPI*)(
    HANDLE,
    PROCESSINFOCLASS,
    PVOID,
    ULONG,
    PULONG
);

using NtSetInformationThread_t = NTSTATUS(NTAPI*)(
    HANDLE,
    ULONG,
    PVOID,
    ULONG
);

[[noreturn]] static void die() {
    TerminateProcess(GetCurrentProcess(), 0);
    __assume(0);
}

static bool check_winapi() {
    if (IsDebuggerPresent())
        return true;

    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return (remote != FALSE);
}

static bool check_debug_port() {
    static auto NtQIP = reinterpret_cast<NtQueryInfoProcess_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess")
    );
    if (!NtQIP) return false;

    HANDLE dbg_port = nullptr;
    NTSTATUS st = NtQIP(
        GetCurrentProcess(),
        static_cast<PROCESSINFOCLASS>(7),
        &dbg_port,
        sizeof(dbg_port),
        nullptr
    );
    return (NT_SUCCESS(st) && dbg_port != nullptr);
}

static bool check_nt_global_flag() {
#ifdef _WIN64
    auto* peb = reinterpret_cast<BYTE*>(__readgsqword(0x60));
    DWORD nt_global_flag = *reinterpret_cast<DWORD*>(peb + 0xBC);
#else
    auto* peb = reinterpret_cast<BYTE*>(__readfsdword(0x30));
    DWORD nt_global_flag = *reinterpret_cast<DWORD*>(peb + 0x68);
#endif
    return (nt_global_flag & 0x70) != 0;
}

static bool check_debug_flags() {
    static auto NtQIP = reinterpret_cast<NtQueryInfoProcess_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess")
    );
    if (!NtQIP) return false;

    DWORD debug_flags = 0;
    NTSTATUS st = NtQIP(
        GetCurrentProcess(),
        static_cast<PROCESSINFOCLASS>(0x1F),
        &debug_flags,
        sizeof(debug_flags),
        nullptr
    );
    return (NT_SUCCESS(st) && debug_flags == 0);
}

static bool check_kernel_debugger() {
    typedef NTSTATUS(NTAPI* NtQuerySystemInfo_t)(ULONG, PVOID, ULONG, PULONG);
    static auto NtQSI = reinterpret_cast<NtQuerySystemInfo_t>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation")
    );
    if (!NtQSI) return false;

    struct {
        BOOLEAN KernelDebuggerEnabled;
        BOOLEAN KernelDebuggerNotPresent;
    } kdbg_info{};

    NTSTATUS st = NtQSI(0x23, &kdbg_info, sizeof(kdbg_info), nullptr);
    return (NT_SUCCESS(st) && kdbg_info.KernelDebuggerEnabled && !kdbg_info.KernelDebuggerNotPresent);
}

static bool check_blacklisted_windows() {
    const char* bad_titles[] = {
        "x64dbg", "x32dbg", "ida pro", "ida64", "ida32", "ghidra", "cheat engine",
        "process hacker", "system informer", "httpdebugger", "fiddler", "charles",
        "wireshark", "dnspy", "scylla", "reclass", "binary ninja", "immunity debugger",
        "ollydbg", "megadumper", "ksdumper", "pestudio", "pe-bear"
    };

    const char* bad_classes[] = {
        "Qt5QWindowIcon", "OLLYDBG", "GBDY_Class", "IDABaseWindow", "QWidget"
    };

    const char* bad_processes[] = {
        "x64dbg.exe", "x32dbg.exe", "ida.exe", "ida64.exe", "idag.exe", "idag64.exe",
        "ghidra.exe", "cheatengine-x86_64.exe", "cheatengine-i386.exe", "ProcessHacker.exe",
        "SystemInformer.exe", "HTTPDebuggerUI.exe", "HTTPDebuggerSvc.exe", "Fiddler.exe",
        "Wireshark.exe", "dnSpy.exe", "Scylla_x64.exe", "Scylla_x86.exe", "ReClass.NET.exe",
        "KsDumper.exe"
    };

    struct EnumData {
        const char** bad_t;
        int count_t;
        const char** bad_c;
        int count_c;
        bool found;
    } data{
        bad_titles, sizeof(bad_titles) / sizeof(bad_titles[0]),
        bad_classes, sizeof(bad_classes) / sizeof(bad_classes[0]),
        false
    };

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* d = reinterpret_cast<EnumData*>(lparam);
        if (!IsWindowVisible(hwnd)) return TRUE;

        char title[256] = { 0 };
        char className[256] = { 0 };

        if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            _strlwr_s(title, sizeof(title));
            for (int i = 0; i < d->count_t; i++) {
                if (strstr(title, d->bad_t[i]) != nullptr) {
                    d->found = true;
                    return FALSE;
                }
            }
        }

        if (GetClassNameA(hwnd, className, sizeof(className)) > 0) {
            for (int i = 0; i < d->count_c; i++) {
                if (strcmp(className, d->bad_c[i]) == 0) {
                    if (strlen(title) > 0) {
                        for (int k = 0; k < d->count_t; k++) {
                            if (strstr(title, d->bad_t[k]) != nullptr) {
                                d->found = true;
                                return FALSE;
                            }
                        }
                    }
                }
            }
        }

        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    if (data.found) return true;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                char exeName[MAX_PATH] = { 0 };
                WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, exeName, sizeof(exeName) - 1, nullptr, nullptr);
                _strlwr_s(exeName, sizeof(exeName));

                for (const char* bad_proc : bad_processes) {
                    if (strstr(exeName, bad_proc) != nullptr) {
                        CloseHandle(hSnapshot);
                        return true;
                    }
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    return false;
}

static bool check_timing() {
    uint64_t t1 = __rdtsc();
    for (volatile int i = 0; i < 1000; i++) {}
    uint64_t t2 = __rdtsc();
    return ((t2 - t1) > 0x10000000);
}

namespace anti_debug {

    bool check_timing_anomaly() {
        return check_timing();
    }

    bool check() {
        if (check_winapi())          { die(); }
        if (check_debug_port())      { die(); }
        if (check_debug_flags())     { die(); }
        if (check_kernel_debugger()) { die(); }
        if (check_nt_global_flag())  { die(); }
        return false;
    }

    void start_watcher() {
        std::thread([]() {
            hide_current_thread();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            while (true) {
                auto start_time = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                auto end_time = std::chrono::steady_clock::now();
                
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                if (elapsed_ms > 6000) {
                    die();
                }

                if (check_winapi() || check_debug_port() || check_debug_flags() || check_kernel_debugger() || check_nt_global_flag() || check_timing() || check_blacklisted_windows()) {
                    die();
                }

                anti_attach::check();
            }
        }).detach();
    }

    void patch_remote_breakin() {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
        if (ntdll && kernel32) {
            void* pDbgUiRemoteBreakin = GetProcAddress(ntdll, "DbgUiRemoteBreakin");
            void* pExitProcess = GetProcAddress(kernel32, "ExitProcess");
            if (pDbgUiRemoteBreakin && pExitProcess) {
                DWORD oldProtect = 0;
                VirtualProtect(pDbgUiRemoteBreakin, 14, PAGE_EXECUTE_READWRITE, &oldProtect);
                unsigned char* bytes = reinterpret_cast<unsigned char*>(pDbgUiRemoteBreakin);
                bytes[0] = 0x48; bytes[1] = 0xB8;
                *reinterpret_cast<void**>(&bytes[2]) = pExitProcess;
                bytes[10] = 0xFF; bytes[11] = 0xE0;
                VirtualProtect(pDbgUiRemoteBreakin, 14, oldProtect, &oldProtect);
            }
        }
    }

    void patch_dbg_breakpoint() {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            void* pDbgBreakPoint = GetProcAddress(ntdll, "DbgBreakPoint");
            if (pDbgBreakPoint) {
                DWORD oldProtect = 0;
                VirtualProtect(pDbgBreakPoint, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
                *reinterpret_cast<unsigned char*>(pDbgBreakPoint) = 0xC3;
                VirtualProtect(pDbgBreakPoint, 1, oldProtect, &oldProtect);
            }
        }
    }

    void hide_current_thread() {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            auto pNtSIT = reinterpret_cast<NtSetInformationThread_t>(
                GetProcAddress(ntdll, "NtSetInformationThread")
            );
            if (pNtSIT) {
                pNtSIT(GetCurrentThread(), 0x11, nullptr, 0);
            }
        }
    }

}

#else

namespace anti_debug {
    bool check() { return false; }
    bool check_timing_anomaly() { return false; }
    void start_watcher() {}
    void patch_remote_breakin() {}
    void patch_dbg_breakpoint() {}
    void hide_current_thread() {}
}

#endif
