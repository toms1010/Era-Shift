// Era Shift - tests for the maths and geometry primitives.
//
// These are the foundation of collision detection, camera work and particle
// motion, so they are the first thing that must be correct.

#include "EraShift/Graphics/Math.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Graphics;

namespace {

constexpr float kEps = 1e-4f;

} // namespace

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------
TEST_CASE("Vec2 arithmetic")
{
    SUBCASE("addition and subtraction are component-wise")
    {
        const Vec2 a{3.0f, -2.0f};
        const Vec2 b{1.5f, 0.5f};
        CHECK(a + b == Vec2{4.5f, -1.5f});
        CHECK(a - b == Vec2{1.5f, -2.5f});
    }

    SUBCASE("scalar multiply works from either side")
    {
        const Vec2 v{2.0f, -3.0f};
        CHECK(v * 2.0f == Vec2{4.0f, -6.0f});
        CHECK(2.0f * v == Vec2{4.0f, -6.0f});
    }

    SUBCASE("length and lengthSquared agree")
    {
        const Vec2 v{3.0f, 4.0f};
        CHECK(v.lengthSquared() == doctest::Approx(25.0f));
        CHECK(v.length() == doctest::Approx(5.0f));
    }

    SUBCASE("normalise produces a unit vector and never divides by zero")
    {
        const Vec2 v{3.0f, 4.0f};
        CHECK(v.normalized().length() == doctest::Approx(1.0f));
        CHECK(Vec2{}.normalized() == Vec2{0.0f, 0.0f});
        CHECK(Vec2{0.0f, 0.0f}.normalized().isZero());
    }

    SUBCASE("dot and cross follow the 2D conventions")
    {
        const Vec2 x{1.0f, 0.0f};
        const Vec2 y{0.0f, 1.0f};
        CHECK(x.dot(y) == doctest::Approx(0.0f));
        CHECK(x.cross(y) == doctest::Approx(1.0f));
        CHECK(y.cross(x) == doctest::Approx(-1.0f));
    }

    SUBCASE("perpendicular rotates 90 degrees counter-clockwise")
    {
        const Vec2 p = Vec2{1.0f, 0.0f}.perpendicular();
        CHECK(p == Vec2{0.0f, 1.0f});
    }
}

// ---------------------------------------------------------------------------
// Rect / CenteredRect
// ---------------------------------------------------------------------------
TEST_CASE("Rect geometry")
{
    SUBCASE("edges derive from position and size")
    {
        const Rect r{10.0f, 20.0f, 100.0f, 50.0f};
        CHECK(r.left() == doctest::Approx(10.0f));
        CHECK(r.top() == doctest::Approx(20.0f));
        CHECK(r.right() == doctest::Approx(110.0f));
        CHECK(r.bottom() == doctest::Approx(70.0f));
        CHECK(r.center() == Vec2{60.0f, 45.0f});
    }

    SUBCASE("contains uses half-open bounds so edges are not double counted")
    {
        const Rect r{0.0f, 0.0f, 10.0f, 10.0f};
        CHECK(r.contains({5.0f, 5.0f}));
        CHECK_FALSE(r.contains({10.0f, 5.0f}));   // right edge is exclusive
        CHECK_FALSE(r.contains({5.0f, 10.0f}));   // bottom edge is exclusive
        CHECK_FALSE(r.contains({-0.01f, 5.0f}));
    }

    SUBCASE("intersects detects touching and overlapping boxes")
    {
        const Rect a{0.0f, 0.0f, 10.0f, 10.0f};
        CHECK(a.intersects(Rect{5.0f, 5.0f, 10.0f, 10.0f}));
        CHECK_FALSE(a.intersects(Rect{10.0f, 0.0f, 10.0f, 10.0f}));  // edge to edge
        CHECK_FALSE(a.intersects(Rect{20.0f, 20.0f, 5.0f, 5.0f}));
    }

    SUBCASE("merge builds the bounding box of two rects")
    {
        const Rect merged = Rect::merge(Rect{0.0f, 0.0f, 10.0f, 10.0f},
                                        Rect{20.0f, -5.0f, 10.0f, 10.0f});
        CHECK(merged == Rect{0.0f, -5.0f, 30.0f, 15.0f});
    }

    SUBCASE("merge ignores empty rects")
    {
        const Rect empty{};
        CHECK(Rect::merge(Rect{1.0f, 2.0f, 3.0f, 4.0f}, empty) == Rect(1.0f, 2.0f, 3.0f, 4.0f));
        CHECK(empty.isEmpty());
    }

    SUBCASE("fromCenter keeps the rectangle centred")
    {
        const Rect r = Rect::fromCenter({50.0f, 50.0f}, 20.0f, 10.0f);
        CHECK(r.center() == Vec2{50.0f, 50.0f});
    }
}

