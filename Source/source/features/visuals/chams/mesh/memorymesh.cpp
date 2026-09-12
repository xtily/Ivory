#include "memorymesh.h"
#include "assetmesh.h"
#include "avatarmesh.h"
#include "../gpu/meshgpu.h"
#include <core/memory/memory.h>
#include <core/globals.h>
#include <core/logger/logger.h>
#include <sdk/game/game.h>
#include <sdk/offsets/offsets.h>
#include <sdk/sdk.h>

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace memorymesh {
namespace {

struct LiveMeshRecord { std::string url; assetmesh::parsed_mesh parsed; };
struct PartMeshBinding {
    std::shared_ptr<LiveMeshRecord> record;
    uint64_t gpu_cache_key = 0;
    DWORD last_refresh_tick = 0;
};
struct CapturedRecord {
    std::shared_ptr<LiveMeshRecord> record;
    std::vector<std::string> keys;
};

static constexpr uint64_t kVertexStride = 40;
static constexpr uint64_t kFaceStride = 12;
static constexpr uint32_t kVertexBatch = 4096;
static constexpr uint32_t kFaceBatch = 8192;
static constexpr DWORD kScanIntervalMs = 750;
static constexpr DWORD kBindingRefreshMs = 1500;

static std::unordered_map<std::string, std::shared_ptr<LiveMeshRecord>> g_lookup;
static std::unordered_set<std::string> g_cached_urls;
static std::unordered_map<uintptr_t, PartMeshBinding> g_part_binding_cache;
static std::mutex g_cache_mutex;
static std::atomic<bool> g_scan_running = false;
static HANDLE g_scan_thread = nullptr;
static uint64_t g_next_gpu_cache_key = 1;
static std::shared_ptr<LiveMeshRecord> try_parse_fmd_memory(uint64_t fmd, const std::string& custom_key = {});

static bool read_safe(uint64_t addr, void* out, size_t size) {
    if (addr < 0x10000 || addr >= 0x7FFFFFFFFFFFull) return false;
    return memory->read_raw(addr, out, size);
}

template <typename T>
static bool read_t(uint64_t addr, T& out) {
    return read_safe(addr, &out, sizeof(out));
}

static std::string trim_copy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string lower_copy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back((char)std::tolower((unsigned char)c));
    return out;
}

static uint64_t extract_asset_id(const std::string& text) {
    const std::string lower = lower_copy(text);
    const std::array<const char*, 4> markers = { "rbxassetid://", "rbxassetid:", "rbxasset://", "?id=" };
    for (const char* marker : markers) {
        const size_t pos = lower.find(marker);
        if (pos == std::string::npos) continue;
        const size_t start = pos + std::strlen(marker);
        size_t end = start;
        while (end < lower.size() && lower[end] >= '0' && lower[end] <= '9') ++end;
        if (end == start) continue;
        try { return std::stoull(lower.substr(start, end - start)); }
        catch (...) { return 0; }
    }
    return 0;
}

static std::vector<std::string> build_keys(const std::string& raw) {
    std::vector<std::string> keys;
    const std::string trimmed = trim_copy(raw);
    if (trimmed.empty()) return keys;
    std::string lk = lower_copy(trimmed);
    keys.push_back(lk);

    if (lk.find("head.mesh") != std::string::npos || lk == "head") {
        keys.push_back("rbxasset://fonts/head.mesh");
        keys.push_back("fonts/head.mesh");
        keys.push_back("head.mesh");
        keys.push_back("rbxasset://avatar/heads/head.mesh");
        keys.push_back("avatar/heads/head.mesh");
        keys.push_back("head");
    }
    if (lk.find("torso.mesh") != std::string::npos || lk == "torso") {
        keys.push_back("rbxasset://avatar/meshes/torso.mesh");
        keys.push_back("avatar/meshes/torso.mesh");
        keys.push_back("torso.mesh");
        keys.push_back("torso");
    }
    if (lk.find("leftarm.mesh") != std::string::npos || lk == "leftarm" || lk == "left arm") {
        keys.push_back("rbxasset://avatar/meshes/leftarm.mesh");
        keys.push_back("avatar/meshes/leftarm.mesh");
        keys.push_back("leftarm.mesh");
        keys.push_back("leftarm");
        keys.push_back("left arm");
    }
    if (lk.find("rightarm.mesh") != std::string::npos || lk == "rightarm" || lk == "right arm") {
        keys.push_back("rbxasset://avatar/meshes/rightarm.mesh");
        keys.push_back("avatar/meshes/rightarm.mesh");
        keys.push_back("rightarm.mesh");
        keys.push_back("rightarm");
        keys.push_back("right arm");
    }
    if (lk.find("leftleg.mesh") != std::string::npos || lk == "leftleg" || lk == "left leg") {
        keys.push_back("rbxasset://avatar/meshes/leftleg.mesh");
        keys.push_back("avatar/meshes/leftleg.mesh");
        keys.push_back("leftleg.mesh");
        keys.push_back("leftleg");
        keys.push_back("left leg");
    }
    if (lk.find("rightleg.mesh") != std::string::npos || lk == "rightleg" || lk == "right leg") {
        keys.push_back("rbxasset://avatar/meshes/rightleg.mesh");
        keys.push_back("avatar/meshes/rightleg.mesh");
        keys.push_back("rightleg.mesh");
        keys.push_back("rightleg");
        keys.push_back("right leg");
    }

    const uint64_t asset_id = extract_asset_id(trimmed);
    if (asset_id != 0) {
        const std::string id_str = std::to_string(asset_id);
        keys.push_back("assetid:" + id_str);
        keys.push_back(id_str);
        keys.push_back("rbxassetid://" + id_str);
        keys.push_back("https://assetdelivery.roblox.com/v1/asset?id=" + id_str);
        keys.push_back("http://www.roblox.com/asset/?id=" + id_str);
    }
    return keys;
}

static const char* builtin_asset_alias(uint64_t asset_id) {
    switch (asset_id) {
    case 1365230:
    case 6340101:
    case 1251392:
    case 27111442:
        return "rbxasset://fonts/head.mesh";
    case 1365219: return "rbxasset://avatar/meshes/torso.mesh";
    case 1365224: return "rbxasset://avatar/meshes/leftarm.mesh";
    case 1365223: return "rbxasset://avatar/meshes/rightarm.mesh";
    case 1365226: return "rbxasset://avatar/meshes/leftleg.mesh";
    case 1365225: return "rbxasset://avatar/meshes/rightleg.mesh";
    default: return nullptr;
    }
}

static std::string read_std_string(uint64_t base) {
    if (base < 0x10000 || base >= 0x7FFFFFFFFFFFull) return "";
    uint64_t size = 0, capacity = 0;
    if (!read_t(base + 0x10, size)) return "";
    read_t(base + 0x18, capacity);
    if (size == 0 || size > 2048) return "";
    uint64_t src = base;
    if (capacity >= 16 || size >= 16) {
        if (!read_t(base, src) || src < 0x10000 || src >= 0x7FFFFFFFFFFFull) return "";
    }
    const size_t len = (size_t)(std::min<uint64_t>)(size, 1023);
    if (len == 0) return "";
    char buf[1024] = {};
    if (!read_safe(src, buf, len)) return "";
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)buf[i];
        if (c == 0) break;
        if (c >= 32 && c < 127) out.push_back((char)c);
    }
    return out;
}

