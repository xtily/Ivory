#include "mainapi.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stb/stb_image.h>
#include <chrono>
#include <algorithm>
#include <thread>
#include <fstream>

c_avatar_3d_api::~c_avatar_3d_api() {
    if (running_.load()) {
        running_.store(false);
        queue_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

void c_avatar_3d_api::initialize() {
    if (running_.load()) return;
    running_.store(true);
    worker_ = std::thread(&c_avatar_3d_api::worker_thread, this);

    c_texture_cache::get().initialize();

}

std::string c_avatar_3d_api::get_cdn_url(const std::string& hash) {
    if (hash.length() < 38) return "";

    int i = 31;
    for (int t = 0; t < 38 && t < static_cast<int>(hash.length()); t++) {
        i ^= static_cast<unsigned char>(hash[t]);
    }

    return "https://t" + std::to_string(i % 8) + ".rbxcdn.com/" + hash;
}

static size_t avatar_3d_curl_write(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::vector<unsigned char>* buffer = static_cast<std::vector<unsigned char>*>(userp);
    buffer->insert(buffer->end(),
        static_cast<unsigned char*>(contents),
        static_cast<unsigned char*>(contents) + total);
    return total;
}

std::vector<unsigned char> c_avatar_3d_api::http_get(const std::string& url, bool decompress, const std::vector<std::string>& extra_headers) {
    CURL* curl = curl_easy_init();
    std::vector<unsigned char> buffer;

    if (!curl) {
        return buffer;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, avatar_3d_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    if (decompress) {
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
        curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 1L);
    }

    struct curl_slist* headers = NULL;
    for (const auto& header : extra_headers) {
        headers = curl_slist_append(headers, header.c_str());
    }

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
    }

    if (headers) {
        curl_slist_free_all(headers);
    }

    curl_easy_cleanup(curl);
    return buffer;
}
static std::vector<unsigned char> decompress_data(const std::vector<unsigned char>& data) {
    if (data.size() < 10 || data[0] != 0x1F || data[1] != 0x8B) {
        return data;
    }

    if (data[2] != 8) {
        return {};
    }

    const unsigned char flags = data[3];
    size_t i = 10;
    if (flags & 0x04) { // FEXTRA
        if (i + 2 > data.size()) return {};
        const size_t xlen = static_cast<size_t>(data[i]) | (static_cast<size_t>(data[i + 1]) << 8);
        i += 2 + xlen;
    }
    if (flags & 0x08) { // FNAME
        while (i < data.size() && data[i] != 0) ++i;
        if (i >= data.size()) return {};
        ++i;
    }
    if (flags & 0x10) { // FCOMMENT
        while (i < data.size() && data[i] != 0) ++i;
        if (i >= data.size()) return {};
        ++i;
    }
    if (flags & 0x02) { // FHCRC
        if (i + 2 > data.size()) return {};
        i += 2;
    }
    if (i + 8 > data.size()) {
        return {};
    }

    const int deflate_len = static_cast<int>(data.size() - i - 8);
    if (deflate_len <= 0) {
        return {};
    }

    int outlen = 0;
    char* out = stbi_zlib_decode_noheader_malloc(
        reinterpret_cast<const char*>(data.data() + i),
        deflate_len,
        &outlen
    );

    if (!out || outlen <= 0) {
        if (out) std::free(out);
        return {};
    }

    std::vector<unsigned char> result(reinterpret_cast<unsigned char*>(out), reinterpret_cast<unsigned char*>(out) + outlen);
    std::free(out);
    return result;
}

bool c_avatar_3d_api::fetch_model_json(const std::string& url, nlohmann::json& json) {
    std::vector<unsigned char> data = http_get(url, true);
    if (data.empty()) {
        return false;
    }

    std::vector<unsigned char> decompressed = decompress_data(data);
    std::string content(decompressed.begin(), decompressed.end());

    if (content.empty() || content[0] != '{') {
        return false;
    }

    try {
        json = nlohmann::json::parse(content);
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

bool c_avatar_3d_api::fetch_api_data(const std::string& user_id, c_avatar_3d_data& data) {
    std::ofstream log("C:\\Users\\8\\AppData\\Local\\Roblox\\avatar_debug.log", std::ios::app);
    log << "[INFO] fetch_api_data started for user: " << user_id << "\n";
    data.target_id = user_id;

    std::string cb = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    std::string api_url = "https://rblxapi.vercel.app/api/avatar?userId=" + user_id + "&cb=" + cb;
    std::vector<std::string> extra_headers = {
        "x-api-key: bW57XpB71S8SlnRPaMVVqxpB4UlW65WE"
    };
    std::vector<unsigned char> response = http_get(api_url, false, extra_headers);
    log << "[INFO] Vercel API response size: " << response.size() << "\n";
    if (!response.empty()) {
        log << "[INFO] Vercel API response: " << std::string(response.begin(), response.end()) << "\n";
    }

    bool use_fallback = response.empty();
    if (!use_fallback) {
        std::string res_str(response.begin(), response.end());
        if (res_str.find("errors") != std::string::npos || res_str.find("error") != std::string::npos) {
            use_fallback = true;
        }
    }

    if (use_fallback) {
        log << "[WARNING] Vercel API failed or empty, calling Roblox thumbnails API fallback...\n";
        api_url = "https://thumbnails.roblox.com/v1/users/avatar-3d?userId=" + user_id + "&cb=" + cb;
        std::vector<std::string> roblox_headers = {
            "x-api-key: 7vXmDkJ/QUCSWxB7niwa0Eu2rdTZ1be+mQAUwRb6/tGmt6bPZXlKaGJHY2lPaUpTVXpJMU5pSXNJbXRwWkNJNkluTnBaeTB5TURJeExUQTNMVEV6VkRFNE9qVXhPalE1W2lJ0luUjVjQ0k2SWtwWFZDSjkuZXlKaGRXUWlPaUpTYjJKc2IzaEpiblJsY201aGJDSXNJbWx6Y3lJNklrTnNiM1ZrUVhWMGFHVnVkR2xqWVhScGIyNVRaWEoyYVdObElpd2lZbUZ6WlVGd2FVdGxlU0k2SWpkMldHMUVhMG92VVZWRFUxZDRRamR1YVhkaE1FVjFNbkprVkZveFltVXJiVkZCVlhkU1lqWXZkRWR0ZERaaVVDSXNJbTkzYm1WeVNXUWlPaUk0TmpnMU5UazFOemM1SWl3aVpYaHdJam94TnpnM09UazRNRFl3TENKcFlYUWlPakUzT0RjNU9UUTBOakFzSW01aVppSTZNVGM0TnprNU5EUTJNSDAuZHlzWVpRcUpNWFNNaEZkSS1oMDlUSXQ2Z3c4MTdZT0oyeURuT0VIUWpRUHByaV95VW5iUWc4Rm5fTm9uOHcyRkdUcHgxTDZPZ3NDUURpZXExWm5yTjVPclkyQXlsWGlvVnlBdHpjT181WWhJRG02a2o2MzdqYW1MNGRzSkdLSUZWTGJVQWJfcTNsajVYci1FTFlJSFdrLVVvWkRfbU5tVGhEdnZDLV9ReUY0cjFGOFdRd18ydkhVMHpOb05jbmtwMko4N3Q2SE1zSFdlb0FCZUxWM0RUUUpyanE1cWZtQnlHUHVzVFZXbFktYzQzdFR5b3JacXE2ZmVrN1FyU3ItWGlxT2x6UVRnYVdkZTNFRnZFWEpiMXlWc0c2Z0FOYTAxb3RObVNVRkpJTU9lSURfWWlQZ3VYWldiVEg5eTBvenlaTldlZk1kS1Y4Z29SSHFoVnFJWHF3"
        };
        response = http_get(api_url, false, roblox_headers);
        log << "[INFO] Roblox API response size: " << response.size() << "\n";
        if (!response.empty()) {
            log << "[INFO] Roblox API response: " << std::string(response.begin(), response.end()) << "\n";
        }
        if (response.empty()) {
            log << "[ERROR] Fallback response empty!\n";
            return false;
        }
    }

    std::string json_str(response.begin(), response.end());
    if (json_str.empty()) {
        return false;
    }

    try {
        nlohmann::json json = nlohmann::json::parse(json_str);

        std::string image_url = "";
        std::string state = "";

        if (json.contains("imageUrl")) {
            image_url = json.value("imageUrl", "");
            state = json.value("state", "");
        }
        else if (json.contains("data") && json["data"].is_object()) {
            auto& data_json = json["data"];
            image_url = data_json.value("imageUrl", "");
            state = data_json.value("state", "");
        }

        nlohmann::json model_json;
        if (json.contains("model")) {
            model_json = json["model"];
        }
        else if (json.contains("data") && json["data"].contains("model")) {
            model_json = json["data"]["model"];
        }
        else if (!image_url.empty()) {
            if (!fetch_model_json(image_url, model_json)) {
                return false;
            }
        }
        else {
            return false;
        }

        data.state = "Completed";
        data.image_url = image_url;

        if (model_json.contains("camera")) {
            auto& cam = model_json["camera"];
            if (cam.contains("position")) {
                data.camera.position[0] = cam["position"].value("x", 0.0f);
                data.camera.position[1] = cam["position"].value("y", 0.0f);
                data.camera.position[2] = cam["position"].value("z", 0.0f);
            }
            if (cam.contains("direction")) {
                data.camera.direction[0] = cam["direction"].value("x", 0.0f);
                data.camera.direction[1] = cam["direction"].value("y", 0.0f);
                data.camera.direction[2] = cam["direction"].value("z", -1.0f);
            }
            data.camera.fov = cam.value("fov", 70.0f);
        }

        if (model_json.contains("aabb")) {
            auto& aabb = model_json["aabb"];
            if (aabb.contains("min")) {
                data.aabb.min[0] = aabb["min"].value("x", 0.0f);
                data.aabb.min[1] = aabb["min"].value("y", 0.0f);
                data.aabb.min[2] = aabb["min"].value("z", 0.0f);
            }
            if (aabb.contains("max")) {
                data.aabb.max[0] = aabb["max"].value("x", 0.0f);
                data.aabb.max[1] = aabb["max"].value("y", 0.0f);
                data.aabb.max[2] = aabb["max"].value("z", 0.0f);
            }
        }

        data.mtl_hash = model_json.value("mtl", model_json.value("mtlUrl", ""));
        data.obj_hash = model_json.value("obj", model_json.value("objUrl", ""));

        data.texture_info.clear();
        data.texture_hashes.clear();

        if (model_json.contains("textures") && model_json["textures"].is_array()) {
            for (const auto& tex : model_json["textures"]) {
                if (tex.is_string()) {
                    data.texture_hashes.push_back(tex.get<std::string>());
                }
                else if (tex.is_object()) {
                    std::string id = tex.value("id", "");
                    std::string url = tex.value("url", "");
                    if (!id.empty() && !url.empty()) {
                        data.texture_info.push_back({ id, url });
                    }
                }
            }
        }

        return !data.obj_hash.empty();
    }
    catch (const std::exception& e) {
        return false;
    }
}

void c_avatar_3d_api::load_files(c_avatar_3d_data& data) {
    std::ofstream log("C:\\Users\\8\\AppData\\Local\\Roblox\\avatar_debug.log", std::ios::app);
    log << "[INFO] load_files started\n";
    if (data.mtl_hash.empty() || data.obj_hash.empty()) {
        log << "[ERROR] mtl_hash or obj_hash empty! mtl: " << data.mtl_hash << ", obj: " << data.obj_hash << "\n";
        return;
    }

    std::string mtl_url = data.mtl_hash.rfind("http", 0) == 0 ? data.mtl_hash : get_cdn_url(data.mtl_hash);
    std::string obj_url = data.obj_hash.rfind("http", 0) == 0 ? data.obj_hash : get_cdn_url(data.obj_hash);
    log << "[INFO] Downloading assets: mtl_url: " << mtl_url << ", obj_url: " << obj_url << "\n";

    data.mtl_data = http_get(mtl_url, true);
    data.obj_data = http_get(obj_url, true);
    log << "[INFO] Downloaded: mtl_data size: " << data.mtl_data.size() << ", obj_data size: " << data.obj_data.size() << "\n";

    if (!data.texture_info.empty()) {
        log << "[INFO] texture_info is not empty (size: " << data.texture_info.size() << ")\n";
        if (!data.mtl_data.empty()) {
            std::string mtl_str(data.mtl_data.begin(), data.mtl_data.end());
            for (auto& [id, url] : data.texture_info) {
                size_t pos = 0;
                while ((pos = mtl_str.find(id, pos)) != std::string::npos) {
                    mtl_str.replace(pos, id.size(), url);
                    pos += url.size();
                }
            }
            data.mtl_data.assign(mtl_str.begin(), mtl_str.end());
        }

        data.texture_data.clear();
        data.texture_data.reserve(data.texture_info.size());
        for (const auto& [id, url] : data.texture_info) {
            data.texture_data.push_back(http_get(url));
        }

        data.texture_hashes.clear();
        for (const auto& [id, url] : data.texture_info) {
            data.texture_hashes.push_back(url);
        }
    }
    else {
        log << "[INFO] texture_hashes size: " << data.texture_hashes.size() << "\n";
        data.texture_data.clear();
        data.texture_data.reserve(data.texture_hashes.size());
        for (const auto& hash : data.texture_hashes) {
            data.texture_data.push_back(http_get(get_cdn_url(hash)));
        }

        if (!data.face_texture_hash.empty()) {
            data.face_texture_data = http_get(get_cdn_url(data.face_texture_hash));
        }
    }

    data.ready = !data.mtl_data.empty() && !data.obj_data.empty();
    log << "[INFO] load_files finished, ready: " << data.ready << "\n";
    if (data.ready) {
        for (size_t i = 0; i < data.texture_data.size(); ++i) {
            request_texture_decode(data.target_id, static_cast<int>(i), data.texture_data[i]);
        }
    }
}

void c_avatar_3d_api::process_user(const std::string& user_id) {
    std::unordered_map<std::string, int> retry_counts;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> retry_times;

    while (running_.load()) {
        c_avatar_3d_data data;

        if (!fetch_api_data(user_id, data)) {
            int retry_count = retry_counts[user_id];
            auto now = std::chrono::steady_clock::now();

            if (retry_count < max_retries) {
                if (retry_times.find(user_id) == retry_times.end() ||
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - retry_times[user_id]).count() >= retry_delay_ms) {
                    retry_counts[user_id]++;
                    retry_times[user_id] = now;
                    continue;
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
            }

            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto it = cache_.find(user_id);
                if (it != cache_.end()) {
                    it->second.state = e_avatar_3d_load_state::failed;
                }
            }
            return;
        }

        load_files(data);

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(user_id);
            if (it != cache_.end()) {
                it->second.data = std::move(data);
                it->second.state = it->second.data.ready ? e_avatar_3d_load_state::loaded : e_avatar_3d_load_state::failed;
                it->second.last_update = std::chrono::steady_clock::now();
            }
        }

        return;
    }
}

void c_avatar_3d_api::worker_thread() {

    while (running_.load()) {
        std::string user_id;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load();
                });

            if (!running_.load() && queue_.empty()) {
                break;
            }

            if (!queue_.empty()) {
                user_id = std::move(queue_.front());
                queue_.pop();
            }
        }

        if (!user_id.empty()) {
            process_user(user_id);
        }
    }

}

