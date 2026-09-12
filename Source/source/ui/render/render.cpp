#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/render/render.h>

#include <ui/menu/settings/functions.h>
#include <ui/menu/data/fonts.h>
#include <misc/imgui_freetype.h>


#include <dwmapi.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <cstdio>
#include <chrono>
#include <thread>
#include <map>
#include <filesystem>

#include <ui/assets/fonts/fontexo2medium.h>
#include <ui/assets/icons/tabicon.h>
#include <ui/assets/fonts/orokfonts.h>
#include <ui/assets/fonts/smallestpixel7.h>
#include <ui/assets/icons/cursorbytes.h>

#include <core/globals.h>
#include <features/visuals/esp/esp.h>
#include <features/visuals/chams/chams.h>
#include <core/scanner/rescan.h>
#include <features/system/settings/settings.h>
#include <features/combat/aimbot/aimbot.h>
#include <features/combat/silent/mouse/mouse.h>
#include <features/system/keybind/keybind.h>
#include <features/system/config/config.h>
#include <features/system/playerlist/playerlist.h>
#include <features/exploits/environment/explorer/explorer.h>
#include <features/exploits/utility/misc/misc.h>
#include "sdk/math/math.h"
#include <sdk/game/game.h>
#include <core/logger/logger.h>

#include <features/combat/silent/mouse/mouse.h>
#include <features/exploits/audio/hitsound/hitsounds.h>
#include <features/combat/silent/raycast/raycast.h>
#include <features/visuals/hit/hit.h>
#include <features/system/notifications/notifications.h>
#include <features/system/waypoints/waypoints.h>
#include <sdk/cache/core/frame.h>
#include <ui/loader/loader.h>
#include <ui/menu/Menu.h>
#include <ui/menu/assets/nav_icons.h>
#include <ui/menu/assets/nav_bytes.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#define MENU_TITLE      "Ivory"
#define BUILD_DATE      __DATE__

#include <vector>
#include <d3d11.h>
#include <core/protection/xorstr.h>

static ID3D11ShaderResourceView* menu_icon_srv = nullptr;
static ID3D11ShaderResourceView* g_custom_cursor_srv = nullptr;

static bool InitCustomCursorTexture(ID3D11Device* device)
{
    if (g_custom_cursor_srv)
        return true;
    if (!device)
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = c_cursor_asset::width;
    desc.Height = c_cursor_asset::height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = c_cursor_asset::pixels;
    init_data.SysMemPitch = c_cursor_asset::width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init_data, &texture);
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(texture, &srv_desc, &g_custom_cursor_srv);
    texture->Release();

    return SUCCEEDED(hr);
}

static void draw_custom_cursor()
{
    if (!var->gui.custom_cursor)
        return;
    if (!var->gui.menu_opened || var->gui.menu_alpha <= 0.01f)
        return;

    if (!g_custom_cursor_srv && render && render->detail && render->detail->device)
        InitCustomCursorTexture(render->detail->device);

    if (!g_custom_cursor_srv)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid(&io.MousePos))
        return;

    ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    const float dpi_scale = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
    const float target_height = 14.0f * dpi_scale;
    float scale = (static_cast<float>(c_cursor_asset::height) > 0.0f) ? (target_height / static_cast<float>(c_cursor_asset::height)) : 1.0f;
    scale = ImClamp(scale, 0.014f, 1.5f);
    ImVec2 cursor_size = ImVec2(static_cast<float>(c_cursor_asset::width) * scale, static_cast<float>(c_cursor_asset::height) * scale);
    ImVec2 cursor_min = io.MousePos;
    ImVec2 cursor_max = ImVec2(cursor_min.x + cursor_size.x, cursor_min.y + cursor_size.y);

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    const ImVec2 shadow_offset(1.0f, -2.0f);
    ImVec2 shadow_min = ImVec2(cursor_min.x + shadow_offset.x, cursor_min.y + shadow_offset.y);
    ImVec2 shadow_max = ImVec2(shadow_min.x + cursor_size.x, shadow_min.y + cursor_size.y);

    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(g_custom_cursor_srv),
        shadow_min,
        shadow_max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f * var->gui.menu_alpha))
    );

    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(g_custom_cursor_srv),
        cursor_min,
        cursor_max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, var->gui.menu_alpha))
    );
}

static ID3D11ShaderResourceView* load_icon_to_srv(ID3D11Device* device, const std::string& filepath)
{
	HICON hIcon = (HICON)LoadImageA(nullptr, filepath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
	if (!hIcon) return nullptr;

	ICONINFO iconInfo;
	if (!GetIconInfo(hIcon, &iconInfo)) {
		DestroyIcon(hIcon);
		return nullptr;
	}

	BITMAP bmpColor;
	if (!GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmpColor)) {
		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		DestroyIcon(hIcon);
		return nullptr;
	}

	int width = bmpColor.bmWidth;
	int height = bmpColor.bmHeight;

	std::vector<DWORD> pixels(width * height);
	HDC hdc = GetDC(nullptr);
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	if (!GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS)) {
		ReleaseDC(nullptr, hdc);
		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		DestroyIcon(hIcon);
		return nullptr;
	}
	ReleaseDC(nullptr, hdc);

	bool has_alpha = false;
	for (int i = 0; i < width * height; ++i) {
		if ((pixels[i] & 0xFF000000) != 0) {
			has_alpha = true;
			break;
		}
	}

	if (!has_alpha && iconInfo.hbmMask) {
		std::vector<DWORD> maskBits(width * height);
		BITMAPINFO bmiMask = {};
		bmiMask.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmiMask.bmiHeader.biWidth = width;
		bmiMask.bmiHeader.biHeight = -height;
		bmiMask.bmiHeader.biPlanes = 1;
		bmiMask.bmiHeader.biBitCount = 32;
		bmiMask.bmiHeader.biCompression = BI_RGB;

		hdc = GetDC(nullptr);
		if (GetDIBits(hdc, iconInfo.hbmMask, 0, height, maskBits.data(), &bmiMask, DIB_RGB_COLORS)) {
			for (int i = 0; i < width * height; ++i) {
				if (maskBits[i] == 0) {
					pixels[i] |= 0xFF000000;
				} else {
					pixels[i] &= 0x00FFFFFF;
				}
			}
		}
		ReleaseDC(nullptr, hdc);
	}

	DeleteObject(iconInfo.hbmColor);
	DeleteObject(iconInfo.hbmMask);
	DestroyIcon(hIcon);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA subResource = {};
	subResource.pSysMem = pixels.data();
	subResource.SysMemPitch = width * sizeof(DWORD);

	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, &subResource, &texture);
	if (FAILED(hr)) return nullptr;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* srv = nullptr;
	hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
	texture->Release();

	if (FAILED(hr)) return nullptr;
	return srv;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
        {
            return 0;
        }
        break;

    case WM_SYSKEYDOWN:
        if (wParam == VK_F4) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCPAINT:
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

render_t::render_t()
{
    detail = std::make_unique<detail_t>();
}

render_t::~render_t()
{
	if (menu_icon_srv)
	{
		menu_icon_srv->Release();
		menu_icon_srv = nullptr;
	}
    destroy_imgui();
    destroy_window();
    destroy_device();
}

bool render_t::create_window()
{
    detail->window_class.cbSize = sizeof(detail->window_class);
    detail->window_class.style = CS_CLASSDC;
    detail->window_class.lpszClassName = "T4";
    detail->window_class.hInstance = GetModuleHandleA(0);
    detail->window_class.lpfnWndProc = wnd_proc;
    detail->window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    detail->window_class.hIcon = LoadIconA(detail->window_class.hInstance, MAKEINTRESOURCEA(1));
    detail->window_class.hIconSm = LoadIconA(detail->window_class.hInstance, MAKEINTRESOURCEA(1));

    RegisterClassExA(&detail->window_class);

    detail->window = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        detail->window_class.lpszClassName,
        "T4",
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        0,
        0,
        detail->window_class.hInstance,
        0
    );

    if (!detail->window)
    {
        return false;
    }

    SetLayeredWindowAttributes(detail->window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA);

    RECT client_area{};
    RECT window_area{};

    GetClientRect(detail->window, &client_area);
    GetWindowRect(detail->window, &window_area);

    POINT diff{};
    ClientToScreen(detail->window, &diff);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(detail->window, &margins);

    ShowWindow(detail->window, SW_HIDE);
    UpdateWindow(detail->window);

    return true;
}

bool render_t::create_device()
{
    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};

    swap_chain_desc.BufferCount = 1;

    swap_chain_desc.BufferDesc.Width = 0;
    swap_chain_desc.BufferDesc.Height = 0;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    swap_chain_desc.OutputWindow = detail->window;

    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swap_chain_desc.Windowed = 1;

    swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;

    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    D3D_FEATURE_LEVEL feature_level;
    D3D_FEATURE_LEVEL feature_level_list[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        feature_level_list,
        2,
        D3D11_SDK_VERSION,
        &swap_chain_desc,
        &detail->swap_chain,
        &detail->device,
        &feature_level,
        &detail->device_context
    );

    if (result == DXGI_ERROR_UNSUPPORTED)
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            feature_level_list,
            2,
            D3D11_SDK_VERSION,
            &swap_chain_desc,
            &detail->swap_chain,
            &detail->device,
            &feature_level,
            &detail->device_context
        );
    }

    if (result != S_OK)
    {
        MessageBoxA(nullptr, "This software can not run on your computer.", "Critical Problem", MB_ICONERROR | MB_OK);
    }

    ID3D11Texture2D* back_buffer{ nullptr };
    detail->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));

    if (back_buffer)
    {
        detail->device->CreateRenderTargetView(back_buffer, nullptr, &detail->render_target_view);
        back_buffer->Release();

        return true;
    }

    return false;
}

static ImFont* tab_icon_font = nullptr;

