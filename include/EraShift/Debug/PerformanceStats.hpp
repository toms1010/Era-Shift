// Era Shift - runtime performance instrumentation.
//
// The overlay is a developer tool, but the counters it reads are cheap enough
// to keep in release builds (a handful of adds per frame) and invaluable when
// tracking down a regression, so the overlay is available in every build and
// simply hidden unless toggled.

#pragma once

#include "EraShift/Core/Time.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace EraShift::Debug {

/// Resident set size of the process, in kilobytes. Returns 0 when the
/// platform does not expose it.
[[nodiscard]] std::uint64_t processMemoryKb();

/// Peak virtual memory size, in kilobytes, or 0 when unavailable.
[[nodiscard]] std::uint64_t processPeakMemoryKb();

/// Number of hardware threads, used to express the simulation budget.
[[nodiscard]] unsigned hardwareThreads();

/// Frame timing, CPU cost and workload counters for one frame.
struct FrameMetrics {
    double   frameDelta     = 0.0;   ///< Seconds between presented frames.
    double   cpuTime        = 0.0;   ///< Seconds spent in update + render.
    double   updateTime     = 0.0;
    double   renderTime     = 0.0;
    std::size_t entityCount = 0;
    std::size_t drawCalls   = 0;
    std::uint64_t residentBytes = 0;
};

/// Rolling aggregate of FrameMetrics, updated once per frame.
class PerformanceStats {
public:
    PerformanceStats() = default;

    /// Call immediately after the frame is presented.
    void sample(double frameDelta, double cpuSeconds, double updateSeconds, double renderSeconds);

    /// Sets the workload counters for the current frame. These come from the
    /// renderer and the world, so Stats does not depend on either.
    void setEntityCount(std::size_t count) noexcept { m_entities = count; }
    void setDrawCalls(std::size_t count) noexcept { m_drawCalls = count; }

    /// Averages over the sampling window.
    [[nodiscard]] double averageFps() const noexcept { return m_frames.averageFps(); }
    [[nodiscard]] double averageFrameTimeMs() const noexcept { return m_frames.averageFrameTimeMs(); }
    [[nodiscard]] double peakFrameTimeMs() const noexcept { return m_frames.peakFrameTimeMs(); }
    [[nodiscard]] double averageCpuMs() const noexcept;
    [[nodiscard]] double averageUpdateMs() const noexcept;
    [[nodiscard]] double averageRenderMs() const noexcept;
    /// Fraction of the frame budget spent simulating and drawing, 0..n.
    [[nodiscard]] double cpuBudgetUsage() const noexcept;

    /// Instantaneous values from the most recent frame.
    [[nodiscard]] double lastFrameTimeMs() const noexcept { return m_last.frameDelta * 1000.0; }
    [[nodiscard]] double lastCpuMs() const noexcept { return m_last.cpuTime * 1000.0; }
    [[nodiscard]] double lastUpdateMs() const noexcept { return m_last.updateTime * 1000.0; }
    [[nodiscard]] double lastRenderMs() const noexcept { return m_last.renderTime * 1000.0; }
    [[nodiscard]] std::size_t entityCount() const noexcept { return m_entities; }
    [[nodiscard]] std::size_t drawCalls() const noexcept { return m_drawCalls; }
    [[nodiscard]] std::uint64_t residentBytes() const noexcept { return m_residentBytes; }

    [[nodiscard]] std::uint64_t frameCount() const noexcept { return m_frames.sampleCount(); }
    [[nodiscard]] std::uint64_t totalFrames() const noexcept { return m_totalFrames; }

    /// Warns once when the frame time exceeds the target for a sustained period.
    [[nodiscard]] bool isBelowTargetFps(double target) const noexcept;

    void reset();

private:
    void refreshMemory();

    Core::FrameTimeWindow m_frames{120};
    FrameMetrics m_last;
    double m_cpuAverage   = 0.0;
    double m_updateAverage = 0.0;
    double m_renderAverage = 0.0;
    double m_targetFrameMs = 1000.0 / 60.0;

    std::size_t   m_entities = 0;
    std::size_t   m_drawCalls = 0;
    std::uint64_t m_residentBytes = 0;
    std::uint64_t m_totalFrames = 0;
    std::uint32_t m_memoryPollCounter = 0;
};

} // namespace EraShift::Debug
