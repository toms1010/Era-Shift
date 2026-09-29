#include "EraShift/Game/Enemy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace EraShift::Game {

namespace {

/// Patrol span an enemy walks, in world units. Derived from its own size rather
/// than the level, so it is the same regardless of what it is guarding.
constexpr float kPatrolSpan = 120.0f;

constexpr float kDeathDuration = 0.45f;
/// How far the dying body flies when killed.
constexpr float kDeathImpulse = 260.0f;

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view enemyName(EnemyKind kind) noexcept
{
    switch (kind) {
        case EnemyKind::Sentinel: return "Sentinel";
        case EnemyKind::Wisp:     return "Wisp";
        case EnemyKind::Warden:   return "Warden";
    }
    return "Unknown";
}

bool parseEnemyKind(std::string_view name, EnemyKind& out) noexcept
{
    constexpr EnemyKind kAll[] = {EnemyKind::Sentinel, EnemyKind::Wisp, EnemyKind::Warden};
    for (const EnemyKind kind : kAll) {
        if (equalsIgnoreCase(name, enemyName(kind))) {
            out = kind;
            return true;
        }
    }
    return false;
}

std::string_view enemyStateName(EnemyState state) noexcept
{
    switch (state) {
        case EnemyState::Idle:    return "Idle";
        case EnemyState::Patrol:  return "Patrol";
        case EnemyState::Chase:   return "Chase";
        case EnemyState::Windup:  return "Windup";
        case EnemyState::Attack:  return "Attack";
        case EnemyState::Recover: return "Recover";
        case EnemyState::Dying:   return "Dying";
        case EnemyState::Asleep:  return "Asleep";
    }
    return "Unknown";
}

EnemyTuning tuningFor(EnemyKind kind) noexcept
{
    switch (kind) {
        case EnemyKind::Sentinel:
            return EnemyTuning{/*maxHealth*/ 3.0f,
                               /*moveSpeed*/ 70.0f,
                               /*chaseSpeed*/ 135.0f,
                               /*aggroRange*/ 190.0f,
                               /*loseRange*/ 300.0f,
                               /*attackRange*/ 36.0f,
                               /*contactDamage*/ 1.0f,
                               /*windupTime*/ 0.35f,
                               /*attackTime*/ 0.18f,
                               /*recoverTime*/ 0.40f,
                               /*lungeSpeed*/ 330.0f,
                               /*lungeTime*/ 0.20f,
                               /*thinkEvery*/ 2.0f,
                               /*flies*/ false,
                               /*hurtFlash*/ 0.12f};

        case EnemyKind::Wisp:
            // Exists only in the Future, hovers, and is fragile but quick. It
            // ignores gravity entirely, which is what reads as "not physical".
            return EnemyTuning{/*maxHealth*/ 2.0f,
                               /*moveSpeed*/ 110.0f,
                               /*chaseSpeed*/ 165.0f,
                               /*aggroRange*/ 260.0f,
                               /*loseRange*/ 420.0f,
                               /*attackRange*/ 30.0f,
                               /*contactDamage*/ 1.0f,
                               /*windupTime*/ 0.22f,
                               /*attackTime*/ 0.14f,
                               /*recoverTime*/ 0.30f,
                               /*lungeSpeed*/ 260.0f,
                               /*lungeTime*/ 0.22f,
                               /*thinkEvery*/ 2.0f,
                               /*flies*/ true,
                               /*hurtFlash*/ 0.10f};

        case EnemyKind::Warden:
            // Exists only in the Past. Slow, heavy, telegraphed. Shifting out
            // of the Past is the clean answer to one, which is the point.
            return EnemyTuning{/*maxHealth*/ 7.0f,
                               /*moveSpeed*/ 42.0f,
                               /*chaseSpeed*/ 70.0f,
                               /*aggroRange*/ 170.0f,
                               /*loseRange*/ 260.0f,
                               /*attackRange*/ 44.0f,
                               /*contactDamage*/ 2.0f,
                               /*windupTime*/ 0.55f,
                               /*attackTime*/ 0.22f,
                               /*recoverTime*/ 0.60f,
                               /*lungeSpeed*/ 200.0f,
                               /*lungeTime*/ 0.24f,
                               /*thinkEvery*/ 3.0f,
                               /*flies*/ false,
                               /*hurtFlash*/ 0.14f};
    }
    return EnemyTuning{};
}

void Enemy::spawn(EnemyKind kind, const Vec2& position, EraMask existsIn)
{
    m_kind     = kind;
    m_tuning   = tuningFor(kind);
    m_existsIn = existsIn == 0 ? kEraMaskAll : existsIn;

    m_body         = Body{};
    m_body.position = position;
    m_body.size     = m_tuning.flies ? Vec2{30.0f, 30.0f} : Vec2{32.0f, 48.0f};

    m_health      = m_tuning.maxHealth;
    m_facing      = 1.0f;
    m_state       = EnemyState::Idle;
    m_stateTimer  = 0.0f;
    m_hurtTimer   = 0.0f;
    m_lungeTimer  = 0.0f;
    m_removed     = false;
    m_era         = Era::Present;

    // The patrol is the span it was placed on, so it turns around at the edges
    // of its own platform without needing to know where the platform is.
    m_patrolLeft  = position.x;
    m_patrolRight = position.x + kPatrolSpan;
}

float Enemy::stateProgress() const noexcept
{
    if (m_state != EnemyState::Windup && m_state != EnemyState::Attack &&
        m_state != EnemyState::Recover) {
        return 1.0f;
    }
    const float duration = m_state == EnemyState::Windup    ? m_tuning.windupTime
                           : m_state == EnemyState::Attack ? m_tuning.attackTime
                                                           : m_tuning.recoverTime;
    if (duration <= 0.0f) {
        return 1.0f;
    }
    return Graphics::clampValue(1.0f - (m_stateTimer / duration), 0.0f, 1.0f);
}

Rect Enemy::attackBox() const noexcept
{
    if (!dangerous()) {
        return {};
    }
    const float reach = m_tuning.attackRange + m_body.size.x * 0.5f;
    const float x = m_facing > 0.0f ? m_body.right() - 4.0f : m_body.position.x - reach + 4.0f;
    // Slightly taller than the body so a duck-under is not trivially free.
    return Rect{x, m_body.position.y - 4.0f, reach, m_body.size.y + 8.0f};
}

void Enemy::setState(EnemyState state, float timer)
{
    m_state      = state;
    m_stateTimer = timer;
}

void Enemy::update(const TileMap& map, Era era, const Vec2& playerPosition, float dt,
                   float frameIndex)
{
    if (m_removed || dt <= 0.0f || !std::isfinite(dt)) {
        return;
    }

    m_era = era;

    m_hurtTimer  = std::max(0.0f, m_hurtTimer - dt);
    m_lungeTimer = std::max(0.0f, m_lungeTimer - dt);

    // Dying runs to completion regardless of era; otherwise an enemy killed in
    // the Present would freeze mid-death when the player shifted to the Past.
    if (m_state == EnemyState::Dying) {
        m_body.velocity.y += 1400.0f * dt;
        moveBody(map, m_body, m_era, dt);
        m_stateTimer -= dt;
        if (m_stateTimer <= 0.0f) {
            m_removed = true;
        }
        return;
    }

    if (!aliveIn(m_era)) {
        setState(EnemyState::Asleep, 0.0f);
        // A sleeping enemy is frozen: no gravity, no movement. Coming back to
        // its era later must find it exactly where it was left, which is the
        // whole point of the mechanic.
        m_body.velocity = Vec2{};
        return;
    }

    if (m_state == EnemyState::Asleep) {
        // Waking up resumes patrol rather than restarting the fight, so an
        // enemy that was chasing does not forget what it was doing.
        setState(EnemyState::Patrol, 0.0f);
    }

    // Staggered thinking. Not every enemy re-evaluates every frame; the states
    // that matter for fairness (the attack windows) are driven by timers rather
    // than by re-deciding, so they stay exact regardless of the stagger.
    const float period = std::max(1.0f, m_tuning.thinkEvery);
    if (std::fmod(frameIndex, period) < 1.0f) {
        think(playerPosition);
    }

    m_stateTimer -= dt;
    switch (m_state) {
        case EnemyState::Windup:
            if (m_stateTimer <= 0.0f) {
                setState(EnemyState::Attack, m_tuning.attackTime);
                m_lungeTimer = m_tuning.lungeTime;
            }
            break;

        case EnemyState::Attack:
            if (m_stateTimer <= 0.0f) {
                setState(EnemyState::Recover, m_tuning.recoverTime);
            }
            break;

        case EnemyState::Recover:
            // Recover is the punish window. Recovering enemies do not move, so
            // a swing that lands during it is guaranteed to connect.
            if (m_stateTimer <= 0.0f) {
                setState(EnemyState::Chase, 0.0f);
            }
            break;

        default:
            break;
    }

    // --- locomotion ---------------------------------------------------------
    switch (m_state) {
        case EnemyState::Chase:
            m_body.velocity.x = m_facing * m_tuning.chaseSpeed;
            if (m_tuning.flies) {
                // Hovering enemies hold station vertically rather than falling.
                const float wanted  = playerPosition.y - m_body.size.y * 0.5f;
                m_body.velocity.y  = Graphics::moveTowards(m_body.velocity.y,
                                                           wanted - m_body.position.y, 220.0f * dt);
            }
            break;

        case EnemyState::Attack:
            m_body.velocity.x = m_lungeTimer > 0.0f ? m_facing * m_tuning.lungeSpeed : 0.0f;
            break;

        case EnemyState::Windup:
        case EnemyState::Recover:
            m_body.velocity.y = m_tuning.flies ? m_body.velocity.y : 0.0f;
            m_body.velocity.x = 0.0f;
            break;

        case EnemyState::Dying:
            break;

        default:
            m_body.velocity.x = m_facing * m_tuning.moveSpeed;
            break;
    }

    if (m_tuning.flies) {
        // Gentle bob so a hovering enemy never looks pinned in the air.
        m_body.velocity.y += std::sin(frameIndex * 0.08f) * 40.0f * dt;
        m_body.velocity.y  = Graphics::clampValue(m_body.velocity.y, -140.0f, 140.0f);
    } else {
        m_body.velocity.y += 1500.0f * dt;
        m_body.velocity.y = std::min(m_body.velocity.y, 900.0f);
    }

    const float previousX = m_body.position.x;
    moveBody(map, m_body, m_era, dt);
    resolvePenetration(map, m_body, m_era);

    if (m_tuning.flies) {
        if (m_body.hitWall) {
            m_facing = -m_facing;
        }
        return;
    }

    // A wall, the end of the patrol, or a wall of empty air ahead (detected by
    // the body failing to move while it wanted to) flips the direction.
    // Without this a patrolling enemy walks off its platform and never returns.
    const float travelled = m_body.position.x - previousX;
    const bool  atEnd = (m_facing > 0.0f && m_body.position.x >= m_patrolRight) ||
                        (m_facing < 0.0f && m_body.position.x <= m_patrolLeft);
    const bool  pushedBack = travelled < 0.0f;
    const bool  noGroundAhead = m_body.onGround && travelled == 0.0f;

    if (m_body.hitWall || atEnd || pushedBack || noGroundAhead) {
        m_facing = -m_facing;
    }
}

void Enemy::think(const Vec2& playerPosition)
{
    const float dx    = playerPosition.x - m_body.center().x;
    const float dy    = playerPosition.y - m_body.center().y;
    const float range = std::sqrt(dx * dx + dy * dy);

    switch (m_state) {
        case EnemyState::Idle:
        case EnemyState::Patrol:
            if (range < m_tuning.aggroRange) {
                m_facing = dx >= 0.0f ? 1.0f : -1.0f;
                setState(EnemyState::Chase, 0.0f);
            }
            break;

        case EnemyState::Chase:
            // Re-evaluate facing every think so a circling player is tracked.
            m_facing = dx >= 0.0f ? 1.0f : -1.0f;

            if (range > m_tuning.loseRange) {
                setState(EnemyState::Patrol, 0.0f);
                break;
            }
            // Only commit to an attack from close range, and only when roughly
            // level with the target: an enemy that lunges at someone three
            // ledges above it looks broken rather than dangerous.
            if (std::fabs(dx) <= m_tuning.attackRange && std::fabs(dy) <= m_body.size.y) {
                setState(EnemyState::Windup, m_tuning.windupTime);
            }
            break;

        default:
            break;
    }
}

bool Enemy::takeDamage(float amount, Vec2 from)
{
    if (m_removed || amount <= 0.0f) {
        return false;
    }
    // An enemy that is not in the current era cannot be hit. This is what stops
    // "swing at everything" from being a universal answer, and it is also why
    // the swing has to be aimed at something visible.
    if (!aliveIn(m_era) || m_state == EnemyState::Dying) {
        return false;
    }

    m_health     -= amount;
    m_hurtTimer   = m_tuning.hurtFlash;

    // Taking a hit always interrupts whatever it was doing, so an enemy cannot
    // be staggered into a state where it is both stopped and still winding up.
    if (m_health <= 0.0f) {
        m_health = 0.0f;
        setState(EnemyState::Dying, kDeathDuration);
        const Vec2 away = (m_body.center() - from);
        const Vec2 dir  = away.isZero() ? Vec2{m_facing, -1.0f} : away.normalized();
        m_body.velocity  = Vec2{dir.x * kDeathImpulse, -kDeathImpulse * 0.6f};
        if (m_tuning.flies) {
            m_body.velocity.y = -kDeathImpulse * 0.4f;
        }
        return true;
    }

    m_hurtTimer = m_tuning.hurtFlash;
    setState(EnemyState::Recover, m_tuning.recoverTime * 0.5f);
    m_body.velocity.x = (m_body.center().x < from.x ? -1.0f : 1.0f) * m_tuning.moveSpeed * 1.5f;
    return true;
}

} // namespace EraShift::Game
