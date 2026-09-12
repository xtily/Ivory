#include "Reflect.h"
#include <sdk/offsets/offsets.h>
#include <features/lua/mem/MemCompat.h>
#include <cstring>

namespace Reflect {
namespace {

struct Table
{
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t empty = 0;
};

bool KeyEquals(uint64_t key, const char* text, size_t len)
{
    char buf[128];
    if (len + 1 > sizeof(buf))
        return false;

    if (!Mem::Get().ReadMemory((uintptr_t)key, buf, len + 1))
        return false;

    return buf[len] == '\0' && std::memcmp(buf, text, len) == 0;
}

bool ReadTable(uintptr_t at, Table* out)
{
    out->start = Mem::Get().Read<uint64_t>(at + Offsets::Reflection::TableStart);
    out->end = Mem::Get().Read<uint64_t>(at + Offsets::Reflection::TableEnd);
    out->empty = Mem::Get().Read<uint64_t>(at + Offsets::Reflection::TableEmpty);

    return out->start && out->end > out->start &&
           (out->end - out->start) <= 0x400000;
}

}

uint64_t Name(uintptr_t base, const char* text)
{
    if (!text || !text[0])
        return 0;

    const auto reg = Mem::Get().Read<uint64_t>(
        base + Offsets::Reflection::NameRegistry);
    if (!reg)
        return 0;

    Table t;
    if (!ReadTable((uintptr_t)reg + Offsets::Reflection::NameTable, &t))
        return 0;

    const size_t len = std::strlen(text);

    for (uint64_t e = t.start; e + Offsets::Reflection::TableStride <= t.end;
         e += Offsets::Reflection::TableStride)
    {
        const auto key = Mem::Get().Read<uint64_t>(e);
        if (!key || key == t.empty)
            continue;

        if (!KeyEquals(key, text, len))
            continue;

        return Mem::Get().Read<uint64_t>(e + Offsets::Reflection::EntryValue);
    }

    return 0;
}

uint64_t Creator(uintptr_t base, uint64_t name)
{
    if (!name)
        return 0;

    Table t;
    if (!ReadTable(base + Offsets::Reflection::CreatorTable, &t))
        return 0;

    for (uint64_t e = t.start; e + Offsets::Reflection::TableStride <= t.end;
         e += Offsets::Reflection::TableStride)
    {
        if (Mem::Get().Read<uint64_t>(e) != name)
            continue;

        return Mem::Get().Read<uint64_t>(e + Offsets::Reflection::EntryValue);
    }

    return 0;
}

uint64_t CreatorByName(uintptr_t base, const char* className)
{
    return Creator(base, Name(base, className));
}

}