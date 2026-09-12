#pragma once
#include <algorithm>
#include <cmath>

namespace animation {
    inline float ease_expo(float time, float delay, float duration) {
        if (duration <= 0.0f) {
            return 1.0f;
        }
        if (time <= delay) {
            return 0.0f;
        }
        float progress = (time - delay) / duration;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        if (progress >= 1.0f) {
            return 1.0f;
        }
        return 1.0f - std::pow(2.0f, -10.0f * progress);
    }

    inline float ease_cubic_out(float progress) {
        return 1.0f - std::pow(1.0f - progress, 3.0f);
    }

    inline float ease_cubic_in(float progress) {
        return progress * progress * progress;
    }
}