c_avatar_3d_data* c_avatar_3d_api::request_data(const std::string& user_id) {
    if (user_id.empty()) return nullptr;
    if (!running_.load()) {
        initialize();
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(user_id);
        if (it != cache_.end()) {
            if (it->second.state == e_avatar_3d_load_state::loaded) {
                return &it->second.data;
            }
            if (it->second.state == e_avatar_3d_load_state::loading) {
                return nullptr;
            }
        }
        else {
            c_avatar_3d_cache_entry entry;
            entry.state = e_avatar_3d_load_state::loading;
            entry.last_update = std::chrono::steady_clock::now();
            cache_[user_id] = entry;
        }
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        bool already_queued = false;
        std::queue<std::string> temp = queue_;
        while (!temp.empty()) {
            if (temp.front() == user_id) {
                already_queued = true;
                break;
            }
            temp.pop();
        }

        if (!already_queued) {
            queue_.push(user_id);
        }
    }

    queue_cv_.notify_one();

    if (!running_.load()) {
        initialize();
    }

    return nullptr;
}

e_avatar_3d_load_state c_avatar_3d_api::get_state(const std::string& user_id) {
    if (user_id.empty()) return e_avatar_3d_load_state::not_loaded;

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(user_id);
    return (it != cache_.end()) ? it->second.state : e_avatar_3d_load_state::not_loaded;
}

