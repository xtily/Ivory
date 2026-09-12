#pragma once
#include <cstdint>
#include <string>

namespace InstanceNew {

    struct PartInitProps {
        bool    apply       = false;
        float   position[3] = { 0.f, 0.f, 0.f };
        float   size[3]     = { 4.f, 1.f, 4.f };
        uint32_t color      = 0x00FFFFFFu;
        float   transparency = 0.f;
        bool    can_collide = true;
    };

    bool Init();

    void Shutdown();

    bool IsReady();

    uintptr_t Create(const std::string& className, uintptr_t parent,
                     const PartInitProps* partInit = nullptr,
                     int timeout_ms = 2000);

    bool SetParent(uintptr_t inst, uintptr_t parent);

    bool SetString(uintptr_t field, const char* text);

    bool SetContent(uintptr_t string_field, const char* text);
}


