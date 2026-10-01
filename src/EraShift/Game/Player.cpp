#include "EraShift/Game/Player.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

namespace {

/// Ground covered between footfalls. A stride, in the sense that matters: it is
/// the distance a foot travels, so the sound lands where the foot lands. At the
/// default 250px/s this is a step roughly every third of a second.
constexpr float kStrideLength = 76.0f;

/// Vertical speed above which a landing counts as an impact rather than a step.
///
/// Just over the speed gravity reaches in a fifth of a second, so the recovery is
/// for jumps and drops rather than for walking down a slope.
constexpr float kLandRecoverySpeed = 320.0f;

} // namespace

void Player::reset(const Vec2& spawn, Era era)
{
    m_era    = era;
    m_health = m_tuning.maxHealth;
    m_chrono = m_tuning.maxChrono;

    m_body        = Body{};
    m_body.position = spawn;
    m_body.size     = Vec2{28.0f, 44.0f};
    m_body.velocity = Vec2{};

    m_facing      = 1.0f;
    m_coyoteTimer = 0.0f;
    m_jumpBuffer  = 0.0f;
    m_dashTimer   = 0.0f;
    m_dashCooldown = 0.0f;
    m_invulnTimer = 0.0f;
    m_hurtTimer   = 0.0f;
    m_eraBlend    = 1.0f;
    m_attack      = AttackState{};
}

void Player::restore(const Vec2& spawn, Era era, float health, float chrono)
{
    reset(spawn, era);
    m_era    = era;
    m_health = Graphics::clampValue(health, 0.0f, m_tuning.maxHealth);
    m_chrono = Graphics::clampValue(chrono, 0.0f, m_tuning.maxChrono);
}

float Player::attackDirection() const noexcept
{
    return m_facing < 0.0f ? -1.0f : 1.0f;
}

Rect Player::attackBox() const noexcept
{
    if (m_attack.phase != AttackPhase::Active) {
        return {};
    }
    const float dir    = attackDirection();
    const float reach  = m_tuning.attackReach;
    const float height = m_tuning.attackHeight;

    // The box sits in front of the body and is vertically centred on it, which
    // is what lets a swing clear a target standing on the same floor.
    const float x = dir > 0.0f ? m_body.right() - 2.0f : m_body.position.x - reach + 2.0f;
    const float y = m_body.position.y + (m_body.size.y - height) * 0.5f;
    return Rect{x, y, reach, height};
}

