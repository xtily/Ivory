#pragma once

#include <core/protection/xorstr.h>

static const std::string BINARY_NAME_STR    = XS("RobloxPlayerBeta.exe");
static const std::string ANTICHEAT_NAME_STR = XS("RobloxPlayerBeta.dll");

#define BINARY_NAME    (BINARY_NAME_STR.c_str())
#define ANTICHEAT_NAME (ANTICHEAT_NAME_STR.c_str())