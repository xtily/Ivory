#pragma once

#include <atomic>
#include <chrono>
#include <sdk/sdk.h>
#include <sdk/math/math.h>

namespace frame_cache
{
    inline std::atomic<uint64_t> frame_counter{ 0 };

    inline math::matrix4 view{};
    inline math::vector2 dims{};
    inline std::atomic<bool> valid{ false };

    inline void tick(rbx::c_visualengine* ve)
    {
        if (!ve || ve->address == 0)
        {
            valid.store(false, std::memory_order_release);
            return;
        }
        view = ve->get_viewmatrix();
        dims = ve->get_dimensions();
        valid.store(true, std::memory_order_release);
        frame_counter.fetch_add(1, std::memory_order_relaxed);
    }
    struct frame_data_t
    {
        math::matrix4 view{};
        math::vector2 dims{};
    };

    inline frame_data_t get_for_thread()
    {
        thread_local frame_data_t  shadow{};
        thread_local uint64_t      last_frame = UINT64_MAX;
        thread_local auto          last_ts    = std::chrono::steady_clock::time_point{};

        const uint64_t cur = frame_counter.load(std::memory_order_relaxed);
        const auto     now = std::chrono::steady_clock::now();
        const auto     age = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ts).count();

        if ((cur != last_frame || age >= 4) && valid.load(std::memory_order_acquire))
        {
            shadow.view = view;
            shadow.dims = dims;
            last_frame  = cur;
            last_ts     = now;
        }
        return shadow;
    }
}