void c_avatar_3d_api::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}

void c_avatar_3d_api::clear_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.erase(user_id);
    c_texture_cache::get().clear_user(user_id);

}

bool c_avatar_3d_api::parse_obj_model(const std::vector<unsigned char>& obj_data, c_obj_model& model) {
    return parse_obj(obj_data, model);
}

bool c_avatar_3d_api::parse_mtl_data(const std::vector<unsigned char>& mtl_data, c_obj_model& model, const std::vector<std::string>& texture_hashes) {
    return parse_mtl(mtl_data, model, texture_hashes);
}

void c_avatar_3d_api::request_texture_decode(const std::string& user_id, int texture_index, const std::vector<unsigned char>& data, bool high_priority) {
    c_texture_cache::get().request_texture(user_id, texture_index, data, high_priority);
}

void c_avatar_3d_api::request_face_texture_decode(const std::string& user_id, const std::vector<unsigned char>& data) {
    c_texture_cache::get().request_face_texture(user_id, data);
}

c_decoded_texture* c_avatar_3d_api::get_decoded_texture(const std::string& user_id, int texture_index) {
    return c_texture_cache::get().get_texture(user_id, texture_index);
}

c_decoded_texture* c_avatar_3d_api::get_decoded_face_texture(const std::string& user_id) {
    return c_texture_cache::get().get_face_texture(user_id);
}