bool render_t::create_imgui()
{
    if (!detail->window || !detail->device || !detail->device_context)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(detail->window))
        return false;
    if (!ImGui_ImplDX11_Init(detail->device, detail->device_context))
        return false;

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH = true;
    cfg.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;
    cfg.FontDataOwnedByAtlas = false;

    var->font.icons[0] = io.Fonts->AddFontFromMemoryTTF(section_icons_hex, sizeof(section_icons_hex), 15.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    var->font.icons[1] = io.Fonts->AddFontFromMemoryTTF(icons_hex, sizeof(icons_hex), 5.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    var->font.tahoma = io.Fonts->AddFontFromMemoryTTF(tahoma_hex, sizeof(tahoma_hex), 13.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    io.FontDefault = var->font.tahoma;

    float m_flDpiScale = 1.0f;
    esp_font_tahoma = io.Fonts->AddFontFromMemoryTTF(fs_tahoma_8px_ttf, fs_tahoma_8px_ttf_len, 12.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    esp_font_undefeated = io.Fonts->AddFontFromMemoryTTF(undefeated_ttf, undefeated_ttf_len, 22.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());
    esp_font_smallest_pixel = io.Fonts->AddFontFromMemoryTTF((void*)smallest_pixel_7_ttf, smallest_pixel_7_ttf_len, 11.0f * m_flDpiScale, &cfg, io.Fonts->GetGlyphRangesCyrillic());

    // Load FontAwesome 6 icons for Linoria v2
    ImFontConfig font_cfg_icons;
    font_cfg_icons.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
    font_cfg_icons.GlyphOffset = ImVec2(0, 0);
    font_cfg_icons.SizePixels = 16.0f;
    font_cfg_icons.FontDataOwnedByAtlas = false;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    Menu::m_pIconFont = io.Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 16.0f, &font_cfg_icons, icon_ranges);

    // Apply Linoria v2 style & default theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.PopupRounding = 0;
    style.GrabRounding = 0;
    style.ScrollbarRounding = 0;

    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;

    style.WindowPadding = ImVec2(6, 6);
    style.ChildPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(5.0f, 4.0f);
    style.CellPadding = ImVec2(1.5f, 1.5f);
    style.ItemSpacing = ImVec2(4, 4);
    style.ItemInnerSpacing = ImVec2(4, 3);
    style.WindowMinSize = ImVec2(0, 0);
    style.ScrollbarSize = 6.0f;

    Menu::ApplyTheme(0);
    Menu::m_bInitialized = true;

    var->gui.menu_opened = false;
    return true;
}

void render_t::destroy_device()
{
	if (detail->render_target_view) detail->render_target_view->Release();
	if (detail->swap_chain) detail->swap_chain->Release();
	if (detail->device_context) detail->device_context->Release();
	if (detail->device) detail->device->Release();
}

void render_t::destroy_window()
{
    DestroyWindow(detail->window);
    UnregisterClassA(detail->window_class.lpszClassName, detail->window_class.hInstance);
}

void render_t::destroy_imgui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void render_t::start_render()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

	static bool s_last_streamproof = !settings::streamproof;
	if (settings::streamproof != s_last_streamproof)
	{
		s_last_streamproof = settings::streamproof;
		SetWindowDisplayAffinity(detail->window, settings::streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	}

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    frame_cache::tick(game::visualengine.get());

    RECT rect;
    if (GetClientRect(detail->window, &rect))
    {
        g::iWidth = rect.right - rect.left;
        g::iHeight = rect.bottom - rect.top;
    }


}

void render_t::end_render()
{
    float clear_color[4]{ 0, 0, 0, 0 };
    detail->device_context->OMSetRenderTargets(1, &detail->render_target_view, nullptr);
    detail->device_context->ClearRenderTargetView(detail->render_target_view, clear_color);

    if (game::visualengine && game::visualengine->address != 0)
    {
        const auto fc   = frame_cache::get_for_thread();
        const math::matrix4& view = fc.view;
        const math::vector2& dims = fc.dims;
        if (dims.x > 0.0f && dims.y > 0.0f)
        {
            std::vector<const cache::entity_t*> local_entities;
            std::vector<const cache::entity_t*> enemy_entities;
            std::vector<const cache::entity_t*> friendly_entities;
            local_entities.clear();
            enemy_entities.clear();
            friendly_entities.clear();

            auto local_snap = cache::get_local_snap();
            std::lock_guard<std::mutex> lock(cache::mtx);
            
            for (const auto& ent : cache::players)
            {
                const bool has_local = local_snap && local_snap->instance.address != 0;
                bool is_local = (has_local && ent.instance.address == local_snap->instance.address) ||
                                (has_local && local_snap->user_id != 0 && ent.user_id == local_snap->user_id) ||
                                (has_local && local_snap->humanoid_root_part.address != 0 && ent.humanoid_root_part.address == local_snap->humanoid_root_part.address) ||
                                (cache::local_character.address != 0 && ent.instance.address == cache::local_character.address);

                bool is_friendly = false;
                if (!is_local)
                {
                    if (ent.priority == cache::player_priority::friendly)
                        is_friendly = true;
                    else if (local_snap && local_snap->team != 0 && ent.team != 0 && ent.team == local_snap->team)
                    {
                        if (game::datamodel && game::datamodel->address != 0 && !game::datamodel->is_mm2() && !game::datamodel->is_arsenal())
                        {
                            is_friendly = true;
                        }
                    }
                    else if (!ent.name.empty() && playerlist::get_priority(ent.name) == cache::player_priority::friendly)
                        is_friendly = true;
                }

                if (ent.health <= 0.0f)
                {
                    const auto& active_cfg = is_local ? settings::visuals::neutral : (is_friendly ? settings::visuals::friendly : settings::visuals::hostile);
                    if (!active_cfg.chams_corpse && !active_cfg.corpse)
                        continue;
                }

                if (is_local)
                {
                    if (settings::visuals::neutral.chams && settings::visuals::neutral.chams_type == 1)
                    {
                        local_entities.push_back(&ent);
                    }
                    else if (settings::visuals::local_player)
                    {
                        friendly_entities.push_back(&ent);
                    }
                }
                else
                {
                    if (is_friendly)
                        friendly_entities.push_back(&ent);
                    else
                        enemy_entities.push_back(&ent);
                }
            }

            if ((!local_entities.empty() || !enemy_entities.empty() || !friendly_entities.empty())
                && !rescan::is_rescanning.load(std::memory_order_acquire))
            {
                float cam_pos[3] = { 0.0f, 0.0f, 0.0f };
                if (game::camera != 0) {
                    math::vector3 cp = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
                    cam_pos[0] = cp.x; cam_pos[1] = cp.y; cam_pos[2] = cp.z;
                }
                HWND roblox_window = game::get_roblox_window();
                POINT roblox_screen_pt{ 0, 0 };
                if (roblox_window) ClientToScreen(roblox_window, &roblox_screen_pt);
                POINT overlay_screen_pt{ 0, 0 };
                if (detail && detail->window) ClientToScreen(detail->window, &overlay_screen_pt);
                float offset_x = static_cast<float>(roblox_screen_pt.x - overlay_screen_pt.x);
                float offset_y = static_cast<float>(roblox_screen_pt.y - overlay_screen_pt.y);

                auto render_chams_group = [&](const std::vector<const cache::entity_t*>& group_entities, const settings::visuals::priority_settings_t& cfg)
                {
                    if (group_entities.empty() || !cfg.chams || cfg.chams_type != 1) return;

                    meshgpu::update_depth_bias(detail->device, cfg.mesh_depth_bias);

                    if (cfg.chams_health_based)
                    {
                        for (const auto* ent_ptr : group_entities)
                        {
                            if (!ent_ptr) continue;
                            float mesh_fill[4];
                            chams_utils::get_chams_color(*ent_ptr, cfg, mesh_fill);
                            if (mesh_fill[3] <= 0.01f || mesh_fill[3] == 0.25f) mesh_fill[3] = 1.0f;

                            const cache::entity_t* single_arr[1] = { ent_ptr };
                            std::vector<const cache::entity_t*> single_vec(single_arr, single_arr + 1);
                            memorymesh::render_ptrs(single_vec, &view.m[0][0], cam_pos, dims.x, dims.y, detail->device, detail->device_context,
                                mesh_fill, cfg.chams_outline_colour,
                                cfg.mesh_shader, cfg.mesh_outline, cfg.mesh_outline_mode,
                                cfg.mesh_outline_style, cfg.mesh_outline_thickness, cfg.mesh_outline_only,
                                cfg.mesh_accessories, offset_x, offset_y);
                        }
                    }
                    else
                    {
                        float mesh_fill[4];
                        cache::entity_t dummy_ent{};
                        chams_utils::get_chams_color(dummy_ent, cfg, mesh_fill);
                        if (mesh_fill[3] <= 0.01f || mesh_fill[3] == 0.25f) mesh_fill[3] = 1.0f;

                        memorymesh::render_ptrs(group_entities, &view.m[0][0], cam_pos, dims.x, dims.y, detail->device, detail->device_context,
                            mesh_fill, cfg.chams_outline_colour,
                            cfg.mesh_shader, cfg.mesh_outline, cfg.mesh_outline_mode,
                            cfg.mesh_outline_style, cfg.mesh_outline_thickness, cfg.mesh_outline_only,
                            cfg.mesh_accessories, offset_x, offset_y);
                    }
                };

                render_chams_group(local_entities, settings::visuals::neutral);
                render_chams_group(enemy_entities, settings::visuals::hostile);
                render_chams_group(friendly_entities, settings::visuals::friendly);
            }
        }
    }

    draw_custom_cursor();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    detail->swap_chain->Present(0, 0);
    if (settings::vsync)
        DwmFlush();

    static bool window_revealed = false;
    if (!window_revealed) {
        ShowWindow(detail->window, SW_SHOW);
        SetForegroundWindow(detail->window);
        BringWindowToTop(detail->window);
        window_revealed = true;
    }
}




static void DrawTabIcon(ImDrawList* draw_list, ImVec2 pos, int tab_index, ImU32 color, float size)
{
	pos.x = (float)(int)(pos.x);
	pos.y = (float)(int)(pos.y);

    float r = size * 0.5f;
    ImVec2 center = pos + ImVec2(r, r);
	center.x = (float)(int)(center.x);
	center.y = (float)(int)(center.y);
    
    if (tab_index == 0)
    {
        draw_list->AddCircle(center, r * 0.65f, color, 32, 1.5f);
        draw_list->AddLine(center - ImVec2(0, r * 0.95f), center - ImVec2(0, r * 0.72f), color, 1.5f);
        draw_list->AddLine(center + ImVec2(0, r * 0.72f), center + ImVec2(0, r * 0.95f), color, 1.5f);
        draw_list->AddLine(center - ImVec2(r * 0.95f, 0), center - ImVec2(r * 0.72f, 0), color, 1.5f);
        draw_list->AddLine(center + ImVec2(r * 0.72f, 0), center + ImVec2(r * 0.95f, 0), color, 1.5f);
        draw_list->AddCircleFilled(center, 1.5f, color, 16);
    }
    else if (tab_index == 1)
    {
        ImVec2 p1 = center - ImVec2(r * 0.8f, 0);
        ImVec2 p2 = center + ImVec2(r * 0.8f, 0);
        
        ImVec2 cp_top1 = center + ImVec2(-r * 0.4f, -r * 0.45f);
        ImVec2 cp_top2 = center + ImVec2(r * 0.4f, -r * 0.45f);
        ImVec2 cp_bot1 = center + ImVec2(-r * 0.4f, r * 0.45f);
        ImVec2 cp_bot2 = center + ImVec2(r * 0.4f, r * 0.45f);
        
        draw_list->AddBezierCubic(p1, cp_top1, cp_top2, p2, color, 1.5f);
        draw_list->AddBezierCubic(p1, cp_bot1, cp_bot2, p2, color, 1.5f);
        
        draw_list->AddCircleFilled(center, r * 0.28f, color, 32);
    }
    else if (tab_index == 2) 
    {
        draw_list->AddCircle(center, r * 0.48f, color, 32, 1.5f);
        draw_list->AddCircle(center, r * 0.18f, color, 32, 1.2f);
        for (int i = 0; i < 8; ++i)
        {
            float angle = i * (IM_PI / 4.0f);
            ImVec2 dir(cosf(angle), sinf(angle));
            draw_list->AddLine(center + dir * (r * 0.46f), center + dir * (r * 0.78f), color, 2.2f);
        }
    }
    else if (tab_index == 3)
    {
        ImVec2 head_center = center - ImVec2(0, r * 0.25f);
        draw_list->AddCircle(head_center, r * 0.28f, color, 32, 1.5f);
        
        draw_list->PathClear();
        draw_list->PathArcTo(center + ImVec2(0, r * 0.8f), r * 0.65f, IM_PI + 0.2f, IM_PI * 2.0f - 0.2f, 32);
        draw_list->PathStroke(color, 0, 1.5f);
        
        draw_list->AddLine(center + ImVec2(-r * 0.12f, r * 0.05f), center + ImVec2(-r * 0.12f, r * 0.2f), color, 1.5f);
        draw_list->AddLine(center + ImVec2(r * 0.12f, r * 0.05f), center + ImVec2(r * 0.12f, r * 0.2f), color, 1.5f);
    }
    else if (tab_index == 4)
    {
		ImVec2 b_min = center - ImVec2(r * 0.75f, r * 0.35f);
		ImVec2 b_max = center + ImVec2(r * 0.75f, r * 0.6f);
		b_min.x = (float)(int)(b_min.x);
		b_min.y = (float)(int)(b_min.y);
		b_max.x = (float)(int)(b_max.x);
		b_max.y = (float)(int)(b_max.y);
        draw_list->AddRect(b_min, b_max, color, 1.5f, 0, 1.5f);

		ImVec2 t_min = center - ImVec2(r * 0.75f, r * 0.6f);
		ImVec2 t_max = center - ImVec2(r * 0.25f, r * 0.35f);
		t_min.x = (float)(int)(t_min.x);
		t_min.y = (float)(int)(t_min.y);
		t_max.x = (float)(int)(t_max.x);
		t_max.y = (float)(int)(t_max.y);
        draw_list->AddRectFilled(t_min, t_max, color, 1.0f);
    }
    else if (tab_index == 5)
    {
        float y1 = (float)(int)(center.y - r * 0.35f);
        float y2 = (float)(int)(center.y + r * 0.35f);
        
        ImVec4 track_col = ImGui::ColorConvertU32ToFloat4(color);
        track_col.w *= 0.4f;
        ImU32 faded_color = ImGui::ColorConvertFloat4ToU32(track_col);
        
        draw_list->AddLine(ImVec2((float)(int)(center.x - r * 0.8f), y1), ImVec2((float)(int)(center.x + r * 0.8f), y1), faded_color, 1.5f);
        draw_list->AddLine(ImVec2((float)(int)(center.x - r * 0.8f), y2), ImVec2((float)(int)(center.x + r * 0.8f), y2), faded_color, 1.5f);
        
        draw_list->AddCircleFilled(ImVec2((float)(int)(center.x - r * 0.25f), y1), 2.5f, color, 32);
        draw_list->AddCircleFilled(ImVec2((float)(int)(center.x + r * 0.3f), y2), 2.5f, color, 32);
    }
}

static bool render_vertical_tab(const char* label, int* current_page, int page_id, int icon_index)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 38.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *current_page = page_id;

    bool active = (*current_page == page_id);
    bool icon_only = (size.x < 60.0f);

    if (active)
    {
        window->DrawList->AddRectFilled(
            pos + ImVec2(4.0f, 2.0f),
            pos + size - ImVec2(4.0f, 2.0f),
            IM_COL32(42, 42, 42, static_cast<int>(220 * style.Alpha)),
            style.ChildRounding
        );
    }

    ImU32 tab_color;
    if (active)
        tab_color = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    else if (hovered)
        tab_color = ImGui::GetColorU32(ImGuiCol_Text);
    else
        tab_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    float icon_size = 18.0f;
    ImVec2 icon_pos;
    if (icon_only)
        icon_pos = pos + ImVec2((size.x - icon_size) * 0.5f, (size.y - icon_size) * 0.5f);
    else
        icon_pos = pos + ImVec2(12.0f, (size.y - icon_size) * 0.5f);

    if (tab_icon_font)
    {
        char text_str[2] = { (char)('A' + icon_index), '\0' };
        ImVec2 glyph_size = tab_icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, text_str);
        ImVec2 centered_pos = icon_pos + ImVec2((icon_size - glyph_size.x) * 0.5f, (icon_size - glyph_size.y) * 0.5f);
        window->DrawList->AddText(tab_icon_font, icon_size, centered_pos, tab_color, text_str);
    }
    else
    {
        DrawTabIcon(window->DrawList, icon_pos, icon_index, tab_color, icon_size);
    }

    if (!icon_only)
    {
        ImVec2 label_size = ImGui::CalcTextSize(label);
        ImVec2 text_pos   = pos + ImVec2(12.0f + icon_size + 10.0f, (size.y - label_size.y) * 0.5f);
        window->DrawList->AddText(text_pos, tab_color, label);
    }

    return pressed;
}

