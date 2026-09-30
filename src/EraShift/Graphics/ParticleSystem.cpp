#include "EraShift/Graphics/ParticleSystem.hpp"

#include "EraShift/Game/Era.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Graphics {

using Game::Era;

namespace {

constexpr float kTwoPi = 6.28318530718f;

/// A value uniformly in [low, high).
///
/// The pool's noise source is signed, in [-1, 1], because that is the useful
/// shape for scattering a velocity. Mapping it onto a *range* therefore means
/// rescaling it, not using it directly: `low + (high - low) * r` with a signed
/// `r` puts half the results below `low`, which for a spawn position means
/// particles appearing outside the room they were spawned into.
float randomRange(ParticleSystem& system, float low, float high) noexcept
{
    return low + (high - low) * (system.nextRandom() * 0.5f + 0.5f);
}

/// A value in [-1, 1]. For velocities and rotations, where a signed spread is
/// what is wanted rather than a range.
float randomSigned(ParticleSystem& system) noexcept
{
    return system.nextRandom();
}

} // namespace

ParticleSystem::ParticleSystem(std::size_t capacity) : m_pool(capacity)
{
    clear();
}

void ParticleSystem::clear() noexcept
{
    for (Particle& particle : m_pool) {
        particle.alive = false;
        particle.life = 0.0f;
    }
    m_cursor = 0;
    m_alive = 0;
    m_dropped = 0;
    // The fractional ambient budget is part of the pool's state, not its
    // contents, so a fresh run should not inherit a half-spent emission.
    m_ambientAccumulator = 0.0f;
}

void ParticleSystem::resize(std::size_t capacity)
{
    m_pool.assign(capacity, Particle{});
    clear();
}

