// Era Shift - the player.
//
// Everything that makes a character feel good to control lives here and nothing
// else does. The three details that separate a character that feels good from
// one that feels broken are all present and all matter:
//
//   * Acceleration and friction rather than instant velocity, so starting and
//     stopping have weight and the player can steer mid-stride.
//   * Coyote time, so a jump pressed a few frames after walking off a ledge
//     still works.
//   * A jump buffer, so a jump pressed a few frames before landing still fires.
//
// A fourth, subtler one: releasing the jump button early cuts the rise short,
// which is what makes the jump height controllable.
//
// The tuning constants are exposed so tests can drive the controller
// deterministically and so a data file can override them later.

#pragma once

#include "EraShift/Game/Actor.hpp"
#include "EraShift/Game/Era.hpp"
#include "EraShift/Game/TileMap.hpp"
#include "EraShift/Graphics/Math.hpp"

namespace EraShift::Game {

using Graphics::Rect;
using Graphics::Vec2;

/// The controls for one simulation step, decoupled from the input system so the
/// controller is testable without SDL.
struct PlayerInput {
    float moveAxis   = 0.0f;   ///< -1 left, +1 right, 0 still.
    bool  jumpPressed = false;  ///< Edge.
    bool  jumpHeld    = false;
    bool  dashPressed = false;  ///< Edge.
    bool  attackPressed = false;
    bool  attackHeld    = false;
};

/// Everything about how the player moves and fights.
struct PlayerTuning {
    float moveSpeed      = 250.0f;
    float groundAccel    = 2600.0f;
    float airAccel       = 1500.0f;
    float groundFriction = 2800.0f;
    float airFriction    = 500.0f;

    float gravity        = 1500.0f;
    float maxFallSpeed   = 900.0f;
    float jumpSpeed      = 530.0f;
    /// Upward speed the rise is clamped to when the jump button is released.
    float jumpCutSpeed   = 200.0f;

    float coyoteTime     = 0.10f;   ///< Seconds of grace after leaving a ledge.
    float jumpBuffer     = 0.12f;   ///< Seconds a press is remembered before landing.

    float dashSpeed      = 620.0f;
    float dashTime       = 0.16f;
    float dashCooldown   = 0.55f;

    float maxHealth      = 5.0f;
    /// Seconds of invulnerability after taking a hit.
    float invulnTime     = 0.9f;
    /// Seconds the hurt *animation* runs for, which is the clip's own length.
    ///
    /// Deliberately much shorter than the invulnerability window. i-frames last
    /// most of a second, and a reaction pose held for most of a second is a
    /// character standing around flinching; the pose is a flinch, and the
    /// invulnerability is a separate rule with a separate length.
    float hurtTime       = 0.22f;
    /// Knockback applied away from the source of the damage.
    float knockbackX     = 190.0f;
    float knockbackY     = 260.0f;

    float maxChrono      = 100.0f;
    float shiftCost      = 25.0f;
    float chronoRegen    = 9.0f;
    /// Seconds the world takes to dissolve between eras after a shift.
    float shiftDuration  = 0.35f;
    /// Seconds of invulnerability granted by a successful shift.
    float shiftGrace     = 0.25f;

    float attackWindup   = 0.06f;
    float attackActive   = 0.10f;
    float attackRecover  = 0.16f;
    /// Reach in front of the player and half-height of the hit box.
    float attackReach    = 44.0f;
    float attackHeight   = 40.0f;
    float attackDamage   = 1.0f;
};

/// The three phases of a swing.
enum class AttackPhase : std::uint8_t { Idle, Windup, Active, Recover };

/// One-step edges, for the presentation layer to turn into sound and particles.
///
/// The simulation already knows when these happen; what it deliberately does not
/// know is that a jump should make a noise. So it publishes the fact and lets the
/// state decide what that fact looks and sounds like. Reset at the top of every
/// `update`, so anything still set is from this step and nothing is.
struct PlayerStepEvents {
    bool  jumped   = false;
    /// Landed this step. Distinguishing the two is what lets a small step off a
    /// ledge stay quiet while a deliberate drop lands with a thump.
    bool  landed   = false;
    /// Vertical speed at the moment of landing, which is what decides how hard
    /// the thump is.
    float landSpeed = 0.0f;
    bool  dashed   = false;
    /// A swing started this step.
    bool  attacked = false;
    /// The swing's hit window opened this step. Distinct from `attacked`, which
    /// fires on the press: a swing that whiffs still has a swing sound.
    bool  swingConnected = false;
    /// Set the step the player is knocked back, so the HUD and the camera can
    /// react to the same instant.
    bool  hurt = false;
    /// True while standing still and off the ground is irrelevant: this is just
    /// "is on the floor", used to pick the idle rather than the fall animation.
    bool  grounded = false;
    /// True on the step a foot would land, derived from distance travelled, so
    /// the walk cycle's footstep matches the ground rather than the clock.
    bool  footstep = false;
};

class Player {
public:
    Player() = default;