struct snow_particle_t
{
	ImVec2 pos;
	float speed;
	float size;
	float drift;
};

struct rain_particle_t
{
	ImVec2 pos;
	float speed;
	float length;
};

namespace ImAdd {
    inline bool TabButtonAccent(const char* label, bool active, const ImVec4& accent_col)
    {
        ImVec2 size = ImGui::CalcTextSize(label) + ImVec2(12.0f, 4.0f);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Text, accent_col);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(140 / 255.f, 140 / 255.f, 140 / 255.f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(12 / 255.f, 16 / 255.f, 25 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(23 / 255.f, 28 / 255.f, 41 / 255.f, 1.0f));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
        return pressed;
    }
}

void render_t::render_menu()
{
    if (loader::open)
        loader::render_loader();

    Menu::DrawAll(var->gui.menu_opened);
}


static void draw_fov_circle(float fov_radius, float circle_colour[4], float outline_colour[4], bool fill_fov = false, float fill_colour[4] = nullptr, bool follow_mouse = false)
{
	if (fov_radius <= 0.f)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	if (!draw)
		return;

	float cx = 0.f;
	float cy = 0.f;

	POINT overlay_pt{ 0, 0 };
	if (render && render->detail && render->detail->window)
		ClientToScreen(render->detail->window, &overlay_pt);

	if (follow_mouse)
	{
		POINT cursor{};
		if (GetCursorPos(&cursor))
		{
			cx = static_cast<float>(cursor.x - overlay_pt.x);
			cy = static_cast<float>(cursor.y - overlay_pt.y);
		}
	}

	if (cx <= 0.f || cy <= 0.f)
	{
		HWND game_hwnd = game::get_roblox_window();
		if (game_hwnd && IsWindow(game_hwnd))
		{
			RECT rect{};
			if (GetClientRect(game_hwnd, &rect))
			{
				POINT center_pt = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
				if (ClientToScreen(game_hwnd, &center_pt))
				{
					cx = static_cast<float>(center_pt.x - overlay_pt.x);
					cy = static_cast<float>(center_pt.y - overlay_pt.y);
				}
			}
		}
	}

	if (cx <= 0.f || cy <= 0.f)
	{
		ImGuiIO& io = ImGui::GetIO();
		cx = io.DisplaySize.x * 0.5f;
		cy = io.DisplaySize.y * 0.5f;
	}

	ImVec2 center(cx, cy);

	if (fill_fov && fill_colour != nullptr)
	{
		ImU32 fill_col = IM_COL32(
			static_cast<int>(fill_colour[0] * 255.f),
			static_cast<int>(fill_colour[1] * 255.f),
			static_cast<int>(fill_colour[2] * 255.f),
			static_cast<int>(fill_colour[3] * 255.f)
		);
		draw->AddCircleFilled(center, fov_radius, fill_col, 64);
	}

	ImU32 outline_col = IM_COL32(
		static_cast<int>(outline_colour[0] * 255.f),
		static_cast<int>(outline_colour[1] * 255.f),
		static_cast<int>(outline_colour[2] * 255.f),
		static_cast<int>(outline_colour[3] * 255.f)
	);

	ImU32 circle_col = IM_COL32(
		static_cast<int>(circle_colour[0] * 255.f),
		static_cast<int>(circle_colour[1] * 255.f),
		static_cast<int>(circle_colour[2] * 255.f),
		static_cast<int>(circle_colour[3] * 255.f)
	);

	draw->AddCircle(center, fov_radius, outline_col, 64, 2.5f);
	draw->AddCircle(center, fov_radius, circle_col, 64, 1.2f);
}