static void compute_bounds(assetmesh::parsed_mesh& mesh) {
    if (mesh.vertices.empty()) {
        mesh.bounds.valid = false;
        return;
    }
    mesh.bounds.min = { mesh.vertices[0].position.x, mesh.vertices[0].position.y, mesh.vertices[0].position.z };
    mesh.bounds.max = mesh.bounds.min;
    for (const auto& v : mesh.vertices) {
        mesh.bounds.min.x = (std::min)(mesh.bounds.min.x, v.position.x);
        mesh.bounds.min.y = (std::min)(mesh.bounds.min.y, v.position.y);
        mesh.bounds.min.z = (std::min)(mesh.bounds.min.z, v.position.z);
        mesh.bounds.max.x = (std::max)(mesh.bounds.max.x, v.position.x);
        mesh.bounds.max.y = (std::max)(mesh.bounds.max.y, v.position.y);
        mesh.bounds.max.z = (std::max)(mesh.bounds.max.z, v.position.z);
    }
    mesh.bounds.center = {
        (mesh.bounds.min.x + mesh.bounds.max.x) * 0.5f,
        (mesh.bounds.min.y + mesh.bounds.max.y) * 0.5f,
        (mesh.bounds.min.z + mesh.bounds.max.z) * 0.5f
    };
    mesh.bounds.size = {
        (std::max)(0.001f, mesh.bounds.max.x - mesh.bounds.min.x),
        (std::max)(0.001f, mesh.bounds.max.y - mesh.bounds.min.y),
        (std::max)(0.001f, mesh.bounds.max.z - mesh.bounds.min.z)
    };
    mesh.bounds.valid = true;
}

