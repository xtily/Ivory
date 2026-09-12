#include "viewport.h"
#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <core/scanner/rescan.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Cheat {
namespace Features {
namespace ViewportSilent {
namespace {

struct Vec2i16 {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

struct Matrix4x4_Raw {
    float view[16]{};
};

std::atomic<bool> g_active{ false };
std::atomic<bool> g_stop{ false };
std::atomic<bool> g_spoofed{ false };
std::atomic<float> g_tx{ 0.f }, g_ty{ 0.f }, g_tz{ 0.f };

HANDLE g_thread = nullptr;
std::uint64_t g_cam = 0;
math::vector2 g_dims{};
Vec2i16 g_last{};
int g_fails = 0;

uintptr_t visual_engine()
{
    uintptr_t mod_base = memory->m_base_address;
    if (!mod_base)
    {
        return 0;
    }

    uintptr_t base = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
    if (!base || base < 0x10000 || base > 0x7FFFFFFFFFFFull)
        base = mod_base;

    uintptr_t ve = memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
    return ve;
}

math::vector2 world_to_screen(const math::vector3& pos, math::vector2& dims_out)
{
    uintptr_t ve = visual_engine();
    if (!ve)
    {
        return {};
    }

    Matrix4x4_Raw raw_matrix = memory->read<Matrix4x4_Raw>(ve + Offsets::VisualEngine::ViewMatrix);
    const float* view = raw_matrix.view;

    dims_out = memory->read<math::vector2>(ve + Offsets::VisualEngine::Dimensions);
    if (dims_out.x < 1.f || dims_out.y < 1.f)
    {
        return {};
    }

    float x = pos.x * view[0] + pos.y * view[1] + pos.z * view[2] + view[3];
    float y = pos.x * view[4] + pos.y * view[5] + pos.z * view[6] + view[7];
    float w = pos.x * view[12] + pos.y * view[13] + pos.z * view[14] + view[15];
    if (w < 0.001f)
    {
        return {};
    }

    float inv = 1.0f / w;
    x *= inv;
    y *= inv;

    return {
        (dims_out.x * 0.5f) * (1.0f + x),
        (dims_out.y * 0.5f) * (1.0f - y)
    };
}

Vec2i16 calc_viewport(const math::vector2& target, const math::vector2& dims, const math::vector2& mouse)
{
    double ty = (double)target.y;
    if (ty > (double)dims.y - 1.0) ty = (double)dims.y - 1.0;
    if (ty < 1.0) ty = 1.0;

    double ratio = (double)mouse.y / ty;
    double vy = (double)dims.y * ratio;
    if (vy > 32767.0) vy = 32767.0;
    if (vy < 1.0) vy = 1.0;

    ratio = vy / (double)dims.y;
    double vx = 2.0 * (double)mouse.x - ratio * (2.0 * (double)target.x - (double)dims.x);
    if (vx > 32767.0) vx = 32767.0;
    if (vx < 1.0) vx = 1.0;

    return { (std::int16_t)std::lround(vx), (std::int16_t)std::lround(vy) };
}

bool mouse_in_viewport(const math::vector2& dims, math::vector2& out)
{
    HWND hwnd = game::get_roblox_window();
    if (!hwnd || !IsWindow(hwnd))
    {
        return false;
    }

    POINT pt{};
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt))
    {
        return false;
    }

    RECT cr{};
    if (!GetClientRect(hwnd, &cr))
    {
        return false;
    }

    float cw = (float)(cr.right - cr.left);
    float ch = (float)(cr.bottom - cr.top);
    if (cw < 1.f || ch < 1.f)
    {
        return false;
    }

    if (pt.x < 0 || pt.y < 0 || pt.x >= cr.right || pt.y >= cr.bottom)
    {
        return false;
    }

    float mx = (float)pt.x * (dims.x / cw);
    float my = (float)pt.y * (dims.y / ch);
    if (mx > dims.x - 1.f) mx = dims.x - 1.f;
    if (mx < 1.f) mx = 1.f;
    if (my > dims.y - 1.f) my = dims.y - 1.f;
    if (my < 1.f) my = 1.f;

