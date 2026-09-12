#include "CallGate.h"
#include "Reflect.h"
#include <sdk/offsets/offsets.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>

#include <windows.h>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>
#include <mutex>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace CallGate {

static std::recursive_mutex g_invoke_mutex;

namespace {

constexpr uintptr_t st_pending = 0x00;
constexpr uintptr_t st_done    = 0x04;
constexpr uintptr_t st_fn      = 0x08;
constexpr uintptr_t st_a0      = 0x10;
constexpr uintptr_t st_a1      = 0x18;
constexpr uintptr_t st_a2      = 0x20;
constexpr uintptr_t st_a3      = 0x28;
constexpr uintptr_t st_ret     = 0x30;
constexpr uintptr_t st_calls   = 0x38;
constexpr uintptr_t st_tid     = 0x40;
constexpr uintptr_t st_scratch = 0x100;

constexpr size_t stub_bytes = 0x120;
constexpr size_t stub_orig_off = 0x1F;

struct Gate {
    bool      installed = false;
    uintptr_t slot = 0;
    uintptr_t orig = 0;
    uintptr_t stub = 0;
    uintptr_t state = 0;
    bool      stub_is_cave = false;
    size_t   cand = 0;
    std::string method;
    uintptr_t chain_thunk      = 0;
    uintptr_t chain_patch_addr = 0;
    uintptr_t chain_orig_saved = 0;
};

const char* const k_candidates[] = {
    "IsA", "FindFirstChild", "GetChildren", "WaitForChild",
    "FindFirstChildOfClass", "GetDescendants", "GetAttribute", "Clone",
};

constexpr size_t k_candidate_count = sizeof(k_candidates) / sizeof(k_candidates[0]);

Gate g_gate;
int  g_last_fail = 0;
bool g_cold[k_candidate_count]{};

size_t page_sz() {
    static size_t v = []() -> size_t {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        return si.dwPageSize ? si.dwPageSize : 0x1000;
    }();
    return v;
}

bool is_exec_protect(DWORD p) {
    DWORD b = p & 0xFF;
    return b == PAGE_EXECUTE || b == PAGE_EXECUTE_READ ||
           b == PAGE_EXECUTE_READWRITE || b == PAGE_EXECUTE_WRITECOPY;
}

DWORD query_protect(uintptr_t a) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(Mem::Get().GetHandle(), (void*)a, &mbi, sizeof(mbi)))
        return 0;
    if (mbi.State != MEM_COMMIT)
        return 0;
    return mbi.Protect;
}