float ParticleSystem::nextRandom() noexcept
{
    // xorshift32. Deterministic on purpose: a particle system you cannot
    // reproduce is a particle system you cannot unit test.
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

Particle* ParticleSystem::claim() noexcept
{
    // Ring allocation: the oldest particle is recycled when the pool is full.
    // A shifting era matters more than a spark, so newer effects win.
    for (std::size_t attempt = 0; attempt < m_pool.size(); ++attempt) {
        Particle& candidate = m_pool[m_cursor];
        m_cursor = (m_cursor + 1) % m_pool.size();
        if (!candidate.alive) {
            return &candidate;
        }
    }
    if (m_pool.empty()) {
        return nullptr;
    }
    ++m_dropped;
    return nullptr;
}

bool ParticleSystem::emit(const Particle& particle) noexcept
{
    Particle* slot = claim();
    if (slot == nullptr) {
        return false;
    }
    *slot = particle;
    slot->alive = true;
    if (slot->maxLife <= 0.0f) {
        slot->maxLife = 0.01f;
    }
    slot->life = slot->maxLife;
    ++m_alive;
    return true;
}

void ParticleSystem::update(float dt) noexcept
{
    if (dt <= 0.0f) {
        return;
    }
    std::size_t alive = 0;
    for (Particle& p : m_pool) {
        if (!p.alive) {
            continue;
        }
        p.life -= dt;
        if (p.life <= 0.0f) {
            p.alive = false;
            continue;
        }
        ++alive;

        p.vy += p.gravity * m_gravityScale * dt;
        if (p.drag > 0.0f) {
            const float shed = std::max(0.0f, 1.0f - p.drag * dt);
            p.vx *= shed;
            p.vy *= shed;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rotation += p.spin * dt;
    }
    m_alive = alive;
}

// ---------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------
void ParticleSystem::emitImpact(float x, float y, float directionX, float directionY, Color color,
                                float scale, int count)
{
    const float length = std::sqrt(directionX * directionX + directionY * directionY);
    const float dx = length > 0.001f ? directionX / length : 0.0f;
    const float dy = length > 0.001f ? directionY / length : -1.0f;
    const float baseAngle = std::atan2(dy, dx);

    for (int i = 0; i < count; ++i) {
        // A cone around the impact direction, plus a couple of straight back.
        const float spread = randomRange(*this, -0.9f, 0.9f);
        const float angle = baseAngle + spread;
        const float speed = randomRange(*this, 90.0f, 340.0f) * scale;
        Particle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - randomRange(*this, 0.0f, 60.0f) * scale;
        p.maxLife = randomRange(*this, 0.12f, 0.34f) * scale;
        p.size = randomRange(*this, 1.5f, 3.5f) * scale;
        p.endSize = 0.0f;
        p.color = color;
        p.endColor = color.withAlpha(0);
        p.gravity = 260.0f;
        p.drag = 3.0f;
        p.shape = ParticleShape::Spark;
        p.stretch = randomRange(*this, 2.0f, 4.5f);
        p.rotation = angle;
        emit(p);
    }

    // The ring. One of these per hit, and it does most of the work of making the
    // hit feel like it connected to something.
    Particle ring;
    ring.x = x;
    ring.y = y;
    ring.maxLife = 0.22f * scale;
    ring.size = 6.0f * scale;
    ring.endSize = 34.0f * scale;
    ring.color = color.withAlpha(200);
    ring.endColor = color.withAlpha(0);
    ring.shape = ParticleShape::Ring;
    ring.drag = 0.0f;
    emit(ring);

    // A flash at the contact point.
    Particle flash;
    flash.x = x;
    flash.y = y;
    flash.maxLife = 0.10f * scale;
    flash.size = 10.0f * scale;
    flash.endSize = 2.0f * scale;
    flash.color = Color::fromFloats(1.0f, 1.0f, 1.0f, 0.9f);
    flash.endColor = color.withAlpha(0);
    flash.shape = ParticleShape::Ghost;
    emit(flash);
}

void ParticleSystem::emitFootfall(float x, float y, float strength, Color color)
{
    const int count = 2 + static_cast<int>(strength * 4.0f);
    for (int i = 0; i < count; ++i) {
        const float side = randomSigned(*this);
        Particle p;
        p.x = x + side * 3.0f;
        p.y = y;
        p.vx = side * randomRange(*this, 20.0f, 70.0f) * strength;
        p.vy = -randomRange(*this, 10.0f, 45.0f) * strength;
        p.maxLife = randomRange(*this, 0.18f, 0.40f);
        p.size = randomRange(*this, 1.0f, 2.2f) * (0.6f + strength);
        p.endSize = p.size * 0.4f;
        p.color = color.withAlpha(150);
        p.endColor = color.withAlpha(0);
        p.gravity = 40.0f;
        p.drag = 2.2f;
        p.shape = ParticleShape::Filled;
        emit(p);
    }
}

void ParticleSystem::emitDashTrail(float x, float y, float facingX, Color color)
{
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.x = x - facingX * randomRange(*this, 0.0f, 8.0f);
        p.y = y + randomRange(*this, -6.0f, 6.0f);
        p.vx = -facingX * randomRange(*this, 20.0f, 80.0f);
        p.vy = randomRange(*this, -10.0f, 10.0f);
        p.maxLife = randomRange(*this, 0.14f, 0.26f);
        p.size = randomRange(*this, 2.0f, 5.0f);
        p.endSize = 0.0f;
        p.color = color.withAlpha(170);
        p.endColor = color.withAlpha(0);
        p.drag = 4.0f;
        p.shape = ParticleShape::Ghost;
        emit(p);
    }
}

void ParticleSystem::emitPickup(float x, float y, Color color)
{
    for (int i = 0; i < 10; ++i) {
        const float angle = randomSigned(*this) * kTwoPi;
        const float speed = randomRange(*this, 30.0f, 110.0f);
        Particle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 40.0f;
        p.maxLife = randomRange(*this, 0.25f, 0.55f);
        p.size = randomRange(*this, 1.5f, 3.0f);
        p.endSize = 0.0f;
        p.color = color;
        p.endColor = color.withAlpha(0);
        p.gravity = 90.0f;
        p.drag = 2.5f;
        p.shape = ParticleShape::Filled;
        emit(p);
    }

    Particle ring;
    ring.x = x;
    ring.y = y;
    ring.maxLife = 0.35f;
    ring.size = 4.0f;
    ring.endSize = 40.0f;
    ring.color = color.withAlpha(220);
    ring.endColor = color.withAlpha(0);
    ring.shape = ParticleShape::Ring;
    emit(ring);
}

void ParticleSystem::emitSeal(float x, float y, Color color)
{
    for (int i = 0; i < 24; ++i) {
        const float angle = randomSigned(*this) * kTwoPi;
        const float speed = randomRange(*this, 40.0f, 160.0f);
        Particle p;
        p.x = x + std::cos(angle) * 4.0f;
        p.y = y + std::sin(angle) * 4.0f;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.maxLife = randomRange(*this, 0.4f, 0.9f);
        p.size = randomRange(*this, 2.0f, 4.5f);
        p.endSize = 0.5f;
        p.color = color;
        p.endColor = color.withAlpha(0);
        p.drag = 1.4f;
        p.spin = randomRange(*this, -6.0f, 6.0f);
        p.rotation = angle;
        p.shape = ParticleShape::Filled;
        emit(p);
    }

    Particle ring;
    ring.x = x;
    ring.y = y;
    ring.maxLife = 0.7f;
    ring.size = 6.0f;
    ring.endSize = 120.0f;
    ring.color = color.withAlpha(230);
    ring.endColor = color.withAlpha(0);
    ring.shape = ParticleShape::Ring;
    emit(ring);
}

void ParticleSystem::emitDeath(float x, float y, Color color)
{
    for (int i = 0; i < 18; ++i) {
        const float angle = randomSigned(*this) * kTwoPi;
        const float speed = randomRange(*this, 20.0f, 120.0f);
        Particle p;
        p.x = x + std::cos(angle) * 5.0f;
        p.y = y + std::sin(angle) * 5.0f;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 30.0f;
        p.maxLife = randomRange(*this, 0.4f, 1.0f);
        p.size = randomRange(*this, 2.0f, 5.0f);
        p.endSize = 0.0f;
        p.color = color;
        p.endColor = color.withAlpha(0);
        p.gravity = -30.0f;   // Rises: the body comes apart upward.
        p.drag = 1.0f;
        p.spin = randomRange(*this, -4.0f, 4.0f);
        p.shape = ParticleShape::Filled;
        emit(p);
    }
}

void ParticleSystem::emitShiftWave(float x, float y, float charge, Color fromColor, Color toColor,
                                   float dt)
{
    // Rate is per second, so this scales with frame time rather than emitting a
    // fixed count per frame (which would make the effect framerate dependent).
    const float rate = 26.0f + charge * 90.0f;
    int count = static_cast<int>(rate * dt + 0.5f);
    if (count < 1 && dt > 0.0f) {
        count = 1;
    }
    count = std::min(count, 12);

    const bool inward = charge < 1.0f;
    const Color tint = fromColor.lerpTo(toColor, Graphics::clampValue(charge, 0.0f, 1.0f));

    for (int i = 0; i < count; ++i) {
        const float angle = randomSigned(*this) * kTwoPi;
        const float radius = randomRange(*this, 18.0f, 90.0f);
        Particle p;
        // Start on a circle and either fall inward or fly outward.
        p.x = x + std::cos(angle) * radius;
        p.y = y + std::sin(angle) * radius * 0.6f;
        const float speed = inward ? randomRange(*this, 30.0f, 130.0f)
                                   : randomRange(*this, 60.0f, 320.0f);
        p.vx = (x - p.x) * (inward ? 1.6f : -0.35f) + std::cos(angle) * speed * 0.4f;
        p.vy = (y - p.y) * (inward ? 1.6f : -0.35f) + std::sin(angle) * speed * 0.4f;
        p.maxLife = randomRange(*this, 0.2f, 0.5f);
        p.size = randomRange(*this, 1.0f, 3.0f);
        p.endSize = 0.0f;
        p.color = tint;
        p.endColor = (inward ? fromColor : toColor).withAlpha(0);
        p.drag = 1.0f;
        p.shape = ParticleShape::Spark;
        p.stretch = randomRange(*this, 1.0f, 3.0f);
        p.rotation = angle;
        emit(p);
    }
}

void ParticleSystem::emitShiftBurst(float x, float y, Color color)
{
    // Three rings, staggered. One reads as a click; three reads as a shockwave.
    for (int i = 0; i < 3; ++i) {
        Particle ring;
        ring.x = x;
        ring.y = y;
        ring.maxLife = 0.45f + 0.22f * static_cast<float>(i);
        ring.size = 8.0f;
        ring.endSize = 140.0f + 90.0f * static_cast<float>(i);
        ring.color = color.withAlpha(static_cast<std::uint8_t>(220 - i * 50));
        ring.endColor = color.withAlpha(0);
        ring.shape = ParticleShape::Ring;
        emit(ring);
    }

    for (int i = 0; i < 34; ++i) {
        const float angle = randomSigned(*this) * kTwoPi;
        const float speed = randomRange(*this, 120.0f, 520.0f);
        Particle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.maxLife = randomRange(*this, 0.25f, 0.7f);
        p.size = randomRange(*this, 1.5f, 4.0f);
        p.endSize = 0.0f;
        p.color = color;
        p.endColor = color.withAlpha(0);
        p.drag = 2.2f;
        p.shape = ParticleShape::Spark;
        p.stretch = randomRange(*this, 2.0f, 5.0f);
        p.rotation = angle;
        emit(p);
    }
}

void ParticleSystem::emitAmbient(Game::Era era, const Bounds& bounds, float dt, float intensity)
{
    if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
        return;
    }

    // Each era's weather is a different particle with different behaviour, which
    // is what makes a room feel like a different place rather than a re-tint.
    float rate = 0.0f;
    Color tint{};
    switch (era) {
        case Era::Past:    rate = 3.0f;  tint = Color::fromFloats(0.55f, 0.85f, 0.45f, 0.0f); break;
        case Era::Present: rate = 2.0f;  tint = Color::fromFloats(0.70f, 0.70f, 0.75f, 0.0f); break;
        case Era::Future:  rate = 5.0f;  tint = Color::fromFloats(0.45f, 0.85f, 0.95f, 0.0f); break;
    }
    rate *= 1.0f + intensity * 2.0f;

    // The count is accumulated across calls rather than recomputed from scratch.
    // At 60Hz a rate of 3 per second is 0.05 particles per frame, and rounding
    // that to an integer gives zero - at which point the "at least one per frame"
    // floor takes over and every era emits exactly the same number. That is how
    // three distinct weathers quietly became one.
    m_ambientAccumulator += rate * dt;
    int count = static_cast<int>(m_ambientAccumulator);
    m_ambientAccumulator -= static_cast<float>(count);
    if (count < 1 && m_ambientAccumulator > 0.0f) {
        count = 1;
        m_ambientAccumulator -= 1.0f;
    }
    // Capped per step, so a one-second hitch cannot spend the whole budget on the
    // one frame where the game is already late.
    count = std::min(count, 6);

    for (int i = 0; i < count; ++i) {
        Particle p;
        p.maxLife = randomRange(*this, 3.0f, 7.0f);
        p.size = randomRange(*this, 1.0f, 2.6f);
        p.endSize = p.size * 0.6f;
        p.color = tint.withAlpha(static_cast<std::uint8_t>(110 + intensity * 80.0f));
        p.endColor = tint.withAlpha(0);
        p.shape = ParticleShape::Ghost;
        p.drag = 0.4f;
        p.gravity = 0.0f;
        p.x = randomRange(*this, bounds.left, bounds.right);
        p.y = randomRange(*this, bounds.top, bounds.bottom);

        switch (era) {
            case Era::Past:
                // Leaves: drift and fall, spinning.
                p.vx = randomRange(*this, -14.0f, 6.0f);
                p.vy = randomRange(*this, 6.0f, 20.0f);
                p.gravity = 4.0f;
                p.spin = randomRange(*this, -3.0f, 3.0f);
                p.maxLife = randomRange(*this, 4.0f, 9.0f);
                p.y = randomRange(*this, bounds.top, bounds.top + bounds.height() * 0.4f);
                break;
            case Era::Present:
                // Dust: nearly weightless, drifting.
                p.vx = randomRange(*this, -6.0f, 6.0f);
                p.vy = randomRange(*this, -3.0f, 3.0f);
                p.gravity = 0.5f;
                p.maxLife = randomRange(*this, 5.0f, 10.0f);
                break;
            case Era::Future:
                // Embers: rise, and streak.
                p.vx = randomRange(*this, -10.0f, 10.0f);
                p.vy = -randomRange(*this, 10.0f, 30.0f);
                p.gravity = -6.0f;
                p.shape = ParticleShape::Spark;
                p.stretch = randomRange(*this, 1.5f, 3.0f);
                p.maxLife = randomRange(*this, 2.0f, 4.5f);
                p.y = randomRange(*this, bounds.top + bounds.height() * 0.5f, bounds.bottom);
                break;
        }
        p.color.a = static_cast<std::uint8_t>(Graphics::clampValue<float>(
            static_cast<float>(p.color.a), 0.0f, 255.0f));
        emit(p);
    }
}

void ParticleSystem::emitGlitch(float x, float y, float width, float amount)
{
    const int count = static_cast<int>(amount * 6.0f) + 1;
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.x = x + randomRange(*this, -0.5f, 0.5f) * width;
        p.y = y + randomRange(*this, -10.0f, 10.0f);
        p.vx = randomRange(*this, 60.0f, 300.0f);
        p.vy = 0.0f;
        p.maxLife = randomRange(*this, 0.04f, 0.12f);
        p.size = randomRange(*this, 6.0f, 30.0f);
        p.endSize = p.size;
        p.color = Color::fromFloats(0.6f, 1.0f, 1.0f, 0.5f);
        p.endColor = Color::fromFloats(1.0f, 0.3f, 0.6f, 0.0f);
        p.shape = ParticleShape::Spark;
        p.stretch = randomRange(*this, 1.0f, 4.0f);
        emit(p);
    }
}

} // namespace EraShift::Graphics