bool Player::update(const TileMap& map, const PlayerInput& input, float dt)
{
    if (dt <= 0.0f || !std::isfinite(dt)) {
        return m_health > 0.0f;
    }
    if (!alive()) {
        m_events = PlayerStepEvents{};
        return false;
    }

    // Cleared first, so every edge below describes this step and nothing older.
    m_events = PlayerStepEvents{};

    const float axis = Graphics::clampValue(input.moveAxis, -1.0f, 1.0f);
    if (std::fabs(axis) > 0.01f) {
        m_facing = axis > 0.0f ? 1.0f : -1.0f;
    }

    // --- timers -------------------------------------------------------------
    m_invulnTimer  = std::max(0.0f, m_invulnTimer - dt);
    m_hurtTimer    = std::max(0.0f, m_hurtTimer - dt);
    m_dashCooldown = std::max(0.0f, m_dashCooldown - dt);
    m_coyoteTimer  = std::max(0.0f, m_coyoteTimer - dt);
    m_jumpBuffer   = std::max(0.0f, m_jumpBuffer - dt);
    m_eraBlend     = std::min(1.0f, m_eraBlend + dt / std::max(0.01f, m_tuning.shiftDuration));

    updateAttack(input, dt);

    const bool onGround = m_body.onGround;

    // --- horizontal ---------------------------------------------------------
    // A dash owns the horizontal axis for its duration. Anything else fights
    // the dash and produces the mushy "I dashed but it also tried to walk" feel.
    if (m_dashTimer > 0.0f) {
        m_dashTimer = std::max(0.0f, m_dashTimer - dt);
        m_body.velocity.x = m_facing * m_tuning.dashSpeed;
        if (m_dashTimer == 0.0f) {
            m_body.velocity.x = 0.0f;
        }
    } else {
        const float accel = onGround ? m_tuning.groundAccel : m_tuning.airAccel;
        if (std::fabs(axis) > 0.01f) {
            m_body.velocity.x = Graphics::moveTowards(m_body.velocity.x, axis * m_tuning.moveSpeed,
                                                       accel * dt);
        } else {
            const float friction = onGround ? m_tuning.groundFriction : m_tuning.airFriction;
            m_body.velocity.x = Graphics::moveTowards(m_body.velocity.x, 0.0f, friction * dt);
        }
    }

    // --- jump ---------------------------------------------------------------
    if (input.jumpPressed) {
        m_jumpBuffer = m_tuning.jumpBuffer;
    }

    if (m_jumpBuffer > 0.0f && (onGround || m_coyoteTimer > 0.0f) && m_dashTimer <= 0.0f) {
        m_body.velocity.y = -m_tuning.jumpSpeed;
        m_jumpBuffer      = 0.0f;
        m_coyoteTimer     = 0.0f;
        m_dashTimer       = 0.0f;
        m_events.jumped   = true;
    } else if (m_jumpBuffer > 0.0f && m_coyoteTimer <= 0.0f && !onGround) {
        // The press is still worth holding on to for a landing inside the
        // buffer window, so the timer is left to run down rather than cleared.
        m_jumpBuffer = std::max(0.0f, m_jumpBuffer - dt);
    }

    // Releasing the button early cuts the rise. Without this the jump is a
    // single fixed height and has no expressive range.
    if (!input.jumpHeld && m_body.velocity.y < -m_tuning.jumpCutSpeed) {
        m_body.velocity.y = -m_tuning.jumpCutSpeed;
    }

    // --- dash ---------------------------------------------------------------
    if (input.dashPressed && m_dashTimer <= 0.0f && m_dashCooldown <= 0.0f) {
        m_dashTimer     = m_tuning.dashTime;
        m_dashCooldown  = m_tuning.dashCooldown + m_tuning.dashTime;
        m_body.velocity.x = m_facing * m_tuning.dashSpeed;
        m_events.dashed    = true;
    }

    // --- vertical -----------------------------------------------------------
    // Gravity is suspended during a dash, which is what makes the dash cover
    // the gap a jump would fall into.
    if (m_dashTimer <= 0.0f) {
        m_body.velocity.y += m_tuning.gravity * dt;
        m_body.velocity.y = std::min(m_body.velocity.y, m_tuning.maxFallSpeed);
    }

    // The landing recovery, counted down every step. Scaled movement rather than
    // none, so the player keeps control the instant they land.
    if (m_landTimer > 0.0f) {
        m_landTimer = std::max(0.0f, m_landTimer - dt);
    }

    // --- integrate ----------------------------------------------------------
    const bool wasGrounded = m_body.onGround;
    // Captured *before* the body is integrated. `moveBody` clamps the vertical
    // velocity to zero on the step it lands, so reading it afterwards - which is
    // what this used to do - reported every landing as a dead stop. The landing
    // dust has therefore been at its weakest possible strength for every landing
    // in the game, from the gentlest step to a drop off the top of the level.
    const float impactSpeed = std::fabs(m_body.velocity.y);

    moveBody(map, m_body, m_era, dt);
    resolvePenetration(map, m_body, m_era);

    if (m_body.onGround) {
        if (!wasGrounded) {
            // Landing refreshes the coyote window, so bouncing off a floor
            // immediately does not need the button pressed again.
            m_coyoteTimer = m_tuning.coyoteTime;
            m_events.landed   = true;
            m_events.landSpeed = impactSpeed;
            m_lastLandSpeed   = impactSpeed;
            // Only a real landing gets a recovery. A one-tile step off a ledge
            // triggers the same edge, and playing a squash for that would make
            // every step down a stair look like an impact.
            if (impactSpeed > kLandRecoverySpeed) {
                m_landTimer = m_tuning.landRecovery;
            }
        }
    } else if (wasGrounded) {
        m_coyoteTimer = m_tuning.coyoteTime;
    }
    m_events.grounded = m_body.onGround;

    // Footsteps are counted in distance, not time. A walk cycle tied to a clock
    // slides its feet at low speed and scrabbles at high; one tied to ground
    // covered puts the sound where the step actually happened.
    if (m_body.onGround) {
        m_strideDistance += std::fabs(m_body.velocity.x) * dt;
    }
    if (m_strideDistance >= kStrideLength) {
        m_strideDistance = 0.0f;
        m_events.footstep = m_body.onGround && std::fabs(m_body.velocity.x) > 20.0f;
    }

    // Falling out of the world is always fatal, whatever the era.
    const Rect world = map.bounds();
    if (!world.isEmpty() && m_body.position.y > world.bottom()) {
        m_health = 0.0f;
        m_body.velocity = Vec2{};
    }

    updateChrono(dt);
    return m_health > 0.0f;
}

