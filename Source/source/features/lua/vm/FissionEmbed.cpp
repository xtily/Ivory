// Fission in-process stub
#include <features/lua/vm/FissionEmbed.h>

namespace Cheat {
namespace Features {
namespace FissionEmbed {

std::string DecompileLuauBytecode(const std::uint8_t* data, std::size_t n)
{
	return ""; // Stub: Fission.Server or local lifter will be used instead
}

void UnescapeLuaByteEscapes(std::string& src)
{
}

} // namespace FissionEmbed
} // namespace Features
} // namespace Cheat