static void draw_keybind_indicator()
{
	if (!settings::misc::keybind_indicator)
		return;

	ImGuiIO& io = ImGui::GetIO();
	float cx = io.DisplaySize.x * 0.5f;
	float cy = io.DisplaySize.y * 0.5f;

	struct bind_check_t {
		const char* display_name;
		bool enabled;
		int vk;
		int mode;
		const char* id;
	};

	bind_check_t binds[] = {
		{ "Aimbot",        settings::aimbot::enabled,                   settings::aimbot::keybind,                                   settings::aimbot::activation_mode,                              "aimbot" },
		{ "Triggerbot",    settings::triggerbot::enabled,               settings::triggerbot::keybind,                               settings::triggerbot::activation_mode,                          "triggerbot" },
		{ "Silent Aim",    settings::silentaim::enabled,                settings::silentaim::keybind,                                settings::silentaim::activation_mode,                           "silentaim" },
		{ "360 Mode",      settings::raycast_silentaim::mode_360,       settings::raycast_silentaim::mode_360_keybind,               settings::raycast_silentaim::mode_360_activation_mode,          "raycast_360" },
		{ "Magic Bullet",  settings::raycast_silentaim::magic_bullet,   settings::raycast_silentaim::magic_bullet_keybind,           settings::raycast_silentaim::magic_bullet_activation_mode,      "magic_bullet" },
		{ "Speedhack",     settings::movement::speedhack::enabled,      settings::movement::speedhack::keybind,                      settings::movement::speedhack::activation_mode,                 "speedhack" },
		{ "Flyhack",       settings::movement::flyhack::enabled,        settings::movement::flyhack::keybind,                        settings::movement::flyhack::activation_mode,                   "flyhack" },
		{ "Noclip",        settings::movement::noclip::enabled,         settings::movement::noclip::keybind,                         settings::movement::noclip::activation_mode,                    "noclip" },
		{ "Bhop",          settings::movement::bhop::enabled,           settings::movement::bhop::keybind,                          settings::movement::bhop::activation_mode,                      "bhop" },
		{ "Spinbot",       settings::movement::spinbot::enabled,        settings::movement::spinbot::keybind,                       settings::movement::spinbot::activation_mode,                   "spinbot" },
		{ "Spin 360",      settings::movement::spin360::enabled,        settings::movement::spin360::keybind,                       1,                                                              "spin360" },
		{ "Freeze Player", settings::freezeplayer::enabled,             settings::freezeplayer::keybind,                             settings::freezeplayer::activation_mode,                        "freezeplayer" },
		{ "Spam TP",       settings::spamtp::enabled,                   settings::spamtp::keybind,                                   settings::spamtp::activation_mode,                              "spamtp" },
		{ "Wallslide",     settings::movement::wallslide::enabled,      settings::movement::wallslide::keybind,                      settings::movement::wallslide::activation_mode,                 "wallslide" },
		{ "Pixelsurf",     settings::movement::pixelsurf::enabled,      settings::movement::pixelsurf::keybind,                      settings::movement::pixelsurf::activation_mode,                 "pixelsurf" },
		{ "Void-Hide",     settings::movement::voidhide::enabled,       settings::movement::voidhide::keybind,                       settings::movement::voidhide::activation_mode,                  "voidhide" },
		{ "Third Person",  settings::movement::third_person::enabled,   settings::movement::third_person::keybind,                   settings::movement::third_person::activation_mode,              "third_person" },
		{ "FOV Changer",   settings::movement::fov_changer::enabled,    settings::movement::fov_changer::keybind,                    settings::movement::fov_changer::activation_mode,               "fov_changer" }
	};

	std::vector<std::pair<std::string, std::string>> active_bind_names;
	for (const auto& b : binds)
	{
		if (b.enabled && keybind::is_key_active(b.vk, b.mode, b.id))
		{
			const char* mstr = (b.mode == 0) ? " [toggle]" : (b.mode == 1) ? " [hold]" : " [always]";
			active_bind_names.push_back({ std::string(b.display_name), std::string(mstr) });
		}
	}

	if (active_bind_names.empty())
		return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list)
		return;

	float start_offset_y = 22.0f;
	if (settings::crosshair::enabled)
	{
		float size = settings::crosshair::size;
		float gap = settings::crosshair::gap;
		start_offset_y = gap + size + 16.0f;
		if (start_offset_y < 22.0f)
			start_offset_y = 22.0f;
	}

	ImU32 feature_col = IM_COL32(
		static_cast<int>(settings::misc::keybind_indicator_feature_colour[0] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_feature_colour[1] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_feature_colour[2] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_feature_colour[3] * 255.f)
	);
	ImU32 mode_col = IM_COL32(
		static_cast<int>(settings::misc::keybind_indicator_mode_colour[0] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_mode_colour[1] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_mode_colour[2] * 255.f),
		static_cast<int>(settings::misc::keybind_indicator_mode_colour[3] * 255.f)
	);
	ImU32 shadow_col = IM_COL32(0, 0, 0, 220);

	float draw_y = cy + start_offset_y;
	float line_height = 16.0f;

	for (const auto& [feat, mode_tag] : active_bind_names)
	{
		std::string full = feat + mode_tag;
		ImVec2 full_size = ImGui::CalcTextSize(full.c_str());
		ImVec2 feat_size = ImGui::CalcTextSize(feat.c_str());
		float text_x = cx - full_size.x * 0.5f;

		ImVec2 bg_min(text_x - 6.0f, draw_y - 1.0f);
		ImVec2 bg_max(text_x + full_size.x + 6.0f, draw_y + line_height - 1.0f);
		draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(10, 10, 14, 160), 3.0f);
		draw_list->AddRect(bg_min, bg_max, IM_COL32(255, 255, 255, 25), 3.0f);

		draw_list->AddText(ImVec2(text_x - 1.0f, draw_y), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x + 1.0f, draw_y), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y - 1.0f), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y + 1.0f), shadow_col, feat.c_str());
		draw_list->AddText(ImVec2(text_x, draw_y), feature_col, feat.c_str());

		float tag_x = text_x + feat_size.x;
		draw_list->AddText(ImVec2(tag_x - 1.0f, draw_y), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x + 1.0f, draw_y), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y - 1.0f), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y + 1.0f), shadow_col, mode_tag.c_str());
		draw_list->AddText(ImVec2(tag_x, draw_y), mode_col, mode_tag.c_str());

		draw_y += line_height;
	}
}

static void draw_crosshair()
{
	if (!settings::crosshair::enabled)
		return;

	ImGuiIO& io = ImGui::GetIO();
	float cx = io.DisplaySize.x * 0.5f;
	float cy = io.DisplaySize.y * 0.5f;

	HWND game_hwnd = game::get_roblox_window();
	if (game_hwnd)
	{
		RECT rect{};
		if (GetClientRect(game_hwnd, &rect))
		{
			POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
			if (ClientToScreen(game_hwnd, &center))
			{
				cx = (float)center.x;
				cy = (float)center.y;
			}
		}
	}

	if (settings::crosshair::attach_to_enemy)
	{
		bool found_target = false;
		math::vector3 target_world_pos{};

		cache::entity_t aim_target = aimbot::get_player();
		if (aim_target.instance.address != 0)
		{
			auto head_it = aim_target.parts.find("Head");
			if (head_it != aim_target.parts.end() && head_it->second.address != 0)
			{
				rbx::c_part part = head_it->second;
				rbx::c_primitive prim = part.get_primitive();
				if (prim.address != 0)
				{
					target_world_pos = prim.get_position();
					found_target = true;
				}
			}
			else
			{
				auto hrp_it = aim_target.parts.find("HumanoidRootPart");
				if (hrp_it != aim_target.parts.end() && hrp_it->second.address != 0)
				{
					rbx::c_part part = hrp_it->second;
					rbx::c_primitive prim = part.get_primitive();
					if (prim.address != 0)
					{
						target_world_pos = prim.get_position();
						found_target = true;
					}
				}
			}
		}

		if (!found_target && game::visualengine && game::visualengine->address != 0)
		{
			static std::vector<cache::entity_t> players_snapshot;
			cache::entity_t local_snapshot;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				players_snapshot = cache::players;
				local_snapshot = cache::get_local_player();
			}

			float closest_dist = 999999.f;
			math::matrix4 view = game::visualengine->get_viewmatrix();
			math::vector2 dims = game::visualengine->get_dimensions();
			float screen_center_x = dims.x * 0.5f;
			float screen_center_y = dims.y * 0.5f;

			for (auto& entity : players_snapshot)
			{
				if (entity.instance.address == local_snapshot.instance.address)
					continue;
				if (settings::teamcheck && local_snapshot.team != 0 && entity.team == local_snapshot.team)
					continue;
				if (entity.health <= 0.f || entity.knocked)
					continue;

				auto head_it = entity.parts.find("Head");
				if (head_it != entity.parts.end() && head_it->second.address != 0)
				{
					rbx::c_part part = head_it->second;
					rbx::c_primitive prim = part.get_primitive();
					if (prim.address != 0)
					{
						math::vector3 wpos = prim.get_position();
						math::vector2 spos;
						if (game::visualengine->world_to_screen(view, dims, wpos, spos))
						{
							float dx = spos.x - screen_center_x;
							float dy = spos.y - screen_center_y;
							float dist = sqrtf(dx * dx + dy * dy);
							if (dist < closest_dist)
							{
								closest_dist = dist;
								target_world_pos = wpos;
								found_target = true;
							}
						}
					}
				}
			}
		}

		if (found_target && game::visualengine && game::visualengine->address != 0)
		{
			math::matrix4 view = game::visualengine->get_viewmatrix();
			math::vector2 dims = game::visualengine->get_dimensions();
			math::vector2 spos;
			if (game::visualengine->world_to_screen(view, dims, target_world_pos, spos))
			{
				cx = spos.x;
				cy = spos.y;
			}
		}
	}

	static ImVec2 cur_crosshair_pos(0.f, 0.f);
	static bool has_crosshair_init = false;
	if (!has_crosshair_init || !settings::crosshair::lerp)
	{
		cur_crosshair_pos = ImVec2(cx, cy);
		has_crosshair_init = true;
	}
	else
	{
		float dt = io.DeltaTime;
		if (dt > 0.1f) dt = 0.016f;
		float speed = (std::max)(1.0f, settings::crosshair::lerp_speed);
		cur_crosshair_pos.x += (cx - cur_crosshair_pos.x) * (1.0f - std::exp(-speed * dt));
		cur_crosshair_pos.y += (cy - cur_crosshair_pos.y) * (1.0f - std::exp(-speed * dt));
		cx = cur_crosshair_pos.x;
		cy = cur_crosshair_pos.y;
	}

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

	static float rotate_angle = 0.f;
	if (settings::crosshair::rotate)
	{
		rotate_angle += (settings::crosshair::rotate_speed * (3.14159265f / 180.f)) * io.DeltaTime;
		if (rotate_angle > 3.14159265f * 2.f)
			rotate_angle -= 3.14159265f * 2.f;
	}
	else
	{
		rotate_angle = 0.f;
	}

	float pulse_offset = 0.f;
	if (settings::crosshair::pulse)
	{
		float time = static_cast<float>(ImGui::GetTime());
		pulse_offset = sinf(time * settings::crosshair::pulse_speed) * settings::crosshair::pulse_amount;
	}

	static float expansion = 0.f;
	if (settings::crosshair::dynamic_gap)
	{
		if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
		{
			expansion = settings::crosshair::expansion_amount;
		}
		else
		{
			expansion = (std::max)(0.f, expansion - settings::crosshair::expansion_decay * io.DeltaTime);
		}
	}
	else
	{
		expansion = 0.f;
	}

	ImU32 col;
	if (settings::crosshair::rainbow)
	{
		float time = static_cast<float>(ImGui::GetTime());
		float r, g, b;
		ImGui::ColorConvertHSVtoRGB(fmodf(time * settings::crosshair::rainbow_speed, 1.f), 1.f, 1.f, r, g, b);
		col = IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f), static_cast<int>(settings::crosshair::colour[3] * 255.f));
	}
	else
	{
		col = IM_COL32(
			static_cast<int>(settings::crosshair::colour[0] * 255.f),
			static_cast<int>(settings::crosshair::colour[1] * 255.f),
			static_cast<int>(settings::crosshair::colour[2] * 255.f),
			static_cast<int>(settings::crosshair::colour[3] * 255.f)
		);
	}

	ImU32 outline_col = IM_COL32(
		static_cast<int>(settings::crosshair::outline_colour[0] * 255.f),
		static_cast<int>(settings::crosshair::outline_colour[1] * 255.f),
		static_cast<int>(settings::crosshair::outline_colour[2] * 255.f),
		static_cast<int>(settings::crosshair::outline_colour[3] * 255.f)
	);

	float size = (std::max)(1.f, settings::crosshair::size + pulse_offset);
	float gap = (std::max)(0.f, settings::crosshair::gap + pulse_offset + expansion);
	float thickness = (std::max)(0.5f, settings::crosshair::thickness);
	bool draw_outline = settings::crosshair::outline;
	int style = settings::crosshair::style;

	auto draw_rotated_line = [&](float start_dist, float end_dist, float angle_offset, ImU32 color, float thick)
	{
		float angle = rotate_angle + angle_offset;
		float cos_a = cosf(angle);
		float sin_a = sinf(angle);

		ImVec2 start(cx + cos_a * start_dist, cy + sin_a * start_dist);
		ImVec2 end(cx + cos_a * end_dist, cy + sin_a * end_dist);

		draw_list->AddLine(start, end, color, thick);
	};

	float pi = 3.14159265f;
	float start_d = gap;
	float end_d = gap + size;

	if (style == 0) 
	{
		if (draw_outline)
		{
			draw_rotated_line(start_d, end_d, pi, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, 0.f, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, -pi * 0.5f, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, pi * 0.5f, outline_col, thickness + 1.5f);
		}
		draw_rotated_line(start_d, end_d, pi, col, thickness);
		draw_rotated_line(start_d, end_d, 0.f, col, thickness);
		draw_rotated_line(start_d, end_d, -pi * 0.5f, col, thickness);
		draw_rotated_line(start_d, end_d, pi * 0.5f, col, thickness);
	}
	else if (style == 1) 
	{
		if (draw_outline)
		{
			draw_rotated_line(start_d, end_d, pi, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, 0.f, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, pi * 0.5f, outline_col, thickness + 1.5f);
		}
		draw_rotated_line(start_d, end_d, pi, col, thickness);
		draw_rotated_line(start_d, end_d, 0.f, col, thickness);
		draw_rotated_line(start_d, end_d, pi * 0.5f, col, thickness);
	}
	else if (style == 2) // Circle
	{
		float radius = gap + size * 0.5f;
		if (draw_outline)
		{
			draw_list->AddCircle(ImVec2(cx, cy), radius, outline_col, 32, thickness + 1.5f);
		}
		draw_list->AddCircle(ImVec2(cx, cy), radius, col, 32, thickness);
	}
	else if (style == 3) // Chevron (^)
	{
		float angle_l = rotate_angle + pi * 0.65f;
		float angle_r = rotate_angle + pi * 0.35f;
		ImVec2 tip(cx, cy - gap);
		ImVec2 left(cx + cosf(angle_l) * size, cy - gap + sinf(angle_l) * size);
		ImVec2 right(cx + cosf(angle_r) * size, cy - gap + sinf(angle_r) * size);

		if (draw_outline)
		{
			draw_list->AddLine(tip, left, outline_col, thickness + 1.5f);
			draw_list->AddLine(tip, right, outline_col, thickness + 1.5f);
		}
		draw_list->AddLine(tip, left, col, thickness);
		draw_list->AddLine(tip, right, col, thickness);
	}
	else if (style == 4) // X-Shape (4 Diagonal Lines)
	{
		float diag = pi * 0.25f;
		if (draw_outline)
		{
			draw_rotated_line(start_d, end_d, diag, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, diag + pi * 0.5f, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, diag + pi, outline_col, thickness + 1.5f);
			draw_rotated_line(start_d, end_d, diag - pi * 0.5f, outline_col, thickness + 1.5f);
		}
		draw_rotated_line(start_d, end_d, diag, col, thickness);
		draw_rotated_line(start_d, end_d, diag + pi * 0.5f, col, thickness);
		draw_rotated_line(start_d, end_d, diag + pi, col, thickness);
		draw_rotated_line(start_d, end_d, diag - pi * 0.5f, col, thickness);
	}
	else if (style == 5) // Square / Box
	{
		float half = gap + size * 0.5f;
		ImVec2 p_min(cx - half, cy - half);
		ImVec2 p_max(cx + half, cy + half);
		if (draw_outline)
		{
			draw_list->AddRect(p_min, p_max, outline_col, 0.0f, 0, thickness + 1.5f);
		}
		draw_list->AddRect(p_min, p_max, col, 0.0f, 0, thickness);
	}

	if (settings::crosshair::dot)
	{
		float dr = (std::max)(1.0f, settings::crosshair::dot_size);
		if (draw_outline)
		{
			draw_list->AddCircleFilled(ImVec2(cx, cy), dr + 1.0f, outline_col);
		}
		draw_list->AddCircleFilled(ImVec2(cx, cy), dr, col);
	}
}