float Player::attackProgress() const noexcept
{
    // Walked backwards through the phases, accumulating the time already spent.
    // Idle is 0 by definition, which is what lets the caller use 0 as "not
    // swinging" without a second flag.
    const PlayerTuning& t = m_tuning;
    switch (m_attack.phase) {
        case AttackPhase::Idle:
            return 0.0f;
        case AttackPhase::Windup:
            return 1.0f - Graphics::clampValue(m_attack.timer / std::max(0.001f, t.attackWindup), 0.0f, 1.0f);
        case AttackPhase::Active: {
            const float total = t.attackWindup + t.attackActive;
            const float done  = t.attackWindup - Graphics::clampValue(
                                                        m_attack.timer / std::max(0.001f, t.attackActive), 0.0f, 1.0f);
            return Graphics::clampValue(done / std::max(0.001f, total), 0.0f, 1.0f);
        }
        case AttackPhase::Recover: {
            const float total = t.attackWindup + t.attackActive + t.attackRecover;
            const float done  = t.attackWindup + t.attackActive - Graphics::clampValue(
                     m_attack.timer / std::max(0.001f, t.attackRecover), 0.0f, 1.0f);
            return Graphics::clampValue(done / std::max(0.001f, total), 0.0f, 1.0f);
        }
    }
    return 0.0f;
}

void Player::updateChrono(float dt)
{
    m_chrono = std::min(m_chrono + m_tuning.chronoRegen * dt, m_tuning.maxChrono);
}

void Player::updateAttack(const PlayerInput& input, float dt)
{
    switch (m_attack.phase) {
        case AttackPhase::Idle:
            if (input.attackPressed) {
                m_attack.phase = AttackPhase::Windup;
                m_attack.timer = m_tuning.attackWindup;
                m_events.attacked = true;
            }
            break;

        case AttackPhase::Windup:
            m_attack.timer -= dt;
            if (m_attack.timer <= 0.0f) {
                m_attack.phase = AttackPhase::Active;
                m_attack.timer = m_tuning.attackActive;
                // The window opening is its own edge. This is the frame the swing
                // becomes dangerous, and both the hit spark and the hit sound hang
                // off it rather than off the press.
                m_events.swingConnected = true;
            }
            break;

        case AttackPhase::Active:
            m_attack.timer -= dt;
            if (m_attack.timer <= 0.0f) {
                m_attack.phase = AttackPhase::Recover;
                m_attack.timer = m_tuning.attackRecover;
            }
            break;

        case AttackPhase::Recover:
            m_attack.timer -= dt;
            if (m_attack.timer <= 0.0f) {
                m_attack.phase = AttackPhase::Idle;
                m_attack.timer = 0.0f;
            }
            break;
    }
}

bool Player::takeDamage(float amount, Vec2 from)
{
    if (amount <= 0.0f || !alive() || invulnerable()) {
        return false;
    }

    m_health = std::max(0.0f, m_health - amount);
    m_invulnTimer = m_tuning.invulnTime;
    // The flinch runs its own, much shorter clock than the i-frames. See
    // `Player::hurt()`.
    m_hurtTimer = m_tuning.hurtTime;
    m_events.hurt = true;

    // Knockback always points away from the source. Dashing cancels it, which
    // gives the dash a defensive use as well as a mobility one.
    const Vec2 away = (m_body.center() - from);
    const Vec2 dir  = away.isZero() ? Vec2{-attackDirection(), -1.0f} : away.normalized();
    m_body.velocity.x = dir.x * m_tuning.knockbackX;
    m_body.velocity.y = std::min(m_body.velocity.y, -m_tuning.knockbackY);
    m_dashTimer      = 0.0f;

    if (!alive()) {
        m_body.velocity = Vec2{};
    }
    return true;
}

void Player::heal(float amount)
{
    // No healing a dead player. The world stops simulating input once the run
    // is lost, so this is mostly belt-and-braces - but a healing pickup that
    // could revive a corpse is a rule nobody intended.
    if (!alive()) {
        return;
    }
    m_health = std::min(m_tuning.maxHealth, m_health + std::max(0.0f, amount));
}

void Player::refillChrono()
{
    m_chrono = m_tuning.maxChrono;
}

bool Player::shiftTo(Era era, const TileMap& map)
{
    if (m_chrono < m_tuning.shiftCost) {
        return false;
    }
    if (era == m_era) {
        return false;
    }

    m_chrono      -= m_tuning.shiftCost;
    m_era          = era;
    m_eraBlend     = 0.0f;
    m_invulnTimer  = std::max(m_invulnTimer, m_tuning.shiftGrace);

    // The world just changed underneath the player. Eject them from anything
    // solid that appeared, and do it after the era changes so the query is
    // against the new world.
    resolvePenetration(map, m_body, m_era);
    return true;
}

} // namespace EraShift::Game
