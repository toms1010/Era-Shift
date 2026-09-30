// Era Shift - small maths and geometry primitives.
//
// Kept dependency free (no SDL, no GLM) so gameplay, physics and tests can all
// use them. Everything is constexpr-friendly and trivially copyable.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iosfwd>
#include <limits>

namespace EraShift::Graphics {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }

    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }


    /// Flips the vector onto the unit circle, preserving direction.
    [[nodiscard]] Vec2 normalized() const;
    [[nodiscard]] float length() const { return std::sqrt(x * x + y * y); }
    [[nodiscard]] float lengthSquared() const { return x * x + y * y; }
    [[nodiscard]] bool isZero(float epsilon = 1e-6f) const { return lengthSquared() <= epsilon * epsilon; }
    [[nodiscard]] float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    /// 2D cross product magnitude (z of the 3D cross product).
    [[nodiscard]] float cross(const Vec2& o) const { return x * o.y - y * o.x; }
    /// Rotates 90 degrees counter-clockwise.
    [[nodiscard]] constexpr Vec2 perpendicular() const { return {-y, x}; }
};

constexpr Vec2 operator*(float s, const Vec2& v) { return {v.x * s, v.y * s}; }

/// Free operators so that expression-composition macros (doctest, Catch2, gtest)
/// can decompose `a == b` without them being hidden inside the class.
constexpr bool operator==(const Vec2& a, const Vec2& b) noexcept { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(const Vec2& a, const Vec2& b) noexcept { return !(a == b); }

/// Linear interpolation. `t` is not clamped so it can overshoot for effects.
[[nodiscard]] constexpr Vec2 lerp(const Vec2& a, const Vec2& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

/// Frame-rate independent exponential smoothing.
///
/// `halfLife` is the time in seconds for the remaining distance to halve.
/// A value of 0 gives instant snapping.
[[nodiscard]] float dampFactor(float halfLife, double deltaSeconds) noexcept;

/// Damping factor for an angular value, wrapping at a full turn.
///
/// Unlike `dampFactor` this takes the short way round: a camera at 350 degrees
/// approaching 10 degrees damps through 360, not backwards through 180.
[[nodiscard]] float dampAngle(float halfLife, double deltaSeconds) noexcept;

/// Axis-aligned rectangle defined by its top-left corner and size.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    [[nodiscard]] constexpr float left() const { return x; }
    [[nodiscard]] constexpr float top() const { return y; }
    [[nodiscard]] constexpr float right() const { return x + w; }
    [[nodiscard]] constexpr float bottom() const { return y + h; }
    [[nodiscard]] constexpr Vec2  position() const { return {x, y}; }
    [[nodiscard]] constexpr Vec2  size() const { return {w, h}; }
    [[nodiscard]] constexpr Vec2  center() const { return {x + w * 0.5f, y + h * 0.5f}; }

    [[nodiscard]] constexpr bool isEmpty() const { return w <= 0.0f || h <= 0.0f; }

    [[nodiscard]] constexpr bool contains(const Vec2& p) const
    {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }

    [[nodiscard]] constexpr bool intersects(const Rect& o) const
    {
        return x < o.right() && right() > o.x && y < o.bottom() && bottom() > o.y;
    }

    [[nodiscard]] static constexpr Rect fromCenter(const Vec2& c, float w, float h)
    {
        return {c.x - w * 0.5f, c.y - h * 0.5f, w, h};
    }

    /// Smallest rectangle containing both rectangles. An empty operand is
    /// ignored, which makes building a bounds volume out of a list of rects
    /// straightforward.
    [[nodiscard]] static Rect merge(const Rect& a, const Rect& b) noexcept;
    [[nodiscard]] static Rect merge(const Rect& a, const Vec2& point) noexcept;
};

constexpr bool operator==(const Rect& a, const Rect& b) noexcept
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
constexpr bool operator!=(const Rect& a, const Rect& b) noexcept { return !(a == b); }

/// Rectangle with position stored as the centre, the form sprites and
/// colliders use most often.
struct CenteredRect {
    Vec2  center;
    float w = 0.0f;
    float h = 0.0f;

    constexpr CenteredRect() = default;
    constexpr CenteredRect(float cx, float cy, float w_, float h_) : center(cx, cy), w(w_), h(h_) {}
    constexpr CenteredRect(const Vec2& c, float w_, float h_) : center(c), w(w_), h(h_) {}

    [[nodiscard]] constexpr float left() const { return center.x - w * 0.5f; }
    [[nodiscard]] constexpr float top() const { return center.y - h * 0.5f; }
    [[nodiscard]] constexpr float right() const { return center.x + w * 0.5f; }
    [[nodiscard]] constexpr float bottom() const { return center.y + h * 0.5f; }

    [[nodiscard]] constexpr bool contains(const Vec2& p) const
    {
        return p.x >= left() && p.x < right() && p.y >= top() && p.y < bottom();
    }

    [[nodiscard]] constexpr bool intersects(const CenteredRect& o) const
    {
        return left() < o.right() && right() > o.left() && top() < o.bottom() && bottom() > o.top();
    }

    [[nodiscard]] constexpr Rect toRect() const { return {left(), top(), w, h}; }
    [[nodiscard]] static constexpr CenteredRect fromRect(const Rect& r) noexcept
    {
        return {r.center(), r.w, r.h};
    }
};

/// Separating-axis test between two axis-aligned boxes.
[[nodiscard]] constexpr bool aabbOverlap(const CenteredRect& a, const CenteredRect& b)
{
    return a.intersects(b);
}

/// Smallest axis-aligned overlap of two boxes, used to push colliders apart.
/// Returns an empty rect when the boxes do not overlap.
[[nodiscard]] Rect overlap(const CenteredRect& a, const CenteredRect& b) noexcept;

/// Signed area of the triangle abc. Sign gives the winding order.
[[nodiscard]] constexpr float cross(const Vec2& a, const Vec2& b, const Vec2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// Segment intersection test used for line-of-sight and projectile sweeps.
[[nodiscard]] bool segmentIntersectsSegment(const Vec2& p0, const Vec2& p1,
                                           const Vec2& q0, const Vec2& q1) noexcept;

/// Slab-method ray/AABB test.
/// @return t in [0,1] of the first hit, or -1 when the segment misses.
[[nodiscard]] float segmentIntersectsAabb(const Vec2& from, const Vec2& to,
                                          const CenteredRect& box) noexcept;

/// Stream inserters, used by logs and test failure messages.
std::ostream& operator<<(std::ostream& os, const Vec2& v);
std::ostream& operator<<(std::ostream& os, const Rect& r);
std::ostream& operator<<(std::ostream& os, const CenteredRect& r);

[[nodiscard]] constexpr bool nearlyEqual(float a, float b, float epsilon = 1e-5f) noexcept
{
    return std::fabs(a - b) <= epsilon;
}

template <typename T>
[[nodiscard]] constexpr T clampValue(T value, T low, T high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] constexpr float inverseLerp(float a, float b, float v) noexcept
{
    return (b - a) == 0.0f ? 0.0f : (v - a) / (b - a);
}

/// Wraps a value into [0, range). Correct for negative inputs.
[[nodiscard]] constexpr float wrap(float value, float range) noexcept
{
    return range <= 0.0f ? 0.0f : value - range * std::floor(value / range);
}

/// Moves `current` towards `target` by at most `maxDelta`.
[[nodiscard]] constexpr float moveTowards(float current, float target, float maxDelta) noexcept
{
    const float diff = target - current;
    if (std::fabs(diff) <= maxDelta) {
        return target;
    }
    return current + (diff > 0.0f ? maxDelta : -maxDelta);
}

} // namespace EraShift::Graphics
