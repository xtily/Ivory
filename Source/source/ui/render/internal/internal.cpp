#include "native_render.h"
#include <ui/render/render.h>
#include <features/system/settings/settings.h>
#include <core/globals.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace native_render
{
    static std::atomic<bool>  g_installed{ false };
    static std::atomic<bool>  g_initialized{ false }; 
    static std::mutex         g_mtx;

    static ID3D11Device*            g_game_device         = nullptr;
    static ID3D11DeviceContext*     g_game_ctx             = nullptr;
    static IDXGISwapChain*          g_game_swapchain       = nullptr;
    static ID3D11RenderTargetView*  g_game_rtv             = nullptr;
    static HWND                     g_game_hwnd            = nullptr;

    using fn_Present        = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using fn_ResizeBuffers  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    static fn_Present       g_orig_present        = nullptr;
    static fn_ResizeBuffers g_orig_resize_buffers  = nullptr;

    static constexpr int VTABLE_PRESENT        = 8;
    static constexpr int VTABLE_RESIZE_BUFFERS = 13;

    static bool create_rtv()
    {
        if (!g_game_device || !g_game_swapchain) return false;

        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(g_game_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
            return false;

        HRESULT hr = g_game_device->CreateRenderTargetView(back_buffer, nullptr, &g_game_rtv);
        back_buffer->Release();
        return SUCCEEDED(hr);
    }

    static void destroy_rtv()
    {
        if (g_game_rtv) { g_game_rtv->Release(); g_game_rtv = nullptr; }
    }

    static void patch_vtable(void** vtable, int slot, void* fn, void** orig)
    {
        DWORD old_prot = 0;
        VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old_prot);
        if (orig) *orig = vtable[slot];
        vtable[slot] = fn;
        VirtualProtect(&vtable[slot], sizeof(void*), old_prot, &old_prot);
    }

    static HRESULT STDMETHODCALLTYPE hook_resize_buffers(
        IDXGISwapChain* sc, UINT bc, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
    {
        destroy_rtv();
        HRESULT hr = g_orig_resize_buffers(sc, bc, w, h, fmt, flags);
        create_rtv();
        return hr;
    }
     
    static HRESULT STDMETHODCALLTYPE hook_present(IDXGISwapChain* sc, UINT sync, UINT flags)
    {
        if (!settings::misc::native_render)
            return g_orig_present(sc, sync, flags);

        if (!g_initialized.load())
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (!g_initialized.load())
            {
                sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_game_device));
                if (!g_game_device) goto call_orig;

                g_game_device->GetImmediateContext(&g_game_ctx);
                g_game_swapchain = sc;

                DXGI_SWAP_CHAIN_DESC scd{};
                sc->GetDesc(&scd);
                g_game_hwnd = scd.OutputWindow;

                if (!create_rtv()) goto call_orig;

                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();

                ImGui_ImplWin32_Init(g_game_hwnd);
                ImGui_ImplDX11_Init(g_game_device, g_game_ctx);

                if (render && render->detail && render->detail->window)
                    ShowWindow(render->detail->window, SW_HIDE);

                g_initialized.store(true);
            }
        }

        if (!g_initialized.load() || !g_game_rtv) goto call_orig;

        {
            g_game_ctx->OMSetRenderTargets(1, &g_game_rtv, nullptr);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            frame_cache::tick(game::visualengine.get());

            render->render_visuals();
            render->render_menu();
            render->render_topbar();

            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

    call_orig:
        return g_orig_present(sc, sync, flags);
    }

    static bool hook_swapchain()
    {
        HWND dummy_hwnd = CreateWindowExA(0, "STATIC", "nr_dummy",
            WS_POPUP, 0, 0, 2, 2, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!dummy_hwnd) return false;

        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount       = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.Width  = 2;
        scd.BufferDesc.Height = 2;
        scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow      = dummy_hwnd;
        scd.SampleDesc.Count  = 1;
        scd.Windowed          = TRUE;
        scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device*      dummy_dev = nullptr;
        ID3D11DeviceContext* dummy_ctx = nullptr;
        IDXGISwapChain*    dummy_sc  = nullptr;
        D3D_FEATURE_LEVEL  fl;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &scd, &dummy_sc, &dummy_dev, &fl, &dummy_ctx);

        if (FAILED(hr)) { DestroyWindow(dummy_hwnd); return false; }

        void** vtable = *reinterpret_cast<void***>(dummy_sc);

        patch_vtable(vtable, VTABLE_PRESENT,
            reinterpret_cast<void*>(&hook_present),
            reinterpret_cast<void**>(&g_orig_present));

        patch_vtable(vtable, VTABLE_RESIZE_BUFFERS,
            reinterpret_cast<void*>(&hook_resize_buffers),
            reinterpret_cast<void**>(&g_orig_resize_buffers));

        dummy_sc->Release();
        dummy_dev->Release();
        dummy_ctx->Release();
        DestroyWindow(dummy_hwnd);

        g_installed.store(true);
        return true;
    }

    bool is_active()
    {
        return g_initialized.load() && settings::misc::native_render;
    }

    bool try_install()
    {
        if (g_installed.load()) return true;
        return hook_swapchain();
    }

    void uninstall()
    {
        if (!g_installed.load()) return;
        std::lock_guard<std::mutex> lk(g_mtx);

        if (g_initialized.load())
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            g_initialized.store(false);
        }

        destroy_rtv();
        if (g_game_ctx) { g_game_ctx->Release(); g_game_ctx = nullptr; }
        if (g_game_device) { g_game_device->Release(); g_game_device = nullptr; }
        g_game_swapchain = nullptr;
        g_game_hwnd = nullptr;

        g_installed.store(false);
    }

    void start()
    {
        std::thread([]() {
            while (!try_install())
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }).detach();
    }

}
