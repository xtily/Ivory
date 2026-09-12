#include "InstanceNew.h"
#include "InstanceCreate.h"
#include "CallGate.h"
#include "Reflect.h"
#include <sdk/offsets/offsets.h>
#include <features/lua/mem/MemCompat.h>
#include <sdk/cache/core/cache.h>
#include <sdk/math/math.h>
#include <core/logger/logger.h>

namespace InstanceNew {

bool Init() {
    if (CallGate::Ready()) return true;
    bool ok = CallGate::Install();
    if (ok) {
        LOG_SUCCESS("InstanceNew: CallGate engine hook initialized successfully.");
    } else {
        LOG_ERROR("InstanceNew: CallGate initialization failed (Fail code: %d).", CallGate::LastFail());
    }
    return ok;
}

void Shutdown() {
    CallGate::Remove();
}

bool IsReady() {
    return CallGate::Ready();
}

uintptr_t Create(const std::string& className, uintptr_t parent, const PartInitProps* partInit, int timeout_ms) {
    uint64_t inst = 0;
    if (!InstanceCreate::New(className.c_str(), parent, &inst) || !inst) {
        LOG_ERROR("InstanceNew: Failed to create class '%s' (Fail code: %d).", className.c_str(), InstanceCreate::LastFail());
        return 0;
    }

    if (partInit && partInit->apply && inst) {
        uintptr_t prim = Mem::Get().Read<uintptr_t>(inst + Offsets::BasePart::Primitive);
        if (prim) {
            Mem::Get().Write<Vector3>(prim + Offsets::Primitive::Position, Vector3{ partInit->position[0], partInit->position[1], partInit->position[2] });
            Mem::Get().Write<Vector3>(prim + Offsets::Primitive::Size, Vector3{ partInit->size[0], partInit->size[1], partInit->size[2] });
            Mem::Get().Write<float>(inst + Offsets::BasePart::Transparency, partInit->transparency);
        }
    }

    return (uintptr_t)inst;
}

bool SetParent(uintptr_t inst, uintptr_t parent) {
    return InstanceCreate::SetParent(inst, parent);
}

bool SetString(uintptr_t field, const char* text) {
    return InstanceCreate::SetString(field, text);
}

bool SetContent(uintptr_t string_field, const char* text) {
    return InstanceCreate::SetContent(string_field, text);
}

} 


