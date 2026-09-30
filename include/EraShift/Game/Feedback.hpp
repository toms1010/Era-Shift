// Era Shift - the feedback system.
//
// One class whose entire job is to answer a question the simulation must never
// have to think about: given that something happened, what should the player
// see, hear and feel?
//
// It is a single class on purpose. The alternative - a sound here, a particle
// system there, a screen flash in the state - spreads the answer to "what does a
// hit look like" across four files, and the next hit to be added is the one that
// only gets some of them. Routing every `WorldEvent` through one switch means a
// new event is visibly incomplete until it has been given a sound and an effect.
//
// The split of responsibility with the rest of the game:
//
//   World            decides *that* something happened, and where
//   FeedbackSystem   decides what that looks and sounds like
//   AudioManager     plays it
//   ParticleSystem   draws it
//   Camera2D         shakes and punches
//
// Nothing here can change the simulation. FeedbackSystem has no reference to
// `World`'s mutable state, only to a const view of it, so no amount of effects
// can make the game unwinnable.

#pragma once

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Game/EraTheme.hpp"
#include "EraShift/Game/World.hpp"
#include "EraShift/Graphics/ParticleSystem.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace EraShift::Game {

/// The four paradox tiers, in order. Paradox is the only number in the game that
/// changes how it *feels* rather than how it plays, so it gets named tiers
/// rather than a continuous slider: a player can be told "you are at Tier 3"
/// and understand that, whereas "your paradox is 41.2" means nothing.
enum class ParadoxTier : std::uint8_t { Calm, Strained, Fractured, Collapse };

[[nodiscard]] ParadoxTier tierForParadox(float paradox) noexcept;
[[nodiscard]] std::string_view toString(ParadoxTier tier) noexcept;
/// The paradox at which each tier begins, for the HUD gauge's tick marks.
[[nodiscard]] float tierThreshold(ParadoxTier tier) noexcept;

/// A timed full-screen effect. The era shift's ripple and the paradox glitch are
/// the same mechanism at different strengths.
struct ScreenEffect {
    /// 0..1 through the effect.
    float progress = 0.0f;
    float duration = 0.0f;
    Color color{};
    /// 0..1, how much of the screen it covers.
    float strength = 0.0f;
    bool  active = false;
};

class FeedbackSystem {
public:
    void reset() noexcept;

    /// Turns the world's event stream into sound, particles and camera work.
    ///
    /// Called once per fixed step, immediately after `World::update`, with the
    /// events that step produced. This is the only place world events are
    /// interpreted.
    void consume(const std::vector<EventRecord>& events, const World& world, StateContext& ctx);

    /// Advances particles, screen effects and the camera punch.
    ///
    /// Runs on a clock that keeps going through hit-stop, deliberately: the
    /// world is frozen, and the spark that caused the freeze keeps travelling.
    void update(StateContext& ctx, double deltaSeconds, const World& world);

    /// World-space effects, drawn over the actors.
    void drawWorld(StateContext& ctx) const;

    /// Screen-space atmosphere: the paradox vignette and the glitch bars.
    ///
    /// Drawn *under* the text, on purpose. Glitch and vignette are atmosphere,
    /// and atmosphere that makes a toast unreadable is not tension, it is a bug.
    /// The bars are the worst offender: six additive bands across the middle of
    /// the screen at Collapse will happily sit exactly where the objective line
    /// is.
    void drawUnderlay(StateContext& ctx) const;

    /// The flash, drawn over everything.
    ///
    /// A flash is light, not decoration, so it belongs on top - it is meant to
    /// wash the whole frame for a fifth of a second on a shift.
    void drawOverlay(StateContext& ctx) const;

    // --- state the rest of the game asks about --------------------------------

    /// 0..1 paradox, eased. The audio manager turns this into tempo
    /// instability, and the renderer into glitch and vignette.
    [[nodiscard]] float tension() const noexcept { return m_tension; }
    [[nodiscard]] ParadoxTier tier() const noexcept { return m_tier; }
    /// 1 for a moment after the tier last changed, falling to 0. The HUD label
    /// scales with it, so crossing a boundary is felt rather than only noticed.
    [[nodiscard]] float tierPulse() const noexcept { return m_hudPulse; }
    /// A white flash over everything, 0..1. Read by the HUD so the whole screen
    /// dims together rather than only the world.
    [[nodiscard]] float flash() const noexcept { return m_flash; }
    /// True while the shift's ripple is still on screen.
    [[nodiscard]] bool shifting() const noexcept { return m_ripple.active; }
    /// Sets the pool size. Called once at level load, never during play; see
    /// `ParticleSystem::resize` for why that matters.
    void setParticleCapacity(std::size_t capacity) noexcept { m_particles.resize(capacity); }
    [[nodiscard]] std::size_t particleCapacity() const noexcept { return m_particles.size(); }

    [[nodiscard]] Graphics::ParticleSystem& particles() noexcept { return m_particles; }
    [[nodiscard]] const Graphics::ParticleSystem& particles() const noexcept { return m_particles; }
    [[nodiscard]] std::size_t particleCount() const noexcept { return m_particles.aliveCount(); }

    /// How long a full-screen effect should last for a given kind of event, and
    /// whether it should be additive. Exposed so the HUD can flash in step.
    static constexpr float kFlashDecay = 4.5f;

private:
    void triggerScreenEffect(ScreenEffect& effect, float duration, Color color, float strength);
    void updateScreenEffect(ScreenEffect& effect, double dt);
    void drawRipple(const StateContext& ctx) const;
    void drawVignette(const StateContext& ctx) const;
    void drawGlitch(const StateContext& ctx) const;
    void drawFlash(const StateContext& ctx) const;
    void drawShiftAura(const StateContext& ctx) const;
    /// Era-tinted, decaying glows left by hits and the shift. Drawn as additive
    /// rectangles, which is the only "lighting" this renderer has.
    void drawGlows(const StateContext& ctx) const;
    void emitGlow(float x, float y, float radius, Color color, float life, float peak);

    struct Glow {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        float life = 0.0f;
        float maxLife = 1.0f;
        float peak = 1.0f;
        Color color{};
    };

    Graphics::ParticleSystem m_particles{768};

    std::vector<Glow> m_glows;
    /// Reused so the common case allocates nothing.
    Graphics::Bounds m_ambientBounds{};

    float m_tension = 0.0f;
    ParadoxTier m_tier = ParadoxTier::Calm;
    float m_flash = 0.0f;
    /// A short, violent zoom-freeze: the camera punch has its own timer, but the
    /// shift also wants a hold that outlasts hit-stop.
    ScreenEffect m_ripple{};
    /// Held while the player is charging a shift, so the aura is continuous
    /// rather than a series of separate bursts.
    float m_charge = 0.0f;
    /// The tier the HUD is currently showing, so a change can be detected and
    /// pulsed. Pulses on a *tier* crossing rather than on a flash, because the
    /// label next to the gauge is the tier and nothing else.
    ParadoxTier m_shownTier = ParadoxTier::Calm;
    /// Straight-line glitch bars, regenerated every few frames at high paradox.
    std::vector<Rect> m_glitchBars;
    float m_glitchTimer = 0.0f;
    float m_hudPulse = 0.0f;
    /// Cached in `update` so `drawWorld` needs only the context. The aura is
    /// drawn on the render clock, which must not need the simulation.
    Graphics::Vec2 m_playerCentre{};
    Graphics::Color m_eraAccent{};
};

} // namespace EraShift::Game
