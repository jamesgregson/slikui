// slikui/style/anim.hpp — tiny 1D tween used for hover/press transitions.
#pragma once

#include "theme.hpp"
#include "slikui/core/time.hpp"

#include <cmath>

namespace slikui {

// Self-animating scalar: advance toward `target` each tick at `speed` per
// second (exponential ease-out). Widgets hold these by value.
struct Anim {
    float value = 0, target = 0;
    float speed = 12.f; // higher = snappier

    bool settled() const { return value == target; }

    bool advance(double dt) {
        if (settled()) return false;
        float k = 1.f - std::exp(-float(speed * dt));
        value += (target - value) * k;
        if (std::fabs(value - target) < 0.001f) value = target;
        return true;
    }
};

} // namespace slikui
