#include "camera.h"
#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>
#include <core/scanner/rescan.h>
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Cheat {
namespace Features {
namespace CameraSilent {
namespace {

struct CFrameRaw {
    float rx[9]{};
    float px{}, py{}, pz{};
};

std::atomic<bool> g_active{ false };
std::atomic<bool> g_stop{ false };
std::atomic<bool> g_spoofed{ false };
std::atomic<float> g_tx{ 0.f }, g_ty{ 0.f }, g_tz{ 0.f };

HANDLE g_thread = nullptr;
std::uint64_t g_cam = 0;
CFrameRaw g_original_cf{};
bool g_saved = false;

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

CFrameRaw read_cframe(std::uint64_t cam)
{
    return memory->read<CFrameRaw>(cam + Offsets::Camera::Rotation);
}

void write_cframe(std::uint64_t cam, const CFrameRaw& cf)
{
    memory->write<CFrameRaw>(cam + Offsets::Camera::Rotation, cf);
}

CFrameRaw build_lookat(const math::vector3& eye, const math::vector3& target)
{
    float dx = target.x - eye.x;
    float dy = target.y - eye.y;
    float dz = target.z - eye.z;
    float len = std::sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 0.0001f) len = 0.0001f;

    float fwd_x = dx / len;
    float fwd_y = dy / len;
    float fwd_z = dz / len;

    float up_x = 0.f, up_y = 1.f, up_z = 0.f;

    float right_x = up_y * fwd_z - up_z * fwd_y;
    float right_y = up_z * fwd_x - up_x * fwd_z;
    float right_z = up_x * fwd_y - up_y * fwd_x;

    float rlen = std::sqrtf(right_x * right_x + right_y * right_y + right_z * right_z);
    if (rlen < 0.0001f)
    {
        up_x = 0.f; up_y = 0.f; up_z = 1.f;
        right_x = up_y * fwd_z - up_z * fwd_y;
        right_y = up_z * fwd_x - up_x * fwd_z;
        right_z = up_x * fwd_y - up_y * fwd_x;
        rlen = std::sqrtf(right_x * right_x + right_y * right_y + right_z * right_z);
    }
    if (rlen > 0.0001f) { right_x /= rlen; right_y /= rlen; right_z /= rlen; }

    float nup_x = fwd_y * right_z - fwd_z * right_y;
    float nup_y = fwd_z * right_x - fwd_x * right_z;
    float nup_z = fwd_x * right_y - fwd_y * right_x;

    CFrameRaw cf{};
    cf.rx[0] = right_x;  cf.rx[1] = nup_x;  cf.rx[2] = -fwd_x;
    cf.rx[3] = right_y;  cf.rx[4] = nup_y;  cf.rx[5] = -fwd_y;
    cf.rx[6] = right_z;  cf.rx[7] = nup_z;  cf.rx[8] = -fwd_z;
    cf.px = eye.x;
    cf.py = eye.y;
    cf.pz = eye.z;
    return cf;
}

void restore_cframe()
{
    if (!g_cam || !g_saved) return;
    std::uint64_t current = 0;
    if (resolve_cam(current) && current == g_cam)
    {
        write_cframe(g_cam, g_original_cf);
    }
    g_saved = false;
}

DWORD WINAPI writer_thread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    bool on = false;
    int fails = 0;

    while (!g_stop.load(std::memory_order_acquire))
    {
        if (!g_active.load(std::memory_order_acquire))
        {
            if (on)
            {
                restore_cframe();
                g_spoofed.store(false, std::memory_order_release);
                fails = 0;
                on = false;
            }
            Sleep(16);
            continue;
        }

        math::vector3 world{
            g_tx.load(std::memory_order_relaxed),
            g_ty.load(std::memory_order_relaxed),
            g_tz.load(std::memory_order_relaxed)
        };

        if (rescan::is_rescanning.load(std::memory_order_acquire) || rescan::require_rescan.load(std::memory_order_acquire))
        {
            if (on) { g_spoofed.store(false, std::memory_order_release); on = false; fails = 0; }
            Sleep(4);
            continue;
        }

        static std::uint64_t s_resolved_cam = 0;
        static int s_resolve_countdown = 0;

        std::uint64_t cam = 0;
        if (s_resolved_cam != 0 && s_resolve_countdown > 0)
        {
            --s_resolve_countdown;
            cam = s_resolved_cam;
        }
        else if (resolve_cam(cam))
        {
            s_resolved_cam = cam;
            s_resolve_countdown = 8;
        }
        else
        {
            s_resolved_cam = 0;
            s_resolve_countdown = 0;
            if (on && ++fails > 40) { restore_cframe(); g_spoofed.store(false); on = false; fails = 0; }
            Sleep(1);
            continue;
        }

        CFrameRaw current = read_cframe(cam);
        math::vector3 eye{ current.px, current.py, current.pz };

        float dx = world.x - eye.x;
        float dy = world.y - eye.y;
        float dz = world.z - eye.z;
        if ((dx * dx + dy * dy + dz * dz) < 0.01f)
        {
            Sleep(1);
            continue;
        }

        if (!g_saved || g_cam != cam)
        {
            g_original_cf = current;
            g_cam = cam;
            g_saved = true;
        }

        CFrameRaw spoofed = build_lookat(eye, world);
        write_cframe(cam, spoofed);
        g_spoofed.store(true, std::memory_order_release);
        on = true;
        fails = 0;

        Sleep(1);

        restore_cframe();
    }

    restore_cframe();
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
    restore_cframe();
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
    g_saved = false;
}

bool Aiming()
{
    return g_active.load(std::memory_order_acquire) &&
           g_spoofed.load(std::memory_order_acquire);
}

}
}
}