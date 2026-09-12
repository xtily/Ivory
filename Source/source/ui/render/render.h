#pragma once
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>

#include <d3d11.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>

inline ImFont* esp_font = nullptr;
inline ImFont* esp_font_tahoma = nullptr;
inline ImFont* esp_font_smallest_pixel = nullptr;
inline ImFont* esp_font_undefeated = nullptr;

inline bool g_watermark_is_dragging = false;
inline bool g_keybind_list_is_dragging = false;
inline bool g_target_hub_is_dragging = false;

struct detail_t {
	HWND window = nullptr;
	WNDCLASSEX window_class = {};
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* device_context = nullptr;
	ID3D11RenderTargetView* render_target_view = nullptr;
	IDXGISwapChain* swap_chain = nullptr;
};

struct menu_theme_t {
	float menu_color[4]           = { 204 / 255.f, 105 / 255.f, 94 / 255.f, 255 / 255.f };
	float menu_color_secondary[4] = {  29 / 255.f,  33 / 255.f,  39 / 255.f, 255 / 255.f };
	float menu_color_text[4]      = { 255 / 255.f, 255 / 255.f, 255 / 255.f, 255 / 255.f };
	float menu_color_border[4]    = {  48 / 255.f,  48 / 255.f,  48 / 255.f, 255 / 255.f };
	float menu_window_rounding    = 0.f;
	float menu_frame_rounding     = 0.f;
	float menu_scrollbar_size     = 6.f;

	bool  line_shine_enabled      = true;
	float line_shine_speed        = 1.0f;
	float line_shine_width        = 80.0f;
	float line_shine_color[4]     = { 204 / 255.f, 105 / 255.f, 94 / 255.f, 220 / 255.f };
	bool  show_watermark          = true;
};

class render_t {
public:
	render_t();
	~render_t();

	std::atomic<bool> running = false;
	bool menu_open = false;
	bool main_window_open = false;
	bool theme_window_open = false;
	int current_tab = 0;
	int theme_tab = 0;
	menu_theme_t theme;

	float menu_alpha = 0.0f;
	std::chrono::steady_clock::time_point menu_fade_start;
	static constexpr float menu_fade_duration = 0.3f;

	void start_render();
	void render_menu();
	void render_visuals();
	void end_render();
	void render_keybind_list();
	void render_target_hub();
	void render_watermark();
	void render_topbar();


	bool create_device();
	bool create_window();
	bool create_imgui();

	std::unique_ptr<detail_t> detail = std::make_unique<detail_t>();
private:
	void destroy_device();
	void destroy_window();
	void destroy_imgui();
};

void RenderWatermark(const char* menu_name = "MyClient", float pos_x = 10.0f, float pos_y = 10.0f, ImU32 accent_color = IM_COL32(18, 128, 224, 255), ImU32 bg_color = IM_COL32(30, 30, 35, 255));
void RenderKeybindList(float pos_x = 10.0f, float pos_y = 50.0f, ImU32 accent_color = IM_COL32(18, 128, 224, 255), ImU32 bg_color = IM_COL32(30, 30, 35, 255));
void RenderTargetHub(float pos_x = 10.0f, float pos_y = 100.0f, ImU32 accent_color = IM_COL32(18, 128, 224, 255), ImU32 bg_color = IM_COL32(30, 30, 35, 255));

inline render_t* render = new render_t;