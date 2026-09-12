#pragma once
#include <cstdint>

namespace CallGate {

bool Install(const char* method_name = nullptr);
void Remove();
bool Ready();

bool Invoke(uint64_t fn,
            uint64_t a0, uint64_t a1,
            uint64_t a2, uint64_t a3,
            uint64_t* out_ret, unsigned timeout_ms = 3000);

uint64_t Scratch();
uint64_t Calls();
uint64_t SlotAddress();
int LastFail();

}