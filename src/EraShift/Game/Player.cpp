#include "EraShift/Game/Player.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

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
        return false;
    }

    const float axis = Graphics::clampValue(input.moveAxis, -1.0f, 1.0f);
    if (std::fabs(axis) > 0.01f) {
        m_facing = axis > 0.0f ? 1.0f : -1.0f;
    }

    // --- timers -------------------------------------------------------------
    m_invulnTimer  = std::max(0.0f, m_invulnTimer - dt);
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
    }

    // --- vertical -----------------------------------------------------------
    // Gravity is suspended during a dash, which is what makes the dash cover
    // the gap a jump would fall into.
    if (m_dashTimer <= 0.0f) {
        m_body.velocity.y += m_tuning.gravity * dt;
        m_body.velocity.y = std::min(m_body.velocity.y, m_tuning.maxFallSpeed);
    }

    // --- integrate ----------------------------------------------------------
    const bool wasGrounded = m_body.onGround;
    moveBody(map, m_body, m_era, dt);
    resolvePenetration(map, m_body, m_era);

    if (m_body.onGround) {
        if (!wasGrounded) {
            // Landing refreshes the coyote window, so bouncing off a floor
            // immediately does not need the button pressed again.
            m_coyoteTimer = m_tuning.coyoteTime;
        }
    } else if (wasGrounded) {
        m_coyoteTimer = m_tuning.coyoteTime;
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
            }
            break;

        case AttackPhase::Windup:
            m_attack.timer -= dt;
            if (m_attack.timer <= 0.0f) {
                m_attack.phase = AttackPhase::Active;
                m_attack.timer = m_tuning.attackActive;
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