bool write_protected(uintptr_t addr, const void* data, size_t size) {
    if (!addr || !data || !size)
        return false;

    DWORD old = 0;
    BOOL changed = VirtualProtectEx(Mem::Get().GetHandle(), (LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old);
    SIZE_T wrote = 0;
    BOOL ok = WriteProcessMemory(Mem::Get().GetHandle(), (LPVOID)addr, data, size, &wrote);
    if (changed)
        VirtualProtectEx(Mem::Get().GetHandle(), (LPVOID)addr, size, old, &old);
    return ok && wrote == size;
}

bool mark_cfg(uintptr_t t) {
    HMODULE h = GetModuleHandleA("kernelbase.dll");
    if (!h) h = GetModuleHandleA("kernel32.dll");
    if (!h) return false;

    FARPROC proc = GetProcAddress(h, "SetProcessValidCallTargets");
    if (!proc) return false;

    struct Info { ULONG_PTR Offset; ULONG Flags; } info{};
    info.Offset = t & (page_sz() - 1);
    info.Flags = 0x00000001;

    using Fn = BOOL(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, void*);
    return ((Fn)proc)(Mem::Get().GetHandle(),
        (void*)(t & ~((uintptr_t)page_sz() - 1)),
        page_sz(), 1, &info) != 0;
}

bool module_range(const wchar_t* name, uintptr_t* out_base, size_t* out_size) {
    uintptr_t base = 0;
    if (wcscmp(name, L"RobloxPlayerBeta.exe") == 0) {
        base = Rbx::Get().Base;
    } else {
        HANDLE proc = Mem::Get().GetHandle();
        if (!proc) return false;
        HMODULE mods[1024];
        DWORD needed = 0;
        if (EnumProcessModules(proc, mods, sizeof(mods), &needed)) {
            DWORD cnt = needed / sizeof(HMODULE);
            for (DWORD i = 0; i < cnt; ++i) {
                wchar_t buf[MAX_PATH]{};
                if (GetModuleBaseNameW(proc, mods[i], buf, MAX_PATH) &&
                    _wcsicmp(buf, name) == 0) {
                    base = (uintptr_t)mods[i];
                    break;
                }
            }
        }
    }
    if (!base) return false;

    int32_t lfanew = Mem::Get().Read<int32_t>(base + 0x3C);
    if (lfanew <= 0 || lfanew > 0x1000) return false;

    uint32_t img = Mem::Get().Read<uint32_t>(base + lfanew + 0x50);
    if (img < 0x1000 || img > 0x20000000u) return false;

    *out_base = base;
    *out_size = img;
    return true;
}

template <typename Cb>
void walk_committed(uintptr_t from, uintptr_t to, bool want_exec, Cb cb) {
    std::vector<uint8_t> buf;
    uintptr_t addr = from;
    MEMORY_BASIC_INFORMATION mbi{};

    while (addr < to &&
           VirtualQueryEx(Mem::Get().GetHandle(), (void*)addr, &mbi, sizeof(mbi)))
    {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress;
        size_t rs = (size_t)mbi.RegionSize;
        uintptr_t next = rb + rs;
        if (next <= addr) break;

        bool usable = mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            is_exec_protect(mbi.Protect) == want_exec;
        if (usable) {
            uintptr_t s = (rb > from) ? rb : from;
            uintptr_t e = (next < to) ? next : to;
            if (e > s) {
                buf.resize((size_t)(e - s));
                if (Mem::Get().ReadMemory(s, buf.data(), buf.size()))
                    cb(s, buf);
            }
        }
        addr = next;
    }
}

bool ok_addr(uintptr_t a) { return a >= 0x10000 && a < 0x7FFFFFFFFFFF; }

uintptr_t find_bound_desc(uintptr_t base, size_t size, uint64_t name) {
    if (!name) return 0;
    uintptr_t found = 0;

    walk_committed(base, base + size, false,
        [&](uintptr_t at, const std::vector<uint8_t>& b) {
            if (found || b.size() < 16) return;

            for (size_t i = 8; i + 8 <= b.size(); i += 8) {
                uint64_t v = 0;
                std::memcpy(&v, b.data() + i, 8);
                if (v != name) continue;

                uintptr_t desc = at + i - 8;
                uintptr_t vt = Mem::Get().Read<uint64_t>(desc);
                if (vt < base || vt >= base + size) continue;

                uintptr_t fn = Mem::Get().Read<uint64_t>(desc + Offsets::WorldRoot::RaycastBoundFn);
                if (fn && is_exec_protect(query_protect((uintptr_t)fn))) {
                    found = desc;
                    return;
                }
            }
        });

    return found;
}

uintptr_t find_exec_cave(size_t need) {
    static const wchar_t* pref[] = {
        L"WebView2Loader.dll",
    };

    for (auto* name : pref) {
        uintptr_t mb = 0;
        size_t ms = 0;
        if (!module_range(name, &mb, &ms)) continue;

        uintptr_t existing_hit = 0;
        walk_committed(mb + 0x1000, mb + ms, true,
            [&](uintptr_t at, const std::vector<uint8_t>& b) {
                if (existing_hit || b.size() < need) return;
                for (size_t i = 0; i + 10 <= b.size(); i += 16) {
                    if (b[i] == 0x49 && b[i + 1] == 0xBA) {
                        uint64_t state_ptr = 0;
                        std::memcpy(&state_ptr, b.data() + i + 2, 8);
                        if (state_ptr >= 0x10000 && state_ptr < 0x7FFFFFFFFFFF) {
                            existing_hit = at + i;
                            return;
                        }
                    }
                }
            });

        if (existing_hit) return existing_hit;

        uintptr_t hit = 0;
        walk_committed(mb + 0x1000, mb + ms, true,
            [&](uintptr_t at, const std::vector<uint8_t>& b) {
                if (hit || b.size() < need) return;
                size_t run = 0;
                for (size_t i = 0; i < b.size(); ++i) {
                    if (b[i] != 0xCC && b[i] != 0x00) {
                        run = 0;
                        continue;
                    }
                    if (++run < need + 0x10) continue;
                    uintptr_t start = at + i + 1 - run;
                    hit = (start + 0x0F) & ~(uintptr_t)0x0F;
                    return;
                }
            });

        if (hit) return hit;
    }
    return 0;
}

void emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> b) {
    c.insert(c.end(), b);
}

