// Era Shift - fixed-timestep game loop.
//
// Simulation runs at a fixed rate so that physics, collision and AI behave
// identically regardless of display refresh rate. Rendering runs once per
// presented frame and receives an interpolation alpha for smooth motion
// between the last two simulation states.
//
//   Input -> Events -> Fixed Update xN -> Render(alpha) -> Present
//
// The loop is pure bookkeeping: it owns no SDL types and is driven either by
// `tick(nowSeconds)` (deterministic, used by tests) or by `run(clock, ...)`
// which pumps SDL itself.

#pragma once

#include "EraShift/Core/Time.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace EraShift::Core {

/// Tuning for the simulation step.
struct LoopConfig {
    /// Fixed simulation step in seconds. 1/60 s (16.67 ms) by default.
    double fixedDelta = 1.0 / 60.0;
    /// Upper bound on simulation steps executed in a single frame. Prevents the
    /// "spiral of death" after a long stall (window drag, breakpoint, load).
    int maxStepsPerFrame = 5;
    /// Frame deltas larger than this are treated as a stall and clamped, so a
    /// suspended process does not resume with a multi-minute simulation jump.
    double maxFrameDelta = 0.25;
    /// Cap the accumulated backlog so unused time is discarded.
    bool discardBacklog = true;
};

/// Snapshot of loop health, surfaced by the debug overlay.
struct LoopStats {
    std::uint64_t frameCount       = 0;
    std::uint64_t totalSteps       = 0;
    std::uint32_t stepsLastFrame   = 0;
    double        frameDelta       = 0.0;  ///< Clamped delta actually used this frame.
    double        simulationAlpha  = 0.0;  ///< 0..1 interpolation between sim states.
    double        backlogSeconds   = 0.0;  ///< Sim time still waiting to be consumed.
    std::uint64_t droppedSteps     = 0;    ///< Steps skipped because maxStepsPerFrame was hit.
    std::uint64_t clampedFrames    = 0;    ///< Frames whose delta exceeded maxFrameDelta.
};

class GameLoop {
public:
    using UpdateFn  = std::function<void(double fixedDelta)>;                  ///< Simulation step
    using RenderFn  = std::function<void(double frameDelta, double alpha)>;   ///< Draw
    using EventFn   = std::function<void(double frameDelta)>;                 ///< OS event pump
    using ShouldQuitFn = std::function<bool()>;

    GameLoop();
    explicit GameLoop(LoopConfig config);

    void setConfig(const LoopConfig& config) noexcept;
    [[nodiscard]] const LoopConfig& config() const noexcept { return m_config; }

    void setUpdateFn(UpdateFn fn) { m_update = std::move(fn); }
    void setRenderFn(RenderFn fn) { m_render = std::move(fn); }
    void setEventFn(EventFn fn) { m_event = std::move(fn); }
    void setShouldQuitFn(ShouldQuitFn fn) { m_shouldQuit = std::move(fn); }

    /// Advances exactly one frame at the supplied absolute time.
    /// @return Loop statistics describing the frame that just completed.
    LoopStats tick(double nowSeconds);

    /// Runs until `shouldQuit` returns true or the callbacks request a stop.
    void run(IClock& clock);

    /// Requests a clean shutdown at the end of the current frame.
    void requestStop() noexcept { m_stopRequested = true; }
    [[nodiscard]] bool stopRequested() const noexcept { return m_stopRequested; }

    /// Drops accumulated simulation time and resets counters. Called when the
    /// player returns from a menu or loads a save.
    void resync(double nowSeconds);

    [[nodiscard]] const LoopStats& stats() const noexcept { return m_stats; }

private:
    void simulate();

    LoopConfig m_config;
    UpdateFn   m_update;
    RenderFn   m_render;
    EventFn    m_event;
    ShouldQuitFn m_shouldQuit;

    double     m_lastTime   = 0.0;
    double     m_accumulator = 0.0;
    bool       m_started    = false;
    bool       m_stopRequested = false;
    LoopStats  m_stats;
};

} // namespace EraShift::Core
