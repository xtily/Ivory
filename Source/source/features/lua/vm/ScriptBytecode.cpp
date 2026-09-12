#include <features/lua/vm/ScriptBytecode.h>
#include <features/lua/vm/FissionEmbed.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <features/lua/vm/ScriptBytecode.h>
#include <features/lua/vm/FissionEmbed.h>


#include "zstd.h"
#include "common/xxhash.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <optional>
#include <algorithm>

namespace Cheat {
namespace Features {
namespace ScriptBytecode {
namespace {

constexpr size_t k_max_lift_insns = 200000;
constexpr size_t k_max_dump_strings = 20000;

enum : int {
	LOP_NOP = 0,
	LOP_LOADNIL = 2,
	LOP_LOADB = 3,
	LOP_LOADN = 4,
	LOP_LOADK = 5,
	LOP_MOVE = 6,
	LOP_GETGLOBAL = 7,
	LOP_GETIMPORT = 12,
	LOP_GETTABLE = 13,
	LOP_GETTABLEKS = 15,
	LOP_SETTABLEKS = 17,
	LOP_NEWTABLE = 19,
	LOP_CALL = 21,
	LOP_RETURN = 22,
	LOP_CLOSURE = 55,
	LOP_PREPVARARGS = 65,
};

struct Reader {
	const std::uint8_t* p{ nullptr };
	const std::uint8_t* end{ nullptr };

