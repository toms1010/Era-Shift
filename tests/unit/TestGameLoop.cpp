// Era Shift - tests for the fixed-timestep game loop.
//
// The loop is driven by explicit `tick(now)` calls against a ManualClock, so
// every assertion here is exact rather than timing dependent.

#include "EraShift/Core/GameLoop.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Core;

namespace {

/// Records how the loop drove its callbacks.
struct Recorder {
    int    updateCount   = 0;
    int    renderCount   = 0;
    int    eventCount    = 0;
    double lastFixedDelta = 0.0;
    double lastFrameDelta  = 0.0;
    double lastAlpha       = -1.0;
    std::vector<double> alphas;

    void attach(GameLoop& loop)
    {
        loop.setUpdateFn([this](double dt) {
            ++updateCount;
            lastFixedDelta = dt;
        });
        loop.setRenderFn([this](double dt, double alpha) {
            ++renderCount;
            lastFrameDelta = dt;
            lastAlpha = alpha;
            alphas.push_back(alpha);
        });
        loop.setEventFn([this](double) { ++eventCount; });
    }
};

} // namespace

TEST_CASE("a first tick only establishes the time base")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    const LoopStats stats = loop.tick(10.0);

    CHECK(recorder.updateCount == 0);
    CHECK(recorder.renderCount == 1);
    CHECK(recorder.eventCount == 1);
    CHECK(stats.frameDelta == doctest::Approx(0.0));
    CHECK(stats.frameCount == 1);
}

TEST_CASE("a full frame runs exactly one fixed step")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    loop.tick(1.0 / 60.0);

    CHECK(recorder.updateCount == 1);
    CHECK(recorder.lastFixedDelta == doctest::Approx(1.0 / 60.0));
    CHECK(recorder.renderCount == 2);
}

TEST_CASE("simulation time is recovered exactly regardless of frame rate")
{
    SUBCASE("a 144 Hz display still steps at 60 Hz")
    {
        GameLoop loop;
        Recorder recorder;
        recorder.attach(loop);

        loop.tick(0.0);
        const int frames = 144;      // one second at 144 Hz
        for (int i = 1; i <= frames; ++i) {
            loop.tick(static_cast<double>(i) / 144.0);
        }
        // One second of wall clock is 60 simulation steps, give or take the
        // partial step carried in the accumulator.
        CHECK(recorder.updateCount >= 59);
        CHECK(recorder.updateCount <= 61);
    }

    SUBCASE("a 30 Hz display runs two steps per frame")
    {
        GameLoop loop;
        Recorder recorder;
        recorder.attach(loop);

        loop.tick(0.0);
        for (int i = 1; i <= 30; ++i) {
            loop.tick(static_cast<double>(i) / 30.0);
        }
        CHECK(recorder.updateCount >= 59);
        CHECK(recorder.updateCount <= 61);
    }
}

TEST_CASE("the loop never runs away after a long stall")
{
    LoopConfig config;
    config.maxFrameDelta    = 0.25;
    config.maxStepsPerFrame = 5;

    GameLoop loop(config);
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    // Simulate a 30 second freeze (a breakpoint, a window drag, a level load).
    const LoopStats stats = loop.tick(30.0);

    CHECK(recorder.updateCount <= 5);
    CHECK(stats.clampedFrames == 1);
    CHECK(stats.droppedSteps > 0);
    CHECK(stats.backlogSeconds == doctest::Approx(0.0));
}

TEST_CASE("a non-monotonic clock does not produce negative deltas")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(10.0);
    const LoopStats stats = loop.tick(5.0);   // time went backwards

    CHECK(stats.frameDelta == doctest::Approx(0.0));
    CHECK(recorder.updateCount == 0);
}

TEST_CASE("interpolation alpha is the fraction of a step already accumulated")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    loop.tick(0.5 / 60.0);
    CHECK(recorder.lastAlpha == doctest::Approx(0.5));

    loop.tick(1.0 / 60.0);
    CHECK(recorder.lastAlpha == doctest::Approx(0.0));
    CHECK(recorder.updateCount == 1);
}

TEST_CASE("alpha stays within [0,1]")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    for (int i = 1; i <= 120; ++i) {
        // A deliberately irregular frame time to exercise the accumulator.
        const double dt = (i % 3 == 0) ? 0.011 : 0.023;
        loop.tick(loop.config().fixedDelta * i + dt);
        CHECK(recorder.lastAlpha >= 0.0);
        CHECK(recorder.lastAlpha <= 1.0);
    }
}

TEST_CASE("resync discards accumulated time")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    loop.tick(0.9 / 60.0);        // leave a partial step in the accumulator
    loop.resync(1.0);

    const LoopStats stats = loop.tick(1.0);
    CHECK(stats.backlogSeconds == doctest::Approx(0.0));
    CHECK(recorder.updateCount == 0);
}

TEST_CASE("a degenerate configuration is clamped instead of dividing by zero")
{
    LoopConfig config;
    config.fixedDelta       = 0.0;
    config.maxStepsPerFrame = 0;

    GameLoop loop(config);
    CHECK(loop.config().fixedDelta > 0.0);
    CHECK(loop.config().maxStepsPerFrame >= 1);

    Recorder recorder;
    recorder.attach(loop);
    loop.tick(0.0);
    loop.tick(1.0);
    CHECK(recorder.updateCount >= 1);
}

TEST_CASE("requestStop and the quit predicate both end the run loop")
{
    SUBCASE("requestStop")
    {
        GameLoop loop;
        loop.setUpdateFn([](double) {});
        loop.setRenderFn([](double, double) {});

        ManualClock clock;
        int frames = 0;
        loop.setRenderFn([&frames](double, double) {
            ++frames;
            if (frames == 5) {
                // The loop re-checks the flag at the top of the next iteration.
            }
        });
        loop.setShouldQuitFn([&loop, &frames] {
            return frames >= 5;
        });

        loop.run(clock);
        CHECK(frames == 5);
        CHECK(loop.stopRequested());
    }
}

TEST_CASE("totals accumulate across frames")
{
    GameLoop loop;
    Recorder recorder;
    recorder.attach(loop);

    loop.tick(0.0);
    for (int i = 1; i <= 10; ++i) {
        loop.tick(static_cast<double>(i) / 60.0);
    }

    const LoopStats stats = loop.stats();
    CHECK(stats.frameCount == 11);
    CHECK(stats.totalSteps == static_cast<std::uint64_t>(recorder.updateCount));
    CHECK(recorder.updateCount >= 9);
    CHECK(recorder.updateCount <= 11);
}