void emit_u32(std::vector<uint8_t>& c, uint32_t v) {
    const auto* b = (const uint8_t*)&v;
    c.insert(c.end(), b, b + 4);
}

void emit_u64(std::vector<uint8_t>& c, uint64_t v) {
    const auto* b = (const uint8_t*)&v;
    c.insert(c.end(), b, b + 8);
}

std::vector<uint8_t> build_stub(uintptr_t state, uintptr_t orig) {
    std::vector<uint8_t> c;

    emit(c, { 0x49, 0xBA });                      
    emit_u64(c, state);
    emit(c, { 0xF0, 0x49, 0xFF, 0x42, 0x38 });   
    emit(c, { 0x41, 0x83, 0x3A, 0x00 });         
    emit(c, { 0x0F, 0x85 });
    size_t fix_slow = c.size();
    emit_u32(c, 0);

    size_t pass = c.size();
    emit(c, { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });
    emit_u64(c, orig);

    size_t slow = c.size();
    emit(c, { 0x49, 0x8B, 0x42, 0x40 });         
    emit(c, { 0x48, 0x85, 0xC0 });              
    emit(c, { 0x74, 0x00 });                  
    size_t fix_take = c.size() - 1;
    emit(c, { 0x65, 0x4C, 0x8B, 0x1C, 0x25 });
    emit_u32(c, 0x48);
    emit(c, { 0x4C, 0x39, 0xD8 });              
    emit(c, { 0x0F, 0x85 });
    size_t fix_pass1 = c.size();
    emit_u32(c, 0);

    size_t take = c.size();
    emit(c, { 0xB8, 0x01, 0x00, 0x00, 0x00 });    
    emit(c, { 0x45, 0x31, 0xDB });                
    emit(c, { 0xF0, 0x45, 0x0F, 0xB1, 0x1A });    
    emit(c, { 0x0F, 0x85 });                     
    size_t fix_pass2 = c.size();
    emit_u32(c, 0);

    emit(c, { 0x48, 0x81, 0xEC });               
    emit_u32(c, 0x88);
    emit(c, { 0x4C, 0x89, 0x94, 0x24 });          
    emit_u32(c, 0x80);
    emit(c, { 0x48, 0x89, 0x4C, 0x24, 0x20 });    
    emit(c, { 0x48, 0x89, 0x54, 0x24, 0x28 });    
    emit(c, { 0x4C, 0x89, 0x44, 0x24, 0x30 });    
    emit(c, { 0x4C, 0x89, 0x4C, 0x24, 0x38 });   
    emit(c, { 0x0F, 0x11, 0x44, 0x24, 0x40 });    
    emit(c, { 0x0F, 0x11, 0x4C, 0x24, 0x50 });   
    emit(c, { 0x0F, 0x11, 0x54, 0x24, 0x60 });   
    emit(c, { 0x0F, 0x11, 0x5C, 0x24, 0x70 });   

    emit(c, { 0x49, 0x8B, 0x42, 0x08 });         
    emit(c, { 0x49, 0x8B, 0x4A, 0x10 });        
    emit(c, { 0x49, 0x8B, 0x52, 0x18 });       
    emit(c, { 0x4D, 0x8B, 0x42, 0x20 });          
    emit(c, { 0x4D, 0x8B, 0x4A, 0x28 });         
    emit(c, { 0xFF, 0xD0 });                      

    emit(c, { 0x4C, 0x8B, 0x94, 0x24 });         
    emit_u32(c, 0x80);
    emit(c, { 0x49, 0x89, 0x42, 0x30 });       
    emit(c, { 0x41, 0xC7, 0x42, 0x04 });         
    emit_u32(c, 1);

    emit(c, { 0x0F, 0x10, 0x44, 0x24, 0x40 });    
    emit(c, { 0x0F, 0x10, 0x4C, 0x24, 0x50 });    
    emit(c, { 0x0F, 0x10, 0x54, 0x24, 0x60 });   
    emit(c, { 0x0F, 0x10, 0x5C, 0x24, 0x70 });   
    emit(c, { 0x48, 0x8B, 0x4C, 0x24, 0x20 });    
    emit(c, { 0x48, 0x8B, 0x54, 0x24, 0x28 });    
    emit(c, { 0x4C, 0x8B, 0x44, 0x24, 0x30 });    
    emit(c, { 0x4C, 0x8B, 0x4C, 0x24, 0x38 });    
    emit(c, { 0x48, 0x81, 0xC4 });               
    emit_u32(c, 0x88);
    emit(c, { 0xE9 });
    size_t fix_pass3 = c.size();
    emit_u32(c, 0);

    auto rel = [&](size_t at, size_t target) {
        int32_t v = (int32_t)((ptrdiff_t)target - (ptrdiff_t)(at + 4));
        std::memcpy(c.data() + at, &v, 4);
    };

    rel(fix_slow, slow);
    rel(fix_pass1, pass);
    rel(fix_pass2, pass);
    rel(fix_pass3, pass);
    c[fix_take] = (uint8_t)(take - (fix_take + 1));
    return c;
}

