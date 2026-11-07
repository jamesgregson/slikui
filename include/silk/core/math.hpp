// silk/core/math.hpp — minimal 2D math used across the library.
#pragma once

#include <algorithm>
#include <cmath>

namespace silk {

struct Vec2 {
    float x = 0, y = 0;

    Vec2() = default;
    Vec2(float px, float py) : x(px), y(py) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }

    bool operator==(const Vec2& o) const = default;

    float length() const { return std::sqrt(x * x + y * y); }
    static Vec2 zero() { return {0, 0}; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

struct Size {
    float w = 0, h = 0;
    bool operator==(const Size& o) const = default;
};

// Axis-aligned rectangle: position is the top-left corner, size is extent.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    bool operator==(const Rect& o) const = default;

    float left()   const { return x; }
    float top()    const { return y; }
    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    Vec2  origin() const { return {x, y}; }
    Vec2  center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    Size  size()   const { return {w, h}; }

    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }

    bool empty() const { return w <= 0 || h <= 0; }

    Rect intersect(Rect o) const {
        float nx = std::max(x, o.x), ny = std::max(y, o.y);
        float nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
        return {nx, ny, std::max(0.f, nr - nx), std::max(0.f, nb - ny)};
    }

    Rect translate(Vec2 d) const { return {x + d.x, y + d.y, w, h}; }

    Rect inflate(float dx, float dy) const {
        return {x - dx, y - dy, w + 2 * dx, h + 2 * dy};
    }

    Rect inset(float px) const { return {x + px, y + px, w - 2 * px, h - 2 * px}; }
};

// Spacing on each side; also used as layout padding/margins.
struct Padding {
    float l = 0, t = 0, r = 0, b = 0;

    float horizontal() const { return l + r; }
    float vertical()   const { return t + b; }
    Vec2  offset()     const { return {l, t}; }
    Size  shrink(Size s) const { return {s.w - horizontal(), s.h - vertical()}; }
    Rect  apply(Rect a)  const { return {a.x + l, a.y + t, a.w - horizontal(), a.h - vertical()}; }

    static Padding all(float p) { return {p, p, p, p}; }
    static Padding sym(float h, float v) { return {h, v, h, v}; }
};

inline Padding pad(float all) { return Padding::all(all); }
inline Padding pad(float h, float v) { return Padding::sym(h, v); }

} // namespace silk
