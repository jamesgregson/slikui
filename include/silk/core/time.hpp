// silk/core/time.hpp — per-frame time passed to Context::tick().
#pragma once

namespace silk {

struct Time {
    double now = 0;   // seconds since first tick (monotonic)
    double dt = 0;    // seconds since previous tick, clamped

    explicit operator bool() const { return dt > 0; }
};

// Library-provided clamped frame delta (avoid huge dt after suspensions).
inline double clamp_dt(double dt, double max = 0.1) {
    return dt < 0 ? 0 : (dt > max ? max : dt);
}

} // namespace silk
