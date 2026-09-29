#include "EraShift/Debug/PerformanceStats.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#if defined(__linux__)
    #include <unistd.h>
#endif

namespace EraShift::Debug {

// ---------------------------------------------------------------------------
// Platform probes
// ---------------------------------------------------------------------------
namespace {

#if defined(__linux__)

/// Reads a field out of /proc/self/status. `getline` keeps the parse simple and
/// runs at most a few times a second.
std::uint64_t readProcStatusKb(const char* field)
{
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) {
        return 0;
    }

    std::string line;
    const std::size_t fieldLength = std::char_traits<char>::length(field);
    while (std::getline(file, line)) {
        if (line.compare(0, fieldLength, field) == 0) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                return 0;
            }
            return static_cast<std::uint64_t>(std::strtoull(line.c_str() + colon + 1, nullptr, 10));
        }
    }
    return 0;
}

#endif

} // namespace

std::uint64_t processMemoryKb()
{
#if defined(__linux__)
    return readProcStatusKb("VmRSS");
#else
    return 0;
#endif
}

std::uint64_t processPeakMemoryKb()
{
#if defined(__linux__)
    return readProcStatusKb("VmHWM");
#else
    return 0;
#endif
}

unsigned hardwareThreads()
{
    const unsigned count = std::thread::hardware_concurrency();
    return count == 0 ? 1u : count;
}

// ---------------------------------------------------------------------------
// PerformanceStats
// ---------------------------------------------------------------------------
void PerformanceStats::refreshMemory()
{
    m_residentBytes = processMemoryKb() * 1024ull;
}

void PerformanceStats::sample(double frameDelta, double cpuSeconds, double updateSeconds, double renderSeconds)
{
    m_last.frameDelta  = frameDelta;
    m_last.cpuTime     = cpuSeconds;
    m_last.updateTime  = updateSeconds;
    m_last.renderTime  = renderSeconds;
    m_last.entityCount = m_entities;
    m_last.drawCalls   = m_drawCalls;

    m_frames.add(frameDelta);
    ++m_totalFrames;

    // Exponential moving average: recent frames dominate, which is what a
    // developer wants when hunting a hitch.
    constexpr double kSmoothing = 0.1;
    m_cpuAverage    += (cpuSeconds    - m_cpuAverage)    * kSmoothing;
    m_updateAverage += (updateSeconds - m_updateAverage) * kSmoothing;
    m_renderAverage += (renderSeconds - m_renderAverage) * kSmoothing;

    // Reading /proc every frame is wasteful; twice a second is plenty.
    if (++m_memoryPollCounter >= 30u) {
        m_memoryPollCounter = 0;
        refreshMemory();
    }
}

double PerformanceStats::averageCpuMs() const noexcept
{
    return m_cpuAverage * 1000.0;
}

double PerformanceStats::averageUpdateMs() const noexcept
{
    return m_updateAverage * 1000.0;
}

double PerformanceStats::averageRenderMs() const noexcept
{
    return m_renderAverage * 1000.0;
}

double PerformanceStats::cpuBudgetUsage() const noexcept
{
    if (m_targetFrameMs <= 0.0) {
        return 0.0;
    }
    return (m_cpuAverage * 1000.0) / m_targetFrameMs;
}

bool PerformanceStats::isBelowTargetFps(double target) const noexcept
{
    if (target <= 0.0 || m_frames.sampleCount() < 30) {
        return false;
    }
    return m_frames.averageFps() < target;
}

void PerformanceStats::reset()
{
    m_frames.clear();
    m_last = FrameMetrics{};
    m_cpuAverage     = 0.0;
    m_updateAverage  = 0.0;
    m_renderAverage  = 0.0;
    m_totalFrames    = 0;
    m_entities       = 0;
    m_drawCalls      = 0;
    m_residentBytes  = 0;
    m_memoryPollCounter = 0;
    refreshMemory();
}

} // namespace EraShift::Debug