	size_t Left() const { return p && p <= end ? static_cast<size_t>(end - p) : 0; }
	bool Take(void* dst, size_t n)
	{
		if (Left() < n) return false;
		std::memcpy(dst, p, n);
		p += n;
		return true;
	}
	bool U8(std::uint8_t& v) { return Take(&v, 1); }
	bool U32(std::uint32_t& v) { return Take(&v, 4); }
	bool VarInt(std::uint32_t& v)
	{
		v = 0;
		std::uint32_t shift = 0;
		for (;;)
		{
			std::uint8_t b = 0;
			if (!U8(b)) return false;
			if (shift >= 32) return false;
			v |= static_cast<std::uint32_t>(b & 0x7f) << shift;
			if ((b & 0x80) == 0) return true;
			shift += 7;
		}
	}
	bool String(std::string& s)
	{
		std::uint32_t n = 0;
		if (!VarInt(n) || Left() < n) return false;
		s.assign(reinterpret_cast<const char*>(p), n);
		p += n;
		return true;
	}
	bool Skip(size_t n)
	{
		if (Left() < n) return false;
		p += n;
		return true;
	}
};

std::string EscapeLua(const std::string& s)
{
	std::string o;
	o.reserve(s.size() + 8);
	o.push_back('"');
	for (unsigned char c : s)
	{
		if (c == '\\' || c == '"') { o.push_back('\\'); o.push_back(static_cast<char>(c)); }
		else if (c == '\n') o += "\\n";
		else if (c == '\r') o += "\\r";
		else if (c == '\t') o += "\\t";
		else if (c < 32 || c >= 127)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\x%02X", c);
			o += buf;
		}
		else o.push_back(static_cast<char>(c));
	}
	o.push_back('"');
	return o;
}

uintptr_t ByteCodeOffset(const std::string& cls, std::uint64_t script_addr = 0)
{
	if (script_addr != 0)
	{
		for (uintptr_t offset = 0x100; offset <= 0x250; offset += 8)
		{
			std::uint64_t bc_obj = memory->read<std::uint64_t>(script_addr + offset);
			if (bc_obj >= 0x10000 && bc_obj < 0x000FFFFFFFFF0000ULL)
			{
				std::uint64_t ptr = memory->read<std::uint64_t>(bc_obj + 0x10);
				std::uint64_t size = memory->read<std::uint64_t>(bc_obj + 0x20);
				if (ptr >= 0x10000 && ptr < 0x000FFFFFFFFF0000ULL && size > 16 && size < (16u * 1024u * 1024u))
				{
					std::uint32_t magic = memory->read<std::uint32_t>(ptr);
					if (magic == 0x31425352 || magic == 0xFD2FB528 || (magic & 0xFF) == 0 || ((magic & 0xFF) >= 3 && (magic & 0xFF) <= 11))
					{
						return offset;
					}
				}
			}
		}
	}
	if (cls == "ModuleScript")
		return (Offsets::ModuleScript::ByteCode != 0 ? Offsets::ModuleScript::ByteCode : 0x150);
	return (Offsets::LocalScript::ByteCode != 0 ? Offsets::LocalScript::ByteCode : 0x150);
}

bool IsLuauVersion(std::uint8_t v)
{
	return (v >= 3 && v <= 11) || v == 0;
}

bool TryRsb1(const std::uint8_t* data, size_t n, std::vector<std::uint8_t>& out)
{
	if (n <= 8 || std::memcmp(data, "RSB1", 4) != 0)
		return false;
	std::uint32_t dec_size = 0;
	std::memcpy(&dec_size, data + 4, 4);
	if (dec_size == 0 || dec_size > (32u * 1024u * 1024u))
		return false;
	const unsigned long long fcs = ZSTD_getFrameContentSize(data + 8, n - 8);
	if (fcs == ZSTD_CONTENTSIZE_ERROR)
		return false;
	if (fcs != ZSTD_CONTENTSIZE_UNKNOWN && fcs != dec_size)
		return false;
	out.resize(dec_size);
	const size_t got = ZSTD_decompress(out.data(), out.size(), data + 8, n - 8);
	if (ZSTD_isError(got))
	{
		out.clear();
		return false;
	}
	out.resize(got);
	return !out.empty() && IsLuauVersion(out[0]);
}

bool TryRobloxSigned(const std::uint8_t* data, size_t n, std::vector<std::uint8_t>& out)
{
	if (n < 8)
		return false;

	std::uint64_t mag = 0;
	std::memcpy(&mag, data, 8);
	if (mag == 0xE009325B4A107A52ull)
	{
		out.assign(data + 8, data + n);
		return !out.empty() && IsLuauVersion(out[0]);
	}

	static const std::uint8_t sig[4] = { 'R', 'S', 'B', '1' };
	std::vector<std::uint8_t> buf(data, data + n);
	std::uint8_t key[4]{};
	for (int i = 0; i < 4; ++i)
		key[i] = static_cast<std::uint8_t>((buf[static_cast<size_t>(i)] ^ sig[i]) - static_cast<std::uint8_t>(i * 41));

	for (size_t i = 0; i < buf.size(); ++i)
		buf[i] ^= static_cast<std::uint8_t>(key[i % 4] + static_cast<std::uint8_t>(i * 41));

	std::uint32_t expect = 0;
	std::memcpy(&expect, key, 4);
	const std::uint32_t got = XXH32(buf.data(), buf.size(), 42u);
	if (got != expect)
		return false;

	if (TryRsb1(buf.data(), buf.size(), out))
		return true;

	if (buf.size() > 8 && std::memcmp(buf.data(), "RSB1", 4) == 0)
	{
		std::uint32_t sz = 0;
		std::memcpy(&sz, buf.data() + 4, 4);
		if (sz == 0)
		{
			out.assign(buf.begin() + 8, buf.end());
			return !out.empty() && IsLuauVersion(out[0]);
		}
	}

	return false;
}

bool TryZstdAt(const std::uint8_t* data, size_t n, std::vector<std::uint8_t>& out)
{
	if (n < 4) return false;
	const unsigned long long sz = ZSTD_getFrameContentSize(data, n);
	if (sz == ZSTD_CONTENTSIZE_ERROR || sz == ZSTD_CONTENTSIZE_UNKNOWN || sz == 0 || sz > (32ull << 20))
		return false;
	out.resize(static_cast<size_t>(sz));
	const size_t got = ZSTD_decompress(out.data(), out.size(), data, n);
	if (ZSTD_isError(got))
	{
		out.clear();
		return false;
	}
	out.resize(got);
	return !out.empty() && IsLuauVersion(out[0]);
}

bool LooksLikeLuau(const std::vector<std::uint8_t>& bc)
{
	return !bc.empty() && IsLuauVersion(bc[0]);
}

std::string HexHead(const std::vector<std::uint8_t>& b, size_t n = 16)
{
	std::string h;
	char buf[4];
	for (size_t i = 0; i < b.size() && i < n; ++i)
	{
		std::snprintf(buf, sizeof(buf), "%02X", b[i]);
		if (i) h.push_back(' ');
		h += buf;
	}
	return h;
}

std::string Base64Encode(const std::uint8_t* data, size_t n)
{
	static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string o;
	o.reserve(((n + 2) / 3) * 4);
	for (size_t i = 0; i < n; i += 3)
	{
		const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16)
			| (i + 1 < n ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0)
			| (i + 2 < n ? static_cast<std::uint32_t>(data[i + 2]) : 0);
		o.push_back(k[(v >> 18) & 63]);
		o.push_back(k[(v >> 12) & 63]);
		o.push_back(i + 1 < n ? k[(v >> 6) & 63] : '=');
		o.push_back(i + 2 < n ? k[v & 63] : '=');
	}
	return o;
}


struct ProtoView {
	std::vector<std::uint32_t> code;
	std::vector<std::string> kstr;
	std::vector<int> kkind;
	std::uint8_t maxstack = 0;
	std::uint8_t numparams = 0;
};

int DecodeOp(std::uint32_t insn, int key)
{
	const int wire = static_cast<int>(insn & 0xFF);
	if (key <= 1)
		return wire;
	return (wire * key) & 0xFF;
}

std::optional<int> RecoverEncodingKey(const std::vector<std::uint32_t>& code)
{
	if (code.empty())
		return std::nullopt;
	const int wire = static_cast<int>(code[0] & 0xFF);
	if (wire == LOP_PREPVARARGS)
		return 0;
	if (((wire * 203) & 0xFF) == LOP_PREPVARARGS)
		return 203;
	for (int key = 3; key < 256; key += 2)
	{
		if (key == 203) continue;
		if (((wire * key) & 0xFF) == LOP_PREPVARARGS)
			return key;
	}
	return std::nullopt;
}

bool SkipProtoBody(Reader& r, std::uint8_t version, ProtoView* capture)
{
	std::uint8_t maxstack = 0, numparams = 0, nup = 0, isva = 0;
	if (!r.U8(maxstack) || !r.U8(numparams) || !r.U8(nup) || !r.U8(isva))
		return false;

	if (version >= 4)
	{
		std::uint8_t flags = 0;
		if (!r.U8(flags)) return false;
		std::uint32_t typesize = 0;
		if (!r.VarInt(typesize) || !r.Skip(typesize)) return false;
	}

	std::uint32_t sizecode = 0;
	if (!r.VarInt(sizecode) || sizecode > 2'000'000 || r.Left() < static_cast<size_t>(sizecode) * 4) return false;
	std::vector<std::uint32_t> code;
	code.resize(sizecode);
	for (std::uint32_t i = 0; i < sizecode; ++i)
	{
		if (!r.U32(code[i])) return false;
	}

	std::uint32_t sizek = 0;
	if (!r.VarInt(sizek) || sizek > 500'000 || r.Left() < sizek) return false;

	std::vector<std::string> kstr;
	std::vector<int> kkind;
	kstr.resize(sizek);
	kkind.assign(sizek, 4);

	for (std::uint32_t i = 0; i < sizek; ++i)
	{
		std::uint8_t tag = 0;
		if (!r.U8(tag)) return false;
		switch (tag)
		{
		case 0: // nil
			kkind[i] = 0; kstr[i] = "nil";
			break;
		case 1: // bool
		{
			std::uint8_t b = 0;
			if (!r.U8(b)) return false;
			kkind[i] = 1; kstr[i] = b ? "true" : "false";
			break;
		}
		case 2: // number
		{
			double d = 0;
			if (!r.Take(&d, 8)) return false;
			kkind[i] = 2;
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.17g", d);
			kstr[i] = buf;
			break;
		}
		case 3: // string
		{
			std::uint32_t idx = 0;
			if (!r.VarInt(idx)) return false;
			kkind[i] = 3;
			// filled later from string table
			kstr[i] = "\x01" + std::to_string(idx); // marker + 1-based string index
			break;
		}
		case 4: // import
		{
			std::uint32_t imp = 0;
			if (!r.U32(imp)) return false;
			kkind[i] = 4; kstr[i] = "import";
			break;
		}
		case 5: // table
		{
			std::uint32_t len = 0;
			if (!r.VarInt(len)) return false;
			for (std::uint32_t j = 0; j < len; ++j)
			{
				std::uint32_t dummy = 0;
				if (!r.VarInt(dummy)) return false;
			}
			kkind[i] = 4; kstr[i] = "{}";
			break;
		}
		case 6: // closure
		{
			std::uint32_t f = 0;
			if (!r.VarInt(f)) return false;
			kkind[i] = 4; kstr[i] = "closure";
			break;
		}
		case 7: // vector
		{
			if (!r.Skip(16)) return false;
			kkind[i] = 4; kstr[i] = "vector";
			break;
		}
		case 8: // table with constants
		{
			std::uint32_t len = 0;
			if (!r.VarInt(len)) return false;
			for (std::uint32_t j = 0; j < len; ++j)
			{
				std::uint32_t key = 0;
				std::int32_t val = 0;
				if (!r.VarInt(key) || !r.Take(&val, 4)) return false;
			}
			kkind[i] = 4; kstr[i] = "{}";
			break;
		}
		case 9: // integer
		{
			std::uint8_t neg = 0;
			if (!r.U8(neg)) return false;
			std::uint64_t mag = 0;
			// varint64
			std::uint32_t shift = 0;
			for (;;)
			{
				std::uint8_t b = 0;
				if (!r.U8(b)) return false;
				mag |= static_cast<std::uint64_t>(b & 0x7f) << shift;
				if ((b & 0x80) == 0) break;
				shift += 7;
				if (shift > 63) return false;
			}
			kkind[i] = 2;
			char buf[64];
			if (neg)
				std::snprintf(buf, sizeof(buf), "-%llu", (unsigned long long)mag);
			else
				std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)mag);
			kstr[i] = buf;
			break;
		}
		case 10: // class shape
		{
			std::uint32_t dummy = 0;
			if (!r.VarInt(dummy)) return false; // classname
			std::uint32_t props = 0, methods = 0;
			if (!r.VarInt(props) || !r.VarInt(methods)) return false;
			const std::uint64_t fields = static_cast<std::uint64_t>(props) + methods;
			if (fields > r.Left()) return false;
			for (std::uint64_t j = 0; j < fields; ++j)
			{
				if (!r.VarInt(dummy)) return false;
			}
			kkind[i] = 4; kstr[i] = "class";
			break;
		}
		default:
			return false;
		}
	}

