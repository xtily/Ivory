#include "InstanceCreate.h"
#include "CallGate.h"
#include "Reflect.h"
#include <sdk/offsets/offsets.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>

#include <windows.h>
#include <cstring>
#include <string>
#include <unordered_map>

namespace InstanceCreate {
namespace {

int g_last_fail = 0;

std::unordered_map<std::string, bool> g_creatable;
uintptr_t g_creatable_base = 0;

}

int LastFail()
{
    return g_last_fail;
}

bool New(const char* className, uint64_t parent, uint64_t* out_addr)
{
    if (out_addr)
        *out_addr = 0;

    if (!className || !className[0])
    {
        g_last_fail = 1;
        return false;
    }

    uintptr_t base = Rbx::Get().Base;
    if (!base)
    {
        g_last_fail = 2;
        return false;
    }

    uint64_t name_hash = Reflect::Name(base, className);

    uint64_t creator = Reflect::Creator(base, name_hash);
    if (!creator)
    {
        creator = Reflect::CreatorByName(base, className);
    }
    if (!creator)
    {
        g_last_fail = 3;
        return false;
    }

    auto vt = Mem::Get().Read<uint64_t>((uintptr_t)creator);
    if (!vt)
    {
        g_last_fail = 4;
        return false;
    }

    auto create_fn = Mem::Get().Read<uint64_t>((uintptr_t)vt + Offsets::Instance::Creator_create);
    auto creatable_fn = Mem::Get().Read<uint64_t>((uintptr_t)vt + Offsets::Instance::Creator_isCreatable);
    if (!create_fn || !creatable_fn)
    {
        g_last_fail = 5;
        return false;
    }

    if (!CallGate::Ready() && !CallGate::Install())
    {
        g_last_fail = 6;
        return false;
    }

    if (g_creatable_base != base)
    {
        g_creatable.clear();
        g_creatable_base = base;
    }

    auto cached = g_creatable.find(className);
    if (cached == g_creatable.end())
    {
        uint64_t ok = 0;
        if (!CallGate::Invoke(creatable_fn, creator, 0, 0, 0, &ok))
        {
            g_last_fail = 8;
            return false;
        }

        cached = g_creatable.emplace(className, (ok & 0xFF) != 0).first;
    }

    if (!cached->second)
    {
        g_last_fail = 9;
        return false;
    }

    uint64_t scratch = CallGate::Scratch();
    if (!scratch)
    {
        g_last_fail = 7;
        return false;
    }

    uint8_t zero[16]{};
    WriteProcessMemory(Mem::Get().GetHandle(), (LPVOID)scratch, zero, sizeof(zero), nullptr);

    uint64_t ret = 0;
    if (!CallGate::Invoke(create_fn, creator, scratch, 0, 0, &ret))
    {
        g_last_fail = 10;
        return false;
    }

    auto inst = Mem::Get().Read<uint64_t>((uintptr_t)scratch);
    if (!inst || !Mem::Get().IsValid((uintptr_t)inst))
    {
        g_last_fail = 11;
        return false;
    }

    if (parent && !SetParent(inst, parent))
    {
        g_last_fail = 12;
        return false;
    }

    if (out_addr)
        *out_addr = inst;

    g_last_fail = 0;
    return true;
}

bool SetString(uint64_t field, const char* text) {
    if (!field || !Mem::Get().IsValid(field) || !text) {
        g_last_fail = 1;
        return false;
    }

    size_t len = std::strlen(text);

    if (len < 16) {
        char buf[16]{};
        std::memcpy(buf, text, len);
        WriteProcessMemory(Mem::Get().GetHandle(), (LPVOID)field, buf, sizeof(buf), nullptr);
        Mem::Get().Write<uint64_t>(field + 0x10, len);
        Mem::Get().Write<uint64_t>(field + 0x18, 15);
        g_last_fail = 0;
        return true;
    }

    uintptr_t base = Rbx::Get().Base;
    if (!base || (!CallGate::Ready() && !CallGate::Install())) {
        g_last_fail = 6;
        return false;
    }

    uint64_t ptr = 0;
    if (!CallGate::Invoke(base + Offsets::Alloc::Malloc, len + 1, 0, 0, 0, &ptr) || !ptr) {
        g_last_fail = 10;
        return false;
    }

    WriteProcessMemory(Mem::Get().GetHandle(), (LPVOID)ptr, text, len + 1, nullptr);
    Mem::Get().Write<uint64_t>(field, ptr);
    Mem::Get().Write<uint64_t>(field + 0x10, len);
    Mem::Get().Write<uint64_t>(field + 0x18, len);
    g_last_fail = 0;
    return true;
}

bool SetContent(uint64_t string_field, const char* text) {
    if (!SetString(string_field, text)) return false;

    uint64_t content = string_field - 0x10;
    Mem::Get().Write<int32_t>(content, (text && text[0]) ? 1 : 0);
    Mem::Get().Write<int32_t>(content + 0x04, 0);
    Mem::Get().Write<int64_t>(content + 0x08, 0);
    return true;
}

bool SetParent(uint64_t inst, uint64_t parent) {
    if (!Mem::Get().IsValid(inst)) {
        g_last_fail = 1;
        return false;
    }

    if (parent != 0 && !Mem::Get().IsValid(parent)) {
        g_last_fail = 2;
        return false;
    }

    uintptr_t base = Rbx::Get().Base;
    if (base && (CallGate::Ready() || CallGate::Install())) {
        uint64_t ret = 0;
        if (CallGate::Invoke(base + Offsets::Instance::SetParent, inst, parent, 0, 0, &ret)) {
            g_last_fail = 0;
            return true;
        }
    }

    g_last_fail = 3;
    return false;
}

}