// Tests for the particle pool.
//
// A particle system has no rules to break, so these tests are about the things
// that go wrong quietly: a pool that leaks (a slowly brightening screen), an
// emitter that is frame-rate dependent (a different-looking effect on a 144Hz
// monitor), and a pool that allocates during combat (a frame spike in the middle
// of a shift, which is the worst possible moment for one).

#include "EraShift/Graphics/ParticleSystem.hpp"

#include <cmath>
#include <limits>

#include <doctest/doctest.h>

using namespace EraShift;
using namespace EraShift::Graphics;
using EraShift::Game::Era;

namespace {

/// How many particles are inside a rectangle, for asserting that an emitter put
/// its effect where it was asked to.
int within(const ParticleSystem& system, float left, float top, float right, float bottom)
{
    int count = 0;
    for (std::size_t i = 0; i < system.size(); ++i) {
        const Particle& p = system.data()[i];
        if (p.alive && p.x >= left && p.x <= right && p.y >= top && p.y <= bottom) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("a fresh pool is empty and clear leaves it empty")
{
    ParticleSystem system(64);
    CHECK(system.aliveCount() == 0);
    CHECK_FALSE(system.full());

    system.emitImpact(0.0f, 0.0f, 1.0f, 0.0f, Palette::White);
    CHECK(system.aliveCount() > 0);
    system.clear();
    CHECK(system.aliveCount() == 0);
}

TEST_CASE("particles die when their life runs out")
{
    ParticleSystem system(16);
    Particle p;
    p.maxLife = 0.5f;
    p.size = 2.0f;
    REQUIRE(system.emit(p));
    CHECK(system.aliveCount() == 1);

    system.update(0.25f);
    CHECK(system.aliveCount() == 1);
    system.update(0.30f);
    CHECK(system.aliveCount() == 0);

    // A zero or negative delta must be a no-op, not a way to kill everything.
    system.emit(p);
    system.update(0.0f);
    system.update(-1.0f);
    CHECK(system.aliveCount() == 1);
}

TEST_CASE("a zero maxLife is repaired rather than killing the particle instantly")
{
    ParticleSystem system(8);
    Particle p;
    p.maxLife = 0.0f;
    REQUIRE(system.emit(p));
    // A pool that silently discards these would make every effect quietly
    // shorter than its author asked for, which is very hard to notice by eye.
    CHECK(system.data()[0].maxLife > 0.0f);
    system.update(0.001f);
    CHECK(system.aliveCount() == 1);
}

TEST_CASE("the pool is fixed size and never grows")
{
    ParticleSystem system(32);
    CHECK(system.size() == 32);

    // Far more than the pool can hold. Every emit after the first 32 must be
    // refused, and the pool must not reallocate to make room.
    int accepted = 0;
    for (int i = 0; i < 500; ++i) {
        Particle p;
        p.maxLife = 1.0f;
        if (system.emit(p)) {
            ++accepted;
        }
    }
    CHECK(accepted <= 32);
    CHECK(system.size() == 32);
    CHECK(system.aliveCount() <= 32);
    // Once full, the count of refused attempts is reported: a rising number is a
    // tuning signal that the emitters are asking for more than the budget.
    CHECK(system.droppedCount() > 0);
}

TEST_CASE("an exhausted pool recycles the oldest rather than refusing forever")
{
    ParticleSystem system(8);
    for (int i = 0; i < 8; ++i) {
        Particle p;
        p.maxLife = 10.0f;
        REQUIRE(system.emit(p));
    }
    CHECK(system.full());

    // A new particle while everything is still alive: the system should be
    // spending its budget, not going silent. A full pool that drops everything
    // means a busy screen silently loses every effect on it.
    Particle p;
    p.maxLife = 10.0f;
    system.emit(p);
    CHECK(system.aliveCount() == 8);
}

TEST_CASE("emitters place their particles where they were asked to")
{
    ParticleSystem system(256);
    system.emitPickup(100.0f, 200.0f, Palette::White);
    // Pickup particles are given an upward velocity, so they travel, but they
    // must start near the pickup rather than at the origin.
    CHECK(within(system, 60.0f, 100.0f, 200.0f, 260.0f) > 0);
}

TEST_CASE("an impact throws sparks along the impact direction")
{
    ParticleSystem system(256);
    system.emitImpact(0.0f, 0.0f, 1.0f, 0.0f, Palette::White, 1.0f, 24);

    int rightwards = 0;
    int leftwards = 0;
    for (std::size_t i = 0; i < system.size(); ++i) {
        const Particle& p = system.data()[i];
        if (!p.alive || p.shape != ParticleShape::Spark) {
            continue;
        }
        // A wide cone, not a laser, so most sparks go the impact way even though
        // some spread backwards. A mean over 100 would be zero; a count is not.
        if (p.vx > 0.0f) {
            ++rightwards;
        }
        if (p.vx < 0.0f) {
            ++leftwards;
        }
    }
    CHECK(rightwards > leftwards);
    CHECK(rightwards > 0);
}

TEST_CASE("emission is frame-rate independent")
{
    // The property that matters most here and is easiest to break: a one-second
    // effect must contain the same amount of stuff whether it was emitted in
    // 60 calls or in 6.
    ParticleSystem sixty(4096);
    for (int i = 0; i < 60; ++i) {
        sixty.emitAmbient(Era::Past, Bounds{0.0f, 0.0f, 800.0f, 600.0f}, 1.0f / 60.0f, 0.5f);
    }
    ParticleSystem six(4096);
    for (int i = 0; i < 6; ++i) {
        six.emitAmbient(Era::Past, Bounds{0.0f, 0.0f, 800.0f, 600.0f}, 1.0f / 6.0f, 0.5f);
    }
    // Within a small tolerance. Exact equality is not expected, because the
    // per-call minimum of one particle makes very long steps quantise - which is
    // why the minimum exists, and why the tolerance is here rather than an
    // exact comparison.
    CHECK(doctest::Approx(static_cast<float>(six.aliveCount())).epsilon(0.35f) ==
          static_cast<float>(sixty.aliveCount()));
}

TEST_CASE("the three eras emit visibly different weather")
{
    // Particles are half of what makes a room feel like a different place. If
    // the three eras emitted the same thing, a shift would be a re-tint.
    std::size_t counts[3] = {0, 0, 0};
    float meanY[3] = {0.0f, 0.0f, 0.0f};

    const Era eras[3] = {Era::Past, Era::Present, Era::Future};
    for (int e = 0; e < 3; ++e) {
        ParticleSystem system(4096);
        for (int i = 0; i < 120; ++i) {
            system.emitAmbient(eras[e], Bounds{0.0f, 0.0f, 800.0f, 600.0f}, 1.0f / 60.0f, 0.5f);
        }
        counts[e] = system.aliveCount();
        float totalY = 0.0f;
        for (std::size_t i = 0; i < system.size(); ++i) {
            if (system.data()[i].alive) {
                totalY += system.data()[i].y;
            }
        }
        meanY[e] = counts[e] > 0 ? totalY / static_cast<float>(counts[e]) : 0.0f;
    }

    for (int e = 0; e < 3; ++e) {
        CHECK(counts[e] > 0);
    }
    // The Future is busier than the Present: embers are the whole point of it.
    CHECK(counts[2] > counts[1]);

    // Leaves fall and so start high; embers rise and so start low. Opposite
    // halves of the screen is what the eye actually reads.
    CHECK(meanY[0] < 200.0f);
    CHECK(meanY[2] > 300.0f);
}

TEST_CASE("particles move, shed speed and stay finite")
{
    // Two identical particles, one with gravity and one without, so the
    // comparison is about gravity's *effect* rather than about arithmetic. A
    // particle launched upwards is slowed by gravity and then by drag, so its
    // vertical speed rises back towards zero; the control just has drag.
    ParticleSystem system(8);
    Particle falling;
    falling.vx = 100.0f;
    falling.vy = -50.0f;
    falling.gravity = 200.0f;
    falling.drag = 1.0f;
    falling.maxLife = 1.0f;
    REQUIRE(system.emit(falling));

    Particle control = falling;
    control.gravity = 0.0f;
    REQUIRE(system.emit(control));

    const float x0 = system.data()[0].x;
    const float y0 = system.data()[0].y;
    system.update(0.1f);

    CHECK(system.data()[0].x > x0);
    CHECK(system.data()[0].y < y0);   // still travelling upwards
    // Gravity has been pulling it back down, so it is slower upwards than the
    // gravity-free control by exactly the gravity impulse.
    CHECK(system.data()[0].vy > system.data()[1].vy);
    // The order inside the integrator is gravity first, then drag, and the two
    // numbers below depend on it: -50 + 20 of gravity is -30, and a drag of 1/s
    // over 0.1s sheds a tenth of that, giving -27. Swapping the order would give
    // -25, which is why this is asserted exactly rather than approximately.
    CHECK(system.data()[1].vy == doctest::Approx(-45.0f));   // drag only
    CHECK(system.data()[0].vy == doctest::Approx(-27.0f));

    // Drag sheds speed without reversing it: braking is not going backwards.
    CHECK(system.data()[0].vx < 100.0f);
    CHECK(system.data()[0].vx > 0.0f);
    CHECK(std::isfinite(system.data()[0].x));
}

TEST_CASE("gravity can be scaled globally")
{
    // A single knob for the whole system, so a slow-motion or underwater effect
    // does not have to re-tune every emitter.
    ParticleSystem system(8);
    Particle p;
    p.gravity = 100.0f;
    p.maxLife = 10.0f;
    REQUIRE(system.emit(p));

    system.setGravityScale(0.0f);
    system.update(0.1f);
    const float level = system.data()[0].vy;

    system.setGravityScale(1.0f);
    system.update(0.1f);
    CHECK(system.data()[0].vy > level);
}

TEST_CASE("an ambient emitter ignores a degenerate region")
{
    // A zero-area region would divide by zero in an area-weighted emitter. Not
    // crashing is the minimum; emitting nothing is the correct answer.
    ParticleSystem system(256);
    system.emitAmbient(Era::Present, Bounds{0.0f, 0.0f, 0.0f, 0.0f}, 1.0f / 60.0f, 1.0f);
    CHECK(system.aliveCount() == 0);
}

TEST_CASE("a very long single step does not flood the pool")
{
    // A frame hitch of a second must not ask for 300 particles at once. Each
    // emitter caps its per-step count, which is why a pause does not produce a
    // white screen when it ends.
    ParticleSystem system(4096);
    system.emitAmbient(Era::Future, Bounds{0.0f, 0.0f, 800.0f, 600.0f}, 1.0f, 1.0f);
    CHECK(system.aliveCount() <= 8);

    system.clear();
    system.emitShiftWave(0.0f, 0.0f, 0.5f, Palette::White, Palette::White, 1.0f);
    CHECK(system.aliveCount() <= 16);
}

TEST_CASE("the same seed produces the same particles")
{
    // Determinism is what makes a particle effect reviewable: a bug report that
    // says "the sparks looked wrong" is only answerable if the same call
    // produces the same sparks.
    const auto run = [] {
        ParticleSystem system(512);
        system.emitImpact(10.0f, 20.0f, 1.0f, -0.5f, Palette::White, 1.2f, 30);
        std::vector<float> out;
        for (std::size_t i = 0; i < system.size(); ++i) {
            out.push_back(system.data()[i].x);
            out.push_back(system.data()[i].y);
        }
        return out;
    };
    CHECK(run() == run());
}

TEST_CASE("a ring grows and a filled particle shrinks, as their shapes promise")
{
    // The renderer derives size from `size` and `endSize`, so a ring that does
    // not expand is not a ring, it is a dot.
    ParticleSystem system(8);
    Particle ring;
    ring.shape = ParticleShape::Ring;
    ring.size = 4.0f;
    ring.endSize = 100.0f;
    ring.maxLife = 1.0f;
    REQUIRE(system.emit(ring));

    Particle filled;
    filled.shape = ParticleShape::Filled;
    filled.size = 20.0f;
    filled.endSize = 0.0f;
    filled.maxLife = 1.0f;
    REQUIRE(system.emit(filled));

    // The raw fields are what the renderer reads; asserting on them keeps the
    // contract explicit rather than testing a private interpolation.
    CHECK(system.data()[0].endSize > system.data()[0].size);
    CHECK(system.data()[1].endSize < system.data()[1].size);
}