static void draw_hitmarkers(float delta_time, bool hit_event)
{
	bool is_hitmarker_enabled = settings::raycast_silentaim::hitmarkers &&
		(settings::raycast_silentaim::enabled ||
		 (settings::silentaim::enabled && settings::silentaim::method == 0));
	if (!is_hitmarker_enabled)
		return;

	static float hitmarker_time = 0.0f;

	if (hit_event)
	{
		hitmarker_time = settings::raycast_silentaim::hitmarker_duration;
	}

	if (hitmarker_time > 0.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		float cx = io.DisplaySize.x * 0.5f;
		float cy = io.DisplaySize.y * 0.5f;

		HWND game_hwnd = game::get_roblox_window();
		if (game_hwnd)
		{
			RECT rect{};
			if (GetClientRect(game_hwnd, &rect))
			{
				POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
				if (ClientToScreen(game_hwnd, &center))
				{
					cx = (float)center.x;
					cy = (float)center.y;
				}
			}
		}

		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

		float duration = settings::raycast_silentaim::hitmarker_duration;
		if (duration <= 0.f) duration = 0.25f;
		float progress = hitmarker_time / duration;
		int alpha = static_cast<int>(progress * settings::raycast_silentaim::hitmarker_colour[3] * 255.f);
		alpha = (alpha < 0) ? 0 : (alpha > 255) ? 255 : alpha;

		const float* hc = settings::raycast_silentaim::hitmarker_colour;
		ImU32 col = IM_COL32(
			static_cast<int>(hc[0] * 255.f),
			static_cast<int>(hc[1] * 255.f),
			static_cast<int>(hc[2] * 255.f),
			alpha);
		ImU32 outline_col = IM_COL32(0, 0, 0, static_cast<int>(alpha * 0.7f));

		float size = settings::raycast_silentaim::hitmarker_size;
		float gap  = settings::raycast_silentaim::hitmarker_gap;
		float thickness = 1.5f;

		// Top-left
		draw_list->AddLine(ImVec2(cx - gap - size, cy - gap - size), ImVec2(cx - gap, cy - gap), outline_col, thickness + 1.0f);
		draw_list->AddLine(ImVec2(cx - gap - size, cy - gap - size), ImVec2(cx - gap, cy - gap), col, thickness);

		// Top-right
		draw_list->AddLine(ImVec2(cx + gap, cy - gap), ImVec2(cx + gap + size, cy - gap - size), outline_col, thickness + 1.0f);
		draw_list->AddLine(ImVec2(cx + gap, cy - gap), ImVec2(cx + gap + size, cy - gap - size), col, thickness);

		// Bottom-left
		draw_list->AddLine(ImVec2(cx - gap - size, cy + gap + size), ImVec2(cx - gap, cy + gap), outline_col, thickness + 1.0f);
		draw_list->AddLine(ImVec2(cx - gap - size, cy + gap + size), ImVec2(cx - gap, cy + gap), col, thickness);

		// Bottom-right
		draw_list->AddLine(ImVec2(cx + gap, cy + gap), ImVec2(cx + gap + size, cy + gap + size), outline_col, thickness + 1.0f);
		draw_list->AddLine(ImVec2(cx + gap, cy + gap), ImVec2(cx + gap + size, cy + gap + size), col, thickness);

		hitmarker_time -= delta_time;
	}
}

static void draw_hit_flash(float delta_time, bool hit_event)
{
	bool is_flash_enabled = settings::raycast_silentaim::hit_flash &&
		(settings::raycast_silentaim::enabled ||
		 (settings::silentaim::enabled && settings::silentaim::method == 0));
	if (!is_flash_enabled)
		return;

	static float flash_time = 0.0f;

	if (hit_event)
	{
		flash_time = settings::raycast_silentaim::hit_flash_duration;
	}

	if (flash_time <= 0.f)
		return;

	ImGuiIO& io = ImGui::GetIO();
	float sw = io.DisplaySize.x;
	float sh = io.DisplaySize.y;

	HWND game_hwnd = game::get_roblox_window();
	if (game_hwnd)
	{
		RECT wr{};
		if (GetWindowRect(game_hwnd, &wr))
		{
			sw = static_cast<float>(wr.right - wr.left);
			sh = static_cast<float>(wr.bottom - wr.top);
		}
	}

	float duration = settings::raycast_silentaim::hit_flash_duration;
	if (duration <= 0.f) duration = 0.12f;
	float progress = flash_time / duration;

	const float* fc = settings::raycast_silentaim::hit_flash_colour;
	int base_alpha = static_cast<int>(fc[3] * 255.f);
	int alpha = static_cast<int>(progress * base_alpha);
	alpha = (alpha < 0) ? 0 : (alpha > 255) ? 255 : alpha;

	ImU32 flash_col_full = IM_COL32(
		static_cast<int>(fc[0] * 255.f),
		static_cast<int>(fc[1] * 255.f),
		static_cast<int>(fc[2] * 255.f),
		alpha);
	ImU32 flash_col_zero = IM_COL32(
		static_cast<int>(fc[0] * 255.f),
		static_cast<int>(fc[1] * 255.f),
		static_cast<int>(fc[2] * 255.f),
		0);

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	float vignette_reach = sh * 0.45f;

	draw_list->AddRectFilledMultiColor(
		ImVec2(0, 0), ImVec2(sw, vignette_reach),
		flash_col_full, flash_col_full, flash_col_zero, flash_col_zero);

	draw_list->AddRectFilledMultiColor(
		ImVec2(0, sh - vignette_reach), ImVec2(sw, sh),
		flash_col_zero, flash_col_zero, flash_col_full, flash_col_full);

	draw_list->AddRectFilledMultiColor(
		ImVec2(0, 0), ImVec2(vignette_reach, sh),
		flash_col_full, flash_col_zero, flash_col_zero, flash_col_full);

	draw_list->AddRectFilledMultiColor(
		ImVec2(sw - vignette_reach, 0), ImVec2(sw, sh),
		flash_col_zero, flash_col_full, flash_col_full, flash_col_zero);

			flash_time -= delta_time;
}

static float get_watermark_cpu_usage()
{
	static FILETIME prev_sys_idle{}, prev_sys_kernel{}, prev_sys_user{};
	static FILETIME prev_proc_creation{}, prev_proc_exit{}, prev_proc_kernel{}, prev_proc_user{};
	static float last_cpu = 0.6f;
	static auto last_calc = std::chrono::steady_clock::now();

	auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_calc).count() < 500)
		return last_cpu;
	last_calc = now;

	FILETIME sys_idle{}, sys_kernel{}, sys_user{};
	FILETIME proc_creation{}, proc_exit{}, proc_kernel{}, proc_user{};

	if (!GetSystemTimes(&sys_idle, &sys_kernel, &sys_user) ||
		!GetProcessTimes(GetCurrentProcess(), &proc_creation, &proc_exit, &proc_kernel, &proc_user))
	{
		return last_cpu;
	}

	auto ft_to_uint64 = [](const FILETIME& ft) -> uint64_t {
		return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	};

	uint64_t sys_k = ft_to_uint64(sys_kernel);
	uint64_t sys_u = ft_to_uint64(sys_user);
	uint64_t prev_sys_k = ft_to_uint64(prev_sys_kernel);
	uint64_t prev_sys_u = ft_to_uint64(prev_sys_user);

	uint64_t proc_k = ft_to_uint64(proc_kernel);
	uint64_t proc_u = ft_to_uint64(proc_user);
	uint64_t prev_proc_k = ft_to_uint64(prev_proc_kernel);
	uint64_t prev_proc_u = ft_to_uint64(prev_proc_user);

	uint64_t total_sys = (sys_k - prev_sys_k) + (sys_u - prev_sys_u);
	uint64_t total_proc = (proc_k - prev_proc_k) + (proc_u - prev_proc_u);

	prev_sys_idle = sys_idle;
	prev_sys_kernel = sys_kernel;
	prev_sys_user = sys_user;
	prev_proc_creation = proc_creation;
	prev_proc_exit = proc_exit;
	prev_proc_kernel = proc_kernel;
	prev_proc_user = proc_user;

	if (total_sys > 0)
	{
		last_cpu = (static_cast<float>(total_proc) / static_cast<float>(total_sys)) * 100.0f;
		if (last_cpu < 0.0f) last_cpu = 0.0f;
		if (last_cpu > 100.0f) last_cpu = 100.0f;
	}
	return last_cpu;
}