    /// Places the player at `spawn` (top-left corner) and restores full state.
    void reset(const Vec2& spawn, Era era);

    /// Puts the player back where a save says they were.
    ///
    /// Deliberately not `shiftTo`: restoring spends no energy and runs no
    /// transition, because a load should not cost the player anything or make
    /// the world visibly dissolve.
    void restore(const Vec2& spawn, Era era, float health, float chrono);

    /// Advances one fixed step. Returns false once the player is dead, so the
    /// caller can stop simulating input for them.
    bool update(const TileMap& map, const PlayerInput& input, float dt);

    /// Edges from the step that just ran. Valid until the next `update`.
    [[nodiscard]] const PlayerStepEvents& stepEvents() const noexcept { return m_events; }

    /// 0..1 through a whole swing, windup included, and 0 when not swinging.
    ///
    /// The animation controller is driven from this rather than from its own
    /// clock. That is the arrangement that keeps the two in agreement: the
    /// simulation decides when the hitbox opens, and the picture is told where
    /// the simulation is, instead of both running separate timers and hoping.
    [[nodiscard]] float attackProgress() const noexcept;

    /// True when the swing's hit window is currently open.
    [[nodiscard]] bool attackWindowOpen() const noexcept
    {
        return m_attack.phase == AttackPhase::Active;
    }

    /// Applies damage. Returns false when the hit was ignored (already dead or
    /// still invulnerable), which is what makes i-frames observable in tests.
    bool takeDamage(float amount, Vec2 from);

    /// Restores health, up to the maximum. Ignored once dead.
    void heal(float amount);
    void refillChrono();

    /// Spends chrono and switches era. Returns false when there was not enough
    /// energy, which is the resource rule the HUD has to communicate.
    bool shiftTo(Era era, const TileMap& map);

    // --- state --------------------------------------------------------------

    [[nodiscard]] const Body& body() const noexcept { return m_body; }
    [[nodiscard]] Body& body() noexcept { return m_body; }
    [[nodiscard]] Era era() const noexcept { return m_era; }
    [[nodiscard]] float health() const noexcept { return m_health; }
    [[nodiscard]] float maxHealth() const noexcept { return m_tuning.maxHealth; }
    [[nodiscard]] bool alive() const noexcept { return m_health > 0.0f; }
    [[nodiscard]] float chrono() const noexcept { return m_chrono; }
    [[nodiscard]] float maxChrono() const noexcept { return m_tuning.maxChrono; }
    [[nodiscard]] float facing() const noexcept { return m_facing; }
    [[nodiscard]] bool invulnerable() const noexcept { return m_invulnTimer > 0.0f; }
    /// True while the hurt reaction is playing.
    ///
    /// Separate from `invulnerable()`, which is most of a second of i-frames and
    /// is not a thing to animate. This is the flinch, and it exists because
    /// `stepEvents().hurt` is an edge lasting exactly one step - a reaction
    /// selected from a one-frame flag is a reaction that never appears.
    [[nodiscard]] bool hurt() const noexcept { return m_hurtTimer > 0.0f; }
    [[nodiscard]] bool dashing() const noexcept { return m_dashTimer > 0.0f; }
    [[nodiscard]] float dashCooldownRemaining() const noexcept { return m_dashCooldown; }
    [[nodiscard]] AttackPhase attackPhase() const noexcept { return m_attack.phase; }
    [[nodiscard]] bool attacking() const noexcept { return m_attack.phase == AttackPhase::Active; }

    /// Era being shifted away from, and how far the transition has progressed.
    /// 1 means fully settled.
    [[nodiscard]] float eraBlend() const noexcept { return m_eraBlend; }

    /// Hit box of the current swing, or an empty rect when not attacking.
    [[nodiscard]] Rect attackBox() const noexcept;

    [[nodiscard]] const PlayerTuning& tuning() const noexcept { return m_tuning; }
    void setTuning(const PlayerTuning& tuning) { m_tuning = tuning; }

private:
    void updateChrono(float dt);
    void updateAttack(const PlayerInput& input, float dt);
    /// Direction the swing faces, as -1 or +1.
    [[nodiscard]] float attackDirection() const noexcept;

    struct AttackState {
        AttackPhase phase = AttackPhase::Idle;
        float timer       = 0.0f;
    };

    PlayerTuning m_tuning;
    Body         m_body;
    Era          m_era = Era::Present;
    float        m_health = 0.0f;
    float        m_chrono = 0.0f;
    float        m_facing = 1.0f;

    float m_coyoteTimer   = 0.0f;
    float m_jumpBuffer    = 0.0f;
    float m_dashTimer     = 0.0f;
    float m_dashCooldown  = 0.0f;
    float m_invulnTimer   = 0.0f;
    float m_hurtTimer     = 0.0f;
    float m_eraBlend      = 1.0f;

    AttackState m_attack;
    PlayerStepEvents m_events;
    /// Accumulated ground distance since the last footstep, in pixels.
    float m_strideDistance = 0.0f;
};

} // namespace EraShift::Game