TEST_CASE("CenteredRect conversion round trips")
{
    const CenteredRect box{{32.0f, 64.0f}, 20.0f, 40.0f};
    const Rect asRect = box.toRect();
    CHECK(asRect == Rect{22.0f, 44.0f, 20.0f, 40.0f});
    CHECK(CenteredRect::fromRect(asRect).center == box.center);
}

TEST_CASE("aabbOverlap is symmetric")
{
    const CenteredRect a{{0.0f, 0.0f}, 10.0f, 10.0f};
    const CenteredRect b{{8.0f, 0.0f}, 10.0f, 10.0f};
    const CenteredRect c{{100.0f, 0.0f}, 10.0f, 10.0f};

    CHECK(aabbOverlap(a, b));
    CHECK(aabbOverlap(b, a));
    CHECK_FALSE(aabbOverlap(a, c));
    CHECK_FALSE(aabbOverlap(c, a));
}

TEST_CASE("overlap returns the smallest intersecting box")
{
    SUBCASE("overlapping boxes")
    {
        const CenteredRect a{{0.0f, 0.0f}, 10.0f, 10.0f};
        const CenteredRect b{{5.0f, 0.0f}, 10.0f, 10.0f};
        // a spans -5..5 on x, b spans 0..10, so the shared strip is 0..5.
        const Rect o = overlap(a, b);
        CHECK_FALSE(o.isEmpty());
        CHECK(o == Rect{0.0f, -5.0f, 5.0f, 10.0f});
    }

    SUBCASE("disjoint boxes give an empty rect")
    {
        const CenteredRect a{{0.0f, 0.0f}, 10.0f, 10.0f};
        const CenteredRect b{{50.0f, 50.0f}, 10.0f, 10.0f};
        CHECK(overlap(a, b).isEmpty());
    }

    SUBCASE("a box fully inside another overlaps completely")
    {
        const CenteredRect outer{{0.0f, 0.0f}, 100.0f, 100.0f};
        const CenteredRect inner{{0.0f, 0.0f}, 10.0f, 10.0f};
        const Rect o = overlap(outer, inner);
        CHECK(o.w == doctest::Approx(10.0f));
        CHECK(o.h == doctest::Approx(10.0f));
    }
}

// ---------------------------------------------------------------------------
// Segments
// ---------------------------------------------------------------------------
TEST_CASE("segmentIntersectsSegment")
{
    SUBCASE("crossing segments intersect")
    {
        CHECK(segmentIntersectsSegment({0.0f, 0.0f}, {10.0f, 10.0f},
                                       {0.0f, 10.0f}, {10.0f, 0.0f}));
    }

    SUBCASE("parallel segments do not intersect")
    {
        CHECK_FALSE(segmentIntersectsSegment({0.0f, 0.0f}, {10.0f, 0.0f},
                                             {0.0f, 5.0f}, {10.0f, 5.0f}));
    }

    SUBCASE("a shared endpoint counts as an intersection")
    {
        CHECK(segmentIntersectsSegment({0.0f, 0.0f}, {10.0f, 0.0f},
                                       {10.0f, 0.0f}, {10.0f, 10.0f}));
    }

    SUBCASE("bounding-box rejection is correct")
    {
        CHECK_FALSE(segmentIntersectsSegment({0.0f, 0.0f}, {1.0f, 1.0f},
                                             {50.0f, 50.0f}, {60.0f, 60.0f}));
    }
}

