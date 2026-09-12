#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>

namespace native_render
{
    bool is_active();
    bool try_install();
    void uninstall();
    void start();
}