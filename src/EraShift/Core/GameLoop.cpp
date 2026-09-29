#include "EraShift/Core/GameLoop.hpp"

#include <algorithm>
#include <utility>

namespace EraShift::Core {

GameLoop::GameLoop()
    : GameLoop(LoopConfig{})
{
}

GameLoop::GameLoop(LoopConfig config)
    : m_config(config)
{
    m_config.fixedDelta     = std::max(m_config.fixedDelta, 1.0e-6);
    m_config.maxStepsPerFrame = std::max(m_config.maxStepsPerFrame, 1);
    m_config.maxFrameDelta  = std::max(m_config.maxFrameDelta, m_config.fixedDelta);
}

void GameLoop::setConfig(const LoopConfig& config) noexcept
{
    m_config               = config;
    m_config.fixedDelta     = std::max(m_config.fixedDelta, 1.0e-6);
    m_config.maxStepsPerFrame = std::max(m_config.maxStepsPerFrame, 1);
    m_config.maxFrameDelta  = std::max(m_config.maxFrameDelta, m_config.fixedDelta);
}

void GameLoop::resync(double nowSeconds)
{
    m_lastTime    = nowSeconds;
    m_accumulator = 0.0;
    m_started     = true;
    m_stats.backlogSeconds = 0.0;
}

void GameLoop::simulate()
{
    m_update(m_config.fixedDelta);
    ++m_stats.totalSteps;
    ++m_stats.stepsLastFrame;
}

LoopStats GameLoop::tick(double nowSeconds)
{
    if (!m_started) {
        m_lastTime = nowSeconds;
        m_started  = true;
    }

    double frameDelta = nowSeconds - m_lastTime;
    m_lastTime = nowSeconds;

    // Guard against non-monotonic clocks and against a long stall.
    if (frameDelta < 0.0) {
        frameDelta = 0.0;
    } else if (frameDelta > m_config.maxFrameDelta) {
        frameDelta = m_config.maxFrameDelta;
        ++m_stats.clampedFrames;
    }

    m_stats.stepsLastFrame = 0;
    m_stats.frameDelta     = frameDelta;

    // 1. OS events are drained once per presented frame, not per sim step.
    if (m_event) {
        m_event(frameDelta);
    }

    // 2. Fixed-rate simulation.
    m_accumulator += frameDelta;
    while (m_accumulator >= m_config.fixedDelta) {
        if (static_cast<int>(m_stats.stepsLastFrame) >= m_config.maxStepsPerFrame) {
            ++m_stats.droppedSteps;
            if (m_config.discardBacklog) {
                m_accumulator = 0.0;
            }
            break;
        }
        if (m_update) {
            simulate();
        } else {
            ++m_stats.totalSteps;
            ++m_stats.stepsLastFrame;
        }
        m_accumulator -= m_config.fixedDelta;
    }

    // 3. Rendering happens once, with an interpolation factor describing where
    //    the display sits between the two most recent simulation states.
    m_stats.simulationAlpha = m_accumulator / m_config.fixedDelta;
    if (m_render) {
        m_render(frameDelta, m_stats.simulationAlpha);
    }

    m_stats.backlogSeconds = m_accumulator;
    ++m_stats.frameCount;

    return m_stats;
}

void GameLoop::run(IClock& clock)
{
    m_stopRequested = false;
    resync(clock.seconds());

    // Present cadence is handled by the host: `tick` is driven by the caller so
    // that SDL's own frame pacing (vsync) stays authoritative. Application uses
    // this loop from inside its event-driven pump.
    while (!m_stopRequested) {
        if (m_shouldQuit && m_shouldQuit()) {
            m_stopRequested = true;
            break;
        }
        tick(clock.seconds());
    }
}

} // namespace EraShift::Core