TEST_CASE("segmentIntersectsAabb")
{
    const CenteredRect box{{50.0f, 50.0f}, 20.0f, 20.0f};   // 40..60 on both axes

    SUBCASE("a segment through the box reports the entry parameter")
    {
        const float t = segmentIntersectsAabb({0.0f, 50.0f}, {100.0f, 50.0f}, box);
        REQUIRE(t >= 0.0f);
        CHECK(t == doctest::Approx(0.4f));
    }

    SUBCASE("a segment that misses returns -1")
    {
        CHECK(segmentIntersectsAabb({0.0f, 0.0f}, {10.0f, 10.0f}, box) < 0.0f);
    }

    SUBCASE("starting inside the box returns zero")
    {
        const float t = segmentIntersectsAabb({50.0f, 50.0f}, {60.0f, 50.0f}, box);
        CHECK(t == doctest::Approx(0.0f));
    }

    SUBCASE("a segment parallel to an axis still detects entry")
    {
        const CenteredRect wide{{50.0f, 0.0f}, 20.0f, 200.0f};
        const float t = segmentIntersectsAabb({0.0f, 0.0f}, {100.0f, 0.0f}, wide);
        REQUIRE(t >= 0.0f);
        CHECK(t == doctest::Approx(0.4f));
    }

    SUBCASE("a degenerate segment inside the box hits")
    {
        const float t = segmentIntersectsAabb({50.0f, 50.0f}, {50.0f, 50.0f}, box);
        CHECK(t >= 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Smoothing and interpolation
// ---------------------------------------------------------------------------
TEST_CASE("dampFactor is frame-rate independent")
{
    SUBCASE("one half-life moves halfway")
    {
        CHECK(dampFactor(1.0f, 1.0) == doctest::Approx(0.5f));
    }

    SUBCASE("two half-lives move three quarters")
    {
        CHECK(dampFactor(1.0f, 2.0) == doctest::Approx(0.75f));
    }

    SUBCASE("the result does not depend on how the time is sliced")
    {
        // Two 0.5 s steps must match one 1.0 s step.
        const float oneStep   = dampFactor(0.5f, 1.0);
        const float twoSteps  = dampFactor(0.5f, 0.5);
        const float combined  = 1.0f - (1.0f - twoSteps) * (1.0f - twoSteps);
        CHECK(oneStep == doctest::Approx(combined).epsilon(1e-4));
    }

    SUBCASE("degenerate inputs are safe")
    {
        CHECK(dampFactor(0.0f, 1.0) == doctest::Approx(1.0f));
        CHECK(dampFactor(1.0f, 0.0) == doctest::Approx(0.0f));
        CHECK(dampFactor(1.0f, -1.0) == doctest::Approx(0.0f));
    }
}

TEST_CASE("scalar helpers")
{
    SUBCASE("lerp interpolates linearly")
    {
        CHECK(lerp(0.0f, 10.0f, 0.25f) == doctest::Approx(2.5f));
        CHECK(lerp(Vec2{0.0f, 0.0f}, Vec2{10.0f, 20.0f}, 0.5f) == Vec2{5.0f, 10.0f});
    }

    SUBCASE("inverseLerp inverts lerp")
    {
        CHECK(inverseLerp(2.0f, 6.0f, 4.0f) == doctest::Approx(0.5f));
        CHECK(inverseLerp(5.0f, 5.0f, 7.0f) == doctest::Approx(0.0f));  // no division by zero
    }

    SUBCASE("wrap handles negatives")
    {
        CHECK(wrap(7.0f, 5.0f) == doctest::Approx(2.0f));
        CHECK(wrap(-1.0f, 5.0f) == doctest::Approx(4.0f));
        CHECK(wrap(0.0f, 5.0f) == doctest::Approx(0.0f));
        CHECK(wrap(3.0f, 0.0f) == doctest::Approx(0.0f));
    }

    SUBCASE("moveTowards never overshoots")
    {
        CHECK(moveTowards(0.0f, 10.0f, 3.0f) == doctest::Approx(3.0f));
        CHECK(moveTowards(0.0f, 2.0f, 3.0f) == doctest::Approx(2.0f));
        CHECK(moveTowards(0.0f, -2.0f, 3.0f) == doctest::Approx(-2.0f));
    }

    SUBCASE("clampValue constrains to the range")
    {
        CHECK(clampValue(5, 0, 10) == 5);
        CHECK(clampValue(-5, 0, 10) == 0);
        CHECK(clampValue(50, 0, 10) == 10);
    }
}
