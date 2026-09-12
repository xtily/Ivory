#include "serverbrowser.h"
#include <sdk/game/game.h>
#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>
#include <features/system/notifications/notifications.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <thread>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace serverbrowser
{
    static std::string safe_json_string(const json& j, const std::string& key)
    {
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return "";
    }

    static int safe_json_int(const json& j, const std::string& key)
    {
        if (j.contains(key) && j[key].is_number())
            return j[key].get<int>();
        return 0;
    }

    static float safe_json_float(const json& j, const std::string& key)
    {
        if (j.contains(key) && j[key].is_number())
            return j[key].get<float>();
        return 0.0f;
    }

    static uint64_t safe_json_uint64(const json& j, const std::string& key)
    {
        if (j.contains(key) && j[key].is_number())
            return j[key].get<uint64_t>();
        return 0ULL;
    }

    c_server_browser::c_server_browser()
    {
        refresh_current_game();
    }

    void c_server_browser::refresh_current_game()
    {
        if (game::datamodel && game::datamodel->address >= 0x10000)
        {
            active_place_id = game::datamodel->get_place_id();
            active_job_id = game::datamodel->get_job_id();
            if (active_job_id.empty())
            {
                for (uintptr_t off = 0x100; off <= 0x140; off += 8)
                {
                    std::string s = memory->read_string(game::datamodel->address + off);
                    if (s.length() >= 32 && s.find('-') != std::string::npos)
                    {
                        active_job_id = s;
                        break;
                    }
                }
            }

            if (active_place_id > 0)
            {
                std::string pid_str = std::to_string(active_place_id);
                strncpy_s(input_place_id, pid_str.c_str(), sizeof(input_place_id) - 1);
            }
        }
    }

    uint64_t c_server_browser::get_target_place_id()
    {
        std::string raw = input_place_id;
        std::string digits;
        for (char c : raw)
        {
            if (isdigit((unsigned char)c))
                digits += c;
        }
        if (!digits.empty())
        {
            try {
                return std::stoull(digits);
            } catch (...) {}
        }
        if (active_place_id > 0)
            return active_place_id;
        return 0;
    }

    static std::string http_get_request(const std::wstring& host, const std::wstring& path)
    {
        std::string response_data;
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession)
            return "";

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return "";
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        DWORD timeout = 10000;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        LPCWSTR headers = L"Accept: application/json\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n";
        WinHttpAddRequestHeaders(hRequest, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL))
        {
            char buffer[8192];
            DWORD bytes_read = 0;
            while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytes_read) && bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                response_data.append(buffer, bytes_read);
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response_data;
    }

    void c_server_browser::fetch_servers_async(uint64_t place_id, const std::string& cursor)
    {
        if (place_id == 0)
        {
            status_message = "Invalid Place ID";
            return;
        }

        if (is_loading)
            return;

        is_loading = true;
        status_message = "Fetching servers for Place " + std::to_string(place_id) + "...";

        std::thread([this, place_id, cursor]() {
            std::wstring host = L"games.roblox.com";
            std::wstringstream wss;
            wss << L"/v1/games/" << place_id << L"/servers/Public?limit=100";
            if (!cursor.empty())
            {
                std::wstring wcursor(cursor.begin(), cursor.end());
                wss << L"&cursor=" << wcursor;
            }

            std::string res = http_get_request(host, wss.str());

            if (res.find("The place is invalid") != std::string::npos)
            {
                std::wstringstream u_wss;
                u_wss << L"/v1/games?universeIds=" << place_id;
                std::string u_res = http_get_request(host, u_wss.str());
                if (!u_res.empty())
                {
                    try {
                        json u_root = json::parse(u_res);
                        if (u_root.contains("data") && u_root["data"].is_array() && !u_root["data"].empty())
                        {
                            uint64_t resolved_place_id = safe_json_uint64(u_root["data"][0], "rootPlaceId");
                            if (resolved_place_id > 0)
                            {
                                std::wstringstream new_wss;
                                new_wss << L"/v1/games/" << resolved_place_id << L"/servers/Public?limit=100";
                                if (!cursor.empty())
                                {
                                    std::wstring wcursor(cursor.begin(), cursor.end());
                                    new_wss << L"&cursor=" << wcursor;
                                }
                                res = http_get_request(host, new_wss.str());
                                {
                                    std::string pid_str = std::to_string(resolved_place_id);
                                    strncpy_s(input_place_id, pid_str.c_str(), sizeof(input_place_id) - 1);
                                    active_place_id = resolved_place_id;
                                }
                            }
                        }
                    } catch (...) {}
                }
            }

            std::lock_guard<std::mutex> lock(mtx);
            is_loading = false;

            if (res.empty())
            {
                status_message = "Failed to query servers (Timeout/Network)";
                return;
            }

            try
            {
                json root = json::parse(res);
                if (root.contains("data") && root["data"].is_array())
                {
                    all_servers.clear();
                    next_cursor = safe_json_string(root, "nextPageCursor");
                    prev_cursor = safe_json_string(root, "previousPageCursor");

                    for (const auto& item : root["data"])
                    {
                        server_entry_t entry;
                        entry.id = safe_json_string(item, "id");
                        entry.max_players = safe_json_int(item, "maxPlayers");
                        entry.playing = safe_json_int(item, "playing");
                        entry.ping = safe_json_int(item, "ping");
                        entry.fps = safe_json_float(item, "fps");
                        all_servers.push_back(entry);
                    }

                    status_message = "Found " + std::to_string(all_servers.size()) + " servers";
                    apply_filter();
                }
                else if (root.contains("errors") && root["errors"].is_array() && !root["errors"].empty())
                {
                    std::string err = safe_json_string(root["errors"][0], "message");
                    if (err.empty()) err = "Unknown error";
                    status_message = "API Error: " + err;
                }
                else
                {
                    status_message = "No servers returned";
                }
            }
            catch (const std::exception& e)
            {
                status_message = std::string("Parse error: ") + e.what();
            }
        }).detach();
    }

    void c_server_browser::apply_filter()
    {
        filtered_servers.clear();
        std::string query = search_filter;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);

        for (const auto& s : all_servers)
        {
            if (exclude_full && s.playing >= s.max_players && s.max_players > 0)
                continue;

            if (!query.empty())
            {
                std::string s_id = s.id;
                std::transform(s_id.begin(), s_id.end(), s_id.begin(), ::tolower);
                if (s_id.find(query) == std::string::npos)
                    continue;
            }

            filtered_servers.push_back(s);
        }

        switch (sort_mode)
        {
        case SORT_LOWEST_PLAYERS:
            std::sort(filtered_servers.begin(), filtered_servers.end(), [](const server_entry_t& a, const server_entry_t& b) {
                if (a.playing != b.playing)
                    return a.playing < b.playing;
                return a.ping < b.ping;
            });
            break;
        case SORT_HIGHEST_PLAYERS:
            std::sort(filtered_servers.begin(), filtered_servers.end(), [](const server_entry_t& a, const server_entry_t& b) {
                if (a.playing != b.playing)
                    return a.playing > b.playing;
                return a.ping < b.ping;
            });
            break;
        case SORT_LOWEST_PING:
            std::sort(filtered_servers.begin(), filtered_servers.end(), [](const server_entry_t& a, const server_entry_t& b) {
                if (a.ping != b.ping)
                    return a.ping < b.ping;
                return a.playing < b.playing;
            });
            break;
        case SORT_HIGHEST_FPS:
            std::sort(filtered_servers.begin(), filtered_servers.end(), [](const server_entry_t& a, const server_entry_t& b) {
                return a.fps > b.fps;
            });
            break;
        }
    }

    void c_server_browser::join_server(uint64_t place_id, const std::string& job_id)
    {
        if (place_id == 0 || job_id.empty())
        {
            notifications::add("Invalid place or job ID");
            return;
        }

        std::string uri = "roblox://experiences/start?placeId=" + std::to_string(place_id) + "&gameInstanceId=" + job_id;
        HINSTANCE res = ShellExecuteA(NULL, "open", uri.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)res > 32)
        {
            notifications::add("Connecting to server...");
        }
        else
        {
            notifications::add("Failed to launch Roblox URI");
        }
    }

    static void set_clipboard_text(const std::string& text)
    {
        if (!OpenClipboard(NULL))
            return;
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hGlob)
        {
            memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
        CloseClipboard();
    }

    void c_server_browser::copy_job_id(const std::string& job_id)
    {
        set_clipboard_text(job_id);
        notifications::add("Job ID copied to clipboard!");
    }

    void c_server_browser::copy_teleport_script(uint64_t place_id, const std::string& job_id)
    {
        std::string script = "game:GetService(\"TeleportService\"):TeleportToPlaceInstance(" +
            std::to_string(place_id) + ", \"" + job_id + "\", game.Players.LocalPlayer)";
        set_clipboard_text(script);
        notifications::add("Teleport code copied to clipboard!");
    }

    void c_server_browser::next_page()
    {
        if (!next_cursor.empty())
        {
            uint64_t pid = get_target_place_id();
            if (pid > 0)
            {
                cursor_history.push_back(next_cursor);
                current_page++;
                fetch_servers_async(pid, next_cursor);
            }
        }
    }

    void c_server_browser::prev_page()
    {
        if (current_page > 1 && !cursor_history.empty())
        {
            cursor_history.pop_back();
            current_page--;
            std::string cursor_to_use = cursor_history.empty() ? "" : cursor_history.back();
            uint64_t pid = get_target_place_id();
            if (pid > 0)
            {
                fetch_servers_async(pid, cursor_to_use);
            }
        }
    }
}