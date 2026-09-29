// Era Shift - tests for the timing primitives.

#include "EraShift/Core/Time.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace EraShift::Core;

TEST_CASE("ManualClock advances deterministically")
{
    ManualClock clock(10.0);
    CHECK(clock.seconds() == doctest::Approx(10.0));

    clock.advance(0.5);
    CHECK(clock.seconds() == doctest::Approx(10.5));

    clock.advance(-0.5);
    CHECK(clock.seconds() == doctest::Approx(10.0));

    clock.setTo(0.0);
    CHECK(clock.seconds() == doctest::Approx(0.0));
}

TEST_CASE("SystemClock is monotonic and starts near zero")
{
    SystemClock clock;
    const double first = clock.seconds();
    CHECK(first >= 0.0);
    CHECK(first < 5.0);   // construction is fast

    const double second = clock.seconds();
    CHECK(second >= first);
}

TEST_CASE("FrameTimeWindow averages frame times")
{
    FrameTimeWindow window(4);
    CHECK(window.averageFps() == doctest::Approx(0.0));
    CHECK(window.averageFrameTimeMs() == doctest::Approx(0.0));

    for (int i = 0; i < 4; ++i) {
        window.add(1.0 / 60.0);
    }
    CHECK(window.sampleCount() == 4);
    CHECK(window.averageFrameTimeMs() == doctest::Approx(1000.0 / 60.0));
    CHECK(window.averageFps() == doctest::Approx(60.0).epsilon(1e-3));
}

TEST_CASE("FrameTimeWindow keeps only the most recent samples")
{
    FrameTimeWindow window(3);
    window.add(1.0);
    window.add(1.0);
    window.add(1.0);
    window.add(0.1);

    CHECK(window.sampleCount() == 3);
    // The average drops even though the peak stays recorded.
    CHECK(window.averageFrameTimeMs() < 1000.0);
    CHECK(window.peakFrameTimeMs() == doctest::Approx(1000.0));
}

TEST_CASE("FrameTimeWindow rejects non-positive and non-finite samples")
{
    FrameTimeWindow window(8);
    window.add(0.0);
    window.add(-1.0);
    window.add(std::nan(""));
    CHECK(window.sampleCount() == 0);

    window.clear();
    CHECK(window.peakFrameTimeMs() == doctest::Approx(0.0));
}

TEST_CASE("FrameTimeWindow treats a zero capacity as one")
{
    FrameTimeWindow window(0);
    window.add(0.5);
    window.add(0.5);
    CHECK(window.sampleCount() == 1);
}

TEST_CASE("Stopwatch measures elapsed time against a manual clock")
{
    ManualClock clock;
    Stopwatch watch(clock);
    watch.reset();
    CHECK(watch.isRunning());

    clock.advance(0.25);
    CHECK(watch.elapsed() == doctest::Approx(0.25));

    watch.pause();
    CHECK_FALSE(watch.isRunning());
    clock.advance(1.0);
    CHECK(watch.elapsed() == doctest::Approx(0.25));   // paused time is excluded

    watch.resume();
    clock.advance(0.75);
    CHECK(watch.elapsed() == doctest::Approx(1.0));
}

TEST_CASE("Stopwatch without a clock is inert rather than crashing")
{
    Stopwatch watch;
    CHECK(watch.elapsed() == doctest::Approx(0.0));
    watch.pause();
    watch.resume();
    CHECK(watch.elapsed() == doctest::Approx(0.0));
}

TEST_CASE("formatDuration renders minutes, seconds and milliseconds")
{
    CHECK(formatDuration(0.0)   == "0:00.000");
    CHECK(formatDuration(1.5)   == "0:01.500");
    CHECK(formatDuration(61.25) == "1:01.250");
    CHECK(formatDuration(-5.0)  == "0:00.000");
    CHECK(formatDuration(std::nan("")) == "0:00.000");
}