static size_t get_watermark_ram_mb()
{
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
	{
		return pmc.WorkingSetSize / (1024 * 1024);
	}
	return 82;
}

void RenderWatermark(const char* menu_name, float pos_x, float pos_y, ImU32 accent_color, ImU32 bg_color)
{
	render->render_watermark();
}

void render_t::render_topbar()
{
}

void render_t::render_watermark()
{
}

void RenderKeybindList(float pos_x, float pos_y, ImU32 accent_color, ImU32 bg_color)
{
	struct anim_state_t {
		float alpha = 0.0f;
		float current_y = 0.0f;
	};
	static std::map<std::string, anim_state_t> item_anims;
	static float no_active_alpha = 1.0f;
	static float current_box_height = 42.0f;
	static auto last_time = std::chrono::high_resolution_clock::now();

	auto now = std::chrono::high_resolution_clock::now();
	float dt = std::chrono::duration<float>(now - last_time).count();
	last_time = now;
	if (dt <= 0.0001f || dt > 0.1f) dt = 1.0f / 60.0f;

	struct bind_spec_t {
		const char* name;
		bool enabled;
		int vk;
		int mode;
		const char* id;
	};

	bind_spec_t specs[] = {
		{ "Aimbot",        settings::aimbot::enabled,                   settings::aimbot::keybind,                                   settings::aimbot::activation_mode,                              "aimbot" },
		{ "Triggerbot",    settings::triggerbot::enabled,               settings::triggerbot::keybind,                               settings::triggerbot::activation_mode,                          "triggerbot" },
		{ "Silent Aim",    settings::silentaim::enabled,                settings::silentaim::keybind,                                settings::silentaim::activation_mode,                           "silentaim" },
		{ "360 Mode",      settings::raycast_silentaim::mode_360,       settings::raycast_silentaim::mode_360_keybind,               settings::raycast_silentaim::mode_360_activation_mode,          "raycast_360" },
		{ "Magic Bullet",  settings::raycast_silentaim::magic_bullet,   settings::raycast_silentaim::magic_bullet_keybind,           settings::raycast_silentaim::magic_bullet_activation_mode,      "magic_bullet" },
		{ "Speedhack",     settings::movement::speedhack::enabled,      settings::movement::speedhack::keybind,                      settings::movement::speedhack::activation_mode,                 "speedhack" },
		{ "Flyhack",       settings::movement::flyhack::enabled,        settings::movement::flyhack::keybind,                        settings::movement::flyhack::activation_mode,                   "flyhack" },
		{ "Bhop",          settings::movement::bhop::enabled,           settings::movement::bhop::keybind,                          settings::movement::bhop::activation_mode,                      "bhop" },
		{ "Spin 360",      settings::movement::spin360::enabled,        settings::movement::spin360::keybind,                       1,                                                              "spin360" },
		{ "Freeze Player", settings::freezeplayer::enabled,             settings::freezeplayer::keybind,                             settings::freezeplayer::activation_mode,                        "freezeplayer" },
		{ "Spam TP",       settings::spamtp::enabled,                   settings::spamtp::keybind,                                   settings::spamtp::activation_mode,                              "spamtp" },
		{ "NPC System",    settings::misc::npc_system_window,           settings::misc::npc_system_keybind,                          settings::misc::npc_system_keybind_mode,                        "npc_system" },
		{ "Wallslide",     settings::movement::wallslide::enabled,      settings::movement::wallslide::keybind,                      settings::movement::wallslide::activation_mode,                 "wallslide" },
		{ "Pixelsurf",     settings::movement::pixelsurf::enabled,      settings::movement::pixelsurf::keybind,                      settings::movement::pixelsurf::activation_mode,                 "pixelsurf" },
		{ "Void-Hide",     settings::movement::voidhide::enabled,       settings::movement::voidhide::keybind,                       settings::movement::voidhide::activation_mode,                  "voidhide" }
	};

	struct render_entry_t {
		std::string name;
		std::string mode_str;
		float alpha;
		float render_y;
		bool is_active;
	};

	std::vector<render_entry_t> draw_entries;
	int active_count = 0;
	float slot_index = 0.0f;
	float line_height = 18.0f;

	for (const auto& spec : specs)
	{
		bool is_act = spec.enabled && keybind::is_key_active(spec.vk, spec.mode, spec.id);
		auto& anim = item_anims[spec.id];

		if (is_act)
		{
			active_count++;
			float target_y = slot_index * line_height;
			slot_index += 1.0f;

			anim.alpha += (1.0f - anim.alpha) * (1.0f - std::exp(-18.0f * dt));
			anim.current_y += (target_y - anim.current_y) * (1.0f - std::exp(-16.0f * dt));
		}
		else
		{
			anim.alpha += (0.0f - anim.alpha) * (1.0f - std::exp(-14.0f * dt));
		}

		if (anim.alpha > 0.005f)
		{
			const char* mstr = (spec.mode == 0) ? "[toggle]" : (spec.mode == 1) ? "[hold]" : "[always]";
			draw_entries.push_back({ spec.name, mstr, anim.alpha, anim.current_y, true });
		}
	}



	float no_active_target = (active_count == 0 && draw_entries.empty()) ? 1.0f : 0.0f;
	no_active_alpha += (no_active_target - no_active_alpha) * (1.0f - std::exp(-14.0f * dt));

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	if (!dl) return;

	float width = 175.0f;
	float header_height = 22.0f;
	float padding = 6.0f;

	float target_content_height = (active_count == 0) ? line_height : (active_count * line_height);
	float target_box_height = header_height + target_content_height + padding;
	current_box_height += (target_box_height - current_box_height) * (1.0f - std::exp(-18.0f * dt));

	ImGuiIO& io = ImGui::GetIO();

	if ((pos_x < 0.0f || pos_y < 0.0f) && io.DisplaySize.x > 100.0f)
	{
		pos_x = 15.0f;
		pos_y = (io.DisplaySize.y - current_box_height) * 0.5f;
		settings::misc::keybind_list_pos_x = pos_x;
		settings::misc::keybind_list_pos_y = pos_y;
	}

	ImVec2 wmin = ImVec2(pos_x, pos_y);
	ImVec2 wmax = ImVec2(pos_x + width, pos_y + current_box_height);

	POINT mouse_pt{};
	GetCursorPos(&mouse_pt);
	POINT overlay_pt{ 0, 0 };
	if (render && render->detail && render->detail->window)
		ClientToScreen(render->detail->window, &overlay_pt);
	ImVec2 mouse_pos = ImVec2(static_cast<float>(mouse_pt.x - overlay_pt.x), static_cast<float>(mouse_pt.y - overlay_pt.y));

	bool hovered = (mouse_pos.x >= pos_x && mouse_pos.x <= pos_x + width && mouse_pos.y >= pos_y && mouse_pos.y <= pos_y + current_box_height);
	if (hovered)
	{
		if (orok_config.custom_cursor)
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		else
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}

	bool lbutton_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prev_lbutton_down = false;
	static bool is_dragging = false;
	static ImVec2 drag_offset = ImVec2(0, 0);

	if (hovered && lbutton_down && !prev_lbutton_down)
	{
		is_dragging = true;
		drag_offset = ImVec2(mouse_pos.x - pos_x, mouse_pos.y - pos_y);
	}
	prev_lbutton_down = lbutton_down;

	if (is_dragging)
	{
		if (lbutton_down)
		{
			pos_x = mouse_pos.x - drag_offset.x;
			pos_y = mouse_pos.y - drag_offset.y;

			if (pos_x < 0.0f) pos_x = 0.0f;
			if (pos_y < 0.0f) pos_y = 0.0f;
			if (pos_x + width > io.DisplaySize.x) pos_x = io.DisplaySize.x - width;
			if (pos_y + current_box_height > io.DisplaySize.y) pos_y = io.DisplaySize.y - current_box_height;

			settings::misc::keybind_list_pos_x = pos_x;
			settings::misc::keybind_list_pos_y = pos_y;

			wmin = ImVec2(pos_x, pos_y);
			wmax = ImVec2(pos_x + width, pos_y + current_box_height);
		}
		else
		{
			is_dragging = false;
		}
	}
	g_keybind_list_is_dragging = is_dragging;

	ImU32 current_accent = accent_color;
	ImU32 title_color = IM_COL32(255, 255, 255, 255);

	ImU32 border_col = IM_COL32(
		static_cast<int>(orok_config.menu_color_border[0] * 255.f),
		static_cast<int>(orok_config.menu_color_border[1] * 255.f),
		static_cast<int>(orok_config.menu_color_border[2] * 255.f),
		static_cast<int>(orok_config.menu_color_border[3] * 255.f)
	);

	dl->AddRect(ImVec2(wmin.x - 3, wmin.y - 3), ImVec2(wmax.x + 3, wmax.y + 3), IM_COL32(0, 0, 0, 255));
	dl->AddRect(ImVec2(wmin.x - 2, wmin.y - 2), ImVec2(wmax.x + 2, wmax.y + 2), border_col);
	dl->AddRect(ImVec2(wmin.x - 1, wmin.y - 1), ImVec2(wmax.x + 1, wmax.y + 1), IM_COL32(0, 0, 0, 255));

	dl->AddRectFilled(wmin, wmax, bg_color);

	dl->AddLine(ImVec2(wmin.x, wmin.y), ImVec2(wmax.x, wmin.y), current_accent);

	const char* title = "Keybinds";
	ImVec2 title_size = ImGui::CalcTextSize(title);
	ImVec2 title_pos = ImVec2(pos_x + (width - title_size.x) * 0.5f, pos_y + 3.0f);
	dl->AddText(title_pos, title_color, title);

	//  line
	dl->AddLine(ImVec2(pos_x + 2, pos_y + header_height), ImVec2(pos_x + width - 2, pos_y + header_height), IM_COL32(60, 60, 65, 255));

	dl->PushClipRect(wmin, wmax, true);

	float base_y = pos_y + header_height + 2.0f;

	if (no_active_alpha > 0.005f)
	{
		const char* empty_str = "No active keybinds";
		ImU32 empty_col = IM_COL32(150, 150, 150, static_cast<int>(255.0f * no_active_alpha));
		dl->AddText(ImVec2(pos_x + 8.0f, base_y), empty_col, empty_str);
	}

	for (const auto& item : draw_entries)
	{
		float item_y = base_y + item.render_y;
		float slide_x = (1.0f - item.alpha) * -6.0f;

		ImU32 name_col = item.is_active
			? IM_COL32(255, 255, 255, static_cast<int>(255.0f * item.alpha))
			: IM_COL32(110, 110, 115, static_cast<int>(200.0f * item.alpha));
		ImU32 mode_col = item.is_active
			? IM_COL32(180, 180, 190, static_cast<int>(255.0f * item.alpha))
			: IM_COL32(70, 70, 75,   static_cast<int>(180.0f * item.alpha));

		dl->AddText(ImVec2(pos_x + 8.0f + slide_x, item_y), name_col, item.name.c_str());

		ImVec2 val_size = ImGui::CalcTextSize(item.mode_str.c_str());
		dl->AddText(ImVec2(pos_x + width - val_size.x - 8.0f - slide_x, item_y), mode_col, item.mode_str.c_str());
	}


	dl->PopClipRect();
}