uint64_t unwrap_stale(uint64_t fn, uintptr_t base, size_t size) {
    uint8_t head[2]{};
    if (!Mem::Get().ReadMemory((uintptr_t)fn, head, sizeof(head)))
        return 0;

    if (head[0] != 0x49 || head[1] != 0xBA)
        return 0;

    uint8_t jmp[2]{};
    if (!Mem::Get().ReadMemory((uintptr_t)fn + stub_orig_off - 6, jmp, sizeof(jmp)))
        return 0;

    if (jmp[0] != 0xFF || jmp[1] != 0x25)
        return 0;

    uint64_t orig = Mem::Get().Read<uint64_t>((uintptr_t)fn + stub_orig_off);
    if (orig < base || orig >= base + size)
        return 0;

    return orig;
}

static uintptr_t try_extract_embedded_fn(uint64_t thunk_addr,
                                         uintptr_t roblox_base, size_t roblox_size,
                                         uint64_t* out_fn)
{
    if (!out_fn) return 0;
    *out_fn = 0;

    constexpr size_t scan_size = 0x200;
    uint8_t buf[scan_size]{};
    if (!Mem::Get().ReadMemory((uintptr_t)thunk_addr, buf, scan_size))
        return 0;

    for (int i = (int)scan_size - 12; i >= 0; --i) {
        if (buf[i] != 0x48 || buf[i + 1] != 0xB8) continue; 
        if (buf[i + 10] != 0xFF || buf[i + 11] != 0xD0) continue;
        uint64_t embedded = 0;
        std::memcpy(&embedded, buf + i + 2, 8);
        if (embedded >= roblox_base && embedded < roblox_base + roblox_size
            && is_exec_protect(query_protect((uintptr_t)embedded)))
        {
            *out_fn = embedded;
            return (uintptr_t)thunk_addr + (uintptr_t)i + 2;
        }
    }
    return 0;
}

static bool install_impl(const char* method_name, size_t from);

__declspec(noinline) static bool install_impl_guarded(const char* method_name, size_t from)
{
    bool result = false;
    __try {
        result = install_impl(method_name, from);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_last_fail = 99;
        result = false;
    }
    return result;
}

