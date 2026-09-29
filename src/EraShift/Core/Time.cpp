#include "EraShift/Core/Time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace EraShift::Core {
namespace {

std::uint64_t nowNanos() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

// ---------------------------------------------------------------------------
// IClock
// ---------------------------------------------------------------------------
IClock::~IClock() = default;

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------
SystemClock::SystemClock()
    : m_start(nowNanos())
{
}

double SystemClock::seconds() const
{
    return static_cast<double>(nowNanos() - m_start) / 1.0e9;
}

double SystemClock::elapsed() const
{
    return seconds();
}

void SystemClock::reset()
{
    m_start = nowNanos();
}

// ---------------------------------------------------------------------------
// ManualClock
// ---------------------------------------------------------------------------
ManualClock::ManualClock(double startSeconds) noexcept
    : m_now(startSeconds)
{
}

void ManualClock::advance(double deltaSeconds) noexcept
{
    m_now += deltaSeconds;
}

void ManualClock::setTo(double seconds) noexcept
{
    m_now = seconds;
}

// ---------------------------------------------------------------------------
// FrameTimeWindow
// ---------------------------------------------------------------------------
FrameTimeWindow::FrameTimeWindow(std::size_t capacity) noexcept
    : m_capacity(capacity == 0 ? 1 : capacity)
{
}

void FrameTimeWindow::add(double deltaSeconds) noexcept
{
    if (!(deltaSeconds > 0.0) || !std::isfinite(deltaSeconds)) {
        return; // guard against pauses and clock skew
    }

    const double ms = deltaSeconds * 1000.0;
    m_peak = std::max(m_peak, ms);

    if (m_samples.size() >= m_capacity) {
        m_samples.pop_front();
    }
    m_samples.push_back(ms);
}

double FrameTimeWindow::averageFrameTimeMs() const noexcept
{
    if (m_samples.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const double sample : m_samples) {
        total += sample;
    }
    return total / static_cast<double>(m_samples.size());
}

double FrameTimeWindow::averageFps() const noexcept
{
    const double frameTime = averageFrameTimeMs();
    if (frameTime <= 0.0) {
        return 0.0;
    }
    return 1000.0 / frameTime;
}

double FrameTimeWindow::peakFrameTimeMs() const noexcept
{
    return m_peak;
}

void FrameTimeWindow::clear() noexcept
{
    m_samples.clear();
    m_peak = 0.0;
}

// ---------------------------------------------------------------------------
// Stopwatch
// ---------------------------------------------------------------------------
void Stopwatch::reset() noexcept
{
    m_started     = (m_clock != nullptr) ? m_clock->seconds() : 0.0;
    m_pausedAt    = m_started;
    m_pausedTotal = 0.0;
    m_running     = true;
}

double Stopwatch::elapsed() const
{
    if (m_clock == nullptr) {
        return 0.0;
    }
    if (!m_running) {
        return m_pausedAt - m_started - m_pausedTotal;
    }
    return m_clock->seconds() - m_started - m_pausedTotal;
}

void Stopwatch::pause() noexcept
{
    if (!m_running || m_clock == nullptr) {
        return;
    }
    m_pausedAt = m_clock->seconds();
    m_running  = false;
}

void Stopwatch::resume() noexcept
{
    if (m_running || m_clock == nullptr) {
        return;
    }
    m_pausedTotal += m_clock->seconds() - m_pausedAt;
    m_running = true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string formatDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }

    const auto totalMs   = static_cast<long long>(seconds * 1000.0);
    const long long mins = totalMs / 60000;
    const long long secs = (totalMs / 1000) % 60;
    const long long millis = totalMs % 1000;

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld.%03lld", mins, secs, millis);
    return std::string(buffer);
}

} // namespace EraShift::Core
