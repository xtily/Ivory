#include "assetmesh.h"

#include <core/memory/memory.h>
#include <core/logger/logger.h>
#include <sdk/offsets/offsets.h>
#include <sdk/sdk.h>

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace assetmesh {

    static std::unordered_map<uint64_t, std::shared_ptr<parsed_mesh>> g_mesh_cache;
    static std::unordered_set<uint64_t> g_pending_assets;
    static std::unordered_map<uint64_t, ULONGLONG> g_retry_after;
    static std::queue<uint64_t> g_request_queue;
    static std::mutex g_state_mutex;
    static std::condition_variable g_queue_cv;
    static std::vector<HANDLE> g_workers;
    static std::atomic<bool> g_running{ false };
    static std::atomic<size_t> g_fetch_ok{ 0 };
    static std::atomic<size_t> g_fetch_fail{ 0 };
    static std::atomic<size_t> g_fail_api{ 0 };
    static std::atomic<size_t> g_fail_download{ 0 };
    static std::atomic<size_t> g_fail_parse{ 0 };
    static char g_last_error[128] = {};
    static std::atomic<int> g_last_http_status{ 0 };
    static std::atomic<uint64_t> g_last_failed_asset_id{ 0 };
    static std::atomic<ULONGLONG> g_last_request_tick{ 0 };
    static constexpr size_t THREAD_POOL_SIZE = 8;
    static constexpr DWORD REQUEST_DELAY_MS = 10;

    static std::unordered_map<int64_t, std::unordered_map<std::string, uint64_t>> g_avatar_asset_cache;
    static std::unordered_set<int64_t> g_avatar_pending;
    static std::unordered_map<int64_t, ULONGLONG> g_avatar_retry_after;
    static std::mutex g_avatar_mutex;

    static void set_error(const char* msg) {
        strncpy_s(g_last_error, msg, sizeof(g_last_error) - 1);
        g_last_error[sizeof(g_last_error) - 1] = '\0';
    }

    static float read_float_le(const uint8_t* data, size_t& offset) {
        float value{};
        std::memcpy(&value, data + offset, sizeof(float));
        offset += sizeof(float);
        return value;
    }

    static uint32_t read_uint32_le(const uint8_t* data, size_t& offset) {
        uint32_t value{};
        std::memcpy(&value, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return value;
    }

    static uint16_t read_uint16_le(const uint8_t* data, size_t& offset) {
        uint16_t value{};
        std::memcpy(&value, data + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);
        return value;
    }

    static uint8_t read_uint8(const uint8_t* data, size_t& offset) {
        return data[offset++];
    }

    static int8_t read_int8(const uint8_t* data, size_t& offset) {
        return static_cast<int8_t>(data[offset++]);
    }

    static bool download_from_url(const std::wstring& host, const std::wstring& path, std::vector<uint8_t>& data, int* out_status = nullptr) {
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        DWORD timeout = 5000;
        WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        LPCWSTR headers = L"Accept: */*\r\nAccept-Encoding: gzip, deflate\r\nReferer: https://www.roblox.com/\r\n";
        WinHttpAddRequestHeaders(hRequest, headers, (DWORD)wcslen(headers), WINHTTP_ADDREQ_FLAG_ADD);

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD status = 0;
        DWORD status_len = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &status_len, nullptr);
        if (out_status) *out_status = (int)status;
        g_last_http_status.store((int)status);
        if (status != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        data.clear();
        DWORD avail = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
            size_t old_size = data.size();
            data.resize(old_size + avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, data.data() + old_size, avail, &read)) {
                data.resize(old_size);
                break;
            }
            if (read < avail) data.resize(old_size + read);
        } while (true);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return !data.empty();
    }

    static bool fetch_avatar_json(int64_t user_id, std::string& out_json) {
        std::wstring path = L"/v1/users/" + std::to_wstring(user_id) + L"/avatar";
        std::vector<uint8_t> data;
        int status = 0;
        if (!download_from_url(L"avatar.roblox.com", path, data, &status)) return false;
        out_json.assign(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }

    static void parse_avatar_assets(const std::string& json, std::unordered_map<std::string, uint64_t>& out_map) {
        out_map.clear();
        size_t assets_pos = json.find("\"assets\"");
        if (assets_pos == std::string::npos) return;

        size_t pos = assets_pos;
        while ((pos = json.find('{', pos)) != std::string::npos) {
            size_t next_open = json.find('{', pos + 1);
            size_t next_close = json.find('}', pos);
            if (next_close == std::string::npos) break;

            uint64_t asset_id = 0;
            size_t id_pos = json.find("\"id\"", pos);
            if (id_pos != std::string::npos && id_pos < next_close) {
                size_t colon = json.find(':', id_pos);
                if (colon != std::string::npos && colon < next_close) {
                    size_t num_start = colon + 1;
                    while (num_start < next_close && (json[num_start] == ' ' || json[num_start] == '\t')) num_start++;
                    size_t num_end = num_start;
                    while (num_end < next_close && json[num_end] >= '0' && json[num_end] <= '9') num_end++;
                    if (num_end > num_start) {
                        try { asset_id = std::stoull(json.substr(num_start, num_end - num_start)); } catch (...) {}
                    }
                }
            }

            std::string type_name;
            size_t at_pos = json.find("\"assetType\"", pos);
            if (at_pos != std::string::npos && at_pos < pos + 250) {
                size_t name_pos = json.find("\"name\"", at_pos);
                if (name_pos != std::string::npos && name_pos < at_pos + 120) {
                    size_t colon = json.find(':', name_pos);
                    if (colon != std::string::npos) {
                        size_t q1 = json.find('"', colon);
                        if (q1 != std::string::npos) {
                            size_t q2 = json.find('"', q1 + 1);
                            if (q2 != std::string::npos) {
                                type_name = json.substr(q1 + 1, q2 - q1 - 1);
                            }
                        }
                    }
                }
            }

            if (asset_id != 0 && !type_name.empty()) {
                out_map[type_name] = asset_id;
            }

            pos = (next_close > pos) ? next_close + 1 : pos + 1;
        }
    }

    static DWORD WINAPI avatar_fetch_thread(LPVOID param) {
        int64_t uid = static_cast<int64_t>(reinterpret_cast<uintptr_t>(param));
        try {
            std::string json;
            if (!fetch_avatar_json(uid, json)) {
                std::lock_guard<std::mutex> lock(g_avatar_mutex);
                g_avatar_pending.erase(uid);
                g_avatar_retry_after[uid] = GetTickCount64() + 15000;
                return 0;
            }

            std::unordered_map<std::string, uint64_t> asset_map;
            parse_avatar_assets(json, asset_map);
            {
                std::lock_guard<std::mutex> lock(g_avatar_mutex);
                g_avatar_pending.erase(uid);
                g_avatar_asset_cache[uid] = asset_map;
            }
            for (const auto& kv : asset_map) {
                if (kv.second != 0) request_mesh(kv.second);
            }
            return 0;
        } catch (...) {
            std::lock_guard<std::mutex> lock(g_avatar_mutex);
            g_avatar_pending.erase(uid);
            g_avatar_retry_after[uid] = GetTickCount64() + 15000;
        }
        return 0;
    }

    static uint64_t get_default_asset_for_part_impl(const char* part_name, bool is_r15) {
        if (is_r15) {
            if (strcmp(part_name, "Head") == 0) return 7430070993;
            if (strcmp(part_name, "UpperTorso") == 0) return 7430071038;
            if (strcmp(part_name, "LowerTorso") == 0) return 7430071109;
            if (strcmp(part_name, "LeftUpperArm") == 0) return 7430071044;
            if (strcmp(part_name, "RightUpperArm") == 0) return 7430071041;
            if (strcmp(part_name, "LeftLowerArm") == 0) return 7430071005;
            if (strcmp(part_name, "RightLowerArm") == 0) return 7430071013;
            if (strcmp(part_name, "LeftHand") == 0) return 7430070991;
            if (strcmp(part_name, "RightHand") == 0) return 7430070997;
            if (strcmp(part_name, "LeftUpperLeg") == 0) return 7430071065;
            if (strcmp(part_name, "RightUpperLeg") == 0) return 7430071119;
            if (strcmp(part_name, "LeftLowerLeg") == 0) return 7430071049;
            if (strcmp(part_name, "RightLowerLeg") == 0) return 7430071105;
            if (strcmp(part_name, "LeftFoot") == 0) return 7430071039;
            if (strcmp(part_name, "RightFoot") == 0) return 7430071082;
        }
        else {
            if (strcmp(part_name, "Head") == 0) return 1365230;
            if (strcmp(part_name, "Torso") == 0) return 1365219;
            if (strcmp(part_name, "Left Arm") == 0) return 1365224;
            if (strcmp(part_name, "Right Arm") == 0) return 1365223;
            if (strcmp(part_name, "Left Leg") == 0) return 1365226;
            if (strcmp(part_name, "Right Leg") == 0) return 1365225;
        }
        return 0;
    }

    static const char* part_name_to_asset_type(const char* part_name) {
        if (strcmp(part_name, "Head") == 0) return "DynamicHead";
        if (strcmp(part_name, "Torso") == 0) return "Torso";
        if (strcmp(part_name, "UpperTorso") == 0) return "Torso";
        if (strcmp(part_name, "LowerTorso") == 0) return "Torso";
        if (strcmp(part_name, "Left Arm") == 0) return "LeftArm";
        if (strcmp(part_name, "Right Arm") == 0) return "RightArm";
        if (strcmp(part_name, "Left Leg") == 0) return "LeftLeg";
        if (strcmp(part_name, "Right Leg") == 0) return "RightLeg";
        if (strcmp(part_name, "LeftUpperArm") == 0) return "LeftArm";
        if (strcmp(part_name, "RightUpperArm") == 0) return "RightArm";
        if (strcmp(part_name, "LeftLowerArm") == 0) return "LeftArm";
        if (strcmp(part_name, "RightLowerArm") == 0) return "RightArm";
        if (strcmp(part_name, "LeftHand") == 0) return "LeftArm";
        if (strcmp(part_name, "RightHand") == 0) return "RightArm";
        if (strcmp(part_name, "LeftUpperLeg") == 0) return "LeftLeg";
        if (strcmp(part_name, "RightUpperLeg") == 0) return "RightLeg";
        if (strcmp(part_name, "LeftLowerLeg") == 0) return "LeftLeg";
        if (strcmp(part_name, "RightLowerLeg") == 0) return "RightLeg";
        if (strcmp(part_name, "LeftFoot") == 0) return "LeftLeg";
        if (strcmp(part_name, "RightFoot") == 0) return "RightLeg";
        return nullptr;
    }

    static std::string extract_json_string(const std::string& json, const char* key) {
        std::string search = "\"";
        search += key;
        search += "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
    }

    static bool split_url(const std::string& url, std::wstring& host, std::wstring& path) {
        size_t proto = url.find("://");
        if (proto == std::string::npos) return false;
        std::string rest = url.substr(proto + 3);
        size_t path_pos = rest.find('/');
        if (path_pos == std::string::npos) return false;
        std::string host_str = rest.substr(0, path_pos);
        std::string path_str = rest.substr(path_pos);
        if (host_str.empty() || path_str.empty()) return false;
        host.assign(host_str.begin(), host_str.end());
        path.assign(path_str.begin(), path_str.end());
        return true;
    }

    static uint64_t extract_asset_id_from_text(const std::string& text) {
        static const char* patterns[] = {
            "rbxassetid://",
            "rbxassetid:",
            "?id=",
            "&id=",
            "\"id\":",
            "\"assetId\":",
            "<url name=\"MeshId\"><url>",
            "<Content name=\"MeshId\"><url>",
            "<int64 name=\"MeshId\">",
            "<int name=\"MeshId\">"
        };
        for (const char* pat : patterns) {
            size_t pat_len = strlen(pat);
            size_t id_pos = text.find(pat);
            while (id_pos != std::string::npos) {
                size_t start = id_pos + pat_len;
                while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '"')) start++;
                size_t url_id = text.find("id=", start);
                if (url_id != std::string::npos && url_id < start + 60) {
                    start = url_id + 3;
                }
                size_t end = start;
                while (end < text.size() && text[end] >= '0' && text[end] <= '9') end++;
                if (end > start) {
                    try {
                        uint64_t val = std::stoull(text.substr(start, end - start));
                        if (val >= 100 && val < 10000000000000ULL) return val;
                    } catch (...) {}
                }
                id_pos = text.find(pat, id_pos + 1);
            }
        }
        return 0;
    }

    static std::string get_mesh_version(const std::vector<uint8_t>& data) {
        if (data.size() < 12) return "";
        std::string version(reinterpret_cast<const char*>(data.data()), 12);
        if (version.rfind("version ", 0) != 0) return "";
        version = version.substr(8);
        while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) version.pop_back();
        return version;
    }

    static void compute_mesh_bounds(parsed_mesh& mesh) {
        if (mesh.vertices.empty()) {
            mesh.bounds.valid = false;
            return;
        }
        mesh.bounds.min = { FLT_MAX, FLT_MAX, FLT_MAX };
        mesh.bounds.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& vertex : mesh.vertices) {
            mesh.bounds.min.x = (std::min)(mesh.bounds.min.x, vertex.position.x);
            mesh.bounds.min.y = (std::min)(mesh.bounds.min.y, vertex.position.y);
            mesh.bounds.min.z = (std::min)(mesh.bounds.min.z, vertex.position.z);
            mesh.bounds.max.x = (std::max)(mesh.bounds.max.x, vertex.position.x);
            mesh.bounds.max.y = (std::max)(mesh.bounds.max.y, vertex.position.y);
            mesh.bounds.max.z = (std::max)(mesh.bounds.max.z, vertex.position.z);
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

    static bool parse_mesh_v1(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh, float scale, bool invert_uv) {
        std::string data_str(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);
        size_t newline_pos = data_str.find('\n');
        if (newline_pos == std::string::npos) return false;
        uint32_t num_faces = 0;
        try {
            num_faces = static_cast<uint32_t>(std::stoul(data_str.substr(0, newline_pos)));
        }
        catch (...) {
            return false;
        }
        size_t start_pos = data_str.find('[');
        if (start_pos == std::string::npos) return false;

        std::vector<std::vector<float>> all_vectors;
        all_vectors.reserve(num_faces * 9);
        std::string remaining = data_str.substr(start_pos);
        size_t pos = 0;
        while (pos < remaining.size()) {
            size_t open = remaining.find('[', pos);
            if (open == std::string::npos) break;
            size_t close = remaining.find(']', open);
            if (close == std::string::npos) break;
            std::string vec_str = remaining.substr(open + 1, close - open - 1);
            std::vector<float> vec;
            std::istringstream iss(vec_str);
            std::string token;
            while (std::getline(iss, token, ',')) {
                try {
                    vec.push_back(std::stof(token));
                }
                catch (...) {
                }
            }
            if (vec.size() == 3) all_vectors.push_back(std::move(vec));
            pos = close + 1;
        }
        uint32_t parsed_faces = static_cast<uint32_t>(all_vectors.size() / 9);
        if (parsed_faces == 0) return false;
        if (num_faces == 0 || num_faces > parsed_faces) num_faces = parsed_faces;

        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(num_faces * 3);
        mesh.indices.reserve(num_faces * 3);
        for (uint32_t f = 0; f < num_faces; ++f) {
            size_t base_idx = static_cast<size_t>(f) * 9;
            for (size_t v = 0; v < 3; ++v) {
                size_t v_idx = base_idx + v * 3;
                if (v_idx >= all_vectors.size()) break;
                mesh_vertex vertex{};
                vertex.position = {
                    all_vectors[v_idx][0] * scale,
                    all_vectors[v_idx][1] * scale,
                    all_vectors[v_idx][2] * scale
                };
                if (v_idx + 1 < all_vectors.size()) {
                    vertex.normal = {
                        all_vectors[v_idx + 1][0],
                        all_vectors[v_idx + 1][1],
                        all_vectors[v_idx + 1][2]
                    };
                }
                if (v_idx + 2 < all_vectors.size()) {
                    vertex.uv = {
                        all_vectors[v_idx + 2][0],
                        all_vectors[v_idx + 2][1]
                    };
                }
                mesh.vertices.push_back(vertex);
            }
            mesh.indices.push_back(f * 3);
            mesh.indices.push_back(f * 3 + 1);
            mesh.indices.push_back(f * 3 + 2);
        }
        return true;
    }

    static bool parse_mesh_v2(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh) {
        size_t pos = offset;
        if (pos + 12 > data.size()) return false;
        uint16_t cb_size = read_uint16_le(data.data(), pos);
        if (cb_size != 12) return false;
        uint8_t cb_vertices_stride = read_uint8(data.data(), pos);
        uint8_t cb_face_stride = read_uint8(data.data(), pos);
        uint32_t num_vertices = read_uint32_le(data.data(), pos);
        uint32_t num_faces = read_uint32_le(data.data(), pos);
        if (num_vertices == 0 || num_faces == 0) return false;
        size_t vertex_size = cb_vertices_stride;
        if (vertex_size < 32) return false;

        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(num_vertices);
        mesh.indices.reserve(num_faces * 3);
        for (uint32_t i = 0; i < num_vertices; ++i) {
            if (pos + vertex_size > data.size()) return false;
            size_t vertex_start = pos;
            mesh_vertex vertex{};
            vertex.position.x = read_float_le(data.data(), pos);
            vertex.position.y = read_float_le(data.data(), pos);
            vertex.position.z = read_float_le(data.data(), pos);
            vertex.normal.x = read_float_le(data.data(), pos);
            vertex.normal.y = read_float_le(data.data(), pos);
            vertex.normal.z = read_float_le(data.data(), pos);
            vertex.uv.x = read_float_le(data.data(), pos);
            vertex.uv.y = read_float_le(data.data(), pos);
            mesh.vertices.push_back(vertex);
            pos = vertex_start + vertex_size;
        }
        size_t face_start = pos;
        size_t face_stride = cb_face_stride;
        for (uint32_t i = 0; i < num_faces; ++i) {
            if (pos + face_stride > data.size()) return false;
            size_t face_entry = pos;
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            pos = face_entry + face_stride;
        }
        return true;
    }

    static bool parse_mesh_v3(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh) {
        size_t pos = offset;
        if (pos + 16 > data.size()) return false;
        uint16_t cb_size = read_uint16_le(data.data(), pos);
        if (cb_size != 16) return false;
        uint8_t cb_vertices_stride = read_uint8(data.data(), pos);
        uint8_t cb_face_stride = read_uint8(data.data(), pos);
        uint16_t num_lods = read_uint16_le(data.data(), pos);
        uint32_t num_vertices = read_uint32_le(data.data(), pos);
        uint32_t num_faces = read_uint32_le(data.data(), pos);
        if (num_vertices == 0 || num_faces == 0) return false;
        size_t vertex_size = cb_vertices_stride;
        if (vertex_size < 32) return false;

        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(num_vertices);
        mesh.indices.reserve(num_faces * 3);
        for (uint32_t i = 0; i < num_vertices; ++i) {
            if (pos + vertex_size > data.size()) return false;
            size_t vertex_start = pos;
            mesh_vertex vertex{};
            vertex.position.x = read_float_le(data.data(), pos);
            vertex.position.y = read_float_le(data.data(), pos);
            vertex.position.z = read_float_le(data.data(), pos);
            vertex.normal.x = read_float_le(data.data(), pos);
            vertex.normal.y = read_float_le(data.data(), pos);
            vertex.normal.z = read_float_le(data.data(), pos);
            vertex.uv.x = read_float_le(data.data(), pos);
            vertex.uv.y = read_float_le(data.data(), pos);
            mesh.vertices.push_back(vertex);
            pos = vertex_start + vertex_size;
        }
        size_t face_start = pos;
        size_t face_stride = cb_face_stride;
        for (uint32_t i = 0; i < num_faces; ++i) {
            if (pos + face_stride > data.size()) return false;
            size_t face_entry = pos;
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            pos = face_entry + face_stride;
        }
        std::vector<uint32_t> lods;
        lods.reserve(num_lods);
        for (uint16_t i = 0; i < num_lods; ++i) {
            if (pos + 4 > data.size()) break;
            lods.push_back(read_uint32_le(data.data(), pos));
        }
        return true;
    }

    static bool parse_mesh_v4(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh) {
        size_t pos = offset;
        if (pos + 24 > data.size()) return false;
        uint16_t header_size = read_uint16_le(data.data(), pos);
        if (header_size != 24) return false;
        uint16_t lod_type = read_uint16_le(data.data(), pos);
        uint32_t num_vertices = read_uint32_le(data.data(), pos);
        uint32_t num_faces = read_uint32_le(data.data(), pos);
        uint16_t num_lods = read_uint16_le(data.data(), pos);
        uint16_t num_bones = read_uint16_le(data.data(), pos);
        uint32_t bone_names_size = read_uint32_le(data.data(), pos);
        uint16_t num_subsets = read_uint16_le(data.data(), pos);
        uint8_t num_hq_lods = read_uint8(data.data(), pos);
        uint8_t unused = read_uint8(data.data(), pos);
        (void)lod_type;
        (void)bone_names_size;
        (void)num_subsets;
        (void)num_hq_lods;
        (void)unused;
        if (num_vertices == 0 || num_faces == 0) return false;

        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(num_vertices);
        mesh.indices.reserve(num_faces * 3);
        for (uint32_t i = 0; i < num_vertices; ++i) {
            if (pos + 40 > data.size()) return false;
            mesh_vertex vertex{};
            vertex.position.x = read_float_le(data.data(), pos);
            vertex.position.y = read_float_le(data.data(), pos);
            vertex.position.z = read_float_le(data.data(), pos);
            vertex.normal.x = read_float_le(data.data(), pos);
            vertex.normal.y = read_float_le(data.data(), pos);
            vertex.normal.z = read_float_le(data.data(), pos);
            vertex.uv.x = read_float_le(data.data(), pos);
            vertex.uv.y = read_float_le(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            mesh.vertices.push_back(vertex);
        }
        if (num_bones > 0) {
            size_t skip = (size_t)num_vertices * 8;
            if (pos + skip > data.size()) return false;
            pos += skip;
        }
        for (uint32_t i = 0; i < num_faces; ++i) {
            if (pos + 12 > data.size()) return false;
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
        }
        std::vector<uint32_t> lods;
        lods.reserve(num_lods);
        for (uint16_t i = 0; i < num_lods; ++i) {
            if (pos + 4 > data.size()) break;
            lods.push_back(read_uint32_le(data.data(), pos));
        }
        return true;
    }

    static bool parse_mesh_v5(const std::vector<uint8_t>& data, size_t offset, parsed_mesh& mesh) {
        size_t pos = offset;
        if (pos + 32 > data.size()) return false;
        uint16_t header_size = read_uint16_le(data.data(), pos);
        if (header_size != 32) return false;
        uint16_t lod_type = read_uint16_le(data.data(), pos);
        uint32_t num_vertices = read_uint32_le(data.data(), pos);
        uint32_t num_faces = read_uint32_le(data.data(), pos);
        uint16_t num_lods = read_uint16_le(data.data(), pos);
        uint16_t num_bones = read_uint16_le(data.data(), pos);
        uint32_t bone_names_size = read_uint32_le(data.data(), pos);
        uint16_t num_subsets = read_uint16_le(data.data(), pos);
        uint8_t num_hq_lods = read_uint8(data.data(), pos);
        uint8_t unused_padding = read_uint8(data.data(), pos);
        uint32_t facs_data_format = read_uint32_le(data.data(), pos);
        uint32_t facs_data_size = read_uint32_le(data.data(), pos);
        (void)lod_type;
        (void)bone_names_size;
        (void)num_subsets;
        (void)num_hq_lods;
        (void)unused_padding;
        (void)facs_data_format;
        if (num_vertices == 0 || num_faces == 0) return false;

        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(num_vertices);
        mesh.indices.reserve(num_faces * 3);
        for (uint32_t i = 0; i < num_vertices; ++i) {
            if (pos + 40 > data.size()) return false;
            mesh_vertex vertex{};
            vertex.position.x = read_float_le(data.data(), pos);
            vertex.position.y = read_float_le(data.data(), pos);
            vertex.position.z = read_float_le(data.data(), pos);
            vertex.normal.x = read_float_le(data.data(), pos);
            vertex.normal.y = read_float_le(data.data(), pos);
            vertex.normal.z = read_float_le(data.data(), pos);
            vertex.uv.x = read_float_le(data.data(), pos);
            vertex.uv.y = read_float_le(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_int8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            read_uint8(data.data(), pos);
            mesh.vertices.push_back(vertex);
        }
        if (num_bones > 0) {
            size_t skip = (size_t)num_vertices * 8;
            if (pos + skip > data.size()) return false;
            pos += skip;
        }
        for (uint32_t i = 0; i < num_faces; ++i) {
            if (pos + 12 > data.size()) return false;
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
            mesh.indices.push_back(read_uint32_le(data.data(), pos));
        }
        std::vector<uint32_t> lods;
        lods.reserve(num_lods);
        for (uint16_t i = 0; i < num_lods; ++i) {
            if (pos + 4 > data.size()) break;
            lods.push_back(read_uint32_le(data.data(), pos));
        }
        return true;
    }

    static bool parse_mesh_from_data(const std::vector<uint8_t>& data, parsed_mesh& mesh) {
        if (data.size() < 12) return false;
        std::string version = get_mesh_version(data);
        if (version.empty()) return false;
        mesh.version = version;
        size_t offset = 0;
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] == '\n') {
                offset = i + 1;
                break;
            }
        }
        bool success = false;
        if (version.rfind("1.00", 0) == 0) success = parse_mesh_v1(data, offset, mesh, 0.5f, true);
        else if (version.rfind("1.", 0) == 0) success = parse_mesh_v1(data, offset, mesh, 1.0f, false);
        else if (version.rfind("2.", 0) == 0) success = parse_mesh_v2(data, offset, mesh);
        else if (version.rfind("3.", 0) == 0) success = parse_mesh_v3(data, offset, mesh);
        else if (version.rfind("4.", 0) == 0) success = parse_mesh_v4(data, offset, mesh);
        else if (version.rfind("5.", 0) == 0 || version.rfind("6.", 0) == 0 || version.rfind("7.", 0) == 0) success = parse_mesh_v5(data, offset, mesh);
        else {
            success = parse_mesh_v5(data, offset, mesh);
            if (!success) success = parse_mesh_v4(data, offset, mesh);
        }
        if (!success) return false;
        if (mesh.vertices.empty() || mesh.indices.empty()) return false;
        compute_mesh_bounds(mesh);
        return mesh.bounds.valid;
    }

    static bool download_mesh_from_asset_id(uint64_t asset_id, std::vector<uint8_t>& mesh_data, int* out_status = nullptr, int depth = 0) {
        if (asset_id == 0) return false;
        if (depth > 2) return false;
        std::vector<uint8_t> api_response;
        int http_status = 0;
        std::wstring path = L"/v2/asset/?id=" + std::to_wstring(asset_id);
        if (!download_from_url(L"assetdelivery.roblox.com", path, api_response, &http_status)) {
            if (out_status) *out_status = http_status;
            return false;
        }
        if (out_status) *out_status = http_status;
        if (api_response.size() >= 12) {
            std::string header(reinterpret_cast<const char*>(api_response.data()), 12);
            if (header.rfind("version ", 0) == 0) {
                mesh_data = std::move(api_response);
                return true;
            }
        }

        std::string response_str(reinterpret_cast<const char*>(api_response.data()), api_response.size());
        uint64_t nested_asset_id = extract_asset_id_from_text(response_str);
        if (nested_asset_id != 0 && nested_asset_id != asset_id) {
            return download_mesh_from_asset_id(nested_asset_id, mesh_data, out_status, depth + 1);
        }

        std::string location_url = extract_json_string(response_str, "location");
        if (location_url.empty()) location_url = extract_json_string(response_str, "url");
        if (location_url.empty()) {
            size_t http_start = response_str.find("http");
            if (http_start != std::string::npos) {
                size_t http_end = response_str.find('"', http_start);
                if (http_end != std::string::npos) location_url = response_str.substr(http_start, http_end - http_start);
            }
        }
        if (location_url.empty()) return false;

        std::wstring host;
        std::wstring location_path;
        if (!split_url(location_url, host, location_path)) return false;
        bool downloaded = download_from_url(host, location_path, mesh_data, &http_status);
        if (out_status) *out_status = http_status;
        if (!downloaded) return false;
        if (mesh_data.size() >= 12) {
            std::string header(reinterpret_cast<const char*>(mesh_data.data()), 12);
            if (header.rfind("version ", 0) == 0) return true;
        }
        std::string nested_text(reinterpret_cast<const char*>(mesh_data.data()), mesh_data.size());
        nested_asset_id = extract_asset_id_from_text(nested_text);
        if (nested_asset_id != 0 && nested_asset_id != asset_id) {
            return download_mesh_from_asset_id(nested_asset_id, mesh_data, out_status, depth + 1);
        }
        return true;
    }

    static std::string read_string_property(uint64_t address) {
        if (address < 0x10000 || address >= 0x7FFFFFFFFFFFull) return "";
        std::string s = memory->read_string(address);
        if (s == "str_error") return "";
        return s;
    }

    static int map_character_mesh_body_part(const std::string& part_name) {
        if (part_name == "Head") return 0;
        if (part_name == "Torso" || part_name == "UpperTorso" || part_name == "LowerTorso") return 1;
        if (part_name == "Left Arm" || part_name == "LeftArm" || part_name == "LeftUpperArm" || part_name == "LeftLowerArm" || part_name == "LeftHand") return 2;
        if (part_name == "Right Arm" || part_name == "RightArm" || part_name == "RightUpperArm" || part_name == "RightLowerArm" || part_name == "RightHand") return 3;
        if (part_name == "Left Leg" || part_name == "LeftLeg" || part_name == "LeftUpperLeg" || part_name == "LeftLowerLeg" || part_name == "LeftFoot") return 4;
        if (part_name == "Right Leg" || part_name == "RightLeg" || part_name == "RightUpperLeg" || part_name == "RightLowerLeg" || part_name == "RightFoot") return 5;
        return -1;
    }

    static std::string get_mesh_id_string(uint64_t part_address) {
        if (part_address == 0) return "";
        rbx::c_instance part{ part_address };
        const std::string class_name = part.get_class_name();
        const std::string part_name = part.get_name();

        if (class_name == "MeshPart") {
            std::string id = read_string_property(part_address + Offsets::MeshPart::MeshId);
            if (!id.empty() && id != "str_error") return id;
        }

        for (rbx::c_instance child : part.get_children<rbx::c_instance>()) {
            if (!child.is_valid()) continue;
            const std::string child_class = child.get_class_name();
            if (child_class == "SpecialMesh" || child_class == "FileMesh" || child_class == "Mesh" || child_class == "CylinderMesh" || child_class == "BlockMesh") {
                std::string mesh_id = read_string_property(child.address + Offsets::SpecialMesh::MeshId);
                if (!mesh_id.empty() && mesh_id != "str_error") return mesh_id;
            }
            else if (child_class == "MeshPart") {
                std::string mesh_id = read_string_property(child.address + Offsets::MeshPart::MeshId);
                if (!mesh_id.empty() && mesh_id != "str_error") return mesh_id;
            }
            else if (child_class == "WrapTarget" || child_class == "WrapLayer") {
                for (rbx::c_instance w_child : child.get_children<rbx::c_instance>()) {
                    if (!w_child.is_valid()) continue;
                    if (w_child.get_class_name() == "MeshPart") {
                        std::string mesh_id = read_string_property(w_child.address + Offsets::MeshPart::MeshId);
                        if (!mesh_id.empty() && mesh_id != "str_error") return mesh_id;
                    }
                }
            }
        }

        uint64_t parent_addr = part.get_parent();
        if (parent_addr != 0) {
            rbx::c_instance parent{ parent_addr };
            std::string parent_cls = parent.get_class_name();
            if (parent_cls == "Accessory" || parent_cls == "Accoutrement" || parent_cls == "Hat") {
                for (rbx::c_instance acc_child : parent.get_children<rbx::c_instance>()) {
                    if (!acc_child.is_valid()) continue;
                    if (acc_child.get_class_name() == "SpecialMesh" || acc_child.get_class_name() == "FileMesh") {
                        std::string mesh_id = read_string_property(acc_child.address + Offsets::SpecialMesh::MeshId);
                        if (!mesh_id.empty() && mesh_id != "str_error") return mesh_id;
                    }
                    else if (acc_child.get_class_name() == "MeshPart") {
                        std::string mesh_id = read_string_property(acc_child.address + Offsets::MeshPart::MeshId);
                        if (!mesh_id.empty() && mesh_id != "str_error") return mesh_id;
                    }
                }
            }

            const int target_body_part = map_character_mesh_body_part(part_name);
            if (target_body_part >= 0) {
                for (rbx::c_instance sibling : parent.get_children<rbx::c_instance>()) {
                    if (!sibling.is_valid()) continue;
                    if (sibling.get_class_name() != "CharacterMesh") continue;
                    int body_part = memory->read<int>(sibling.address + Offsets::CharacterMesh::BodyPart);
                    std::string mesh_id = read_string_property(sibling.address + Offsets::CharacterMesh::MeshId);
                    if (!mesh_id.empty() && mesh_id != "str_error") {
                        bool all_digits = true;
                        for (char c : mesh_id) { if (!isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; } }
                        if (all_digits) return "rbxassetid://" + mesh_id;
                        return mesh_id;
                    }

                    uint64_t numeric_id64 = memory->read<uint64_t>(sibling.address + Offsets::CharacterMesh::MeshId);
                    if (numeric_id64 >= 100 && numeric_id64 < 10000000000ULL) {
                        return "rbxassetid://" + std::to_string(numeric_id64);
                    }
                    uint32_t numeric_id32 = memory->read<uint32_t>(sibling.address + Offsets::CharacterMesh::MeshId);
                    if (numeric_id32 >= 100 && numeric_id32 < 1000000000U) {
                        return "rbxassetid://" + std::to_string(numeric_id32);
                    }
                }
            }
        }

        return "";
    }

    static uint64_t extract_asset_id(const std::string& mesh_id_string) {
        if (mesh_id_string.empty() || mesh_id_string == "str_error") return 0;

        if (mesh_id_string.rfind("rbxassetid://", 0) == 0) {
            try { return std::stoull(mesh_id_string.substr(13)); }
            catch (...) {}
        }
        if (mesh_id_string.rfind("rbxasset://", 0) == 0) {
            try { return std::stoull(mesh_id_string.substr(11)); }
            catch (...) {}
        }
        size_t id_pos = mesh_id_string.find("id=");
        if (id_pos != std::string::npos) {
            std::string id_str = mesh_id_string.substr(id_pos + 3);
            size_t end_pos = id_str.find_first_of("& \n\r\t/\"'\\");
            if (end_pos != std::string::npos) id_str = id_str.substr(0, end_pos);
            try {
                uint64_t val = std::stoull(id_str);
                if (val > 0) return val;
            }
            catch (...) {}
        }

        bool all_digits = true;
        for (char c : mesh_id_string) {
            if (!isdigit(static_cast<unsigned char>(c))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits && !mesh_id_string.empty()) {
            try { return std::stoull(mesh_id_string); }
            catch (...) {}
        }

        std::string num_buf;
        for (char c : mesh_id_string) {
            if (isdigit(static_cast<unsigned char>(c))) {
                num_buf += c;
            } else {
                if (num_buf.length() >= 5) {
                    try {
                        uint64_t val = std::stoull(num_buf);
                        if (val > 0) return val;
                    } catch (...) {}
                }
                num_buf.clear();
            }
        }
        if (num_buf.length() >= 5) {
            try {
                uint64_t val = std::stoull(num_buf);
                if (val > 0) return val;
            } catch (...) {}
        }

        return 0;
    }

    static DWORD WINAPI worker_thread_proc(LPVOID) {
        while (g_running.load()) {
            try {
                uint64_t asset_id = 0;
                {
                    std::unique_lock<std::mutex> lock(g_state_mutex);
                    g_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [] {
                        return !g_request_queue.empty() || !g_running.load();
                        });
                    if (!g_running.load()) break;
                    if (g_request_queue.empty()) continue;
                    asset_id = g_request_queue.front();
                    g_request_queue.pop();
                }

                ULONGLONG now = GetTickCount64();
                ULONGLONG last = g_last_request_tick.load();
                if (last != 0 && now - last < REQUEST_DELAY_MS) {
                    Sleep((DWORD)(REQUEST_DELAY_MS - (now - last)));
                }
                g_last_request_tick.store(GetTickCount64());

                std::vector<uint8_t> mesh_data;
                int http_status = 0;
                if (!download_mesh_from_asset_id(asset_id, mesh_data, &http_status)) {
                    g_fetch_fail++;
                    g_fail_api++;
                    g_last_failed_asset_id.store(asset_id);
                    {
                        std::lock_guard<std::mutex> lock(g_state_mutex);
                        g_pending_assets.erase(asset_id);
                        ULONGLONG retry_ms = (http_status == 401 || http_status == 403) ? 30000ull : 10000ull;
                        g_retry_after[asset_id] = GetTickCount64() + retry_ms;
                    }
                    set_error("assetdelivery fetch failed");
                    continue;
                }

                auto mesh = std::make_shared<parsed_mesh>();
                if (!parse_mesh_from_data(mesh_data, *mesh)) {
                    g_fetch_fail++;
                    g_fail_parse++;
                    g_last_failed_asset_id.store(asset_id);
                    {
                        std::lock_guard<std::mutex> lock(g_state_mutex);
                        g_pending_assets.erase(asset_id);
                        g_retry_after[asset_id] = GetTickCount64() + 60000ull;
                    }
                    set_error("mesh parse failed");
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_mesh_cache[asset_id] = mesh;
                    g_pending_assets.erase(asset_id);
                    g_retry_after.erase(asset_id);
                }
                g_fetch_ok++;
            } catch (...) {
                set_error("mesh worker crashed");
                Sleep(100);
            }
        }
        return 0;
    }

    void initialize() {
        if (g_running.load()) return;
        g_running.store(true);
        g_workers.reserve(THREAD_POOL_SIZE);
        for (size_t i = 0; i < THREAD_POOL_SIZE; ++i) {
            HANDLE worker = CreateThread(nullptr, 0, worker_thread_proc, nullptr, 0, nullptr);
            if (worker) g_workers.push_back(worker);
        }
    }

    void shutdown() {
        g_running.store(false);
        g_queue_cv.notify_all();
        for (HANDLE worker : g_workers) {
            WaitForSingleObject(worker, INFINITE);
            CloseHandle(worker);
        }
        g_workers.clear();
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_mesh_cache.clear();
        g_pending_assets.clear();
        g_retry_after.clear();
        std::queue<uint64_t> empty;
        std::swap(g_request_queue, empty);
        {
            std::lock_guard<std::mutex> av_lock(g_avatar_mutex);
            g_avatar_asset_cache.clear();
            g_avatar_pending.clear();
            g_avatar_retry_after.clear();
        }
    }

    uint64_t get_mesh_asset_id_from_part(uintptr_t part_address) {
        if (part_address == 0) return 0;
        return extract_asset_id(get_mesh_id_string(part_address));
    }

    std::string get_mesh_id_string_from_part(uintptr_t part_address) {
        if (part_address == 0) return "";
        return get_mesh_id_string(part_address);
    }

    void request_mesh(uint64_t asset_id) {
        if (asset_id == 0) return;
        if (!g_running.load()) initialize();
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_mesh_cache.find(asset_id) != g_mesh_cache.end()) return;
        auto retry_it = g_retry_after.find(asset_id);
        if (retry_it != g_retry_after.end()) {
            ULONGLONG now = GetTickCount64();
            if (now < retry_it->second) return;
            g_retry_after.erase(retry_it);
        }
        if (g_pending_assets.find(asset_id) != g_pending_assets.end()) return;
        g_pending_assets.insert(asset_id);
        g_request_queue.push(asset_id);
        g_queue_cv.notify_one();
    }

    static std::shared_ptr<parsed_mesh> create_procedural_head_mesh() {
        auto m = std::make_shared<parsed_mesh>();
        m->version = "procedural_r6_head";
        const int num_rings = 32;
        const int num_sectors = 32;

        struct ProfilePoint {
            float y;
            float r;
        };
        std::vector<ProfilePoint> profile;
        profile.reserve(num_rings + 1);

        for (int i = 0; i <= num_rings; ++i) {
            float t = (float)i / (float)num_rings; // 0.0 at bottom to 1.0 at top
            float y = 0.0f;
            float r = 0.0f;
            if (t <= 0.30f) {
                // Bottom dome (curving from bottom apex at y=-0.58 to cylinder at y=-0.25)
                float u = t / 0.30f; // 0 to 1
                float angle = u * (1.5707963f); // 0 to pi/2
                y = -0.58f + sinf(angle) * 0.33f;
                r = cosf(1.5707963f - angle) * 0.52f;
            } else if (t >= 0.70f) {
                // Top dome (curving from cylinder at y=+0.25 to top apex at y=+0.58)
                float u = (t - 0.70f) / 0.30f; // 0 to 1
                float angle = u * (1.5707963f); // 0 to pi/2
                y = 0.25f + sinf(angle) * 0.33f;
                r = cosf(angle) * 0.52f;
            } else {
                // Cylindrical middle
                float u = (t - 0.30f) / 0.40f;
                y = -0.25f + u * 0.50f;
                r = 0.52f;
            }
            profile.push_back({ y, r });
        }

        for (size_t ring = 0; ring < profile.size(); ++ring) {
            float y = profile[ring].y;
            float r = profile[ring].r;
            float t = (float)ring / (float)(profile.size() - 1);
            for (int s = 0; s <= num_sectors; ++s) {
                float theta = 6.2831853f * (float)s / (float)num_sectors;
                mesh_vertex v{};
                v.position.x = cosf(theta) * r;
                v.position.y = y;
                v.position.z = sinf(theta) * r;
                float nx = cosf(theta);
                float nz = sinf(theta);
                float ny = 0.0f;
                if (y > 0.25f) ny = (y - 0.25f) / 0.33f;
                else if (y < -0.25f) ny = (y + 0.25f) / 0.33f;
                float nlen = sqrtf(nx * nx * (r > 0.01f ? 1.0f : 0.0f) + ny * ny + nz * nz * (r > 0.01f ? 1.0f : 0.0f));
                if (nlen > 1e-4f) { nx /= nlen; ny /= nlen; nz /= nlen; }
                v.normal = { nx, ny, nz };
                v.uv = { (float)s / (float)num_sectors, t };
                m->vertices.push_back(v);
            }
        }

        const int ring_stride = num_sectors + 1;
        for (size_t ring = 0; ring < profile.size() - 1; ++ring) {
            for (int s = 0; s < num_sectors; ++s) {
                uint32_t curr = (uint32_t)(ring * ring_stride + s);
                uint32_t next = curr + ring_stride;

                m->indices.push_back(curr);
                m->indices.push_back(next);
                m->indices.push_back(curr + 1);

                m->indices.push_back(next);
                m->indices.push_back(next + 1);
                m->indices.push_back(curr + 1);
            }
        }

        compute_mesh_bounds(*m);
        return m;
    }

    static std::shared_ptr<parsed_mesh> create_procedural_beveled_box(float r = 0.16f) {
        auto m = std::make_shared<parsed_mesh>();
        m->version = "procedural_beveled";
        const int grid = 10;
        const float w = 0.5f - r;

        auto project_point = [w, r](float x, float y, float z, mesh_vertex& out) {
            float cx = (std::max)(-w, (std::min)(w, x));
            float cy = (std::max)(-w, (std::min)(w, y));
            float cz = (std::max)(-w, (std::min)(w, z));
            float dx = x - cx;
            float dy = y - cy;
            float dz = z - cz;
            float len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len > 1e-5f) {
                float inv = 1.0f / len;
                out.normal = { dx * inv, dy * inv, dz * inv };
                out.position = { cx + out.normal.x * r, cy + out.normal.y * r, cz + out.normal.z * r };
            } else {
                float max_abs = (std::max)({ fabsf(x), fabsf(y), fabsf(z) });
                if (max_abs == fabsf(z)) out.normal = { 0.0f, 0.0f, (z > 0 ? 1.0f : -1.0f) };
                else if (max_abs == fabsf(y)) out.normal = { 0.0f, (y > 0 ? 1.0f : -1.0f), 0.0f };
                else out.normal = { (x > 0 ? 1.0f : -1.0f), 0.0f, 0.0f };
                out.position = { x, y, z };
            }
        };

        struct FaceDef {
            float origin[3];
            float u_axis[3];
            float v_axis[3];
        };

        const FaceDef faces[6] = {
            { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
            { {  0.5f, -0.5f, -0.5f }, {-1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
            { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,-1.0f } },
            { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
            { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f } },
            { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } }
        };

        for (int f = 0; f < 6; ++f) {
            const auto& face = faces[f];
            uint32_t base_idx = (uint32_t)m->vertices.size();
            for (int i = 0; i <= grid; ++i) {
                float u = (float)i / (float)grid;
                for (int j = 0; j <= grid; ++j) {
                    float v = (float)j / (float)grid;
                    float x = face.origin[0] + face.u_axis[0] * u + face.v_axis[0] * v;
                    float y = face.origin[1] + face.u_axis[1] * u + face.v_axis[1] * v;
                    float z = face.origin[2] + face.u_axis[2] * u + face.v_axis[2] * v;
                    mesh_vertex vert{};
                    project_point(x, y, z, vert);
                    vert.uv = { u, v };
                    m->vertices.push_back(vert);
                }
            }
            int row_stride = grid + 1;
            for (int i = 0; i < grid; ++i) {
                for (int j = 0; j < grid; ++j) {
                    uint32_t top_left = base_idx + i * row_stride + j;
                    uint32_t top_right = top_left + 1;
                    uint32_t bottom_left = top_left + row_stride;
                    uint32_t bottom_right = bottom_left + 1;

                    m->indices.push_back(top_left);
                    m->indices.push_back(bottom_left);
                    m->indices.push_back(top_right);

                    m->indices.push_back(top_right);
                    m->indices.push_back(bottom_left);
                    m->indices.push_back(bottom_right);
                }
            }
        }

        compute_mesh_bounds(*m);
        return m;
    }

    std::shared_ptr<const parsed_mesh> get_mesh(uint64_t asset_id) {
        if (asset_id == 0) return {};
        std::lock_guard<std::mutex> lock(g_state_mutex);
        auto it = g_mesh_cache.find(asset_id);
        if (it == g_mesh_cache.end()) return {};
        return it->second;
    }

    std::shared_ptr<const parsed_mesh> get_mesh_for_part(const char* part_name, bool is_r15, uint64_t asset_id) {
        if (asset_id != 0) {
            auto m = get_mesh(asset_id);
            if (m && !m->vertices.empty() && !m->indices.empty()) return m;
        }

        static std::shared_ptr<parsed_mesh> default_head = create_procedural_head_mesh();
        static std::shared_ptr<parsed_mesh> default_beveled = create_procedural_beveled_box(0.04f);

        if (part_name && strcmp(part_name, "Head") == 0) {
            return default_head;
        }
        return default_beveled;
    }

    void request_avatar_assets(int64_t user_id) {
        if (user_id == 0) return;
        {
            std::lock_guard<std::mutex> lock(g_avatar_mutex);
            if (g_avatar_asset_cache.find(user_id) != g_avatar_asset_cache.end()) return;
            auto retry_it = g_avatar_retry_after.find(user_id);
            if (retry_it != g_avatar_retry_after.end()) {
                if (GetTickCount64() < retry_it->second) return;
                g_avatar_retry_after.erase(retry_it);
            }
            if (g_avatar_pending.find(user_id) != g_avatar_pending.end()) return;
            g_avatar_pending.insert(user_id);
        }
        HANDLE th = CreateThread(nullptr, 0, avatar_fetch_thread, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(user_id)), 0, nullptr);
        if (th)
            CloseHandle(th);
    }

    uint64_t get_default_asset_for_part(const char* part_name, bool is_r15) {
        return get_default_asset_for_part_impl(part_name, is_r15);
    }

    uint64_t get_mesh_asset_id_for_part(int64_t user_id, const char* part_name, bool is_r15) {
        if (user_id == 0 || !part_name) return 0;
        const char* asset_type = part_name_to_asset_type(part_name);
        if (!asset_type) return 0;
        std::lock_guard<std::mutex> lock(g_avatar_mutex);
        auto it = g_avatar_asset_cache.find(user_id);
        if (it == g_avatar_asset_cache.end()) return 0;
        auto type_it = it->second.find(asset_type);
        if (type_it != it->second.end()) return type_it->second;
        if (strcmp(part_name, "Head") == 0) {
            type_it = it->second.find("Head");
            if (type_it != it->second.end()) return type_it->second;
        }
        return 0;
    }

    debug_stats get_debug_stats() {
        debug_stats stats{};
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            stats.cache_count = g_mesh_cache.size();
            stats.pending_count = g_pending_assets.size();
            stats.queue_size = g_request_queue.size();
            stats.retry_after_count = g_retry_after.size();
            stats.fetch_ok = g_fetch_ok.load();
            stats.fetch_fail = g_fetch_fail.load();
            stats.fail_api = g_fail_api.load();
            stats.fail_download = g_fail_download.load();
            stats.fail_parse = g_fail_parse.load();
            strncpy_s(stats.last_error, g_last_error, sizeof(stats.last_error) - 1);
            stats.last_http_status = g_last_http_status.load();
            stats.last_failed_asset_id = g_last_failed_asset_id.load();
        }
        {
            std::lock_guard<std::mutex> lock(g_avatar_mutex);
            stats.avatar_cache_count = g_avatar_asset_cache.size();
            stats.avatar_pending_count = g_avatar_pending.size();
        }
        return stats;
    }

}