void render_t::render_keybind_list()
{
	if (!settings::misc::keybind_list)
		return;

	ImU32 accent_col = IM_COL32(
		static_cast<int>(orok_config.menu_color[0] * 255.f),
		static_cast<int>(orok_config.menu_color[1] * 255.f),
		static_cast<int>(orok_config.menu_color[2] * 255.f),
		static_cast<int>(orok_config.menu_color[3] * 255.f)
	);

	ImU32 bg_col = IM_COL32(
		static_cast<int>(orok_config.menu_color_secondary[0] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[1] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[2] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[3] * 255.f)
	);

	RenderKeybindList(settings::misc::keybind_list_pos_x, settings::misc::keybind_list_pos_y, accent_col, bg_col);
}

void RenderTargetHub(float pos_x, float pos_y, ImU32 accent_color, ImU32 bg_color)
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	if (!dl) return;

	cache::entity_t target{};
	bool has_target = false;

	cache::entity_t aim_target = aimbot::get_player();
	cache::entity_t silent_target = silentaim::get_target();
	if (aim_target.instance.address != 0)
	{
		target = aim_target;
		has_target = true;
	}
	else if (silent_target.instance.address != 0)
	{
		target = silent_target;
		has_target = true;
	}
	else if (raycast_silentaim::target_address != 0)
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		for (const auto& entity : cache::players)
		{
			if (entity.instance.address == raycast_silentaim::target_address)
			{
				target = entity;
				has_target = true;
				break;
			}
		}
	}

	if (!has_target)
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (!cache::players.empty())
		{
			float best_dist = 999999.0f;
			int best_idx = -1;

			for (size_t i = 0; i < cache::players.size(); ++i)
			{
				const auto& entity = cache::players[i];
				if (!entity.instance.address) continue;

				if (entity.humanoid_root_part.address != 0)
				{
					rbx::c_primitive prim = entity.humanoid_root_part.get_primitive();
					if (prim.address != 0)
					{
						math::vector3 pos = prim.get_position();
						if (cache::get_local_player().humanoid_root_part.address != 0)
						{
							rbx::c_primitive local_prim = cache::get_local_player().humanoid_root_part.get_primitive();
							if (local_prim.address != 0)
							{
								float dist = (pos - local_prim.get_position()).length();
								if (dist < best_dist)
								{
									best_dist = dist;
									best_idx = static_cast<int>(i);
								}
							}
						}
						else
						{
							best_idx = 0;
							break;
						}
					}
				}
			}

			if (best_idx >= 0 && best_idx < static_cast<int>(cache::players.size()))
			{
				target = cache::players[best_idx];
				has_target = true;
			}
		}
	}

	std::string name_str = has_target ? (target.display_name.empty() ? target.name : target.display_name) : "-";
	std::string tool_str = has_target ? (target.tool_name.empty() ? "None" : target.tool_name) : "-";

	float health = has_target ? target.health : 0.0f;
	float max_health = (has_target && target.max_health > 0.0f) ? target.max_health : 100.0f;
	if (health < 0.0f) health = 0.0f;
	if (health > max_health) health = max_health;

	float dist_m = 0.0f;
	if (has_target && target.humanoid_root_part.address != 0)
	{
		rbx::c_primitive target_prim = target.humanoid_root_part.get_primitive();
		rbx::c_primitive local_prim = cache::get_local_player().humanoid_root_part.get_primitive();
		if (target_prim.address != 0 && local_prim.address != 0)
		{
			math::vector3 local_pos = local_prim.get_position();
			math::vector3 target_pos = target_prim.get_position();
			float dist_studs = (target_pos - local_pos).length();
			dist_m = dist_studs / 3.57f;
		}
	}

	std::string status_str = "idle";
	ImColor status_col = ImColor(0.5f, 0.5f, 0.5f, 1.0f);
	if (has_target)
	{
		if (target.knocked)
		{
			status_str = "knocked";
			status_col = ImColor(0.9f, 0.5f, 0.1f, 1.0f);
		}
		else if (health <= 0.0f)
		{
			status_str = "dead";
			status_col = ImColor(0.9f, 0.2f, 0.2f, 1.0f);
		}
		else
		{
			status_str = "targeting";
			status_col = ImColor(0.2f, 0.9f, 0.3f, 1.0f);
		}
	}

	float width = 310.0f;
	float height = 125.0f;

	ImU32 border_col = IM_COL32(
		static_cast<int>(orok_config.menu_color_border[0] * 255.f),
		static_cast<int>(orok_config.menu_color_border[1] * 255.f),
		static_cast<int>(orok_config.menu_color_border[2] * 255.f),
		static_cast<int>(orok_config.menu_color_border[3] * 255.f)
	);

	ImVec2 wmin = ImVec2(pos_x, pos_y);
	ImVec2 wmax = ImVec2(pos_x + width, pos_y + height);

	dl->AddRect(ImVec2(wmin.x - 3, wmin.y - 3), ImVec2(wmax.x + 3, wmax.y + 3), IM_COL32(0, 0, 0, 255));
	dl->AddRect(ImVec2(wmin.x - 2, wmin.y - 2), ImVec2(wmax.x + 2, wmax.y + 2), border_col);
	dl->AddRect(ImVec2(wmin.x - 1, wmin.y - 1), ImVec2(wmax.x + 1, wmax.y + 1), IM_COL32(0, 0, 0, 255));

	dl->AddRectFilled(wmin, wmax, bg_color);
	dl->AddLine(ImVec2(wmin.x, wmin.y), ImVec2(wmax.x, wmin.y), accent_color);

	const char* title_text = "target hub";
	ImVec2 title_sz = ImGui::CalcTextSize(title_text);
	ImVec2 title_pos = ImVec2(wmin.x + (width - title_sz.x) * 0.5f, wmin.y + 3.0f);
	dl->AddText(title_pos, IM_COL32(220, 220, 220, 255), title_text);

	dl->AddLine(ImVec2(wmin.x + 4.0f, wmin.y + 20.0f), ImVec2(wmax.x - 4.0f, wmin.y + 20.0f), IM_COL32(45, 45, 50, 255));

	float avatar_x = wmin.x + 10.0f;
	float avatar_y = wmin.y + 28.0f;
	float avatar_size = 85.0f;
	ImVec2 av_min = ImVec2(avatar_x, avatar_y);
	ImVec2 av_max = ImVec2(avatar_x + avatar_size, avatar_y + avatar_size);

	dl->AddRectFilled(av_min, av_max, IM_COL32(18, 18, 22, 255));
	dl->AddRect(av_min, av_max, IM_COL32(45, 45, 52, 255));

	const char* av_text = "no avatar";
	ImVec2 av_txt_sz = ImGui::CalcTextSize(av_text);
	ImVec2 av_txt_pos = ImVec2(avatar_x + (avatar_size - av_txt_sz.x) * 0.5f, avatar_y + (avatar_size - av_txt_sz.y) * 0.5f);
	dl->AddText(av_txt_pos, IM_COL32(90, 90, 95, 255), av_text);

	float right_x = avatar_x + avatar_size + 12.0f;
	float cur_y = avatar_y + 2.0f;

	dl->AddText(ImVec2(right_x, cur_y), IM_COL32(240, 240, 245, 255), name_str.c_str());
	cur_y += 16.0f;

	if (has_target)
	{
		char info_buf[64];
		snprintf(info_buf, sizeof(info_buf), "Tool: %s  |  %.0fm", tool_str.c_str(), dist_m);
		dl->AddText(ImVec2(right_x, cur_y), IM_COL32(150, 150, 155, 255), info_buf);
	}
	else
	{
		dl->AddText(ImVec2(right_x, cur_y), IM_COL32(150, 150, 155, 255), "-");
	}
	cur_y += 20.0f;

	char hp_text[32];
	snprintf(hp_text, sizeof(hp_text), "%.0f / %.0f", health, max_health);
	dl->AddText(ImVec2(right_x, cur_y), IM_COL32(220, 220, 225, 255), hp_text);
	cur_y += 16.0f;

	float bar_w = wmax.x - right_x - 12.0f;
	float bar_h = 10.0f;
	ImVec2 bar_min = ImVec2(right_x, cur_y);
	ImVec2 bar_max = ImVec2(right_x + bar_w, cur_y + bar_h);

	dl->AddRectFilled(bar_min, bar_max, IM_COL32(22, 22, 26, 255));
	dl->AddRect(bar_min, bar_max, IM_COL32(48, 48, 55, 255));

	float hp_ratio = (max_health > 0.0f) ? (health / max_health) : 0.0f;
	if (hp_ratio > 1.0f) hp_ratio = 1.0f;

	if (hp_ratio > 0.0f)
	{
		float fill_w = bar_w * hp_ratio;
		ImVec2 fill_max = ImVec2(right_x + fill_w, cur_y + bar_h);
		dl->AddRectFilled(bar_min, fill_max, accent_color);
	}
	cur_y += 16.0f;

	dl->AddText(ImVec2(right_x, cur_y), IM_COL32(140, 140, 145, 255), "status");
	ImVec2 stat_sz = ImGui::CalcTextSize(status_str.c_str());
	dl->AddText(ImVec2(wmax.x - stat_sz.x - 12.0f, cur_y), status_col, status_str.c_str());
}