    out.x = mx;
    out.y = my;
    return true;
}

bool resolve_cam(std::uint64_t& cam)
{
    uintptr_t dm = game::datamodel ? game::datamodel->address : 0;
    if (!dm) return false;
    uintptr_t workspace = memory->read<uintptr_t>(dm + Offsets::DataModel::Workspace);
    if (workspace < 0x10000 || workspace >= 0x7FFFFFFFFFFFull) return false;
    uintptr_t camera = memory->read<uintptr_t>(workspace + Offsets::Workspace::CurrentCamera);
    if (camera < 0x10000 || camera >= 0x7FFFFFFFFFFFull) return false;
    cam = camera;
    return true;
}

Vec2i16 g_orig_viewport{ 0, 0 };
bool g_has_orig_viewport = false;

void write_viewport(const Vec2i16& v)
{
    if (!g_cam)
    {
        return;
    }

    static int s_validate_countdown = 0;
    if (s_validate_countdown > 0)
    {
        --s_validate_countdown;
    }
    else
    {
        s_validate_countdown = 8;
        std::uint64_t current = 0;
        if (!resolve_cam(current) || current != g_cam)
        {
            return;
        }
    }

    if (!g_spoofed.load(std::memory_order_relaxed))
    {
        Vec2i16 cur = memory->read<Vec2i16>(g_cam + Offsets::Camera::Viewport);
        if (cur.x > 10 && cur.y > 10)
        {
            g_orig_viewport = cur;
            g_has_orig_viewport = true;
        }
    }

    memory->write<Vec2i16>(g_cam + Offsets::Camera::Viewport, v);
    g_spoofed.store(true, std::memory_order_release);
}

void restore_viewport()
{
    if (!g_cam)
    {
        return;
    }

    std::uint64_t current = 0;
    if (!resolve_cam(current) || current != g_cam)
    {
        return;
    }

    Vec2i16 v{};
    if (g_has_orig_viewport && g_orig_viewport.x > 10 && g_orig_viewport.y > 10)
    {
        v = g_orig_viewport;
    }
    else if (g_dims.x >= 1.f && g_dims.y >= 1.f)
    {
        v.x = (std::int16_t)std::lround(g_dims.x);
        v.y = (std::int16_t)std::lround(g_dims.y);
    }
    else
    {
        math::vector2 ve_dims = game::visualengine ? game::visualengine->get_dimensions() : math::vector2{};
        if (ve_dims.x >= 1.f && ve_dims.y >= 1.f)
        {
            v.x = (std::int16_t)std::lround(ve_dims.x);
            v.y = (std::int16_t)std::lround(ve_dims.y);
        }
    }

    if (v.x > 10 && v.y > 10)
    {
        memory->write<Vec2i16>(g_cam + Offsets::Camera::Viewport, v);
    }
}

bool compute(Vec2i16& out)
{
    math::vector3 world{
        g_tx.load(std::memory_order_relaxed),
        g_ty.load(std::memory_order_relaxed),
        g_tz.load(std::memory_order_relaxed)
    };

    std::uint64_t cam = 0;
    if (!resolve_cam(cam))
    {
        return false;
    }

    math::vector2 dims{};
    math::vector2 w2s = world_to_screen(world, dims);
    if (w2s.x <= 0.f || w2s.y <= 0.f || w2s.x >= dims.x || w2s.y >= dims.y)
    {
        return false;
    }

    math::vector2 mouse{};
    if (!mouse_in_viewport(dims, mouse))
    {
        return false;
    }

    out = calc_viewport(w2s, dims, mouse);
    g_cam = cam;
    g_dims = dims;

    if (!g_spoofed.load(std::memory_order_relaxed))
    {
        Vec2i16 cur = memory->read<Vec2i16>(cam + Offsets::Camera::Viewport);
        if (cur.x > 10 && cur.y > 10)
        {
            g_orig_viewport = cur;
            g_has_orig_viewport = true;
        }
    }
    return true;
}

DWORD WINAPI writer_thread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    bool on = false;

    while (!g_stop.load(std::memory_order_acquire))
    {
        if (!g_active.load(std::memory_order_acquire))
        {
            if (on)
            {
                restore_viewport();
                g_spoofed.store(false, std::memory_order_release);
                g_fails = 0;
                on = false;
            }
            Sleep(8);
            continue;
        }

        if (rescan::is_rescanning.load(std::memory_order_acquire) || rescan::require_rescan.load(std::memory_order_acquire))
        {
            if (on) { restore_viewport(); g_spoofed.store(false, std::memory_order_release); on = false; g_fails = 0; }
            Sleep(4);
            continue;
        }

        Vec2i16 v{};
        if (compute(v))
        {
            g_last = v;
            g_fails = 0;
            on = true;
            write_viewport(g_last);
        }
        else if (on && g_fails < 15)
        {
            ++g_fails;
            write_viewport(g_last);
        }
        else if (on)
        {
            restore_viewport();
            g_spoofed.store(false, std::memory_order_release);
            on = false;
            g_fails = 0;
        }

        Sleep(1);
    }

    restore_viewport();
    g_spoofed.store(false, std::memory_order_release);
    return 0;
}

void ensure_thread()
{
    if (g_thread) return;
    g_stop.store(false, std::memory_order_release);
    g_thread = CreateThread(nullptr, 0, &writer_thread, nullptr, 0, nullptr);
    if (g_thread)
        SetThreadPriority(g_thread, THREAD_PRIORITY_HIGHEST);
}

}

void Restore()
{
    g_active.store(false, std::memory_order_release);
    restore_viewport();
    g_spoofed.store(false, std::memory_order_release);
}

void SetActive(bool on, const math::vector3& world_target)
{
    if (!on)
    {
        Restore();
        return;
    }

    ensure_thread();
    g_tx.store(world_target.x, std::memory_order_relaxed);
    g_ty.store(world_target.y, std::memory_order_relaxed);
    g_tz.store(world_target.z, std::memory_order_relaxed);
    g_active.store(true, std::memory_order_release);
}

void Shutdown()
{
    g_active.store(false, std::memory_order_release);
    if (!g_thread) return;
    g_stop.store(true, std::memory_order_release);
    WaitForSingleObject(g_thread, 1000);
    CloseHandle(g_thread);
    g_thread = nullptr;
    g_cam = 0;
    g_dims = {};
}

bool Aiming()
{
    return g_active.load(std::memory_order_acquire) &&
           g_spoofed.load(std::memory_order_acquire);
}

}
}
}
