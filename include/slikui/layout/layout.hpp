// slikui/layout/layout.hpp — constraint-based layout primitives.
//
// Widgets implement two passes:
//   measure(area)  -> intrinsic Size under Constraints
//   arrange(area)  -> assign final frames to self + children
// Flex distribution (how Row/Column share space) lives here too so both
// containers and tests can use the exact same math.
#pragma once

#include "slikui/core/math.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace slikui {

inline constexpr float kInfinite = 1e9f;

// Cross-axis alignment used by containers and text.
enum class CrossMode : std::uint8_t { Stretch, Start, Center };

enum class Sizing : std::uint8_t { Hug, Fixed, Expand };

// Bounds within which a widget must size itself.
struct Constraints {
    Vec2 min = Vec2::zero();
    Vec2 max = {kInfinite, kInfinite};

    static Constraints loose(Vec2 maximum = {kInfinite, kInfinite}) {
        return {Vec2::zero(), maximum};
    }
    static Constraints tight(Size s) {
        return {{s.w, s.h}, {s.w, s.h}};
    }

    bool is_bounded_x() const { return max.x < kInfinite; }
    bool is_bounded_y() const { return max.y < kInfinite; }

    Size clamp(Size s) const {
        return {std::clamp(s.w, min.x, max.x), std::clamp(s.h, min.y, max.y)};
    }

    Constraints loosen_x() const { return {Vec2::zero(), {kInfinite, max.y}}; }
    Constraints loosen_y() const { return {Vec2::zero(), {max.x, kInfinite}}; }

    Constraints tighten(const Padding& p) const {
        Vec2 lo{min.x + p.horizontal(), min.y + p.vertical()};
        Vec2 hi{max.x - p.horizontal(), max.y - p.vertical()};
        return {lo, hi};
    }
};

// Distributes `total` main-axis space across children.
// `measured` are hug sizes, `expand` children split the leftover equally.
// Returns {offset, size} per child (input order preserved).
inline std::vector<std::pair<float, float>> distribute(
    float total, const std::vector<float>& measured,
    const std::vector<bool>& expand, float gap) {

    size_t n = measured.size();
    std::vector<std::pair<float, float>> out(n, {0.f, 0.f});
    if (n == 0) return out;

    float used = gap * float(n - 1);
    for (float m : measured) used += m;

    float extra = 0;
    size_t expanders = 0;
    for (size_t i = 0; i < n; ++i)
        if (expand[i]) ++expanders;

    if (expanders > 0 && total > used) {
        extra = (total - used) / float(expanders);
        used = total;
    }

    float pos = 0;
    for (size_t i = 0; i < n; ++i) {
        float sz = measured[i] + (expand[i] ? extra : 0);
        out[i] = {pos, sz};
        pos += sz + gap;
    }
    return out;
}

} // namespace slikui