bool install_impl(const char* method_name, size_t from) {
    if (g_gate.installed) return true;

    g_last_fail = 0;

    uintptr_t base = 0;
    size_t size = 0;
    if (!module_range(L"RobloxPlayerBeta.exe", &base, &size)) {
        g_last_fail = 1;
        return false;
    }

    const bool forced = method_name && method_name[0];

    const char* method = nullptr;
    uintptr_t slot = 0;
    uint64_t orig = 0;
    size_t picked = from;

    uint64_t      chain_thunk_addr  = 0;
    uintptr_t     chain_patch_addr  = 0;
    uint64_t      chain_orig_saved  = 0;

    for (size_t i = from; i < k_candidate_count; ++i) {
        if (!forced && g_cold[i]) continue;

        const char* try_name = forced ? method_name : k_candidates[i];

        const uint64_t name = Reflect::Name(base, try_name);
        const uintptr_t desc = name ? find_bound_desc(base, size, name) : 0;
        if (desc) {
            const uintptr_t s = desc + Offsets::WorldRoot::RaycastBoundFn;
            auto fn = Mem::Get().Read<uint64_t>(s);

            if (fn < base || fn >= base + size) {
                uint64_t unwrapped = unwrap_stale(fn, base, size);
                if (unwrapped) {
                    fn = unwrapped;
                } else {
                    uint64_t embedded_fn = 0;
                    uintptr_t patch_addr = try_extract_embedded_fn(fn, base, size, &embedded_fn);
                    if (patch_addr && embedded_fn) {
                        chain_thunk_addr = fn;
                        chain_patch_addr = patch_addr;
                        chain_orig_saved  = embedded_fn;
                        fn = embedded_fn;
                    }
                }
            }

            if (fn) {
                method = try_name;
                slot = s;
                orig = fn;
                picked = i;
                break;
            }
        }

        if (forced) break;
    }

    if (!slot || !orig) {
        g_last_fail = 2;
        return false;
    }

    uintptr_t state = Mem::Get().Allocate(page_sz());
    if (!state) {
        g_last_fail = 4;
        return false;
    }

    std::vector<uint8_t> zero(page_sz(), 0);
    WriteProcessMemory(Mem::Get().GetHandle(), (LPVOID)state, zero.data(), zero.size(), nullptr);

    uintptr_t cave = find_exec_cave(stub_bytes);
    if (!cave) {
        VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)state, 0, MEM_RELEASE);
        g_last_fail = 5;
        return false;
    }
    bool was_already_stub = false;
    uint8_t cave_sig[2]{};
    if (Mem::Get().ReadMemory(cave, cave_sig, sizeof(cave_sig)) && cave_sig[0] == 0x49 && cave_sig[1] == 0xBA) {
        uint64_t existing_state = 0;
        if (Mem::Get().ReadMemory(cave + 2, &existing_state, 8) && existing_state >= 0x10000 && existing_state < 0x7FFFFFFFFFFF) {
            VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)state, 0, MEM_RELEASE);
            state = (uintptr_t)existing_state;
            was_already_stub = true;
        }
    }

    if (!was_already_stub) {
        auto stub = build_stub(state, orig);
        if (!write_protected(cave, stub.data(), stub.size())) {
            VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)state, 0, MEM_RELEASE);
            g_last_fail = 6;
            return false;
        }

        if (chain_thunk_addr && chain_patch_addr) {
            if (!write_protected(chain_patch_addr, &cave, sizeof(cave))) {
                if (!write_protected(slot, &cave, sizeof(cave))) {
                    VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)state, 0, MEM_RELEASE);
                    g_last_fail = 7;
                    return false;
                }
                chain_thunk_addr = 0;
            }
        } else {
            if (!write_protected(slot, &cave, sizeof(cave))) {
                VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)state, 0, MEM_RELEASE);
                g_last_fail = 7;
                return false;
            }
        }
    }

    g_gate.installed = true;
    g_gate.slot = slot;
    g_gate.orig = orig;
    g_gate.stub = cave;
    g_gate.state = state;
    g_gate.stub_is_cave = true;
    g_gate.cand = picked;
    if (method) g_gate.method = method;
    g_gate.chain_thunk      = chain_thunk_addr;
    g_gate.chain_patch_addr = chain_patch_addr;
    g_gate.chain_orig_saved = chain_orig_saved;

    return true;
}

} 

bool Ready() { return g_gate.installed; }
int LastFail() { return g_last_fail; }
uint64_t SlotAddress() { return g_gate.slot; }
uint64_t Scratch() { return g_gate.state ? g_gate.state + st_scratch : 0; }

uint64_t Calls() {
    if (!g_gate.state) return 0;
    return Mem::Get().Read<uint64_t>(g_gate.state + st_calls);
}

bool Install(const char* method_name) {
    return install_impl_guarded(method_name, 0);
}

void Remove() {
    if (!g_gate.installed) return;
    Mem::Get().Write<uint64_t>(g_gate.slot, (uint64_t)g_gate.orig);
    Sleep(50);
    if (!g_gate.stub_is_cave && g_gate.stub)
        VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)g_gate.stub, 0, MEM_RELEASE);
    if (g_gate.state)
        VirtualFreeEx(Mem::Get().GetHandle(), (LPVOID)g_gate.state, 0, MEM_RELEASE);
    g_gate = Gate{};
}