	std::uint32_t sizep = 0;
	if (!r.VarInt(sizep)) return false;
	for (std::uint32_t i = 0; i < sizep; ++i)
	{
		std::uint32_t child = 0;
		if (!r.VarInt(child)) return false;
	}

	std::uint32_t linedefined = 0;
	std::uint32_t debugname = 0;
	if (!r.VarInt(linedefined) || !r.VarInt(debugname)) return false;

	std::uint8_t lineinfo = 0;
	if (!r.U8(lineinfo)) return false;
	if (lineinfo)
	{
		std::uint8_t linegap = 0;
		if (!r.U8(linegap)) return false;
		if (linegap >= 32 || sizecode == 0) return false;
		const size_t intervals = ((static_cast<size_t>(sizecode) - 1) >> linegap) + 1;
		if (!r.Skip(static_cast<size_t>(sizecode) + intervals * 4)) return false;
	}

	std::uint8_t debuginfo = 0;
	if (!r.U8(debuginfo)) return false;
	if (debuginfo)
	{
		std::uint32_t sizelocals = 0;
		if (!r.VarInt(sizelocals)) return false;
		for (std::uint32_t i = 0; i < sizelocals; ++i)
		{
			std::uint32_t a = 0, b = 0, c = 0;
			std::uint8_t reg = 0;
			if (!r.VarInt(a) || !r.VarInt(b) || !r.VarInt(c) || !r.U8(reg)) return false;
		}
		std::uint32_t sizeup = 0;
		if (!r.VarInt(sizeup)) return false;
		for (std::uint32_t i = 0; i < sizeup; ++i)
		{
			std::uint32_t u = 0;
			if (!r.VarInt(u)) return false;
		}
	}

