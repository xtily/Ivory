#pragma once
#include <cstdint>

namespace InstanceCreate {

bool New(const char* className, uint64_t parent, uint64_t* out_addr);
bool SetParent(uint64_t inst, uint64_t parent);
bool SetString(uint64_t field, const char* text);
bool SetContent(uint64_t string_field, const char* text);
int LastFail();

} // namespace InstanceCreate