void render_t::render_target_hub()
{
	if (!settings::misc::target_hud)
		return;

	ImGuiIO& io = ImGui::GetIO();
	float width = 310.0f;
	float height = 125.0f;

	float pos_x = settings::misc::target_hud_pos_x;
	float pos_y = settings::misc::target_hud_pos_y;

	if ((pos_x < 0.0f || pos_y < 0.0f) && io.DisplaySize.x > 100.0f)
	{
		pos_x = io.DisplaySize.x - width - 20.0f;
		pos_y = io.DisplaySize.y - height - 40.0f;
		settings::misc::target_hud_pos_x = pos_x;
		settings::misc::target_hud_pos_y = pos_y;
	}

	POINT mouse_pt{};
	GetCursorPos(&mouse_pt);
	POINT overlay_pt{ 0, 0 };
	if (detail && detail->window)
		ClientToScreen(detail->window, &overlay_pt);
	ImVec2 mouse_pos = ImVec2(static_cast<float>(mouse_pt.x - overlay_pt.x), static_cast<float>(mouse_pt.y - overlay_pt.y));

	bool hovered = (mouse_pos.x >= pos_x && mouse_pos.x <= pos_x + width && mouse_pos.y >= pos_y && mouse_pos.y <= pos_y + height);
	if (hovered)
	{
		if (orok_config.custom_cursor)
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		else
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}

	bool lbutton_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prev_lbutton_down = false;
	static bool is_dragging = false;
	static ImVec2 drag_offset = ImVec2(0, 0);

	if (hovered && lbutton_down && !prev_lbutton_down)
	{
		is_dragging = true;
		drag_offset = ImVec2(mouse_pos.x - pos_x, mouse_pos.y - pos_y);
	}
	prev_lbutton_down = lbutton_down;

	if (is_dragging)
	{
		if (lbutton_down)
		{
			pos_x = mouse_pos.x - drag_offset.x;
			pos_y = mouse_pos.y - drag_offset.y;

			if (pos_x < 0.0f) pos_x = 0.0f;
			if (pos_y < 0.0f) pos_y = 0.0f;
			if (pos_x + width > io.DisplaySize.x) pos_x = io.DisplaySize.x - width;
			if (pos_y + height > io.DisplaySize.y) pos_y = io.DisplaySize.y - height;

			settings::misc::target_hud_pos_x = pos_x;
			settings::misc::target_hud_pos_y = pos_y;
		}
		else
		{
			is_dragging = false;
		}
	}
	g_target_hub_is_dragging = is_dragging;

	ImU32 accent_col = IM_COL32(
		static_cast<int>(orok_config.menu_color[0] * 255.f),
		static_cast<int>(orok_config.menu_color[1] * 255.f),
		static_cast<int>(orok_config.menu_color[2] * 255.f),
		static_cast<int>(orok_config.menu_color[3] * 255.f)
	);

	ImU32 bg_col = IM_COL32(
		static_cast<int>(orok_config.menu_color_secondary[0] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[1] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[2] * 255.f),
		static_cast<int>(orok_config.menu_color_secondary[3] * 255.f)
	);

	RenderTargetHub(pos_x, pos_y, accent_col, bg_col);
}

static void draw_silentaim_snapline()
{
	if (!settings::silentaim::snapline)
	{
		return;
	}

	bool is_raycast_active = settings::raycast_silentaim::enabled || (settings::silentaim::enabled && settings::silentaim::method == 0);
	bool is_mouse_active = settings::silentaim::enabled && settings::silentaim::method == 1;

	bool target_acquired = false;
	math::vector2 target_client_pos{};

	if (raycast_silentaim::target_acquired)
	{
		target_acquired = true;
		target_client_pos = raycast_silentaim::target_screen_pos;
	}
	else if (is_mouse_active && silentaim::state.data_ready)
	{
		target_acquired = true;
		target_client_pos = silentaim::state.target_screen_pos;
	}

	if (!target_acquired || !game::visualengine)
	{
		return;
	}

	HWND roblox_window = game::get_roblox_window();
	POINT roblox_screen_pt{ 0, 0 };
	if (roblox_window)
	{
		ClientToScreen(roblox_window, &roblox_screen_pt);
	}

	math::vector2 dims = game::visualengine->get_dimensions();

	ImVec2 start_pos(static_cast<float>(roblox_screen_pt.x) + dims.x * 0.5f, static_cast<float>(roblox_screen_pt.y) + dims.y * 0.5f);
	ImVec2 end_pos(static_cast<float>(roblox_screen_pt.x) + target_client_pos.x, static_cast<float>(roblox_screen_pt.y) + target_client_pos.y);

	static ImVec2 anim_start_pos = start_pos;
	static ImVec2 anim_end_pos = end_pos;
	static float anim_alpha = 0.0f;
	static auto last_anim_time = std::chrono::high_resolution_clock::now();

	auto now_anim = std::chrono::high_resolution_clock::now();
	float dt = std::chrono::duration<float>(now_anim - last_anim_time).count();
	last_anim_time = now_anim;
	if (dt > 0.1f) dt = 0.016f;

	if (!settings::silentaim::snapline_lerp)
	{
		anim_start_pos = start_pos;
		anim_end_pos = end_pos;
		anim_alpha = 1.0f;
	}
	else
	{
		float lerp_speed = 22.0f;
		anim_start_pos.x += (start_pos.x - anim_start_pos.x) * (1.0f - std::exp(-lerp_speed * dt));
		anim_start_pos.y += (start_pos.y - anim_start_pos.y) * (1.0f - std::exp(-lerp_speed * dt));
		anim_end_pos.x += (end_pos.x - anim_end_pos.x) * (1.0f - std::exp(-lerp_speed * dt));
		anim_end_pos.y += (end_pos.y - anim_end_pos.y) * (1.0f - std::exp(-lerp_speed * dt));
		anim_alpha += (1.0f - anim_alpha) * (1.0f - std::exp(-15.0f * dt));
	}

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	if (!draw) return;

	float base_alpha = settings::silentaim::snapline_colour[3] * anim_alpha;
	ImU32 col = IM_COL32(
		static_cast<int>(settings::silentaim::snapline_colour[0] * 255.f),
		static_cast<int>(settings::silentaim::snapline_colour[1] * 255.f),
		static_cast<int>(settings::silentaim::snapline_colour[2] * 255.f),
		static_cast<int>(base_alpha * 255.f)
	);

	ImU32 outline_col = IM_COL32(0, 0, 0, static_cast<int>(base_alpha * 220.f));

	ImVec2 s = anim_start_pos;
	ImVec2 e = anim_end_pos;

	draw->AddLine(s, e, outline_col, 2.5f);
	draw->AddLine(s, e, col, 1.2f);
}

static std::string get_short_key_name(int vk_code)
{
	if (vk_code == VK_XBUTTON1) return "xb1";
	if (vk_code == VK_XBUTTON2) return "xb2";
	if (vk_code == VK_LBUTTON) return "mb1";
	if (vk_code == VK_RBUTTON) return "mb2";
	if (vk_code == VK_MBUTTON) return "mb3";
	if (vk_code == VK_SHIFT) return "shift";
	if (vk_code == VK_CONTROL) return "ctrl";
	if (vk_code == VK_MENU) return "alt";
	if (vk_code == VK_CAPITAL) return "caps";
	if (vk_code == VK_SPACE) return "space";
	if (vk_code == VK_TAB) return "tab";
	if (vk_code == VK_INSERT) return "ins";
	if (vk_code == VK_DELETE) return "del";
	if (vk_code == VK_ESCAPE) return "esc";

	std::string name = keybind::get_key_name(vk_code);
	std::transform(name.begin(), name.end(), name.begin(), ::tolower);
	return name;
}



static float get_fov_scale()
{
	uint64_t cam = 0;
	if (game::datamodel && game::datamodel->address >= 0x10000)
	{
		uint64_t ws = memory->read<uint64_t>(game::datamodel->address + Offsets::DataModel::Workspace);
		if (ws >= 0x10000)
		{
			cam = memory->read<uint64_t>(ws + Offsets::Workspace::CurrentCamera);
		}
	}
	if (cam < 0x10000)
	{
		cam = game::camera;
	}

	if (cam < 0x10000)
		return 1.0f;

	float cur = memory->read<float>(cam + Offsets::Camera::FieldOfView);
	if (cur <= 0.001f || std::isnan(cur) || std::isinf(cur))
		return 1.0f;

	float cur_deg = (cur < 3.2f) ? (cur * (180.0f / 3.14159265358979323846f)) : cur;
	if (cur_deg <= 1.0f || cur_deg > 170.0f)
		return 1.0f;

	float scale = 70.0f / cur_deg;
	if (scale < 0.1f) scale = 0.1f;
	if (scale > 10.0f) scale = 10.0f;
	return scale;
}

void render_t::render_visuals()
{
	if (!loader::is_loaded)
		return;

	rbx::g_in_render_context = true;
	esp::run();

	draw_silentaim_snapline();

	if (settings::silentaim::draw_fov)
	{
		float fov_radius = settings::silentaim::fov;
		if (settings::silentaim::dynamic_fov)
		{
			fov_radius *= get_fov_scale();
		}
		draw_fov_circle(fov_radius, settings::silentaim::fov_circle_colour, settings::silentaim::fov_outline_colour, settings::silentaim::fill_fov, settings::silentaim::fov_fill_colour, settings::silentaim::method == 1);
	}

	if (settings::raycast_silentaim::draw_fov)
	{
		float fov_radius = settings::raycast_silentaim::fov;
		if (settings::raycast_silentaim::dynamic_fov || settings::silentaim::dynamic_fov)
		{
			fov_radius *= get_fov_scale();
		}
		draw_fov_circle(fov_radius, settings::raycast_silentaim::fov_circle_colour, settings::raycast_silentaim::fov_outline_colour, settings::raycast_silentaim::fill_fov, settings::raycast_silentaim::fov_fill_colour, false);
	}

	if (settings::aimbot::draw_fov)
	{
		if (settings::aimbot::ring_fov_enabled)
		{
			float inner = settings::aimbot::ring_fov_inner;
			float outer = settings::aimbot::ring_fov_outer;
			if (settings::aimbot::dynamic_fov)
			{
				float scale = get_fov_scale();
				inner *= scale;
				outer *= scale;
			}
			draw_fov_circle(inner, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, false);
			draw_fov_circle(outer, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, false);
		}
		else
		{
			float fov_radius = settings::aimbot::fov;
			if (settings::aimbot::dynamic_fov)
			{
				fov_radius *= get_fov_scale();
			}
			draw_fov_circle(fov_radius, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour, settings::aimbot::fill_fov, settings::aimbot::fov_fill_colour, false);
		}
	}

	if (settings::triggerbot::draw_fov)
	{
		draw_fov_circle(settings::triggerbot::threshold, settings::triggerbot::fov_circle_colour, settings::triggerbot::fov_outline_colour, settings::triggerbot::fill_fov, settings::triggerbot::fov_fill_colour, true);
	}

	draw_keybind_indicator();
	render_target_hub();
	draw_crosshair();
	notifications::render();

	if (game::visualengine && game::visualengine->address != 0)
	{
		const auto fc   = frame_cache::get_for_thread();
		const math::matrix4& view = fc.view;
		const math::vector2& dims = fc.dims;
		if (dims.x > 50.0f && dims.y > 50.0f)
		{
			hitvisuals::render(ImGui::GetBackgroundDrawList(), view, dims);

			HWND roblox_window = game::get_roblox_window();
			POINT roblox_screen_pt{ 0, 0 };
			if (roblox_window)
			{
				ClientToScreen(roblox_window, &roblox_screen_pt);
			}
			POINT overlay_screen_pt{ 0, 0 };
			if (detail && detail->window)
			{
				ClientToScreen(detail->window, &overlay_screen_pt);
			}
			float wp_offset_x = static_cast<float>(roblox_screen_pt.x - overlay_screen_pt.x);
			float wp_offset_y = static_cast<float>(roblox_screen_pt.y - overlay_screen_pt.y);

			waypoints::render_esp(ImGui::GetBackgroundDrawList(), view, dims, ImVec2(wp_offset_x, wp_offset_y));
		}
	}

	float delta_time = ImGui::GetIO().DeltaTime;
	bool hit_event = hitsound::hit_registered.exchange(false, std::memory_order_acq_rel);
	draw_hitmarkers(delta_time, hit_event);
	draw_hit_flash(delta_time, hit_event);

	rbx::g_in_render_context = false;
}
