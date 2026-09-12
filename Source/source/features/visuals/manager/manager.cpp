#include "manager.h"
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <sstream>
#include <iostream>
#include <stb/stb_image.h>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace visuals
{
    CAvatarManager& CAvatarManager::Get()
    {
        static CAvatarManager instance;
        return instance;
    }

    CAvatarManager::~CAvatarManager()
    {
        Clear();
    }

    void CAvatarManager::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_textures)
        {
            if (pair.second)
            {
                pair.second->Release();
            }
        }
        for (auto& pair : m_full_textures)
        {
            if (pair.second)
            {
                pair.second->Release();
            }
        }
        m_textures.clear();
        m_full_textures.clear();
        m_decoded_queue.clear();
        m_pending_requests.clear();
        m_pending_full_requests.clear();
        m_pending_usernames.clear();
    }

    bool CAvatarManager::HttpGet(const std::wstring& host, const std::wstring& path, std::string& out_data)
    {
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        WinHttpSetTimeouts(hRequest, 3000, 3000, 5000, 5000);

        bool success = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD bytes_available = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0)
            {
                std::vector<char> buffer(bytes_available);
                DWORD bytes_read = 0;
                if (WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0)
                {
                    out_data.append(buffer.data(), bytes_read);
                }
            }
            success = !out_data.empty();
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return success;
    }

    bool CAvatarManager::HttpGetUrl(const std::string& full_url, std::vector<uint8_t>& out_bytes)
    {
        std::string url = full_url;
        if (url.rfind("https://", 0) == 0)
            url = url.substr(8);
        else if (url.rfind("http://", 0) == 0)
            url = url.substr(7);

        size_t slash_pos = url.find('/');
        if (slash_pos == std::string::npos) return false;

        std::string host_str = url.substr(0, slash_pos);
        std::string path_str = url.substr(slash_pos);

        std::wstring host(host_str.begin(), host_str.end());
        std::wstring path(path_str.begin(), path_str.end());

        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        WinHttpSetTimeouts(hRequest, 5000, 5000, 10000, 10000);

        bool success = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD bytes_available = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0)
            {
                std::vector<uint8_t> buffer(bytes_available);
                DWORD bytes_read = 0;
                if (WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0)
                {
                    out_bytes.insert(out_bytes.end(), buffer.data(), buffer.data() + bytes_read);
                }
            }
            success = !out_bytes.empty();
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return success;
    }

    std::uint64_t CAvatarManager::LookupUserIdByUsername(const std::string& username)
    {
        std::string json_payload = "{\"usernames\":[\"" + username + "\"],\"excludeBannedUsers\":true}";

        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return 0;

        HINTERNET hConnect = WinHttpConnect(hSession, L"users.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return 0;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v1/usernames/users", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return 0;
        }

        WinHttpSetTimeouts(hRequest, 3000, 3000, 5000, 5000);

        LPCWSTR headers = L"Content-Type: application/json\r\n";
        std::string response_data;

        if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L, (LPVOID)json_payload.data(), (DWORD)json_payload.size(), (DWORD)json_payload.size(), 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD bytes_available = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0)
            {
                std::vector<char> buffer(bytes_available);
                DWORD bytes_read = 0;
                if (WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0)
                {
                    response_data.append(buffer.data(), bytes_read);
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!response_data.empty())
        {
            try
            {
                json parsed = json::parse(response_data);
                if (parsed.contains("data") && parsed["data"].is_array() && !parsed["data"].empty())
                {
                    if (parsed["data"][0].contains("id"))
                    {
                        return parsed["data"][0]["id"].get<std::uint64_t>();
                    }
                }
            }
            catch (...) {}
        }
        return 0;
    }

    void CAvatarManager::FetchWorker(std::uint64_t user_id, std::string username, bool is_full)
    {
        if (user_id == 0 && !username.empty())
        {
            user_id = LookupUserIdByUsername(username);
            if (user_id != 0)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_username_to_id[username] = user_id;
            }
        }

        if (user_id == 0) return;

        std::wstring path = is_full
            ? (L"/v1/users/avatar?userIds=" + std::to_wstring(user_id) + L"&size=420x420&format=Png&isCircular=false")
            : (L"/v1/users/avatar-headshot?userIds=" + std::to_wstring(user_id) + L"&size=150x150&format=Png&isCircular=false");

        std::string json_resp;
        if (!HttpGet(L"thumbnails.roblox.com", path, json_resp))
            return;

        std::string image_url;
        try
        {
            json parsed = json::parse(json_resp);
            if (parsed.contains("data") && parsed["data"].is_array() && !parsed["data"].empty())
            {
                if (parsed["data"][0].contains("imageUrl"))
                {
                    image_url = parsed["data"][0]["imageUrl"].get<std::string>();
                }
            }
        }
        catch (...)
        {
            return;
        }

        if (image_url.empty()) return;

        std::vector<uint8_t> png_bytes;
        if (!HttpGetUrl(image_url, png_bytes) || png_bytes.empty())
            return;

        int width = 0, height = 0, channels = 0;
        unsigned char* decoded = stbi_load_from_memory(png_bytes.data(), (int)png_bytes.size(), &width, &height, &channels, 4);
        if (!decoded || width <= 0 || height <= 0)
            return;

        DecodedAvatarImage item;
        item.user_id = user_id;
        item.is_full = is_full;
        item.width = width;
        item.height = height;
        item.rgba_data.assign(decoded, decoded + (width * height * 4));
        stbi_image_free(decoded);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_decoded_queue.push_back(std::move(item));
        }
    }

    void CAvatarManager::ProcessPendingTextures(ID3D11Device* device)
    {
        if (!device) return;

        std::vector<DecodedAvatarImage> to_process;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_decoded_queue.empty()) return;
            to_process.swap(m_decoded_queue);
        }

        for (auto& item : to_process)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = item.width;
            desc.Height = item.height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA subResource{};
            subResource.pSysMem = item.rgba_data.data();
            subResource.SysMemPitch = item.width * 4;

            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&desc, &subResource, &texture)))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;

                ID3D11ShaderResourceView* srv = nullptr;
                if (SUCCEEDED(device->CreateShaderResourceView(texture, &srvDesc, &srv)))
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (item.is_full)
                    {
                        if (m_full_textures.find(item.user_id) != m_full_textures.end() && m_full_textures[item.user_id])
                        {
                            m_full_textures[item.user_id]->Release();
                        }
                        m_full_textures[item.user_id] = srv;
                    }
                    else
                    {
                        if (m_textures.find(item.user_id) != m_textures.end() && m_textures[item.user_id])
                        {
                            m_textures[item.user_id]->Release();
                        }
                        m_textures[item.user_id] = srv;
                    }
                }
                texture->Release();
            }
        }
    }

    void CAvatarManager::RequestAvatar(std::uint64_t user_id, const std::string& username)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (user_id != 0)
        {
            if (m_textures.find(user_id) != m_textures.end() || m_pending_requests.find(user_id) != m_pending_requests.end())
                return;

            m_pending_requests.insert(user_id);
            std::thread([this, user_id, username]() {
                FetchWorker(user_id, username, false);
            }).detach();
        }
        else if (!username.empty())
        {
            auto it = m_username_to_id.find(username);
            if (it != m_username_to_id.end())
            {
                RequestAvatar(it->second, username);
                return;
            }

            if (m_pending_usernames.find(username) != m_pending_usernames.end())
                return;

            m_pending_usernames.insert(username);
            std::thread([this, username]() {
                FetchWorker(0, username, false);
            }).detach();
        }
    }

    void CAvatarManager::RequestFullAvatar(std::uint64_t user_id, const std::string& username)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (user_id != 0)
        {
            if (m_full_textures.find(user_id) != m_full_textures.end() || m_pending_full_requests.find(user_id) != m_pending_full_requests.end())
                return;

            m_pending_full_requests.insert(user_id);
            std::thread([this, user_id, username]() {
                FetchWorker(user_id, username, true);
            }).detach();
        }
        else if (!username.empty())
        {
            auto it = m_username_to_id.find(username);
            if (it != m_username_to_id.end())
            {
                RequestFullAvatar(it->second, username);
                return;
            }

            if (m_pending_usernames.find(username) != m_pending_usernames.end())
                return;

            m_pending_usernames.insert(username);
            std::thread([this, username]() {
                FetchWorker(0, username, true);
            }).detach();
        }
    }

    ID3D11ShaderResourceView* CAvatarManager::GetAvatar(ID3D11Device* device, std::uint64_t user_id, const std::string& username)
    {
        ProcessPendingTextures(device);

        std::lock_guard<std::mutex> lock(m_mutex);

        if (user_id != 0)
        {
            auto it = m_textures.find(user_id);
            if (it != m_textures.end())
            {
                return it->second;
            }
        }
        else if (!username.empty())
        {
            auto it_uid = m_username_to_id.find(username);
            if (it_uid != m_username_to_id.end())
            {
                auto it = m_textures.find(it_uid->second);
                if (it != m_textures.end())
                {
                    return it->second;
                }
            }
        }

        if (user_id != 0 || !username.empty())
        {
            m_mutex.unlock();
            try { RequestAvatar(user_id, username); }
            catch (...) { }
            m_mutex.lock();
        }

        return nullptr;
    }

    ID3D11ShaderResourceView* CAvatarManager::GetFullAvatar(ID3D11Device* device, std::uint64_t user_id, const std::string& username)
    {
        ProcessPendingTextures(device);

        std::lock_guard<std::mutex> lock(m_mutex);

        if (user_id != 0)
        {
            auto it = m_full_textures.find(user_id);
            if (it != m_full_textures.end())
            {
                return it->second;
            }
        }
        else if (!username.empty())
        {
            auto it_uid = m_username_to_id.find(username);
            if (it_uid != m_username_to_id.end())
            {
                auto it = m_full_textures.find(it_uid->second);
                if (it != m_full_textures.end())
                {
                    return it->second;
                }
            }
        }

        if (user_id != 0 || !username.empty())
        {
            m_mutex.unlock();
            try { RequestFullAvatar(user_id, username); }
            catch (...) { }
            m_mutex.lock();
        }

        return nullptr;
    }
}