	if (version >= 11)
	{
		std::uint32_t n = 0;
		if (!r.VarInt(n)) return false;
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::uint8_t t = 0;
			std::uint32_t pc = 0;
			if (!r.U8(t) || !r.VarInt(pc)) return false;
		}
	}

	if (capture)
	{
		capture->code = std::move(code);
		capture->kstr = std::move(kstr);
		capture->kkind = std::move(kkind);
		capture->maxstack = maxstack;
		capture->numparams = numparams;
	}
	return true;
}

void ResolveStringConstants(ProtoView& proto, const std::vector<std::string>& strings)
{
	for (size_t i = 0; i < proto.kstr.size(); ++i)
	{
		if (proto.kkind[i] != 3) continue;
		if (proto.kstr[i].size() < 2 || proto.kstr[i][0] != '\x01') continue;
		const int idx = std::atoi(proto.kstr[i].c_str() + 1);
		if (idx >= 1 && idx <= static_cast<int>(strings.size()))
			proto.kstr[i] = EscapeLua(strings[static_cast<size_t>(idx - 1)]);
		else
			proto.kstr[i] = "\"?\"";
	}
}

std::string LiftProto(const ProtoView& proto, int enc_key, const std::vector<std::string>& /*strings*/)
{
	std::ostringstream ss;
	const auto& code = proto.code;
	if (code.size() > k_max_lift_insns)
		ss << "-- truncated: " << code.size() << " instructions, lifting first " << k_max_lift_insns << "\n";
	auto reg = [](int r) {
		char b[16];
		std::snprintf(b, sizeof(b), "v%d", r);
		return std::string(b);
	};

	const size_t last = std::min<size_t>(code.size(), k_max_lift_insns);
	for (size_t pc = 0; pc < last; ++pc)
	{
		const std::uint32_t insn = code[pc];
		const int op = DecodeOp(insn, enc_key);
		const int A = static_cast<int>((insn >> 8) & 0xFF);
		const int B = static_cast<int>((insn >> 16) & 0xFF);
		const int C = static_cast<int>((insn >> 24) & 0xFF);
		const int D = static_cast<int>(static_cast<std::int16_t>(insn >> 16));

		auto need_aux = [&](int o) {
			return o == LOP_GETIMPORT || o == LOP_GETGLOBAL || o == 8 || o == LOP_GETTABLEKS
				|| o == LOP_SETTABLEKS || o == 16;
		};

		std::uint32_t aux = 0;
		if (need_aux(op) && pc + 1 < code.size())
		{
			aux = code[pc + 1];
			++pc;
		}

		switch (op)
		{
		case LOP_PREPVARARGS:
			ss << "-- function prep varargs\n";
			break;
		case LOP_LOADNIL:
			ss << "local " << reg(A) << " = nil\n";
			break;
		case LOP_LOADB:
			ss << "local " << reg(A) << " = " << (B ? "true" : "false") << "\n";
			break;
		case LOP_LOADN:
			ss << "local " << reg(A) << " = " << D << "\n";
			break;
		case LOP_LOADK:
			if (D >= 0 && D < static_cast<int>(proto.kstr.size()))
				ss << "local " << reg(A) << " = " << proto.kstr[static_cast<size_t>(D)] << "\n";
			else
				ss << "local " << reg(A) << " = nil -- bad K\n";
			break;
		case LOP_MOVE:
			ss << "local " << reg(A) << " = " << reg(B) << "\n";
			break;
		case LOP_GETIMPORT:
		{
			const int count = static_cast<int>(aux >> 30);
			const int id0 = static_cast<int>((aux >> 20) & 1023);
			const int id1 = static_cast<int>((aux >> 10) & 1023);
			const int id2 = static_cast<int>(aux & 1023);
			std::string path;
			auto append_k = [&](int id) {
				if (id < 0 || id >= static_cast<int>(proto.kstr.size())) return;
				std::string s = proto.kstr[static_cast<size_t>(id)];
				if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
					s = s.substr(1, s.size() - 2);
				if (!path.empty()) path.push_back('.');
				path += s;
			};
			if (count >= 1) append_k(id0);
			if (count >= 2) append_k(id1);
			if (count >= 3) append_k(id2);
			if (path.empty()) path = "?";
			ss << "local " << reg(A) << " = " << path << "\n";
			break;
		}
		case LOP_GETTABLEKS:
		{
			const int kid = static_cast<int>(aux);
			std::string key = (kid >= 0 && kid < static_cast<int>(proto.kstr.size()))
				? proto.kstr[static_cast<size_t>(kid)] : "\"?\"";
			ss << "local " << reg(A) << " = " << reg(B) << "[" << key << "]\n";
			break;
		}
		case LOP_SETTABLEKS:
		{
			const int kid = static_cast<int>(aux);
			std::string key = (kid >= 0 && kid < static_cast<int>(proto.kstr.size()))
				? proto.kstr[static_cast<size_t>(kid)] : "\"?\"";
			ss << reg(B) << "[" << key << "] = " << reg(A) << "\n";
			break;
		}
		case LOP_GETTABLE:
			ss << "local " << reg(A) << " = " << reg(B) << "[" << reg(C) << "]\n";
			break;
		case LOP_NEWTABLE:
			ss << "local " << reg(A) << " = {}\n";
			break;
		case LOP_CALL:
		{
			ss << "local " << reg(A) << " = " << reg(A) << "(";
			const int nparams = B == 0 ? -1 : (B - 1);
			if (nparams < 0)
				ss << "...";
			else
			{
				for (int i = 1; i <= nparams; ++i)
				{
					if (i > 1) ss << ", ";
					ss << reg(A + i);
				}
			}
			ss << ")\n";
			break;
		}
		case LOP_RETURN:
		{
			ss << "return";
			const int n = B == 0 ? -1 : (B - 1);
			if (n < 0)
				ss << " ...";
			else if (n > 0)
			{
				ss << " ";
				for (int i = 0; i < n; ++i)
				{
					if (i) ss << ", ";
					ss << reg(A + i);
				}
			}
			ss << "\n";
			break;
		}
		case LOP_CLOSURE:
			ss << "local " << reg(A) << " = function() end -- proto " << D << "\n";
			break;
		default:
			ss << "-- op " << op << " A=" << A << " B=" << B << " C=" << C << "\n";
			break;
		}
	}
	return std::move(ss).str();
}

