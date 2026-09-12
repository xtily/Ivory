#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cheat {
namespace Features {
namespace ScriptBytecode {

bool IsScriptClass(const std::string& cls);

// raw bytes as stored in Roblox (may be RSB1+zstd / signed / encoded)
bool ReadRaw(std::uint64_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out);

// normalize → raw Luau bytecode (version byte 3..11). status explains path/failure.
bool Normalize(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out, std::string* status = nullptr);

// alias used by older call sites
bool Decompress(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out);

bool ReadDecompressed(std::uint64_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out);

// readable source: Fission.Server :3001 если жив, иначе built-in lifter
std::string Decompile(const std::vector<std::uint8_t>& raw_or_luau, const char* chunk_name = "script");

// opcode-level bytecode disassembly
std::string Disassemble(const std::vector<std::uint8_t>& raw_or_luau, const char* chunk_name = "script");

// try localhost Fission.Server POST /luau/decompile. empty = unavailable/fail
std::string DecompileViaFission(const std::vector<std::uint8_t>& luau_bc);

// ping :3001, если нет — стартануть Fission.Server.exe рядом с читом
bool EnsureFissionServer();

} // namespace ScriptBytecode
} // namespace Features
} // namespace Cheat
