#pragma once
#include <cstdint>

namespace Reflect {

uint64_t Name(uintptr_t base, const char* text);
uint64_t Creator(uintptr_t base, uint64_t name);
uint64_t CreatorByName(uintptr_t base, const char* className);

}