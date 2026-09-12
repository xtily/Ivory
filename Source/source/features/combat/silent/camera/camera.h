#pragma once
#include <sdk/math/math.h>

namespace Cheat {
namespace Features {
namespace CameraSilent {

void SetActive(bool on, const math::vector3& world_target = {});
void Restore();
void Shutdown();
bool Aiming();

}
}
}