static bool invoke_once(uint64_t fn, uint64_t a0, uint64_t a1,
                        uint64_t a2, uint64_t a3,
                        uint64_t* out_ret, unsigned timeout_ms,
                        bool* out_cold)
{
    if (out_ret) *out_ret = 0;
    if (out_cold) *out_cold = false;

    if (!g_gate.installed || !fn) {
        g_last_fail = 10;
        return false;
    }

    uintptr_t s = g_gate.state;
    uint64_t hits_before = Mem::Get().Read<uint64_t>(s + st_calls);
    Mem::Get().Write<uint32_t>(s + st_done, 0);
    Mem::Get().Write<uint64_t>(s + st_ret, 0);
    Mem::Get().Write<uint64_t>(s + st_fn, fn);
    Mem::Get().Write<uint64_t>(s + st_a0, a0);
    Mem::Get().Write<uint64_t>(s + st_a1, a1);
    Mem::Get().Write<uint64_t>(s + st_a2, a2);
    Mem::Get().Write<uint64_t>(s + st_a3, a3);

    Mem::Get().Write<uint32_t>(s + st_pending, 1);

    DWORD deadline = GetTickCount() + timeout_ms;
    while (GetTickCount() < deadline) {
        if (Mem::Get().Read<uint32_t>(s + st_done) == 1) {
            if (out_ret) *out_ret = Mem::Get().Read<uint64_t>(s + st_ret);
            g_last_fail = 0;
            return true;
        }
        if (!Mem::Get().IsAlive()) break;
        Sleep(1);
    }

    Mem::Get().Write<uint32_t>(s + st_pending, 0);
    uint64_t hits_after = Mem::Get().Read<uint64_t>(s + st_calls);
    if (out_cold) *out_cold = (hits_after == hits_before);
    g_last_fail = 12;
    return false;
}

__declspec(noinline) static bool invoke_once_guarded(
    uint64_t fn, uint64_t a0, uint64_t a1,
    uint64_t a2, uint64_t a3,
    uint64_t* out_ret, unsigned timeout_ms,
    bool* out_cold)
{
    bool result = false;
    __try {
        result = invoke_once(fn, a0, a1, a2, a3, out_ret, timeout_ms, out_cold);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_cold) *out_cold = false;
        g_last_fail = 98;
        result = false;
    }
    return result;
}

static void remove_guarded()
{
    HANDLE    proc            = Mem::Get().GetHandle();
    uint64_t  slot            = g_gate.slot;
    uint64_t  orig            = g_gate.orig;
    bool      free_stub       = !g_gate.stub_is_cave && g_gate.stub;
    LPVOID    stub_ptr        = (LPVOID)g_gate.stub;
    LPVOID    state_ptr       = (LPVOID)g_gate.state;
    uintptr_t chain_thunk     = g_gate.chain_thunk;
    uintptr_t chain_patch     = g_gate.chain_patch_addr;
    uint64_t  chain_orig      = g_gate.chain_orig_saved;

    g_gate = Gate{};

    if (chain_thunk && chain_patch && chain_orig) {
        WriteProcessMemory(proc, (LPVOID)chain_patch, &chain_orig, sizeof(chain_orig), nullptr);
    } else {
        WriteProcessMemory(proc, (LPVOID)slot, &orig, sizeof(orig), nullptr);
    }

    Sleep(50);

    if (free_stub)  VirtualFreeEx(proc, stub_ptr,  0, MEM_RELEASE);
    if (state_ptr)  VirtualFreeEx(proc, state_ptr, 0, MEM_RELEASE);
}


bool Invoke(uint64_t fn, uint64_t a0, uint64_t a1,
            uint64_t a2, uint64_t a3,
            uint64_t* out_ret, unsigned timeout_ms)
{
    if (timeout_ms > 300) timeout_ms = 300;

    for (size_t tries = 0; tries < k_candidate_count; ++tries)
    {
        bool cold = false;
        bool ok   = false;

        {
            std::lock_guard<std::recursive_mutex> lk(g_invoke_mutex);
            ok = invoke_once_guarded(fn, a0, a1, a2, a3, out_ret, timeout_ms, &cold);
        }

        if (ok) return true;

        if (!cold || !g_gate.installed) return false;

        {
            std::lock_guard<std::recursive_mutex> lk(g_invoke_mutex);
            if (g_gate.installed) {
                g_cold[g_gate.cand] = true;
                remove_guarded();
            }
        }


        Sleep(15);

        {
            std::lock_guard<std::recursive_mutex> lk(g_invoke_mutex);
            if (!install_impl_guarded(nullptr, 0))
                return false;
        }
    }
    return false;
}

}  