std::string DecompileLuau(const std::vector<std::uint8_t>& bc, const char* name)
{
	std::ostringstream ss;
	ss << "-- decompiled with Ivory\n";
	ss << "-- chunk: " << (name ? name : "script") << "\n";

	if (!LooksLikeLuau(bc))
	{
		ss << "-- not Luau bytecode (head: " << HexHead(bc) << ")\n";
		ss << "-- maybe signed/encrypted; need RSB1+zstd or version 3..11\n";
		return ss.str();
	}

	Reader r{ bc.data(), bc.data() + bc.size() };
	std::uint8_t version = 0;
	r.U8(version);
	ss << "-- luau version: " << static_cast<unsigned>(version) << "\n";

	if (version == 0)
	{
		ss << "--[[\n" << std::string(reinterpret_cast<const char*>(r.p), r.Left()) << "\n--]]\n";
		return ss.str();
	}

	std::uint8_t types_ver = 0;
	if (version >= 4)
	{
		if (!r.U8(types_ver))
			return ss.str() + "-- bad types version\n";
		ss << "-- types version: " << static_cast<unsigned>(types_ver) << "\n";
	}

	std::uint32_t string_count = 0;
	if (!r.VarInt(string_count) || string_count > 2'000'000 || string_count > r.Left())
	{
		ss << "-- failed string table\n";
		return ss.str();
	}

	std::vector<std::string> strings;
	strings.reserve(string_count);
	for (std::uint32_t i = 0; i < string_count; ++i)
	{
		std::string s;
		if (!r.String(s))
		{
			ss << "-- truncated strings at " << i << "\n";
			return ss.str();
		}
		strings.push_back(std::move(s));
	}

	if (types_ver == 3)
	{
		std::uint8_t idx = 0;
		if (!r.U8(idx)) return ss.str() + "-- bad userdata map\n";
		while (idx != 0)
		{
			std::uint32_t namei = 0;
			if (!r.VarInt(namei)) return ss.str() + "-- bad userdata name\n";
			if (!r.U8(idx)) return ss.str() + "-- bad userdata map\n";
		}
	}

	std::uint32_t proto_count = 0;
	if (!r.VarInt(proto_count) || proto_count == 0 || proto_count > 200000 || proto_count > r.Left())
	{
		ss << "-- bad proto count\n";
		return ss.str();
	}

	std::vector<ProtoView> protos(proto_count);
	for (std::uint32_t i = 0; i < proto_count; ++i)
	{
		if (!SkipProtoBody(r, version, &protos[i]))
		{
			ss << "-- failed proto " << i << "\n";
			// fall back to string dump
			ss << "\nlocal _K = {\n";
			const size_t shown = std::min<size_t>(strings.size(), k_max_dump_strings);
			for (size_t si = 0; si < shown; ++si)
				ss << "  [" << (si + 1) << "] = " << EscapeLua(strings[si]) << ",\n";
			ss << "}\n";
			if (shown < strings.size())
				ss << "-- truncated: " << (strings.size() - shown) << " more strings\n";
			ss << "return _K\n";
			return ss.str();
		}
		ResolveStringConstants(protos[i], strings);
	}

	std::uint32_t main_id = 0;
	if (!r.VarInt(main_id) || main_id >= proto_count)
	{
		ss << "-- bad main proto id\n";
		return ss.str();
	}

	auto key = RecoverEncodingKey(protos[main_id].code);
	const int enc = key ? *key : 203;
	ss << "-- encoding key: " << enc << (key ? "" : " (guess)") << "\n";
	ss << "-- protos: " << proto_count << "  main: " << main_id << "\n\n";

	ss << LiftProto(protos[main_id], enc, strings);

	for (std::uint32_t i = 0; i < proto_count; ++i)
	{
		if (i == main_id) continue;
		if (protos[i].code.size() > 80) continue;
		ss << "\n-- <proto " << i << ">\n";
		ss << LiftProto(protos[i], enc, strings);
	}

	return std::move(ss).str();
}

} // namespace

