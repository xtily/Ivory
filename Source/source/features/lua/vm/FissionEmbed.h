#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Cheat {
namespace Features {
namespace FissionEmbed {

// in-process Fission. empty = fail
std::string DecompileLuauBytecode(const std::uint8_t* data, std::size_t n);

// \208\160 / \xD0 → utf8 в строковых литералах
void UnescapeLuaByteEscapes(std::string& src);

} // namespace FissionEmbed
} // namespace Features
} // namespace Cheat