static bool read_vertices(uint64_t first, uint32_t count, uint32_t vstride, std::vector<assetmesh::mesh_vertex>& out) {
    out.clear();
    out.reserve((size_t)count);
    uint64_t addr = first;
    uint32_t left = count;
    while (left > 0) {
        const uint32_t batch = (std::min)(kVertexBatch, left);
        std::vector<unsigned char> buf((size_t)(batch * vstride));
        if (!read_safe(addr, buf.data(), buf.size())) return false;
        for (uint32_t j = 0; j < batch; ++j) {
            const size_t b = (size_t)(j * vstride);
            assetmesh::mesh_vertex v{};
            std::memcpy(&v.position.x, buf.data() + b, sizeof(float));
            std::memcpy(&v.position.y, buf.data() + b + 4, sizeof(float));
            std::memcpy(&v.position.z, buf.data() + b + 8, sizeof(float));
            if (vstride >= 24) {
                std::memcpy(&v.normal.x, buf.data() + b + 12, sizeof(float));
                std::memcpy(&v.normal.y, buf.data() + b + 16, sizeof(float));
                std::memcpy(&v.normal.z, buf.data() + b + 20, sizeof(float));
            } else {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (vstride >= 32) {
                std::memcpy(&v.uv.x, buf.data() + b + 24, sizeof(float));
                std::memcpy(&v.uv.y, buf.data() + b + 28, sizeof(float));
            }
            float len_sq = v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z;
            if (len_sq < 1e-4f || !std::isfinite(len_sq)) {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }
            out.push_back(v);
        }
        left -= batch;
        addr += batch * vstride;
    }
    return true;
}

static bool read_faces(uint64_t first, uint32_t count, std::vector<uint32_t>& out) {
    out.clear();
    out.reserve((size_t)count * 3);
    uint64_t addr = first;
    uint32_t left = count;
    while (left > 0) {
        const uint32_t batch = (std::min)(kFaceBatch, left);
        std::vector<unsigned char> buf((size_t)(batch * kFaceStride));
        if (!read_safe(addr, buf.data(), buf.size())) return false;
        for (uint32_t j = 0; j < batch; ++j) {
            const size_t b = (size_t)(j * kFaceStride);
            uint32_t a = 0, c = 0, d = 0;
            std::memcpy(&a, buf.data() + b, sizeof(uint32_t));
            std::memcpy(&c, buf.data() + b + 4, sizeof(uint32_t));
            std::memcpy(&d, buf.data() + b + 8, sizeof(uint32_t));
            out.push_back(a);
            out.push_back(c);
            out.push_back(d);
        }
        left -= batch;
        addr += batch * kFaceStride;
    }
    return true;
}

static uint32_t read_faces_max_index(uint64_t first, uint32_t count) {
    uint64_t addr = first;
    uint32_t left = count;
    uint32_t max_idx = 0;
    while (left > 0) {
        const uint32_t batch = (std::min)(kFaceBatch, left);
        std::vector<unsigned char> buf((size_t)(batch * kFaceStride));
        if (!read_safe(addr, buf.data(), buf.size())) return 0;
        for (uint32_t j = 0; j < batch; ++j) {
            const size_t b = (size_t)(j * kFaceStride);
            for (int k = 0; k < 3; ++k) {
                uint32_t idx = 0;
                std::memcpy(&idx, buf.data() + b + (k * 4), sizeof(uint32_t));
                if (idx > max_idx) max_idx = idx;
            }
        }
        left -= batch;
        addr += batch * kFaceStride;
    }
    return max_idx;
}

static uint32_t get_lod0_face_count(uint64_t fmd, uint32_t total) {
    for (uint64_t off = 0xA8; off <= 0xD0; off += 0x08) {
        uint64_t lf = 0, ll = 0;
        if (!read_t(fmd + off, lf) || !read_t(fmd + off + 0x08, ll) || ll <= lf) continue;
        const uint64_t lc = (ll - lf) / 4;
        if (lc < 2 || lc > 20) continue;
        uint32_t faces = 0;
        if (read_t(lf + 4, faces) && faces > 0 && faces <= total) return faces;
    }
    return total;
}

static std::vector<uint64_t> get_nodes(uint64_t cache_ptr) {
    std::vector<uint64_t> nodes;
    uint64_t sentinel = 0, node = 0;
    if (!read_t(cache_ptr + 0x08, sentinel) || sentinel < 0x10000 || sentinel >= 0x7FFFFFFFFFFFull) return nodes;
    if (!read_t(sentinel, node) || node < 0x10000 || node >= 0x7FFFFFFFFFFFull || node == sentinel) {
        if (!read_t(sentinel + 0x08, node) || node < 0x10000 || node >= 0x7FFFFFFFFFFFull) return nodes;
    }
    int walked = 0;
    while (node >= 0x10000 && node < 0x7FFFFFFFFFFFull && node != sentinel && walked < 10000) {
        nodes.push_back(node);
        ++walked;
        uint64_t next = 0;
        if (!read_t(node, next) || next < 0x10000 || next >= 0x7FFFFFFFFFFFull || next == node) break;
        node = next;
    }
    return nodes;
}

static bool looks_like_cache_group(uint64_t base_ptr, uint64_t& evict, uint64_t& pinned) {
    uint64_t first = 0;
    if (!read_t(base_ptr, first)) return false;
    if ((first & 0xffffffffull) != 0x02000000ull || (first >> 32) != 0) return false;

    for (uint64_t off = 0x10; off <= 0x40; off += 0x08) {
        uint64_t p1 = 0, p2 = 0;
        if (read_t(base_ptr + off, p1) && p1 >= 0x10000 && p1 < 0x7FFFFFFFFFFFull) {
            if (read_t(base_ptr + off + 0x08, p2) && p2 >= 0x10000 && p2 < 0x7FFFFFFFFFFFull) {
                uint64_t s1 = 0, s2 = 0;
                if (read_t(p1 + 0x08, s1) && s1 >= 0x10000 && s1 < 0x7FFFFFFFFFFFull) {
                    if (read_t(p2 + 0x08, s2) && s2 >= 0x10000 && s2 < 0x7FFFFFFFFFFFull) {
                        evict = p1;
                        pinned = p2;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static uint64_t g_cached_svc_addr = 0;
static uint64_t g_cached_evict = 0;
static uint64_t g_cached_pinned = 0;

static bool find_cache(uint64_t& evict, uint64_t& pinned) {
    evict = 0;
    pinned = 0;
    uintptr_t dm_addr = (game::datamodel && game::datamodel->address != 0) ? game::datamodel->address : 0;
    if (dm_addr == 0) {
        auto dm_obj = rbx::c_datamodel::get();
        if (dm_obj) dm_addr = dm_obj->address;
    }
    if (dm_addr == 0) return false;
    rbx::c_instance dm{ dm_addr };
    uint64_t svc_addr = dm.find_first_child_by_class("MeshContentProvider");
    if (svc_addr == 0) svc_addr = dm.find_first_child("MeshContentProvider");
    if (svc_addr == 0) return false;

    if (g_cached_svc_addr == svc_addr && g_cached_evict != 0 && g_cached_pinned != 0) {
        if (looks_like_cache_group(g_cached_evict, evict, pinned)) {
            evict = g_cached_evict;
            pinned = g_cached_pinned;
            return true;
        }
    }

    uint64_t s_p1 = 0, s_p0 = 0;
    read_t(svc_addr + 0x08, s_p1);
    read_t(svc_addr, s_p0);
    const std::array<uint64_t, 3> scan_bases = { s_p1, s_p0, svc_addr };
    for (uint64_t scan_base : scan_bases) {
        if (scan_base < 0x10000 || scan_base >= 0x7FFFFFFFFFFFull) continue;
        for (uint64_t off = 0; off <= 0x3000; off += 0x08) {
            uint64_t candidate = 0;
            if (!read_t(scan_base + off, candidate) || candidate < 0x10000 || candidate >= 0x7FFFFFFFFFFFull) continue;
            uint64_t ce = 0, cp = 0;
            if (!looks_like_cache_group(candidate, ce, cp)) continue;
            if (get_nodes(ce).empty() && get_nodes(cp).empty()) continue;
            evict = ce;
            pinned = cp;
            g_cached_svc_addr = svc_addr;
            g_cached_evict = ce;
            g_cached_pinned = cp;
            return true;
        }
    }
    return false;
}

static bool capture_node(uint64_t node, std::unordered_set<std::string>& known_urls, std::vector<CapturedRecord>& out_records) {
    const std::string url = read_std_string(node + 0x10);
    if (url.empty() || known_urls.find(url) != known_urls.end()) return false;

    std::shared_ptr<LiveMeshRecord> record = nullptr;
    for (uint64_t ci_off = 0x10; ci_off <= 0x80; ci_off += 0x08) {
        uint64_t ci = 0;
        if (!read_t(node + ci_off, ci) || ci < 0x10000 || ci >= 0x7FFFFFFFFFFFull) continue;
        for (uint64_t fmd_off = 0; fmd_off <= 0x80; fmd_off += 0x08) {
            uint64_t fmd = 0;
            if (!read_t(ci + fmd_off, fmd) || fmd < 0x10000 || fmd >= 0x7FFFFFFFFFFFull) continue;
            record = try_parse_fmd_memory(fmd, url);
            if (record) break;
        }
        if (record) break;
    }

    if (!record) return false;

    std::vector<std::string> keys = build_keys(url);
    if (keys.empty()) return false;

    known_urls.insert(url);
    out_records.push_back({ record, std::move(keys) });
    return true;
}

static void perform_scan_once() {
    uint64_t evict = 0, pinned = 0;
    if (!find_cache(evict, pinned)) {
        return;
    }

    std::unordered_set<std::string> known_urls;
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        known_urls = g_cached_urls;
    }

    std::vector<CapturedRecord> captured;
    const std::vector<uint64_t> evict_nodes = get_nodes(evict);
    const std::vector<uint64_t> pinned_nodes = get_nodes(pinned);
    captured.reserve(evict_nodes.size() + pinned_nodes.size());
    for (uint64_t node : evict_nodes) capture_node(node, known_urls, captured);
    for (uint64_t node : pinned_nodes) capture_node(node, known_urls, captured);

    if (captured.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    for (auto& entry : captured) {
        g_cached_urls.insert(entry.record->url);
        for (const std::string& key : entry.keys) {
            g_lookup[key] = entry.record;
        }
    }
}

static DWORD WINAPI scan_thread_main(LPVOID) {
    while (g_scan_running.load()) {
        perform_scan_once();
        Sleep(500);
    }
    return 0;
}

static std::shared_ptr<LiveMeshRecord> try_parse_fmd_memory(uint64_t fmd, const std::string& custom_key) {
    if (fmd < 0x10000 || fmd >= 0x7FFFFFFFFFFFull) return nullptr;

    uint64_t v_first = 0, v_last = 0;
    uint32_t detected_vstride = 0;
    uint32_t total_verts = 0;
    uint64_t found_v_off = 0xFFFFFFFFull;

    {
        uint64_t vstart = 0, vend = 0;
        if (read_t(fmd + 0x00, vstart) && read_t(fmd + 0x08, vend) &&
            vstart >= 0x10000 && vstart < 0x7FFFFFFFFFFFull && vend > vstart) {
            const uint64_t v_diff = vend - vstart;
            static constexpr uint32_t kStrides[] = { 40, 32, 48, 56, 64, 0 };
            for (int si = 0; kStrides[si] != 0; ++si) {
                const uint32_t s = kStrides[si];
                if ((v_diff % s) == 0 && (v_diff / s) >= 24 && (v_diff / s) <= 500000) {
                    detected_vstride = s;
                    total_verts = (uint32_t)(v_diff / s);
                    v_first = vstart;
                    v_last = vend;
                    found_v_off = 0x00;
                    break;
                }
            }
        }
    }

    if (detected_vstride == 0) {
        static constexpr uint32_t kPreferredStrides[] = { 40, 32, 48, 56, 64, 72, 80, 88, 96, 24, 20, 16, 0 };
        for (uint64_t v_off = 0; v_off <= 0xA0; v_off += 0x08) {
            uint64_t vf2 = 0, vl2 = 0;
            if (read_t(fmd + v_off, vf2) && read_t(fmd + v_off + 0x08, vl2)) {
                if (vf2 >= 0x10000 && vf2 < 0x7FFFFFFFFFFFull && vl2 > vf2) {
                    const uint64_t v_diff = vl2 - vf2;
                    for (int si = 0; kPreferredStrides[si] != 0; ++si) {
                        const uint32_t s = kPreferredStrides[si];
                        if ((v_diff % s) == 0 && (v_diff / s) >= 24 && (v_diff / s) <= 500000) {
                            detected_vstride = s;
                            total_verts = (uint32_t)(v_diff / s);
                            v_first = vf2;
                            v_last = vl2;
                            found_v_off = v_off;
                            break;
                        }
                    }
                    if (detected_vstride != 0) break;
                }
            }
        }
    }

    if (detected_vstride == 0 || total_verts < 24) return nullptr;

    uint64_t f_first = 0, f_last = 0;
    {
        uint64_t fstart = 0, fend = 0;
        if (read_t(fmd + 0x30, fstart) && read_t(fmd + 0x38, fend) &&
            fstart >= 0x10000 && fstart < 0x7FFFFFFFFFFFull && fend > fstart && fstart != v_first) {
            const uint64_t f_diff = fend - fstart;
            if ((f_diff % kFaceStride) == 0) {
                uint32_t faces = (uint32_t)(f_diff / kFaceStride);
                if (faces >= 8 && faces <= 500000) {
                    f_first = fstart;
                    f_last = fend;
                }
            }
        }
    }
    if (f_first == 0) {
        for (uint64_t f_off = 0; f_off <= 0xC0; f_off += 0x08) {
            if (f_off == found_v_off || f_off == 0x30) continue; 
            uint64_t ff = 0, fl = 0;
            if (read_t(fmd + f_off, ff) && read_t(fmd + f_off + 0x08, fl)) {
                if (ff >= 0x10000 && ff < 0x7FFFFFFFFFFFull && fl > ff && ff != v_first) {
                    const uint64_t f_diff = fl - ff;
                    if ((f_diff % kFaceStride) == 0) {
                        uint32_t faces = (uint32_t)(f_diff / kFaceStride);
                        if (faces >= 8 && faces <= 500000) {
                            f_first = ff;
                            f_last = fl;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (f_first == 0 || f_last <= f_first) return nullptr;

    const uint32_t total_faces = (uint32_t)((f_last - f_first) / kFaceStride);
    if (total_faces < 8 || total_faces > 500000) return nullptr;

    auto record = std::make_shared<LiveMeshRecord>();
    record->url = custom_key.empty() ? ("memory_fmd_" + std::to_string(fmd)) : custom_key;
    if (!read_vertices(v_first, total_verts, detected_vstride, record->parsed.vertices)) return nullptr;
    if (!read_faces(f_first, total_faces, record->parsed.indices)) return nullptr;

    {
        constexpr float kMaxCoord = 20.0f;
        uint32_t bad = 0;
        for (const auto& v : record->parsed.vertices) {
            if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) ||
                std::fabs(v.position.x) > kMaxCoord || std::fabs(v.position.y) > kMaxCoord || std::fabs(v.position.z) > kMaxCoord) {
                ++bad;
            }
        }
        if (bad > 0 && bad > record->parsed.vertices.size() / 50) return nullptr;
    }

    {
        const uint32_t max_idx = static_cast<uint32_t>(record->parsed.vertices.size());
        for (uint32_t idx : record->parsed.indices) {
            if (idx >= max_idx) return nullptr;
        }
    }

    compute_bounds(record->parsed);
    if (!record->parsed.bounds.valid) return nullptr;

    {
        const auto& b = record->parsed.bounds;
        if (b.size.x > 50.0f || b.size.y > 50.0f || b.size.z > 50.0f) return nullptr;
    }

    return record;
}

static std::shared_ptr<LiveMeshRecord> scan_part_memory_for_mesh(uint64_t part_address) {
    if (part_address < 0x10000 || part_address >= 0x7FFFFFFFFFFFull) return nullptr;

    std::vector<uint64_t> targets = { part_address };
    rbx::c_instance part_inst{ part_address };
    for (auto child : part_inst.get_children<rbx::c_instance>()) {
        if (child.is_valid()) targets.push_back(child.address);
    }

    for (uint64_t tgt : targets) {
        for (uint64_t off = 0x20; off <= 0x280; off += 0x08) {
            uint64_t ptr1 = 0;
            if (!read_t(tgt + off, ptr1) || ptr1 < 0x10000 || ptr1 >= 0x7FFFFFFFFFFFull) continue;

            auto rec = try_parse_fmd_memory(ptr1);
            if (rec) return rec;

            for (uint64_t sub_off : { 0x28ULL, 0x20ULL, 0x30ULL, 0x38ULL, 0x10ULL }) {
                uint64_t ptr2 = 0;
                if (read_t(ptr1 + sub_off, ptr2) && ptr2 >= 0x10000 && ptr2 < 0x7FFFFFFFFFFFull) {
                    auto sub_rec = try_parse_fmd_memory(ptr2);
                    if (sub_rec) return sub_rec;
                }
            }
        }
    }
    return nullptr;
}

}

struct CachedPartEntry {
    meshgpu::ResolvedMeshDraw resolved{};
    DWORD last_check = 0;
};
static std::unordered_map<uintptr_t, CachedPartEntry> s_fast_part_cache;

meshgpu::ResolvedMeshDraw resolve_live_mesh_gpu(const cache::entity_t& entity, const std::string& part_name, uintptr_t part_address) {
    if (!part_address) {
        return {};
    }

    const DWORD now = GetTickCount();

    auto fast_it = s_fast_part_cache.find(part_address);
    if (fast_it != s_fast_part_cache.end()) {
        if (fast_it->second.resolved.mesh != nullptr &&
            fast_it->second.resolved.cache_key != 9999991ull &&
            fast_it->second.resolved.cache_key != 9999992ull) {
            return fast_it->second.resolved;
        }
    }

    if (s_fast_part_cache.size() > 4000) {
        s_fast_part_cache.clear();
    }

    static const std::unordered_set<std::string> standard_limbs = {
        "Head", "Torso", "UpperTorso", "LowerTorso", "LeftUpperArm", "LeftLowerArm", "LeftHand",
        "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg",
        "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Left Arm", "Right Arm",
        "Left Leg", "Right Leg"
    };

    bool is_standard = (standard_limbs.find(part_name) != standard_limbs.end()) || (part_name.rfind("pfLimb", 0) == 0);
    bool is_r15 = (part_name.find("Upper") != std::string::npos ||
                   part_name.find("Lower") != std::string::npos ||
                   part_name.find("Hand") != std::string::npos ||
                   part_name.find("Foot") != std::string::npos ||
                   entity.parts.count("UpperTorso") > 0 ||
                   entity.parts.count("LowerTorso") > 0);

    if (is_standard) {
        // 1. Check if the part has a custom CharacterMesh / SpecialMesh attached (e.g. Korblox Leg)
        std::string mesh_id = assetmesh::get_mesh_id_string_from_part(part_address);
        if (!mesh_id.empty() && mesh_id != "str_error" && mesh_id != "rbxassetid://1148197775") {
            std::vector<std::string> custom_keys = build_keys(mesh_id);
            std::lock_guard<std::mutex> lock(g_cache_mutex);
            for (const auto& k : custom_keys) {
                auto it = g_lookup.find(k);
                if (it != g_lookup.end()) {
                    meshgpu::ResolvedMeshDraw res{};
                    res.cache_key = reinterpret_cast<uint64_t>(it->second.get());
                    if (res.cache_key == 0) res.cache_key = g_next_gpu_cache_key++;
                    res.mesh = &it->second->parsed;
                    s_fast_part_cache[part_address] = { res, now };
                    return res;
                }
            }
        }

        // 2. Standard block limb fallback (head.mesh, torso.mesh, leftleg.mesh, etc.)
        std::vector<std::string> limb_keys = build_keys(part_name);
        {
            std::lock_guard<std::mutex> lock(g_cache_mutex);
            for (const auto& k : limb_keys) {
                auto it = g_lookup.find(k);
                if (it != g_lookup.end()) {
                    meshgpu::ResolvedMeshDraw res{};
                    res.cache_key = reinterpret_cast<uint64_t>(it->second.get());
                    if (res.cache_key == 0) res.cache_key = g_next_gpu_cache_key++;
                    res.mesh = &it->second->parsed;
                    s_fast_part_cache[part_address] = { res, now };
                    return res;
                }
            }
        }

        auto fallback = assetmesh::get_mesh_for_part(part_name.c_str(), is_r15, 0);
        if (fallback) {
            meshgpu::ResolvedMeshDraw res{};
            res.cache_key = (part_name == "Head") ? 9999991ull : (9999992ull + (is_r15 ? 100ull : 0ull));
            res.mesh = fallback.get();
            s_fast_part_cache[part_address] = { res, now };
            return res;
        }
    }

    std::string mesh_id = assetmesh::get_mesh_id_string_from_part(part_address);
    if (!mesh_id.empty() && mesh_id != "str_error") {
        std::vector<std::string> acc_keys = build_keys(mesh_id);
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        for (const auto& k : acc_keys) {
            auto it = g_lookup.find(k);
            if (it != g_lookup.end()) {
                meshgpu::ResolvedMeshDraw res{};
                res.cache_key = reinterpret_cast<uint64_t>(it->second.get());
                if (res.cache_key == 0) res.cache_key = g_next_gpu_cache_key++;
                res.mesh = &it->second->parsed;
                s_fast_part_cache[part_address] = { res, now };
                return res;
            }
        }
    }

    return {};
}

void start() {
    bool expected = false;
    if (!g_scan_running.compare_exchange_strong(expected, true)) {
        return;
    }
    g_scan_thread = CreateThread(nullptr, 0, &scan_thread_main, nullptr, 0, nullptr);
    if (!g_scan_thread) {
        g_scan_running.store(false);
    }
}

void render(const std::vector<cache::entity_t>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_col[4], const float outline_col[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, float viewport_offset_x, float viewport_offset_y) {
    if (!device || !context || entities.empty()) return;

    meshgpu::render_advanced(entities, view, camera_pos, viewport_width, viewport_height, device, context, fill_col, outline_col, shader_type, enable_outline, outline_mode, outline_style, outline_thickness, outline_only, enable_accessories, &resolve_live_mesh_gpu, viewport_offset_x, viewport_offset_y);
}

void render_ptrs(const std::vector<const cache::entity_t*>& entities, const float view[16], const float camera_pos[3], float viewport_width, float viewport_height, ID3D11Device* device, ID3D11DeviceContext* context, const float fill_col[4], const float outline_col[4], int shader_type, bool enable_outline, int outline_mode, int outline_style, float outline_thickness, bool outline_only, bool enable_accessories, float viewport_offset_x, float viewport_offset_y) {
    if (!device || !context || entities.empty()) return;

    meshgpu::render_advanced_ptrs(entities, view, camera_pos, viewport_width, viewport_height, device, context, fill_col, outline_col, shader_type, enable_outline, outline_mode, outline_style, outline_thickness, outline_only, enable_accessories, &resolve_live_mesh_gpu, viewport_offset_x, viewport_offset_y);
}

void shutdown() {
    g_scan_running.store(false);
    if (g_scan_thread) {
        WaitForSingleObject(g_scan_thread, INFINITE);
        CloseHandle(g_scan_thread);
        g_scan_thread = nullptr;
    }

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_lookup.clear();
    g_cached_urls.clear();
    g_part_binding_cache.clear();
    g_next_gpu_cache_key = 1;
}

}