bool IsScriptClass(const std::string& cls)
{
	return cls == "LocalScript" || cls == "Script" || cls == "ModuleScript";
}

bool ReadRaw(std::uint64_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out)
{
	out.clear();
	if (script_addr == 0 || !IsScriptClass(class_name))
		return false;

	uintptr_t bc_offset = ByteCodeOffset(class_name, script_addr);
	const std::uint64_t bc_obj = memory->read<std::uint64_t>(script_addr + bc_offset);
	if (bc_obj < 0x10000 || bc_obj >= 0x000FFFFFFFFF0000ULL)
		return false;

	const std::uint64_t ptr = memory->read<std::uint64_t>(bc_obj + 0x10);
	const std::uint64_t size = memory->read<std::uint64_t>(bc_obj + 0x20);
	if (ptr < 0x10000 || ptr >= 0x000FFFFFFFFF0000ULL || size == 0 || size > (16u * 1024u * 1024u))
		return false;

	out.resize(static_cast<size_t>(size));
	ULONG bytes_read = 0;
	{
		std::lock_guard<std::mutex> lock(memory->m_handle_mtx);
		Luck_ReadVirtualMemory(memory->m_process_handle, reinterpret_cast<void*>(ptr), out.data(), static_cast<ULONG>(size), &bytes_read);
	}
	out.resize(bytes_read > 0 ? bytes_read : size);
	return true;
}

