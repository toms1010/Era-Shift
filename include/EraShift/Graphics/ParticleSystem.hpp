// Era Shift - particles.
//
// Also pure arithmetic, no renderer. That is deliberate: particles are one of
// the things most likely to be tuned by feel, and a pool you can unit test is
// much easier to tune than one you can only watch.
//
// The pool is fixed size and preallocated. A game that spawns particles during
// combat must never be the thing that decides to call into the allocator, because
// a frame spike in the middle of a shift is exactly the moment it hurts most.

#pragma once

#include "EraShift/Game/Era.hpp"
#include "EraShift/Graphics/Color.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace EraShift::Graphics {

/// How a particle is drawn. All of them are rectangles; these are just the four
/// ways the existing rectangle renderer can fake something more.
enum class ParticleShape : std::uint8_t {
    Filled,   ///< Solid, fading. Dust, debris, motes.
    Spark,    ///< Stretched along its velocity. Impacts.
    Ring,     ///< An expanding outline. Shockwaves, shift rings.
    Ghost,    ///< Filled, additive, always faint. Aura and light.
};

struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float life = 0.0f;        ///< Seconds left.
    float maxLife = 1.0f;
    float size = 2.0f;
    float endSize = 0.0f;     ///< Interpolated to at end of life.
    float rotation = 0.0f;
    float spin = 0.0f;
    float gravity = 0.0f;
    float drag = 0.0f;        ///< Fraction of velocity shed per second.
    Color  color{};
    Color  endColor{};
    ParticleShape shape = ParticleShape::Filled;
    bool  alive = false;
    /// For Spark and Ring: the shape is elongated/stretched by this much.
    float stretch = 1.0f;
};

/// A rectangle, in world space, for emitters that need to fill a region.
struct Bounds {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    [[nodiscard]] float width() const noexcept { return right - left; }
    [[nodiscard]] float height() const noexcept { return bottom - top; }
    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= left && px <= right && py >= top && py <= bottom;
    }
};

class ParticleSystem {
public:
    explicit ParticleSystem(std::size_t capacity = 640);

    void clear() noexcept;
    void update(float dt) noexcept;

    /// Adds a particle. Returns false when the pool is full, which is a tuning
    /// signal rather than an error: it means the emitters are asking for more
    /// than the budget allows.
    bool emit(const Particle& particle) noexcept;

    // --- emitters -----------------------------------------------------------
    /// A hit: sparks along the impact direction plus a widening ring.
    void emitImpact(float x, float y, float directionX, float directionY, Color color,
                    float scale = 1.0f, int count = 14);
    /// Dust kicked up on landing or running.
    void emitFootfall(float x, float y, float strength, Color color);
    /// A short-lived trail segment behind a dash.
    void emitDashTrail(float x, float y, float facingX, Color color);
    /// A pickup: a bright core that lifts and spreads.
    void emitPickup(float x, float y, Color color);
    /// A seal, which is a bigger, slower version of a pickup.
    void emitSeal(float x, float y, Color color);
    /// An enemy dissolving in its own era's colour.
    void emitDeath(float x, float y, Color color);
    /// The shift. `charge` is 0..1; a low value pulls particles inward toward
    /// the player, a high one throws them outward. This is the single most
    /// important effect in the game and it is one call.
    void emitShiftWave(float x, float y, float charge, Color fromColor, Color toColor, float dt);
    /// The impact half of a shift: a hard ring and a wide debris burst.
    void emitShiftBurst(float x, float y, Color color);
    /// Era-specific weather, emitted continuously over `bounds`.
    void emitAmbient(Game::Era era, const Bounds& bounds, float dt, float intensity);
    /// A short glitch of horizontal offsets, for high paradox. Drawn by the
    /// renderer as stretched sparks at fixed y.
    void emitGlitch(float x, float y, float width, float amount);

    // --- inspection ---------------------------------------------------------
    [[nodiscard]] const Particle* data() const noexcept { return m_pool.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_pool.size(); }
    [[nodiscard]] std::size_t aliveCount() const noexcept { return m_alive; }
    [[nodiscard]] std::size_t droppedCount() const noexcept { return m_dropped; }
    [[nodiscard]] bool full() const noexcept { return m_alive >= m_pool.size(); }

    void setGravityScale(float scale) noexcept { m_gravityScale = scale; }

    /// Resizes the pool and empties it.
    ///
    /// Only for start-up and level load. This is the one operation that
    /// reallocates, which is why it is not something an emitter can reach: the
    /// guarantee the rest of this class makes is that nothing allocates during
    /// play, and a resize would break it. Exposed so `graphics.maxParticles` in
    /// the config is a real setting rather than a decoration - a setting that
    /// looks live and does nothing is worse than no setting.
    void resize(std::size_t capacity);


    /// The pool's deterministic noise source, exposed so the emitters and any
    /// test can share it.
    [[nodiscard]] float nextRandom() noexcept;

private:
    Particle* claim() noexcept;

    std::vector<Particle> m_pool;
    std::size_t m_cursor = 0;
    std::size_t m_alive = 0;
    std::size_t m_dropped = 0;
    std::uint32_t m_random = 0x9E3779B9u;
    float m_gravityScale = 1.0f;
    /// The fractional remainder of the ambient rate, carried between calls. See
    /// `emitAmbient`: without this a rate below one particle per frame is lost
    /// to rounding, and every era ends up emitting the same number.
    float m_ambientAccumulator = 0.0f;
};

} // namespace EraShift::Graphics
