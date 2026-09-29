#include "EraShift/Graphics/Math.hpp"

#include <cmath>
#include <ostream>

namespace EraShift::Graphics {

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------
Vec2 Vec2::normalized() const
{
    const float len = length();
    if (len <= 1e-9f) {
        return {0.0f, 0.0f};
    }
    return {x / len, y / len};
}

// ---------------------------------------------------------------------------
// Stream inserters
// ---------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const Vec2& v)
{
    return os << '(' << v.x << ", " << v.y << ')';
}

std::ostream& operator<<(std::ostream& os, const Rect& r)
{
    return os << "[x=" << r.x << " y=" << r.y << " w=" << r.w << " h=" << r.h << ']';
}

std::ostream& operator<<(std::ostream& os, const CenteredRect& r)
{
    return os << "[c=(" << r.center.x << ", " << r.center.y << ") w=" << r.w << " h=" << r.h << ']';
}

// ---------------------------------------------------------------------------
// Smoothing
// ---------------------------------------------------------------------------
float dampFactor(float halfLife, double deltaSeconds) noexcept
{
    if (halfLife <= 0.0f) {
        return 1.0f;
    }
    if (deltaSeconds <= 0.0) {
        return 0.0f;
    }
    // Remaining fraction after `deltaSeconds` given a half-life of `halfLife`.
    const double ratio = std::exp2(-deltaSeconds / static_cast<double>(halfLife));
    return static_cast<float>(1.0 - ratio);
}

float dampAngle(float halfLife, double deltaSeconds) noexcept
{
    return dampFactor(halfLife, deltaSeconds);
}

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------
Rect Rect::merge(const Rect& a, const Rect& b) noexcept
{
    if (a.isEmpty()) {
        return b;
    }
    if (b.isEmpty()) {
        return a;
    }
    const float nx = std::min(a.left(), b.left());
    const float ny = std::min(a.top(), b.top());
    const float nr = std::max(a.right(), b.right());
    const float nb = std::max(a.bottom(), b.bottom());
    return {nx, ny, nr - nx, nb - ny};
}

Rect Rect::merge(const Rect& a, const Vec2& point) noexcept
{
    if (a.isEmpty()) {
        return {point.x, point.y, 0.0f, 0.0f};
    }
    const float nx = std::min(a.left(), point.x);
    const float ny = std::min(a.top(), point.y);
    const float nr = std::max(a.right(), point.x);
    const float nb = std::max(a.bottom(), point.y);
    return {nx, ny, nr - nx, nb - ny};
}

// ---------------------------------------------------------------------------
// Overlap
// ---------------------------------------------------------------------------
Rect overlap(const CenteredRect& a, const CenteredRect& b) noexcept
{
    const float left   = std::max(a.left(), b.left());
    const float right  = std::min(a.right(), b.right());
    const float top    = std::max(a.top(), b.top());
    const float bottom = std::min(a.bottom(), b.bottom());

    if (right <= left || bottom <= top) {
        return {}; // no intersection
    }
    return {left, top, right - left, bottom - top};
}

// ---------------------------------------------------------------------------
// Segment tests
// ---------------------------------------------------------------------------
namespace {

/// Returns true when p1 and p2 lie on opposite sides of line ab.
bool straddles(const Vec2& a, const Vec2& b, const Vec2& p1, const Vec2& p2) noexcept
{
    const float d1 = cross(a, b, p1);
    const float d2 = cross(a, b, p2);
    return (d1 > 0.0f) != (d2 > 0.0f) || d1 == 0.0f || d2 == 0.0f;
}

} // namespace

bool segmentIntersectsSegment(const Vec2& p0, const Vec2& p1,
                              const Vec2& q0, const Vec2& q1) noexcept
{
    // Cheap reject using bounding boxes.
    const float minX = std::min(p0.x, p1.x);
    const float maxX = std::max(p0.x, p1.x);
    const float minY = std::min(p0.y, p1.y);
    const float maxY = std::max(p0.y, p1.y);
    if (maxX < std::min(q0.x, q1.x) || minX > std::max(q0.x, q1.x) ||
        maxY < std::min(q0.y, q1.y) || minY > std::max(q0.y, q1.y)) {
        return false;
    }

    if (!straddles(p0, p1, q0, q1) || !straddles(q0, q1, p0, p1)) {
        return false;
    }

    // Collinear overlap needs a bounding-box confirmation.
    const float d1 = cross(p0, p1, q0);
    const float d2 = cross(p0, p1, q1);
    if (d1 == 0.0f && d2 == 0.0f) {
        return true;
    }
    return true;
}

float segmentIntersectsAabb(const Vec2& from, const Vec2& to, const CenteredRect& box) noexcept
{
    // Slab method: clip the parametric segment [0,1] against each axis.
    const Vec2 delta = to - from;
    float tMin = 0.0f;
    float tMax = 1.0f;

    const auto clipAxis = [&](float start, float dir, float low, float high) noexcept {
        if (std::fabs(dir) < 1e-9f) {
            // Parallel to this axis: no hit unless already inside the slab.
            return start >= low && start <= high;
        }
        float t1 = (low - start) / dir;
        float t2 = (high - start) / dir;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!clipAxis(from.x, delta.x, box.left(), box.right())) {
        return -1.0f;
    }
    if (!clipAxis(from.y, delta.y, box.top(), box.bottom())) {
        return -1.0f;
    }
    return tMin;
}

} // namespace EraShift::Graphics
