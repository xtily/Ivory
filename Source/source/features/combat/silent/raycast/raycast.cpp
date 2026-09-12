#include "raycast.h"
#include <features/system/settings/settings.h>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/cache/core/cache.h>
#include <sdk/cache/core/frame.h>
#include <sdk/cache/bodyparts/bodyparts.h>
#include <core/logger/logger.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cfloat>
#include <psapi.h>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <features/system/keybind/keybind.h>
#include <features/system/playerlist/playerlist.h>
#include <sdk/wallcheck/wallcheck.h>
#include <features/combat/silent/viewport/viewport.h>
#include <features/combat/silent/camera/camera.h>
#include <features/combat/aimbot/aimbot.h>
#include <core/scanner/rescan.h>
#include <features/instance.new/reflection/Reflect.h>

namespace raycast_silentaim {

    bool           target_acquired  = false;
    math::vector2  target_screen_pos{};
    uint64_t       target_address   = 0;

    static math::vector3 raycast_prediction_offset = {};

    static void log_raycast(const std::string& text) {
    }

    static std::string to_hex(uintptr_t v) {
        char buf[32]; sprintf_s(buf, "0x%llX", (unsigned long long)v);
        return std::string(buf);
    }

    void set_active(bool on, math::vector3 target_pos = {}, uintptr_t target_part = 0, math::vector3 cam_pos = {});
    static void restore_position_spoof();

    constexpr uintptr_t desc_rva_z    = Offsets::WorldRoot::RaycastBoundDesc;
    constexpr uintptr_t bound_fn_offset = Offsets::WorldRoot::RaycastBoundFn;

#pragma pack(push, 4)
    struct RaycastState {
        std::uint32_t active   = 0;
        std::uint32_t reserved = 0;
        float         target_x = 0.f;
        float         target_y = 0.f;
        float         target_z = 0.f;
        float         scale    = 1.15f;
        std::uint64_t calls    = 0;
        float         cam_x    = 0.f;
        float         cam_y    = 0.f;
        float         cam_z    = 0.f;
        float         limit_sq = 900.f;
    };
#pragma pack(pop)
    static_assert(offsetof(RaycastState, active)   == 0x00, "active");
    static_assert(offsetof(RaycastState, target_x) == 0x08, "target");
    static_assert(offsetof(RaycastState, calls)    == 0x18, "calls");

    struct Hook {
        std::uintptr_t slot             = 0;
        std::uintptr_t thunk            = 0;
        std::uintptr_t state            = 0;
        std::uintptr_t originalFunction = 0;
        std::uintptr_t module_base      = 0;
        bool           thunk_owned      = false;
        bool           installed        = false;
        bool           active           = false;
    };

    Hook g_hook{};
    bool g_wallbang = false;
    auto g_lastFail = std::chrono::steady_clock::time_point{};

    // Position Spoof wallbang — shared state between aim thread and spoof thread
    static std::atomic<uintptr_t> s_spoof_hrp_prim{ 0 };
    static std::atomic<bool>      s_spoof_active{ false };
    static math::vector3          s_spoof_saved_pos{};
    static math::vector3          s_spoof_target_pos{};  // written by aim thread, read by spoof thread
    static std::mutex             s_spoof_mtx;

    bool addr_ok(uintptr_t a) {
        return a >= 0x10000ull && a < 0x00007FFFFFFFFFFFull;
    }

    bool valid_float(float v) { return std::isfinite(v) && v > -1e8f && v < 1e8f; }

    bool w_mem(uintptr_t a, const void* d, std::size_t s) {
        if (!addr_ok(a) || !d || !s || !memory->m_process_handle) return false;
        std::lock_guard<std::mutex> lk(memory->m_handle_mtx);
        SIZE_T w = 0;
        return WriteProcessMemory(memory->m_process_handle, (void*)a, d, s, &w) && w == s;
    }

    bool r_mem(uintptr_t a, void* d, std::size_t s) {
        if (!addr_ok(a) || !d || !s || !memory->m_process_handle) return false;
        std::lock_guard<std::mutex> lk(memory->m_handle_mtx);
        SIZE_T r = 0;
        return ReadProcessMemory(memory->m_process_handle, (void*)a, d, s, &r) && r == s;
    }

    bool read_val(uintptr_t a, void* d, std::size_t s) {
        SIZE_T r = 0;
        return ReadProcessMemory(memory->m_process_handle, (void*)a, d, s, &r) && r == s;
    }

    std::size_t page_sz() {
        static std::size_t p = [] {
            SYSTEM_INFO i{}; GetSystemInfo(&i);
            return i.dwPageSize ? (std::size_t)i.dwPageSize : 0x1000u;
        }();
        return p;
    }