bool Normalize(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out, std::string* status)
{
	out.clear();
	if (raw.empty())
	{
		if (status) *status = "empty";
		return false;
	}

	if (LooksLikeLuau(raw))
	{
		out = raw;
		if (status) *status = "raw luau";
		return true;
	}

	if (TryRobloxSigned(raw.data(), raw.size(), out))
	{
		if (status) *status = "roblox-sign + RSB1+zstd";
		return true;
	}

	if (TryRsb1(raw.data(), raw.size(), out))
	{
		if (status) *status = "RSB1+zstd";
		return true;
	}

	for (size_t i = 1; i + 8 <= raw.size(); ++i)
	{
		if (raw[i] == 'R' && raw[i + 1] == 'S' && raw[i + 2] == 'B' && raw[i + 3] == '1')
		{
			if (TryRsb1(raw.data() + i, raw.size() - i, out))
			{
				if (status) *status = "RSB1+zstd @+" + std::to_string(i);
				return true;
			}
		}
	}

	for (size_t i = 0; i + 4 <= raw.size(); ++i)
	{
		if (raw[i] == 0x28 && raw[i + 1] == 0xB5 && raw[i + 2] == 0x2F && raw[i + 3] == 0xFD)
		{
			if (TryZstdAt(raw.data() + i, raw.size() - i, out))
			{
				if (status) *status = "zstd frame @+" + std::to_string(i);
				return true;
			}
		}
	}

	if (raw.size() > 8)
	{
		std::uint32_t maybe = 0;
		std::memcpy(&maybe, raw.data(), 4);
		if (maybe > 16 && maybe < (16u << 20) && TryZstdAt(raw.data() + 4, raw.size() - 4, out))
		{
			if (status) *status = "u32+zstd";
			return true;
		}
		std::memcpy(&maybe, raw.data() + 4, 4);
		if (maybe > 16 && maybe < (16u << 20) && TryZstdAt(raw.data() + 8, raw.size() - 8, out))
		{
			if (status) *status = "skip4+u32+zstd";
			return true;
		}
	}

	if (status)
		*status = "unrecognized (head " + HexHead(raw) + ") — encrypted/signed?";
	out = raw;
	return false;
}

bool Decompress(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out)
{
	return Normalize(raw, out, nullptr);
}

bool ReadDecompressed(std::uint64_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out)
{
	std::vector<std::uint8_t> raw;
	if (!ReadRaw(script_addr, class_name, raw))
		return false;
	return Normalize(raw, out, nullptr);
}

