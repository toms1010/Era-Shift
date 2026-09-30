// Era Shift - enemies.
//
// Three things distinguish an enemy from a player with different numbers:
//
//   * It has an era mask. A Sentinel exists in every era; a Wisp only exists in
//     the Future; a Warden only in the Past. Shifting out of an enemy's era
//     does not kill it, it puts it to sleep, and shifting back wakes it up with
//     its health intact. That is the "choices persist" promise of the premise,
//     expressed as a mechanic instead of a slogan.
//   * It has a small state machine rather than a single behaviour, so an
//     attacking enemy is visibly committed and can be punished.
//   * It is updated on a stagger, so a hundred enemies do not cost a hundred
//     full AI evaluations per frame.

#pragma once

#include "EraShift/Game/Actor.hpp"
#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Graphics/Math.hpp"

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

enum class EnemyKind : std::uint8_t {
    Sentinel,  ///< Present in every era. Patrols, charges when it sees you.
    Wisp,      ///< Future only. Flies, drifts, harasses.
    Warden,    ///< Past only. Slow, heavy, hits hard. Guards the path.
};

[[nodiscard]] std::string_view enemyName(EnemyKind kind) noexcept;
[[nodiscard]] bool parseEnemyKind(std::string_view name, EnemyKind& out) noexcept;

/// What an enemy is doing. Shown by the debug overlay and used to decide when
/// it can be hit.
enum class EnemyState : std::uint8_t {
    Asleep,    ///< Exists, but not in the current era.
    Idle,      ///< Standing still until the player comes into range.
    Patrol,    ///< Walking back and forth over its own platform.
    Chase,     ///< Moving towards the player.
    Windup,    ///< Committed to an attack, before it becomes dangerous.
    Attack,    ///< Dangerous.
    Recover,   ///< Vulnerable tail end of an attack.
    Dying,     ///< Playing out its death before removal.
};

[[nodiscard]] std::string_view enemyStateName(EnemyState state) noexcept;

/// Per-kind tuning. Values differ enough between kinds that a single struct with
/// defaults would be a lie about how much they actually vary.
struct EnemyTuning {
    float maxHealth   = 3.0f;
    float moveSpeed   = 70.0f;
    float chaseSpeed  = 130.0f;
    float aggroRange  = 190.0f;
    float loseRange   = 320.0f;
    float attackRange = 34.0f;
    float contactDamage = 1.0f;
    /// Windup length. A visible telegraph the player can react to.
    float windupTime  = 0.35f;
    float attackTime  = 0.16f;
    float recoverTime = 0.40f;
    /// How often the lunge accelerates the enemy.
    float lungeSpeed  = 340.0f;
    float lungeTime   = 0.18f;
    /// Fraction of the frame this enemy thinks on. Lower is cheaper.
    float thinkEvery  = 2.0f;
    bool  flies       = false;
    float hurtFlash   = 0.12f;
};

[[nodiscard]] EnemyTuning tuningFor(EnemyKind kind) noexcept;

/// One enemy. Plain data plus behaviour; the world owns them in a vector.
class Enemy {
public:
    Enemy() = default;

    void spawn(EnemyKind kind, const Vec2& position, EraMask existsIn);

    /// A stable identity, unique for the lifetime of a run.
    ///
    /// Enemies live in a vector that gets compacted when one dies, so a vector
    /// index is not an identity: shift two entries and every index after them
    /// now points at a different enemy. The presentation layer keeps per-enemy
    /// state - an animation controller, a flash timer - and it needs something
    /// that survives the compaction. Set by `World` at spawn; never reused.
    [[nodiscard]] std::uint32_t id() const noexcept { return m_id; }
    void setId(std::uint32_t id) noexcept { m_id = id; }

/// Advances one step. `playerPosition` and `era` are passed in rather than
    /// reached through the world, so an enemy never needs a world pointer and
    /// its behaviour is a pure function of what it is told.
    void update(const TileMap& map, Era era, const Vec2& playerPosition, float dt,
                float frameIndex);

    /// Applies a hit. Returns false when the enemy is asleep, already dying or
    /// the hit missed its window.
    bool takeDamage(float amount, Vec2 from);

    [[nodiscard]] bool aliveIn(Era era) const noexcept
    {
        return m_health > 0.0f && maskIncludes(m_existsIn, era);
    }
    [[nodiscard]] bool inCurrentEra() const noexcept { return aliveIn(m_era); }
    [[nodiscard]] bool removed() const noexcept { return m_removed; }

    [[nodiscard]] EnemyKind kind() const noexcept { return m_kind; }
    [[nodiscard]] EnemyState state() const noexcept { return m_state; }
    [[nodiscard]] EraMask existsIn() const noexcept { return m_existsIn; }
    /// The era this enemy was last updated for.
    [[nodiscard]] Era era() const noexcept { return m_era; }
    [[nodiscard]] float health() const noexcept { return m_health; }
    [[nodiscard]] float maxHealth() const noexcept { return m_tuning.maxHealth; }
    [[nodiscard]] float facing() const noexcept { return m_facing; }
    [[nodiscard]] bool hurt() const noexcept { return m_hurtTimer > 0.0f; }
    [[nodiscard]] bool dangerous() const noexcept { return m_state == EnemyState::Attack; }
    /// Seconds left in the current state.
    [[nodiscard]] float stateTimer() const noexcept { return m_stateTimer; }
    /// 0 at the start of the current state, 1 when it finishes. Drives the
    /// wind-up telegraph, which is only readable if it visibly fills.
    [[nodiscard]] float stateProgress() const noexcept;
    [[nodiscard]] float contactDamage() const noexcept { return m_tuning.contactDamage; }

    /// Damage box, meaningful only while `dangerous()`.
    [[nodiscard]] Rect attackBox() const noexcept;
    [[nodiscard]] const Body& body() const noexcept { return m_body; }
    [[nodiscard]] const EnemyTuning& tuning() const noexcept { return m_tuning; }

private:
    void setState(EnemyState state, float timer);
    void think(const Vec2& playerPosition);

    EnemyKind  m_kind = EnemyKind::Sentinel;
    std::uint32_t m_id = 0;
    EnemyState m_state = EnemyState::Idle;
    EnemyTuning m_tuning;
    Body        m_body;
    EraMask     m_existsIn = kEraMaskAll;
    Era         m_era = Era::Present;

    float m_health = 1.0f;
    float m_facing = 1.0f;
    float m_stateTimer = 0.0f;
    float m_hurtTimer  = 0.0f;
    float m_lungeTimer = 0.0f;
    /// Left edge of the patrol it was spawned on, used to turn around.
    float m_patrolLeft  = 0.0f;
    float m_patrolRight = 0.0f;
    bool  m_removed = false;
};

} // namespace EraShift::Game