    DWORD query_protect(uintptr_t a) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(memory->m_process_handle, (void*)a, &mbi, sizeof(mbi))) return 0;
        return mbi.Protect;
    }

    bool is_executable_protect(DWORD p) {
        DWORD x = p & 0xFF;
        return x == PAGE_EXECUTE || x == PAGE_EXECUTE_READ ||
               x == PAGE_EXECUTE_READWRITE || x == PAGE_EXECUTE_WRITECOPY;
    }

    uintptr_t get_raycast_slot(uintptr_t base) {
        if (!base || !memory->m_process_handle) {
            log_raycast("[get_raycast_slot ERROR] Invalid base or process handle");
            return 0;
        }

        static uintptr_t s_cached_slot = 0;
        static uintptr_t s_cached_base = 0;
        if (s_cached_slot && s_cached_base == base) {
            uintptr_t cur_fn = memory->read<uint64_t>(s_cached_slot);
            if (addr_ok(cur_fn)) return s_cached_slot;
            s_cached_slot = 0;
        }

        int32_t lfanew = memory->read<int32_t>(base + 0x3C);
        uint32_t img_sz = 0;
        if (lfanew > 0 && lfanew <= 0x1000) {
            img_sz = memory->read<uint32_t>(base + lfanew + 0x50);
        }
        if (img_sz < 0x1000 || img_sz > 0x20000000u) {
            img_sz = 0x8000000u;
        }

        log_raycast(std::format("[get_raycast_slot] Scanning module base={}, img_sz=0x{:X}", to_hex(base), img_sz));

        // --- Stage 1: Try Reflection System ---
        uint64_t name = Reflect::Name(base, "Raycast");
        if (name) {
            log_raycast(std::format("[get_raycast_slot] Reflect::Name('Raycast') = 0x{:X}", name));
            uintptr_t addr = base;
            MEMORY_BASIC_INFORMATION mbi{};
            while (addr < base + img_sz && VirtualQueryEx(memory->m_process_handle, (void*)addr, &mbi, sizeof(mbi))) {
                uintptr_t rb = (uintptr_t)mbi.BaseAddress;
                size_t rs = (size_t)mbi.RegionSize;
                uintptr_t next = rb + rs;
                if (next <= addr) break;
                if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && !is_executable_protect(mbi.Protect)) {
                    uintptr_t s = (rb > base) ? rb : base;
                    uintptr_t e = (next < base + img_sz) ? next : base + img_sz;
                    if (e > s) {
                        std::vector<uint8_t> buf(e - s);
                        if (r_mem(s, buf.data(), buf.size())) {
                            for (size_t i = 8; i + 8 <= buf.size(); i += 8) {
                                uint64_t v = 0;
                                std::memcpy(&v, buf.data() + i, 8);
                                if (v == name) {
                                    uintptr_t desc = s + i - 8;
                                    uintptr_t vt = memory->read<uint64_t>(desc);
                                    if (vt >= base && vt < base + img_sz) {
                                        uintptr_t slot_addr = desc + bound_fn_offset;
                                        uintptr_t fn = memory->read<uint64_t>(slot_addr);
                                        if (addr_ok(fn) && is_executable_protect(query_protect(fn))) {
                                            log_raycast("[get_raycast_slot] Stage 1 found slot " + to_hex(slot_addr) + " -> fn=" + to_hex(fn));
                                            s_cached_slot = slot_addr;
                                            s_cached_base = base;
                                            return slot_addr;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                addr = next;
            }
        } else {
            log_raycast("[get_raycast_slot] Stage 1 Reflect::Name returned 0");
        }

        // --- Stage 2: Direct String & 2-Hop Reflection Pointer Scan ---
        std::vector<uintptr_t> string_addrs;
        {
            const char target_str[] = "Raycast";
            const size_t str_len = sizeof(target_str);
            uintptr_t addr = base;
            MEMORY_BASIC_INFORMATION mbi{};
            while (addr < base + img_sz && VirtualQueryEx(memory->m_process_handle, (void*)addr, &mbi, sizeof(mbi))) {
                uintptr_t rb = (uintptr_t)mbi.BaseAddress;
                size_t rs = (size_t)mbi.RegionSize;
                uintptr_t next = rb + rs;
                if (next <= addr) break;
                if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && !is_executable_protect(mbi.Protect)) {
                    uintptr_t s = (rb > base) ? rb : base;
                    uintptr_t e = (next < base + img_sz) ? next : base + img_sz;
                    if (e > s) {
                        std::vector<uint8_t> buf(e - s);
                        if (r_mem(s, buf.data(), buf.size())) {
                            for (size_t i = 0; i + str_len <= buf.size(); ++i) {
                                if (std::memcmp(buf.data() + i, target_str, str_len) == 0) {
                                    string_addrs.push_back(s + i);
                                }
                            }
                        }
                    }
                }
                addr = next;
            }
        }

        log_raycast(std::format("[get_raycast_slot] Stage 2 found {} 'Raycast' string occurrences", string_addrs.size()));

        // Step 2a: Find all name object candidates that point to the string
        std::unordered_set<uintptr_t> name_candidates;
        for (uintptr_t str_addr : string_addrs) {
            name_candidates.insert(str_addr); // Also try direct string pointer

            uintptr_t addr = base;
            MEMORY_BASIC_INFORMATION mbi{};
            while (addr < base + img_sz && VirtualQueryEx(memory->m_process_handle, (void*)addr, &mbi, sizeof(mbi))) {
                uintptr_t rb = (uintptr_t)mbi.BaseAddress;
                size_t rs = (size_t)mbi.RegionSize;
                uintptr_t next = rb + rs;
                if (next <= addr) break;
                if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && !is_executable_protect(mbi.Protect)) {
                    uintptr_t s = (rb > base) ? rb : base;
                    uintptr_t e = (next < base + img_sz) ? next : base + img_sz;
                    if (e > s) {
                        std::vector<uint8_t> buf(e - s);
                        if (r_mem(s, buf.data(), buf.size())) {
                            for (size_t i = 0; i + 8 <= buf.size(); i += 8) {
                                uint64_t ptr_val = 0;
                                std::memcpy(&ptr_val, buf.data() + i, 8);
                                if (ptr_val == str_addr) {
                                    uintptr_t ptr_loc = s + i;
                                    name_candidates.insert(ptr_loc);
                                    if (ptr_loc >= base + 8) name_candidates.insert(ptr_loc - 8);
                                    if (ptr_loc >= base + 16) name_candidates.insert(ptr_loc - 16);
                                    if (i + 16 <= buf.size()) {
                                        uint64_t next_val = 0;
                                        std::memcpy(&next_val, buf.data() + i + 8, 8);
                                        if (addr_ok(next_val)) name_candidates.insert(next_val);
                                    }
                                }
                            }
                        }
                    }
                }
                addr = next;
            }
        }

        log_raycast(std::format("[get_raycast_slot] Stage 2 collected {} name/descriptor candidates", name_candidates.size()));

        // Step 2b: Find method descriptors referencing any name candidate
        for (uintptr_t name_cand : name_candidates) {
            uintptr_t addr = base;
            MEMORY_BASIC_INFORMATION mbi{};
            while (addr < base + img_sz && VirtualQueryEx(memory->m_process_handle, (void*)addr, &mbi, sizeof(mbi))) {
                uintptr_t rb = (uintptr_t)mbi.BaseAddress;
                size_t rs = (size_t)mbi.RegionSize;
                uintptr_t next = rb + rs;
                if (next <= addr) break;
                if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && !is_executable_protect(mbi.Protect)) {
                    uintptr_t s = (rb > base) ? rb : base;
                    uintptr_t e = (next < base + img_sz) ? next : base + img_sz;
                    if (e > s) {
                        std::vector<uint8_t> buf(e - s);
                        if (r_mem(s, buf.data(), buf.size())) {
                            for (size_t i = 0; i + 8 <= buf.size(); i += 8) {
                                uint64_t ptr_val = 0;
                                std::memcpy(&ptr_val, buf.data() + i, 8);
                                if (ptr_val == name_cand) {
                                    uintptr_t ptr_loc = s + i;
                                    for (uintptr_t offset_back : { 0x08ull, 0x10ull, 0x18ull, 0x00ull }) {
                                        if (ptr_loc < base + offset_back) continue;
                                        uintptr_t desc_cand = ptr_loc - offset_back;
                                        uintptr_t vt = memory->read<uint64_t>(desc_cand);
                                        if (vt >= base && vt < base + img_sz) {
                                            for (uintptr_t fn_off : { bound_fn_offset, 0x78ull, 0x88ull, 0x70ull, 0x90ull, 0x80ull, 0x68ull, 0x98ull }) {
                                                uintptr_t slot_cand = desc_cand + fn_off;
                                                uintptr_t fn_cand = memory->read<uint64_t>(slot_cand);
                                                if (addr_ok(fn_cand) && fn_cand >= base && fn_cand < base + img_sz && is_executable_protect(query_protect(fn_cand))) {
                                                    log_raycast(std::format("[get_raycast_slot] Stage 2 SUCCESS! found slot=0x{:X} (desc=0x{:X}, fn=0x{:X})",
                                                        slot_cand, desc_cand, fn_cand));
                                                    s_cached_slot = slot_cand;
                                                    s_cached_base = base;
                                                    return slot_cand;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                addr = next;
            }
        }

        // --- Stage 3: Static Slot Fallback ---
        uintptr_t static_slot = base + desc_rva_z + bound_fn_offset;
        uintptr_t static_fn = 0;
        if (r_mem(static_slot, &static_fn, sizeof(static_fn)) && addr_ok(static_fn)) {
            log_raycast("[get_raycast_slot] Fallback to static slot " + to_hex(static_slot));
            s_cached_slot = static_slot;
            s_cached_base = base;
            return static_slot;
        }

        log_raycast("[get_raycast_slot ERROR] Could not find valid raycast slot");
        return 0;
    }

    uintptr_t get_module_base(const wchar_t* name) {
        if (!memory->m_process_handle) return 0;
        HMODULE mods[1024]; DWORD needed = 0;
        if (!EnumProcessModules(memory->m_process_handle, mods, sizeof(mods), &needed)) return 0;
        DWORD cnt = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < cnt; ++i) {
            wchar_t buf[MAX_PATH]{};
            if (GetModuleBaseNameW(memory->m_process_handle, mods[i], buf, MAX_PATH) &&
                _wcsicmp(buf, name) == 0)
                return (uintptr_t)mods[i];
        }
        return 0;
    }

    bool protect_remote(uintptr_t address, std::size_t size, DWORD protection,
                        DWORD* old_protect = nullptr)
    {
        if (!addr_ok(address) || !size || !memory->m_process_handle) return false;

        uintptr_t page_mask = ~((uintptr_t)page_sz() - 1);
        uintptr_t base = address & page_mask;
        uintptr_t end  = (address + size + page_sz() - 1) & page_mask;
        std::size_t span = (std::size_t)(end - base);

        using NtProtectFn = LONG(WINAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);
        static NtProtectFn nt_protect = nullptr;
        if (!nt_protect) {
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll) nt_protect = (NtProtectFn)GetProcAddress(ntdll, "NtProtectVirtualMemory");
        }

        auto try_one = [&](DWORD req) -> bool {
            DWORD old = 0;
            if (VirtualProtectEx(memory->m_process_handle, (void*)base, span, req, &old)) {
                if (old_protect) *old_protect = old;
                return true;
            }
            if (!nt_protect) return false;
            PVOID  nb = (void*)base;
            SIZE_T ns = span;
            ULONG  no = 0;
            LONG st = nt_protect(memory->m_process_handle, &nb, &ns, req, &no);
            if (st >= 0) { if (old_protect) *old_protect = (DWORD)no; return true; }
            return false;
        };

        if (try_one(protection))                                           return true;
        if (protection == PAGE_EXECUTE_READWRITE && try_one(PAGE_EXECUTE_WRITECOPY)) return true;
        if (protection == PAGE_READWRITE         && try_one(PAGE_WRITECOPY))         return true;
        return false;
    }

    bool write_protected(uintptr_t address, const void* data, std::size_t size) {
        if (!addr_ok(address) || !data || !size) return false;
        DWORD old = 0;
        bool changed = protect_remote(address, size, PAGE_EXECUTE_READWRITE, &old);
        bool wrote   = w_mem(address, data, size);
        if (changed) protect_remote(address, size, old, nullptr);
        return wrote;
    }

    bool mark_cfg(uintptr_t t) {
        auto resolve = []() -> FARPROC {
            const char* mods[] = {
                "kernelbase.dll", "kernel32.dll",
                "api-ms-win-core-memory-l1-1-3.dll"
            };
            for (auto* m : mods) {
                HMODULE h = GetModuleHandleA(m);
                if (!h) h = LoadLibraryA(m);
                if (!h) continue;
                FARPROC p = GetProcAddress(h, "SetProcessValidCallTargets");
                if (p) return p;
            }
            return nullptr;
        };

        FARPROC proc = resolve();
        if (!proc) {
            log_raycast("[mark_cfg WARNING] SetProcessValidCallTargets not found");
            return false;
        }

        struct Info { ULONG_PTR Offset; ULONG Flags; } info{};
        info.Offset = t & (page_sz() - 1);
        info.Flags  = CFG_CALL_TARGET_VALID;

        using Fn = BOOL(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, void*);
        SetLastError(0);
        BOOL ok = ((Fn)proc)(
            memory->m_process_handle,
            (void*)(t & ~((uintptr_t)page_sz() - 1)),
            page_sz(), 1, &info);

        if (!ok) {
            log_raycast("[mark_cfg ERROR] failed err=" + std::to_string(GetLastError()));
            return false;
        }
        log_raycast("[mark_cfg OK] " + to_hex(t));
        return true;
    }

    void append_u64(std::vector<std::uint8_t>& c, std::uint64_t v) {
        const auto* b = (const std::uint8_t*)&v;
        c.insert(c.end(), b, b + 8);
    }

    void patch_rel32(std::vector<std::uint8_t>& c, std::size_t o, std::size_t t) {
        std::int32_t v = (std::int32_t)((std::ptrdiff_t)t - (std::ptrdiff_t)(o + 4));
        std::memcpy(c.data() + o, &v, 4);
    }

    std::vector<std::uint8_t> make_hook_thunk(std::uintptr_t state, std::uintptr_t orig) {
        std::vector<std::uint8_t> c;
        c.reserve(384);
        std::vector<std::size_t> inactive;

        auto je_inactive = [&] {
            c.insert(c.end(), { 0x0F, 0x84 });
            inactive.push_back(c.size());
            c.insert(c.end(), { 0, 0, 0, 0 });
        };
        auto jbe_inactive = [&] {
            c.insert(c.end(), { 0x0F, 0x86 });
            inactive.push_back(c.size());
            c.insert(c.end(), { 0, 0, 0, 0 });
        };
        auto ja_inactive = [&] {
            c.insert(c.end(), { 0x0F, 0x87 });
            inactive.push_back(c.size());
            c.insert(c.end(), { 0, 0, 0, 0 });
        };

        c.insert(c.end(), { 0x48, 0x83, 0xEC, 0x68 });
        c.insert(c.end(), { 0x49, 0xBA });
        append_u64(c, state);
        c.insert(c.end(), { 0x41, 0x83, 0x3A, 0x00 });
        je_inactive();
        c.insert(c.end(), { 0x4D, 0x85, 0xC0 }); je_inactive();
        c.insert(c.end(), { 0x4D, 0x85, 0xC9 }); je_inactive();

        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x42, 0x08 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x5C, 0x00 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x40 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x4A, 0x0C });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x5C, 0x48, 0x04 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x44 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x52, 0x10 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x5C, 0x50, 0x08 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x54, 0x24, 0x48 });

        c.insert(c.end(), { 0x0F, 0x28, 0xD8 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xDB });
        c.insert(c.end(), { 0x0F, 0x28, 0xE1 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xDC });
        c.insert(c.end(), { 0x0F, 0x28, 0xE2 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xDC });
        c.insert(c.end(), { 0xF3, 0x0F, 0x51, 0xDB });
        c.insert(c.end(), { 0x0F, 0x57, 0xED });
        c.insert(c.end(), { 0x0F, 0x2E, 0xDD });
        jbe_inactive();

        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x21 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x69, 0x04 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xED });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xE5 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x69, 0x08 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xED });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xE5 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x51, 0xE4 });
        c.insert(c.end(), { 0x0F, 0x57, 0xED });
        c.insert(c.end(), { 0x0F, 0x2E, 0xE5 });
        jbe_inactive();

        c.insert(c.end(), { 0x41, 0x8B, 0x42, 0x04 });
        c.insert(c.end(), { 0xA8, 0x01 });
        c.insert(c.end(), { 0x0F, 0x85 });
        const std::size_t wallbang_jmp = c.size();
        c.insert(c.end(), { 0, 0, 0, 0 });

        c.insert(c.end(), { 0x0F, 0x28, 0xEC });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5E, 0xEB });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xC5 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xCD });
        c.insert(c.end(), { 0xF3, 0x0F, 0x59, 0xD5 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x11, 0x01 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x11, 0x49, 0x04 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x11, 0x51, 0x08 });
        c.insert(c.end(), { 0x49, 0xFF, 0x42, 0x18 });
        c.push_back(0xE9);
        const std::size_t to_call = c.size();
        c.insert(c.end(), { 0, 0, 0, 0 });

        const std::size_t wallbang_off = c.size();
        patch_rel32(c, wallbang_jmp, wallbang_off);

        c.insert(c.end(), { 0xF3, 0x0F, 0x5E, 0xC3 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5E, 0xCB });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5E, 0xD3 });

        c.insert(c.end(), { 0x0F, 0x28, 0xE0 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x59, 0x62, 0x14 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x6A, 0x08 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5C, 0xEC });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x6C, 0x24, 0x50 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x64, 0x24, 0x40 });
        c.insert(c.end(), { 0x0F, 0x28, 0xE1 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x59, 0x62, 0x14 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x6A, 0x0C });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5C, 0xEC });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x6C, 0x24, 0x54 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x64, 0x24, 0x44 });
        c.insert(c.end(), { 0x0F, 0x28, 0xE2 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x59, 0x62, 0x14 });
        c.insert(c.end(), { 0xF3, 0x41, 0x0F, 0x10, 0x6A, 0x10 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x5C, 0xEC });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x6C, 0x24, 0x58 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x58, 0xE4 });
        c.insert(c.end(), { 0xF3, 0x0F, 0x11, 0x64, 0x24, 0x48 });

        c.insert(c.end(), { 0x4C, 0x8D, 0x44, 0x24, 0x50 });
        c.insert(c.end(), { 0x4C, 0x8D, 0x4C, 0x24, 0x40 });
        c.insert(c.end(), { 0x49, 0xFF, 0x42, 0x18 });

        const std::size_t call_off = c.size();
        patch_rel32(c, to_call, call_off);
        const std::size_t inactive_off = c.size();
        for (auto o : inactive) patch_rel32(c, o, inactive_off);

        c.insert(c.end(), { 0x48, 0x8B, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00 });
        c.insert(c.end(), { 0x48, 0x89, 0x44, 0x24, 0x20 });
        c.insert(c.end(), { 0x48, 0xB8 });
        append_u64(c, orig);
        c.insert(c.end(), { 0xFF, 0xD0 });
        c.insert(c.end(), { 0x48, 0x83, 0xC4, 0x68 });
        c.push_back(0xC3);
        return c;
    }

    bool region_is_padding(uintptr_t a, std::size_t n) {
        std::vector<std::uint8_t> buf(n);
        if (!read_val(a, buf.data(), n)) return false;
        for (auto b : buf) if (b != 0xCC && b != 0x00 && b != 0x90) return false;
        return true;
    }

    uintptr_t find_cave_in_module(uintptr_t module_base, std::size_t need,
                                  uintptr_t min_offset, uintptr_t ignore)
    {
        if (!addr_ok(module_base) || !need) return 0;

        IMAGE_DOS_HEADER dos{};
        if (!read_val(module_base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

        IMAGE_NT_HEADERS64 nt{};
        uintptr_t nt_addr = module_base + (uintptr_t)dos.e_lfanew;
        if (!read_val(nt_addr, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;

        uintptr_t section_base = nt_addr + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
                                         + nt.FileHeader.SizeOfOptionalHeader;

        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER sec{};
            if (!read_val(section_base + (uintptr_t)i * sizeof(sec), &sec, sizeof(sec))) break;
            if (!(sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;

            uintptr_t sec_off  = sec.VirtualAddress;
            std::size_t sec_sz = sec.Misc.VirtualSize ? sec.Misc.VirtualSize : sec.SizeOfRawData;
            if (!sec_off || sec_sz < need) continue;

            uintptr_t scan_off = sec_off;
            if (scan_off < min_offset) scan_off = min_offset;
            if (scan_off >= sec_off + sec_sz) continue;

            uintptr_t scan_start = module_base + scan_off;
            std::size_t scan_sz  = (std::size_t)(sec_off + sec_sz - scan_off);

            std::vector<std::uint8_t> buf(scan_sz);
            if (!read_val(scan_start, buf.data(), buf.size())) continue;

            std::size_t run_start = 0, run_len = 0;
            for (std::size_t j = 0; j < buf.size(); ++j) {
                std::uint8_t b = buf[j];
                if (b != 0x00 && b != 0xCC && b != 0x90) { run_len = 0; run_start = j + 1; continue; }
                ++run_len;
                if (run_len < need) continue;

                uintptr_t cand    = scan_start + run_start;
                uintptr_t aligned = (cand + 0x0F) & ~(uintptr_t)0x0F;
                std::size_t loss  = (std::size_t)(aligned - cand);
                if (run_len < need + loss) continue;
                if (ignore && aligned == ignore) continue;
                if (g_hook.thunk && aligned == g_hook.thunk) continue;
                return aligned;
            }
        }
        return 0;
    }

    uintptr_t find_exec_cave(std::size_t need, uintptr_t  = 0,
                             uintptr_t ignore = 0)
    {
        static const wchar_t* pref[] = {
            L"winsta.dll", L"win32u.dll", L"uxtheme.dll", L"dwmapi.dll",
            L"msctf.dll",  L"TextInputFramework.dll", L"CoreMessaging.dll", L"user32.dll",
        };

        for (std::size_t mi = 0; mi < sizeof(pref) / sizeof(pref[0]); ++mi) {
            uintptr_t mod = get_module_base(pref[mi]);
            if (!mod) continue;
            uintptr_t min_off = (mi == 0) ? 0x2000u : 0x1000u;
            uintptr_t cave    = find_cave_in_module(mod, need, min_off, ignore);
            if (cave) return cave;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t addr = 0, fallback_rwx = 0;
        while (VirtualQueryEx(memory->m_process_handle, (void*)addr, &mbi, sizeof(mbi))) {
            uintptr_t base = (uintptr_t)mbi.BaseAddress;
            std::size_t size = (std::size_t)mbi.RegionSize;
            addr = base + size;
            if (addr < base) break;
            if (mbi.State != MEM_COMMIT) continue;
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) continue;
            if (!is_executable_protect(mbi.Protect)) continue;
            if (size < need) continue;
            if (g_hook.thunk && base <= g_hook.thunk && g_hook.thunk < addr) continue;

            for (std::size_t off = 0; off + need <= size; off += 0x10) {
                uintptr_t cand = base + off;
                if (ignore && cand == ignore) continue;
                if (region_is_padding(cand, need)) return cand;
            }

            if (!fallback_rwx && mbi.Type == MEM_PRIVATE &&
                (mbi.Protect & 0xFF) == PAGE_EXECUTE_READWRITE && size >= need + 0x40) {
                uintptr_t cand = base + size - need;
                if (!ignore || cand != ignore) fallback_rwx = cand;
            }
        }
        return fallback_rwx;
    }

    uintptr_t alloc_exec_page() {
        uintptr_t p = (uintptr_t)VirtualAllocEx(memory->m_process_handle, nullptr,
                                                  page_sz(), MEM_COMMIT | MEM_RESERVE,
                                                  PAGE_EXECUTE_READWRITE);
        if (!p) return 0;
        if (!is_executable_protect(query_protect(p))) {
            VirtualFreeEx(memory->m_process_handle, (void*)p, 0, MEM_RELEASE);
            return 0;
        }
        return p;
    }

    bool install() {
        if (g_hook.installed) return true;

        uintptr_t base = memory->m_base_address;
        if (!base) return false;

        auto now = std::chrono::steady_clock::now();
        if (g_lastFail.time_since_epoch().count() != 0 &&
            now - g_lastFail < std::chrono::milliseconds(1500)) return false;

        uintptr_t slot = get_raycast_slot(base);
        if (!slot) {
            g_lastFail = now;
            log_raycast("[install ERROR] failed to resolve raycast slot");
            return false;
        }
        uintptr_t fn   = 0;
        if (!r_mem(slot, &fn, sizeof(fn)) || !addr_ok(fn)) {
            g_lastFail = now;
            log_raycast("[install ERROR] bad handler at slot " + to_hex(slot));
            return false;
        }

        if (!g_hook.state) {
            g_hook.state = (uintptr_t)VirtualAllocEx(memory->m_process_handle, nullptr,
                                                       page_sz(), MEM_COMMIT | MEM_RESERVE,
                                                       PAGE_READWRITE);
        }
        if (!g_hook.state) {
            g_lastFail = now;
            log_raycast("[install ERROR] state alloc failed");
            return false;
        }
        log_raycast("[install] state=" + to_hex(g_hook.state));

        auto thunk = make_hook_thunk(g_hook.state, fn);
        if (thunk.size() > 0x200) {
            g_lastFail = now;
            log_raycast("[install ERROR] stub too large: " + std::to_string(thunk.size()));
            return false;
        }

        bool owned = false;
        uintptr_t stub = 0, ignore_cave = 0;

        for (int attempt = 0; attempt < 8 && !stub; ++attempt) {
            uintptr_t cand = find_exec_cave(0x200, base, ignore_cave);
            if (!cand) break;
            SetLastError(0);
            DWORD old_prot = 0;
            bool prot_ok = protect_remote(cand, thunk.size(), PAGE_EXECUTE_READWRITE, &old_prot);
            SetLastError(0);
            if (!write_protected(cand, thunk.data(), thunk.size())) {
                if (prot_ok) protect_remote(cand, thunk.size(), old_prot, nullptr);
                ignore_cave = cand;
                continue;
            }
            stub = cand; owned = false;
        }

        if (!stub) {
            stub = alloc_exec_page();
            owned = (stub != 0);
            if (stub) {
                SetLastError(0);
                if (!write_protected(stub, thunk.data(), thunk.size())) {
                    VirtualFreeEx(memory->m_process_handle, (void*)stub, 0, MEM_RELEASE);
                    stub = 0; owned = false;
                }
            }
        }

        if (!stub) {
            g_lastFail = now;
            log_raycast("[install ERROR] no host for stub");
            return false;
        }

        RaycastState empty{};
        SetLastError(0);
        if (!w_mem(g_hook.state, &empty, sizeof(empty))) {
            g_lastFail = now;
            if (owned) VirtualFreeEx(memory->m_process_handle, (void*)stub, 0, MEM_RELEASE);
            log_raycast("[install ERROR] state write failed");
            return false;
        }

        FlushInstructionCache(memory->m_process_handle, (void*)stub, thunk.size());
        mark_cfg(stub);

        if (!is_executable_protect(query_protect(stub))) {
            g_lastFail = now;
            if (owned) VirtualFreeEx(memory->m_process_handle, (void*)stub, 0, MEM_RELEASE);
            log_raycast("[install ERROR] stub not executable after write");
            return false;
        }

        protect_remote(slot, 8, PAGE_READWRITE, nullptr);
        uintptr_t verify = 0;
        if (!write_protected(slot, &stub, sizeof(stub)) ||
            (!r_mem(slot, &verify, sizeof(verify)) || verify != stub))
        {
            g_lastFail = now;
            if (owned) VirtualFreeEx(memory->m_process_handle, (void*)stub, 0, MEM_RELEASE);
            log_raycast("[install ERROR] slot write failed at " + to_hex(slot));
            return false;
        }

        g_hook.slot             = slot;
        g_hook.module_base      = base;
        g_hook.originalFunction = fn;
        g_hook.thunk            = stub;
        g_hook.thunk_owned      = owned;
        g_hook.installed        = true;
        g_hook.active           = false;
        log_raycast("[INSTALL SUCCESS] slot=" + to_hex(slot) +
                    " fn=" + to_hex(fn) + " stub=" + to_hex(stub) +
                    " state=" + to_hex(g_hook.state) +
                    " cave=" + std::string(owned ? "no (alloc)" : "yes"));
        return true;
    }

    void remove_hook() {
        if (g_hook.installed && addr_ok(g_hook.originalFunction) && g_hook.slot) {
            write_protected(g_hook.slot, &g_hook.originalFunction, sizeof(g_hook.originalFunction));
        }
        Sleep(50);
        if (g_hook.thunk && !g_hook.thunk_owned) {
            std::vector<std::uint8_t> pad(0x200, 0xCC);
            write_protected(g_hook.thunk, pad.data(), pad.size());
        }
        if (g_hook.thunk && g_hook.thunk_owned)
            VirtualFreeEx(memory->m_process_handle, (void*)g_hook.thunk, 0, MEM_RELEASE);
        if (g_hook.state)
            VirtualFreeEx(memory->m_process_handle, (void*)g_hook.state, 0, MEM_RELEASE);
        g_hook     = {};
        g_wallbang = false;
    }

    void ensure(bool want) {
        if (rescan::is_rescanning || rescan::require_rescan) {
            if (g_hook.installed) {
                remove_hook();
            }
            return;
        }

        uintptr_t base = memory->m_base_address;
        static uintptr_t s_last_base = 0;

        if (g_hook.installed && base && s_last_base && base != s_last_base) {
            remove_hook();
            log_raycast("[ensure] module rebased, hook removed");
        }
        if (base) s_last_base = base;

        if (want) {
            if (!base) return;
            
            static auto last_ensure_check = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            bool run_check = !g_hook.installed || (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ensure_check).count() >= 250);
            
            if (run_check) {
                last_ensure_check = now;
                if (g_hook.installed && g_hook.thunk && g_hook.slot) {
                    uintptr_t cur  = 0;
                    if (r_mem(g_hook.slot, &cur, sizeof(cur)) && addr_ok(cur) && cur != g_hook.thunk) {
                        log_raycast("[ensure] Slot overwritten by engine, reinstalling hook");
                        g_hook.installed = false;
                    }
                }
                if (!g_hook.installed) {
                    g_lastFail = {};
                    install();
                }
            }
        } else if (g_hook.installed) {
            remove_hook();
        }
    }

    void set_active(bool on, math::vector3 target_pos, uintptr_t target_part, math::vector3 cam_pos) {
        ensure(on);

        struct active_state_t {
            std::uint32_t active   = 0;
            std::uint32_t reserved = 0;
            float         target_x = 0.f;
            float         target_y = 0.f;
            float         target_z = 0.f;
            float         scale    = 1.15f;
        };
        static active_state_t last_written{};

        if (!on) {
            if (g_hook.active && g_hook.state) {
                std::uint32_t v = 0;
                w_mem(g_hook.state, &v, sizeof(v));
                g_hook.active = false;
                last_written = {};
            }
            g_wallbang = false;
            restore_position_spoof();
            return;
        }
        if (!g_hook.installed) return;

        bool wallbang_on = settings::raycast_silentaim::magic_bullet;
        if (settings::raycast_silentaim::magic_bullet_keybind > 0)
        {
            keybind::keybind_t kb{};
            kb.key  = settings::raycast_silentaim::magic_bullet_keybind;
            kb.mode = static_cast<keybind::activation_mode>(
                          settings::raycast_silentaim::magic_bullet_activation_mode);
            wallbang_on = settings::raycast_silentaim::magic_bullet && keybind::is_active(kb);
        }

        active_state_t state{};
        state.active = 1;
        state.reserved = wallbang_on ? 1u : 0u;
        state.target_x = target_pos.x;
        state.target_y = target_pos.y;
        state.target_z = target_pos.z;
        state.scale = 1.15f;

        if (state.active != last_written.active ||
            state.reserved != last_written.reserved ||
            state.target_x != last_written.target_x ||
            state.target_y != last_written.target_y ||
            state.target_z != last_written.target_z ||
            state.scale != last_written.scale) 
        {
            if (w_mem(g_hook.state, &state, sizeof(state))) {
                last_written = state;
            }
        }
        g_hook.active = true;
        g_wallbang    = wallbang_on;

        // Position Spoof method — aim thread just signals the target to the spoof thread
        if (wallbang_on && settings::raycast_silentaim::magic_bullet_method == 1)
        {
            uintptr_t hrp_prim = 0;
            {
                std::lock_guard<std::mutex> lk(cache::local_player_mtx);
                auto& lp = cache::local_player;
                if (lp.humanoid_root_part.address != 0)
                    hrp_prim = lp.humanoid_root_part.get_primitive().address;
            }
            {
                std::lock_guard<std::mutex> lk(s_spoof_mtx);
                s_spoof_hrp_prim.store(hrp_prim);
                s_spoof_target_pos = { target_pos.x, target_pos.y + 1.0f, target_pos.z };
                s_spoof_active.store(true);
            }
        }
        else
        {
            s_spoof_active.store(false);
            s_spoof_hrp_prim.store(0);
        }
    }

    static void restore_position_spoof()
    {
        s_spoof_active.store(false);
        s_spoof_hrp_prim.store(0);
    }

    // Dedicated high-priority thread for invisible position spoofing.
    // Uses QueryPerformanceCounter for sub-ms precision:
    //   write spoof pos → busy-wait 0.3ms → restore real pos → sleep 30ms (replication cycle)
    // At 60fps (16.67ms/frame), the position is "wrong" for only 0.3ms = ~1.8% of one frame.
    // Effectively invisible to the local render thread.
    static void position_spoof_worker()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        const long long pulse_ticks = freq.QuadPart * 3 / 10000; // 0.3ms in QPC ticks

        while (true)
        {
            if (!s_spoof_active.load() || s_spoof_hrp_prim.load() == 0 ||
                !settings::raycast_silentaim::magic_bullet_method)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            uintptr_t prim;
            math::vector3 spoof_pos;
            {
                std::lock_guard<std::mutex> lk(s_spoof_mtx);
                prim      = s_spoof_hrp_prim.load();
                spoof_pos = s_spoof_target_pos;
            }

            if (!prim) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }

            // Read current real position fresh each pulse
            math::vector3 real_pos = memory->read<math::vector3>(prim + Offsets::Primitive::Position);

            // Write spoof position
            memory->write<math::vector3>(prim + Offsets::Primitive::Position, spoof_pos);

            // Busy-wait exactly 0.3ms using QPC — far more precise than sleep_for
            LARGE_INTEGER start{}, now{};
            QueryPerformanceCounter(&start);
            do { QueryPerformanceCounter(&now); } while ((now.QuadPart - start.QuadPart) < pulse_ticks);

            // Restore immediately
            memory->write<math::vector3>(prim + Offsets::Primitive::Position, real_pos);

            // Wait for next Roblox replication cycle (~30ms at 30Hz)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    static rbx::c_part get_target_part_for_entity(const cache::entity_t& entity,
                                                   int target_part_idx,
                                                   const math::matrix4& vm,
                                                   const math::vector2& dims,
                                                   const math::vector2& center,
                                                   const math::vector3& cam_pos = {},
                                                   bool is_360 = false)
    {
        if (target_part_idx == 0) {
            auto head = entity.parts.find("Head");
            if (head != entity.parts.end() && head->second.address) return head->second;
            return entity.humanoid_root_part;
        } else if (target_part_idx == 1) {
            for (const auto& name : bodyparts::get_part_names(entity, "Torso")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 2) {
            if (entity.humanoid_root_part.address) return entity.humanoid_root_part;
            for (const auto& name : bodyparts::get_part_names(entity, "HumanoidRootPart")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 3) {
            for (const auto& name : bodyparts::get_part_names(entity, "LeftArm")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 4) {
            for (const auto& name : bodyparts::get_part_names(entity, "RightArm")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 5) {
            for (const auto& name : bodyparts::get_part_names(entity, "LeftLeg")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 6) {
            for (const auto& name : bodyparts::get_part_names(entity, "RightLeg")) {
                auto it = entity.parts.find(name);
                if (it != entity.parts.end() && it->second.address) return it->second;
            }
        } else if (target_part_idx == 7 || target_part_idx == 8) {
            static const char* all_bones[] = {
                "Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart",
                "LeftArm", "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
                "RightArm", "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
                "LeftLeg", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
                "RightLeg", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg"
            };
            rbx::c_part best_part(0);
            float best_dist = FLT_MAX;
            for (const char* bone : all_bones) {
                auto it = entity.parts.find(bone);
                if (it != entity.parts.end() && it->second.address) {
                    rbx::c_primitive prim = const_cast<rbx::c_part&>(it->second).get_primitive();
                    if (prim.address) {
                        math::vector3 p = prim.get_position();
                        if (is_360) {
                            float dx = p.x - cam_pos.x, dy = p.y - cam_pos.y, dz = p.z - cam_pos.z;
                            float d3 = dx*dx + dy*dy + dz*dz;
                            if (d3 < best_dist) {
                                best_dist = d3;
                                best_part = it->second;
                            }
                        } else {
                            math::vector2 scr{};
                            if (game::visualengine && game::visualengine->world_to_screen(vm, dims, p, scr)) {
                                float dx = scr.x - center.x, dy = scr.y - center.y;
                                float d2 = dx*dx + dy*dy;
                                if (d2 < best_dist) {
                                    best_dist = d2;
                                    best_part = it->second;
                                }
                            }
                        }
                    }
                }
            }
            if (best_part.address) return best_part;
        }
        auto head = entity.parts.find("Head");
        if (head != entity.parts.end() && head->second.address) return head->second;
        return entity.humanoid_root_part;
    }

    static bool is_rivals_game()
    {
        if (!game::datamodel || game::datamodel->address == 0) return false;
        uint64_t pid = game::datamodel->get_place_id();
        uint64_t gid = game::datamodel->get_game_id();
        return pid == 17625359962ULL || gid == 17625359962ULL || pid == 6035872082ULL || gid == 6035872082ULL;
    }

    static bool is_enemy_using_katana_uncached(const cache::entity_t& entity);

    static bool is_enemy_using_katana(const cache::entity_t& entity)
    {
        if (!is_rivals_game()) return false;

        static std::unordered_map<std::uint64_t, std::pair<bool, std::int64_t>> s_cache;
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto cit = s_cache.find(entity.instance.address);
        if (cit != s_cache.end() && now_ms - cit->second.second < 500)
            return cit->second.first;

        bool result = is_enemy_using_katana_uncached(entity);
        if (s_cache.size() > 256) s_cache.clear();
        s_cache[entity.instance.address] = { result, now_ms };
        return result;
    }

    static bool is_enemy_using_katana_uncached(const cache::entity_t& entity)
    {
        if (!is_rivals_game()) return false;
        if (!entity.tool_name.empty())
        {
            std::string lower_tool = entity.tool_name;
            std::transform(lower_tool.begin(), lower_tool.end(), lower_tool.begin(), ::tolower);
            if (lower_tool.find("katana") != std::string::npos ||
                lower_tool.find("cutlass") != std::string::npos ||
                lower_tool.find("sword") != std::string::npos ||
                lower_tool.find("blade") != std::string::npos ||
                lower_tool.find("saber") != std::string::npos ||
                lower_tool.find("keytana") != std::string::npos)
            {
                return true;
            }
        }

        std::uint64_t char_addr = entity.instance.address;
        if (char_addr != 0)
        {
            rbx::c_instance character(char_addr);
            std::vector<std::uint64_t> children = character.get_children();
            for (std::uint64_t child_addr : children)
            {
                rbx::c_instance child(child_addr);
                if (!child.address) continue;
                std::string name = child.get_name();
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

                if (lower_name.find("katana") != std::string::npos ||
                    lower_name.find("cutlass") != std::string::npos ||
                    lower_name.find("sword") != std::string::npos ||
                    lower_name.find("blade") != std::string::npos ||
                    lower_name.find("saber") != std::string::npos ||
                    lower_name.find("keytana") != std::string::npos)
                {
                    return true;
                }

                if (child.find_first_child("_katana_deflect_active") != 0 ||
                    child.find_first_child("_katana_deflect_hit") != 0)
                {
                    return true;
                }

                std::string cls = child.get_class_name();
                if (cls == "Model" || cls == "Tool" || cls == "Folder")
                {
                    std::vector<std::uint64_t> sub_children = child.get_children();
                    for (std::uint64_t sub_addr : sub_children)
                    {
                        rbx::c_instance sub(sub_addr);
                        if (!sub.address) continue;
                        std::string sub_name = sub.get_name();
                        std::string lower_sub = sub_name;
                        std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);
                        if (lower_sub.find("katana") != std::string::npos ||
                            lower_sub.find("cutlass") != std::string::npos ||
                            lower_sub.find("_katana_deflect") != std::string::npos)
                        {
                            return true;
                        }
                    }
                }
            }
        }

        if (game::datamodel && game::datamodel->address != 0 && !entity.name.empty())
        {
            uintptr_t dm = game::datamodel->address;
            uintptr_t ws = memory->read<uintptr_t>(dm + Offsets::DataModel::Workspace);
            if (ws != 0)
            {
                rbx::c_instance workspace_inst(ws);
                rbx::c_instance assets = workspace_inst.find_first_child("Assets");
                rbx::c_instance viewmodels_folder(0);
                if (assets.address != 0)
                {
                    rbx::c_instance temp = assets.find_first_child("Temp");
                    if (temp.address != 0)
                    {
                        viewmodels_folder = temp.find_first_child("ViewModels");
                    }
                }
                if (viewmodels_folder.address == 0)
                {
                    viewmodels_folder = workspace_inst.find_first_child("ViewModels");
                }

                if (viewmodels_folder.address != 0)
                {
                    std::string prefix_lower = entity.name + " - ";
                    std::transform(prefix_lower.begin(), prefix_lower.end(), prefix_lower.begin(), ::tolower);

                    std::vector<std::uint64_t> vm_children = viewmodels_folder.get_children();
                    for (std::uint64_t vm_addr : vm_children)
                    {
                        rbx::c_instance vm(vm_addr);
                        if (!vm.address) continue;
                        std::string vm_name = vm.get_name();
                        std::string vm_lower = vm_name;
                        std::transform(vm_lower.begin(), vm_lower.end(), vm_lower.begin(), ::tolower);

                        if (vm_lower.rfind(prefix_lower, 0) == 0)
                        {
                            if (vm_lower.find("katana") != std::string::npos ||
                                vm_lower.find("cutlass") != std::string::npos ||
                                vm_lower.find("sword") != std::string::npos ||
                                vm_lower.find("blade") != std::string::npos ||
                                vm_lower.find("saber") != std::string::npos ||
                                vm_lower.find("keytana") != std::string::npos)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        return false;
    }

    static float get_fov_scale()
    {
        uint64_t cam = 0;
        if (game::datamodel && game::datamodel->address >= 0x10000)
        {
            uint64_t ws = memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::Workspace);
            if (ws >= 0x10000)
            {
                cam = memory->read<uint64_t>(ws + Offsets::Workspace::CurrentCamera);
            }
        }
        if (cam < 0x10000)
        {
            cam = game::camera;
        }

        if (cam < 0x10000)
            return 1.0f;

        float cur = memory->read<float>(cam + Offsets::Camera::FieldOfView);
        if (cur <= 0.001f || std::isnan(cur) || std::isinf(cur))
            return 1.0f;

        float cur_deg = (cur < 3.2f) ? (cur * (180.0f / 3.14159265358979323846f)) : cur;
        if (cur_deg <= 1.0f || cur_deg > 170.0f)
            return 1.0f;

        float scale = 70.0f / cur_deg;
        if (scale < 0.1f) scale = 0.1f;
        if (scale > 10.0f) scale = 10.0f;
        return scale;
    }

    void raycast_worker() {
        log_raycast("[raycast_worker] Worker thread started");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        uint64_t log_counter = 0;
        while (true) {
            bool main_raycast_active = settings::silentaim::enabled && settings::silentaim::method == 0;
            bool main_viewport_active = settings::silentaim::enabled && settings::silentaim::method == 2;
            bool main_camera_active = settings::silentaim::enabled && settings::silentaim::method == 3;
            bool is_enabled = settings::raycast_silentaim::enabled || main_raycast_active || main_viewport_active || main_camera_active;

            bool keyActive = false;
            if (settings::silentaim::shoot_spectating && playerlist::is_spectating()) {
                is_enabled = true;
                keyActive = true;
            } else if (is_enabled) {
                if (settings::raycast_silentaim::enabled) {
                    keyActive = keybind::is_key_active(settings::raycast_silentaim::keybind,
                                                      settings::raycast_silentaim::activation_mode,
                                                      "raycast_silentaim");
                } else if (settings::silentaim::enabled) {
                    keyActive = keybind::is_key_active(settings::silentaim::keybind,
                                                      settings::silentaim::activation_mode,
                                                      "silentaim");
                }
            }

            if (!is_enabled || !keyActive) {
                target_acquired = false;
                target_screen_pos = math::vector2{ -1.f, -1.f };
                target_address = 0;
                set_active(false);
                Cheat::Features::ViewportSilent::SetActive(false);
                Cheat::Features::CameraSilent::SetActive(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (!game::visualengine || !game::visualengine->address) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            uintptr_t dm = game::datamodel ? game::datamodel->address : 0;
            if (!dm) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            uintptr_t workspace = memory->read<uintptr_t>(dm + Offsets::DataModel::Workspace);
            if (!workspace) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const auto fc = frame_cache::get_for_thread();
            const math::vector2& dims = fc.dims;
            const math::matrix4& vm   = fc.view;
            if (dims.x <= 0.f || dims.y <= 0.f) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            math::vector3 camPos{};
            uintptr_t camera = memory->read<uintptr_t>(workspace + Offsets::Workspace::CurrentCamera);
            if (camera) camPos = memory->read<math::vector3>(camera + Offsets::Camera::Position);

            math::vector2 center{ dims.x / 2.f, dims.y / 2.f };
            bool raycast_360 = settings::raycast_silentaim::mode_360;
            if (settings::raycast_silentaim::mode_360_keybind > 0)
            {
                if (keybind::is_key_active(settings::raycast_silentaim::mode_360_keybind,
                                          settings::raycast_silentaim::mode_360_activation_mode,
                                          "raycast_360"))
                {
                    raycast_360 = true;
                }
            }
            bool  is_360  = raycast_360 || settings::silentaim::mode_360;
            bool  use_fov = settings::raycast_silentaim::enabled
                                ? settings::raycast_silentaim::use_fov
                                : settings::silentaim::use_fov;
            float fovSize = settings::raycast_silentaim::enabled
                                ? settings::raycast_silentaim::fov
                                : settings::silentaim::fov;
            if (fovSize < 1.f) fovSize = 1.f;
            if (settings::silentaim::dynamic_fov || (settings::raycast_silentaim::enabled && settings::raycast_silentaim::dynamic_fov))
            {
                fovSize *= get_fov_scale();
            }
            float fovRadius   = (!use_fov) ? 1e6f : fovSize;
            float closestDistSq = (is_360 || !use_fov) ? 1e12f : (fovRadius * fovRadius);

            math::vector3 targetPos{};
            uintptr_t     targetPart = 0;
            math::vector2 bestScreenPos{};
            uint64_t      bestEntityAddr = 0;
            bool          found = false;

            bool use_teamcheck  = settings::teamcheck || settings::silentaim::teamcheck || settings::raycast_silentaim::teamcheck;
            bool use_prediction = settings::raycast_silentaim::enabled
                                    ? settings::raycast_silentaim::enable_prediction
                                    : settings::silentaim::enable_prediction;

            // Zero-copy atomic snap loads — no mutex, no heap alloc.
            auto players_snap_ptr = cache::get_players_snap();
            auto local_snap_ptr   = cache::get_local_snap();
            if (!players_snap_ptr) continue;
            const std::vector<cache::entity_t>& players_snapshot = *players_snap_ptr;
            static cache::entity_t s_empty_local{};
            const cache::entity_t& local_player_snapshot = local_snap_ptr ? *local_snap_ptr : s_empty_local;

            const uint64_t local_addr = local_player_snapshot.instance.address;
            const uint64_t local_team = local_player_snapshot.team;

            if (settings::silentaim::use_aimbot_target) {
                uint64_t aimbot_target_addr = aimbot::player.instance.address;
                if (!aimbot_target_addr) {
                    set_active(false);
                    Cheat::Features::ViewportSilent::SetActive(false);
                    Cheat::Features::CameraSilent::SetActive(false);
                    target_acquired = false; target_screen_pos = {}; target_address = 0;
                    std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::aim_thread_sleep));
                    continue;
                }
            }

            const cache::entity_t* spectate_target = nullptr;
            if (settings::silentaim::shoot_spectating && playerlist::is_spectating()) {
                std::string spec_name = playerlist::get_spectating_name();
                for (const auto& ent : players_snapshot) {
                    if (ent.name == spec_name && ent.health > 0.f) {
                        spectate_target = &ent;
                        break;
                    }
                }
            }

            for (const auto& entity : players_snapshot) {
                if (spectate_target) {
                    if (entity.instance.address != spectate_target->instance.address) continue;
                } else {
                    if (entity.instance.address == local_addr) continue;
                    if (settings::silentaim::use_aimbot_target && entity.instance.address != aimbot::player.instance.address) continue;
                    if (entity.health <= 0.f) continue;
                    if (use_teamcheck && local_team != 0 && entity.team == local_team) continue;
                    if (settings::silentaim::knock_check && entity.knocked) continue;
                    if (settings::silentaim::health_check_enabled && entity.health < settings::silentaim::min_health) continue;
                    if (main_viewport_active && settings::silentaim::katana_check && is_enemy_using_katana(entity)) continue;
                }

                rbx::c_part tpart = get_target_part_for_entity(
                    entity, settings::silentaim::target_part, vm, dims, center, camPos, is_360);
                if (!tpart.address) continue;

                rbx::c_primitive prim = const_cast<rbx::c_part&>(tpart).get_primitive();
                if (!prim.address) continue;

                math::vector3 pos = prim.get_position();
                if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

                if (use_prediction && entity.humanoid_root_part.address) {
                    rbx::c_primitive hrp = const_cast<rbx::c_part&>(entity.humanoid_root_part).get_primitive();
                    if (hrp.address) {
                        math::vector3 vel = memory->read<math::vector3>(
                            hrp.address + Offsets::Primitive::AssemblyLinearVelocity);
                        float factor_x = (10.0f - settings::silentaim::prediction_x) * 0.1f;
                        float factor_y = (10.0f - settings::silentaim::prediction_y) * 0.1f;
                        raycast_prediction_offset = { vel.x * factor_x, vel.y * factor_y, vel.z * factor_x };
                        pos.x += raycast_prediction_offset.x;
                        pos.y += raycast_prediction_offset.y;
                        pos.z += raycast_prediction_offset.z;
                    }
                }

                if (is_360) {
                    float dx = pos.x - camPos.x, dy = pos.y - camPos.y, dz = pos.z - camPos.z;
                    float d3sq = dx*dx + dy*dy + dz*dz;
                    if (d3sq <= closestDistSq) {
                        closestDistSq = d3sq; targetPos = pos;
                        targetPart = tpart.address;
                        math::vector2 scr{};
                        if (!game::visualengine->world_to_screen(vm, dims, pos, scr)) scr = center;
                        bestScreenPos = scr; bestEntityAddr = entity.instance.address; found = true;
                    }
                } else {
                    math::vector2 scr{};
                    if (!game::visualengine->world_to_screen(vm, dims, pos, scr)) continue;
                    float dx = scr.x - center.x, dy = scr.y - center.y;
                    float d2sq = dx*dx + dy*dy;
                    if (d2sq <= closestDistSq) {
                        closestDistSq = d2sq; targetPos = pos;
                        targetPart = tpart.address;
                        bestScreenPos = scr; bestEntityAddr = entity.instance.address; found = true;
                    }
                }
            }

            if (!found) {
                set_active(false);
                Cheat::Features::ViewportSilent::SetActive(false);
                Cheat::Features::CameraSilent::SetActive(false);
                target_acquired = false; target_screen_pos = {}; target_address = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::aim_thread_sleep));
                continue;
            }

            target_acquired   = true;
            target_screen_pos = bestScreenPos;
            target_address    = bestEntityAddr;

            static auto last_target_log = std::chrono::steady_clock::now();
            auto now_log = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now_log - last_target_log).count() >= 3)
            {
                last_target_log = now_log;
                uint64_t total_calls = get_calls();
                log_raycast(std::format("Target active: pos=({:.1f}, {:.1f}, {:.1f}) | hook_installed={} | raycast_calls={}",
                    targetPos.x, targetPos.y, targetPos.z, g_hook.installed, total_calls));
            }

            if (main_raycast_active || settings::raycast_silentaim::enabled) {
                set_active(true, targetPos, targetPart, camPos);
            } else {
                set_active(false);
            }

            if (main_viewport_active) {
                Cheat::Features::ViewportSilent::SetActive(true, targetPos);
            } else {
                Cheat::Features::ViewportSilent::SetActive(false);
            }

            if (main_camera_active) {
                Cheat::Features::CameraSilent::SetActive(true, targetPos);
            } else {
                Cheat::Features::CameraSilent::SetActive(false);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::aim_thread_sleep));
        }
    }

    void init() {
        static std::once_flag once;
        std::call_once(once, [] {
            log_raycast("Raycast");
            std::thread(raycast_worker).detach();
            std::thread(position_spoof_worker).detach();
        });
    }

    void run()  { init(); }

    void draw_fov() {
        if (!settings::raycast_silentaim::draw_fov) return;
        math::vector2 dims = game::visualengine->get_dimensions();
        if (dims.x <= 0.f || dims.y <= 0.f) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 center{ dims.x / 2.f, dims.y / 2.f };
        ImU32 col = IM_COL32(
            (int)(settings::raycast_silentaim::fov_circle_colour[0] * 255),
            (int)(settings::raycast_silentaim::fov_circle_colour[1] * 255),
            (int)(settings::raycast_silentaim::fov_circle_colour[2] * 255),
            (int)(settings::raycast_silentaim::fov_circle_colour[3] * 255));
        dl->AddCircle(center, settings::raycast_silentaim::fov, col, 64, 1.5f);
    }

    uint64_t get_calls() {
        if (!g_hook.installed || !g_hook.state) return 0;
        uint64_t val = 0;
        r_mem(g_hook.state + offsetof(RaycastState, calls), &val, sizeof(val));
        return val;
    }

}
