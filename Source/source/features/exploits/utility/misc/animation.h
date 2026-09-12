#pragma once

namespace animation {
    float ease_expo(float time, float delay, float duration);
    float ease_cubic_out(float progress);
    float ease_cubic_in(float progress);
}