std::string Decompile(const std::vector<std::uint8_t>& raw_or_luau, const char* chunk_name)
{
	std::string norm_status;
	std::vector<std::uint8_t> bc;
	const bool ok = Normalize(raw_or_luau, bc, &norm_status);

	std::string hdr = "-- normalize: " + norm_status + "\n";

	if (!ok || !LooksLikeLuau(bc))
	{
		hdr += DecompileLuau(raw_or_luau, chunk_name);
		hdr += "\n-- normalize did not produce Luau bytecode.\n";
		return hdr;
	}

	{
		std::string fission = FissionEmbed::DecompileLuauBytecode(bc.data(), bc.size());
		if (!fission.empty())
		{
			hdr += "-- via Fission (in-process)\n\n";
			fission.insert(0, hdr);
			return fission;
		}
	}

	hdr += DecompileLuau(bc, chunk_name);
	hdr += "\n-- (built-in lifter; Fission failed)\n";
	return hdr;
}

std::string Disassemble(const std::vector<std::uint8_t>& raw_or_luau, const char* chunk_name)
{
	std::vector<std::uint8_t> bc;
	std::string norm_status;
	const bool ok = Normalize(raw_or_luau, bc, &norm_status);
	const auto& target_bc = (ok && LooksLikeLuau(bc)) ? bc : raw_or_luau;

	std::ostringstream ss;
	ss << "-- Disassembly: " << (chunk_name ? chunk_name : "script") << " (Luau Bytecode)\n";
	ss << "-- Bytecode size: " << target_bc.size() << " bytes\n";
	ss << "-- Status: " << norm_status << "\n\n";

	if (target_bc.empty())
	{
		ss << "-- [Empty Bytecode] --\n";
		return ss.str();
	}

	Reader r{ target_bc.data(), target_bc.data() + target_bc.size() };
	std::uint8_t version = 0;
	if (!r.U8(version))
	{
		ss << "-- [Failed to read version byte] --\n";
		return ss.str();
	}
	ss << "-- Bytecode format version: " << (int)version << "\n\n";

	if (version >= 4)
	{
		std::uint8_t typesversion = 0;
		if (!r.U8(typesversion))
		{
			ss << "-- [Failed to read typesversion] --\n";
			return ss.str();
		}
	}

	std::uint32_t string_count = 0;
	if (!r.VarInt(string_count) || string_count > 20000)
	{
		ss << "-- [Invalid string table size] --\n";
		return ss.str();
	}
	std::vector<std::string> strings(string_count);
	for (std::uint32_t i = 0; i < string_count; ++i)
	{
		if (!r.String(strings[i])) break;
	}

	std::uint32_t proto_count = 0;
	if (!r.VarInt(proto_count) || proto_count > 10000)
	{
		ss << "-- [Invalid proto table size] --\n";
		return ss.str();
	}
	std::vector<ProtoView> protos(proto_count);
	for (std::uint32_t i = 0; i < proto_count; ++i)
	{
		if (!SkipProtoBody(r, version, &protos[i])) break;
	}

	std::uint32_t main_id = 0;
	if (!r.VarInt(main_id) || main_id >= proto_count)
	{
		main_id = 0;
	}

	auto key = RecoverEncodingKey(protos.empty() ? std::vector<std::uint32_t>() : protos[main_id].code);
	const int enc = key ? *key : 203;

	for (std::uint32_t p = 0; p < proto_count; ++p)
	{
		ss << "; ========================================\n";
		ss << "; Proto #" << p << (p == main_id ? " [Main Entry]" : "") << " (Instructions: " << protos[p].code.size() << ")\n";
		ss << "; ========================================\n";
		const auto& code = protos[p].code;
		for (size_t pc = 0; pc < code.size(); ++pc)
		{
			const std::uint32_t insn = code[pc];
			const int op = DecodeOp(insn, enc);
			const int a = static_cast<int>((insn >> 8) & 0xFF);
			const int b = static_cast<int>((insn >> 16) & 0xFF);
			const int c = static_cast<int>((insn >> 24) & 0xFF);
			char line[128];
			snprintf(line, sizeof(line), "  %04zu:  OP_%-4d  r%-3d, r%-3d, r%-3d    ; raw: 0x%08X\n",
				pc, op, a, b, c, insn);
			ss << line;
		}
		ss << "\n";
	}

	return ss.str();
}

} 
}
}