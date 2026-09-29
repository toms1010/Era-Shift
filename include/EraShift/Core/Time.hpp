// Era Shift - timing primitives.
//
// Every timing consumer depends on `IClock` rather than on SDL or the system
// clock. That makes the game loop and all gameplay timing fully deterministic
// under test: a test advances a `ManualClock` and asserts on the result.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace EraShift::Core {

/// Monotonic time source, in seconds.
class IClock {
public:
    IClock() = default;
    virtual ~IClock();

    IClock(const IClock&)            = delete;
    IClock& operator=(const IClock&) = delete;

    [[nodiscard]] virtual double seconds() const = 0;
};

/// Clock driven by the host performance counter (SDL or std::chrono).
class SystemClock final : public IClock {
public:
    SystemClock();

    [[nodiscard]] double seconds() const override;

    /// Seconds since the clock was constructed or last reset.
    [[nodiscard]] double elapsed() const;

    void reset();

private:
    std::uint64_t m_start;
};

/// Clock advanced explicitly by the caller. Intended for tests and replays.
class ManualClock final : public IClock {
public:
    explicit ManualClock(double startSeconds = 0.0) noexcept;

    [[nodiscard]] double seconds() const override { return m_now; }

    void advance(double deltaSeconds) noexcept;
    void setTo(double seconds) noexcept;

private:
    double m_now;
};

/// Fixed-size window of recent frame times used to derive stable FPS numbers.
class FrameTimeWindow {
public:
    explicit FrameTimeWindow(std::size_t capacity = 120) noexcept;

    void add(double deltaSeconds) noexcept;

    /// Mean frames per second over the window. 0 when no samples were added.
    [[nodiscard]] double averageFps() const noexcept;
    /// Mean frame time in milliseconds. 0 when no samples were added.
    [[nodiscard]] double averageFrameTimeMs() const noexcept;
    /// Worst frame time in milliseconds since the last clear().
    [[nodiscard]] double peakFrameTimeMs() const noexcept;
    [[nodiscard]] std::size_t sampleCount() const noexcept { return m_samples.size(); }

    void clear() noexcept;

private:
    std::size_t   m_capacity;
    std::deque<double> m_samples;
    double        m_peak = 0.0;
};

/// Measures a single span of CPU work between two explicit marks.
class Stopwatch {
public:
    Stopwatch() = default;
    explicit Stopwatch(IClock& clock) : m_clock(&clock) {}

    void bind(IClock& clock) noexcept { m_clock = &clock; }

    void reset() noexcept;
    /// Elapsed seconds since construction or the last reset().
    [[nodiscard]] double elapsed() const;
    void pause() noexcept;
    void resume() noexcept;
    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

private:
    IClock* m_clock    = nullptr;
    double  m_started  = 0.0;
    double  m_pausedAt = 0.0;
    double  m_pausedTotal = 0.0;
    bool    m_running  = false;
};

/// Renders a duration as "m:ss.mmm", used by the debug overlay.
[[nodiscard]] std::string formatDuration(double seconds);

} // namespace EraShift::Core
