#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>

namespace visuals
{
    struct DecodedAvatarImage
    {
        std::uint64_t user_id = 0;
        bool is_full = false;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba_data;
    };

    class CAvatarManager
    {
    public:
        static CAvatarManager& Get();

        ~CAvatarManager();

        ID3D11ShaderResourceView* GetAvatar(ID3D11Device* device, std::uint64_t user_id, const std::string& username = "");
        ID3D11ShaderResourceView* GetFullAvatar(ID3D11Device* device, std::uint64_t user_id, const std::string& username = "");
        void RequestAvatar(std::uint64_t user_id, const std::string& username = "");
        void RequestFullAvatar(std::uint64_t user_id, const std::string& username = "");
        void ProcessPendingTextures(ID3D11Device* device);
        void Clear();

    private:
        CAvatarManager() = default;

        void FetchWorker(std::uint64_t user_id, std::string username, bool is_full);
        static bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& out_data);
        static bool HttpGetUrl(const std::string& full_url, std::vector<uint8_t>& out_bytes);
        static std::uint64_t LookupUserIdByUsername(const std::string& username);

        std::mutex m_mutex;
        std::unordered_map<std::uint64_t, ID3D11ShaderResourceView*> m_textures;
        std::unordered_map<std::uint64_t, ID3D11ShaderResourceView*> m_full_textures;
        std::unordered_map<std::string, std::uint64_t> m_username_to_id;
        std::unordered_set<std::uint64_t> m_pending_requests;
        std::unordered_set<std::uint64_t> m_pending_full_requests;
        std::unordered_set<std::string> m_pending_usernames;
        std::vector<DecodedAvatarImage> m_decoded_queue;
    };
}
