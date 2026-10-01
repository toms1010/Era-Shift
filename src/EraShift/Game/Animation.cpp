#include "EraShift/Game/Animation.hpp"

#include "EraShift/Game/Enemy.hpp"
#include "EraShift/Game/Player.hpp"
#include "EraShift/Graphics/Math.hpp"

#include <algorithm>
#include <array>

namespace EraShift::Game {

namespace {

/// Smoothstep, so a pose interpolates with no velocity discontinuity at the
/// key. Linear blending between keys looks like a machine; this looks like mass.
float smooth(float t) noexcept
{
    t = Graphics::clampValue(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float mix(float a, float b, float t) noexcept { return a + (b - a) * t; }

/// A key, with the pose written out in full.
///
/// Written field by field rather than through a positional helper on purpose.
/// Nine floats in a row is not reviewable, and a value that silently lands in
/// `spread` instead of `armExtension` produces an animation that compiles, runs,
/// and is wrong - which is the worst failure mode available to a table like this.
/// The field names are the documentation.
AnimationKey key(float time, Pose pose) noexcept
{
    return AnimationKey{time, pose};
}

/// A pose with everything neutral except the fields a clip cares about.
struct Rig {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float lean = 0.0f;
    float limbSwing = 0.0f;
    float limbSpread = 0.0f;
    float armExtension = 0.0f;
    float alpha = 1.0f;

    /// Squash, stretch and lean: the three that read at a glance.
    [[nodiscard]] Pose pose() const noexcept
    {
        return Pose{scaleX, scaleY, offsetX, offsetY, lean, limbSwing, limbSpread,
                    armExtension, alpha};
    }
};

using ClipTable = std::array<AnimationClip, static_cast<std::size_t>(AnimId::Count)>;

ClipTable buildTable()
{
    ClipTable t{};

    // --- player -------------------------------------------------------------
    // Written with designated initialisers throughout. A `Rig` has nine fields,
    // and a positional literal of nine floats is a value waiting to land in the
    // wrong one - which compiles, runs, and is silently wrong. Naming the fields
    // makes that impossible and doubles as the documentation for what each part
    // of the animation is for.

    {
        // A slow breath, with the two halves deliberately not identical: a
        // perfectly symmetric idle cycle reads as a machine.
        auto& c = t[static_cast<std::size_t>(AnimId::Idle)];
        c = {AnimId::Idle, 1.6f, true,
             {key(0.0f, (Rig{.scaleY = 1.00f}.pose())),
              key(0.4f, (Rig{.scaleY = 1.02f, .offsetY = -1.5f, .limbSpread = 0.4f}.pose())),
              key(0.8f, (Rig{.scaleY = 0.99f}.pose())),
              key(1.2f, (Rig{.scaleY = 1.02f, .offsetY = -1.5f, .limbSpread = -0.4f}.pose())),
              key(1.6f, (Rig{.scaleY = 1.00f}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Walk)];
        c = {AnimId::Walk, 0.6f, true,
             {key(0.00f, (Rig{.scaleX = 0.98f, .scaleY = 1.02f, .lean = 2.0f, .armExtension = 0.2f}.pose())),
              key(0.15f, (Rig{.scaleY = 0.98f, .offsetY = -2.0f, .lean = 3.0f, .limbSwing = 0.6f, .armExtension = 0.2f}.pose())),
              key(0.30f, (Rig{.scaleX = 0.98f, .scaleY = 1.02f, .lean = 0.0f, .armExtension = 0.0f}.pose())),
              key(0.45f, (Rig{.scaleY = 0.98f, .offsetY = -2.0f, .lean = 3.0f, .limbSwing = -0.6f, .armExtension = 0.2f}.pose())),
              key(0.60f, (Rig{.scaleX = 0.98f, .scaleY = 1.02f, .lean = 2.0f, .armExtension = 0.2f}.pose()))},
             {{0.15f, AnimEvent::Footstep}, {0.45f, AnimEvent::Footstep}}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Run)];
        c = {AnimId::Run, 0.42f, true,
             {key(0.000f, (Rig{.scaleX = 0.96f, .scaleY = 1.04f, .offsetY = -1.0f, .lean = 6.0f, .armExtension = 0.3f}.pose())),
              key(0.105f, (Rig{.scaleX = 1.04f, .scaleY = 0.96f, .offsetY = 1.0f, .lean = 8.0f, .limbSwing = 0.8f, .armExtension = 0.3f}.pose())),
              key(0.210f, (Rig{.scaleX = 0.96f, .scaleY = 1.04f, .offsetY = -1.0f, .lean = 2.0f, .armExtension = 0.3f}.pose())),
              key(0.315f, (Rig{.scaleX = 1.04f, .scaleY = 0.96f, .offsetY = 1.0f, .lean = 8.0f, .limbSwing = -0.8f, .armExtension = 0.3f}.pose())),
              key(0.420f, (Rig{.scaleX = 0.96f, .scaleY = 1.04f, .offsetY = -1.0f, .lean = 6.0f, .armExtension = 0.3f}.pose()))},
             {{0.105f, AnimEvent::Footstep}, {0.315f, AnimEvent::Footstep}}};
    }
    {
        // Squash then stretch: the shape every 2D platformer uses for a jump,
        // and the reason a jump reads as an impulse rather than as a translation.
        auto& c = t[static_cast<std::size_t>(AnimId::JumpStart)];
        c = {AnimId::JumpStart, 0.12f, false,
             {key(0.00f, (Rig{.scaleY = 0.80f}.pose())),
              key(0.12f, (Rig{.scaleX = 0.92f, .scaleY = 1.18f, .lean = 4.0f}.pose()))},
             {{0.0f, AnimEvent::JumpPush}}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Jump)];
        c = {AnimId::Jump, 0.5f, true,
             {key(0.0f, (Rig{.scaleX = 0.90f, .scaleY = 1.14f, .lean = 5.0f, .armExtension = 0.2f}.pose())),
              key(0.5f, (Rig{.scaleX = 0.86f, .scaleY = 1.18f, .lean = 6.0f, .limbSpread = 0.4f, .armExtension = 0.2f}.pose()))},
             {}};
    }
    {
        // The lean flips negative in the air: falling reads as loss of control,
        // rising as effort. It is a small number doing a lot of work.
        auto& c = t[static_cast<std::size_t>(AnimId::Fall)];
        c = {AnimId::Fall, 0.5f, true,
             {key(0.0f, (Rig{.scaleX = 0.92f, .scaleY = 1.10f, .lean = -4.0f, .limbSwing = -0.3f, .armExtension = 0.4f}.pose())),
              key(0.5f, (Rig{.scaleX = 0.90f, .scaleY = 1.12f, .lean = -6.0f, .limbSwing = 0.3f, .armExtension = 0.4f}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Land)];
        c = {AnimId::Land, 0.16f, false,
             {key(0.00f, (Rig{.scaleX = 1.22f, .scaleY = 0.72f, .limbSpread = 0.6f}.pose())),
              key(0.16f, (Rig{}.pose()))},
             {{0.0f, AnimEvent::LandImpact}}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Dash)];
        c = {AnimId::Dash, 0.28f, false,
             {key(0.00f, (Rig{.scaleX = 1.30f, .scaleY = 0.80f, .lean = 16.0f, .armExtension = 0.5f}.pose())),
              key(0.14f, (Rig{.scaleX = 1.34f, .scaleY = 0.74f, .lean = 18.0f, .limbSpread = -0.4f, .armExtension = 0.6f}.pose())),
              key(0.28f, (Rig{.lean = 4.0f}.pose()))},
             {{0.0f, AnimEvent::DashTrail}, {0.10f, AnimEvent::DashTrail}}};
    }
    {
        // The swing. Its key times are not chosen - they are read off
        // PlayerTuning: the windup ends at 0.06s, the active window closes at
        // 0.16s, and recovery ends the clip at 0.32s. The two markers sit on
        // those boundaries for the same reason. This is the one clip that must
        // agree with the simulation, and the test "the attack clip's hit window
        // matches the player's swing" is what stops the two drifting apart.
        auto& c = t[static_cast<std::size_t>(AnimId::Attack)];
        c = {AnimId::Attack, 0.32f, false,
             {key(0.00f, (Rig{.scaleX = 0.88f, .scaleY = 1.06f, .lean = -10.0f, .limbSpread = 0.2f}.pose())),
              key(0.06f, (Rig{.scaleX = 0.86f, .scaleY = 1.08f, .offsetX = -1.0f, .lean = -16.0f, .limbSpread = 0.3f, .armExtension = 0.15f}.pose())),
              key(0.16f, (Rig{.scaleX = 1.06f, .scaleY = 0.94f, .offsetX = 2.0f, .lean = 12.0f, .limbSwing = -0.2f, .armExtension = 1.0f}.pose())),
              key(0.24f, (Rig{.scaleX = 1.02f, .scaleY = 0.98f, .offsetX = 1.0f, .lean = 8.0f, .armExtension = 0.6f}.pose())),
              key(0.32f, (Rig{.lean = 2.0f}.pose()))},
             {{0.06f, AnimEvent::AttackHit}, {0.16f, AnimEvent::AttackEnd}}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::Hurt)];
        c = {AnimId::Hurt, 0.22f, false,
             {key(0.00f, (Rig{.scaleX = 1.14f, .scaleY = 0.86f, .offsetX = -2.0f, .lean = -14.0f, .limbSwing = -0.4f}.pose())),
              key(0.10f, (Rig{.scaleX = 1.10f, .scaleY = 0.90f, .offsetX = -1.0f, .lean = -10.0f, .limbSwing = -0.2f}.pose())),
              key(0.22f, (Rig{}.pose()))},
             {}};
    }
    {
        // Ends fully transparent: a body that fades rather than pops. `alpha` is
        // named explicitly because the death is the one animation where the
        // default of 1.0 is wrong, and a silent default is how a fade ends up not
        // existing at all.
        auto& c = t[static_cast<std::size_t>(AnimId::Death)];
        c = {AnimId::Death, 1.0f, false,
             {key(0.00f, (Rig{}.pose())),
              key(0.25f, (Rig{.scaleX = 1.10f, .scaleY = 0.90f, .offsetX = -1.0f, .lean = -8.0f, .limbSwing = -0.3f}.pose())),
              key(1.00f, (Rig{.scaleX = 1.30f, .scaleY = 0.55f, .offsetY = 6.0f, .lean = 22.0f, .limbSwing = 0.5f, .alpha = 0.0f}.pose()))},
             {{0.9f, AnimEvent::DeathEnd}}};
    }
    {
        // Held while the shift charges. The VFX layer reads this pose to place the
        // gathering aura, so a charge that is not visible on the character is a
        // charge the player cannot aim.
        auto& c = t[static_cast<std::size_t>(AnimId::ShiftCharge)];
        c = {AnimId::ShiftCharge, 0.4f, true,
             {key(0.0f, (Rig{.scaleX = 0.94f, .scaleY = 1.06f, .lean = -4.0f, .alpha = 0.9f}.pose())),
              key(0.2f, (Rig{.scaleX = 0.90f, .scaleY = 1.10f, .offsetY = -2.0f, .lean = -6.0f, .limbSpread = 0.5f, .alpha = 0.7f}.pose())),
              key(0.4f, (Rig{.scaleX = 0.94f, .scaleY = 1.06f, .lean = -4.0f, .alpha = 0.9f}.pose()))},
             {}};
    }

    // --- enemy --------------------------------------------------------------
    // The three attack-state clips are timed off EnemyTuning's windupTime,
    // attackTime and recoverTime, for the same reason the player's swing is: the
    // telegraph and the moment of danger have to be the same moment.
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EIdle)];
        c = {AnimId::EIdle, 1.8f, true,
             {key(0.0f, (Rig{}.pose())),
              key(0.9f, (Rig{.scaleY = 0.96f, .offsetY = 1.0f, .limbSpread = 0.2f}.pose())),
              key(1.8f, (Rig{}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EPatrol)];
        c = {AnimId::EPatrol, 0.9f, true,
             {key(0.00f, (Rig{.scaleX = 0.98f, .scaleY = 1.02f}.pose())),
              key(0.45f, (Rig{.scaleY = 0.98f, .offsetY = -1.0f, .lean = 3.0f, .limbSwing = 0.3f}.pose())),
              key(0.90f, (Rig{.scaleX = 0.98f, .scaleY = 1.02f}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EChase)];
        c = {AnimId::EChase, 0.5f, true,
             {key(0.00f, (Rig{.scaleX = 0.94f, .scaleY = 1.06f, .lean = 5.0f, .armExtension = 0.2f}.pose())),
              key(0.25f, (Rig{.scaleX = 1.04f, .scaleY = 0.96f, .offsetY = 2.0f, .lean = 7.0f, .limbSpread = 0.5f, .armExtension = 0.2f}.pose())),
              key(0.50f, (Rig{.scaleX = 0.94f, .scaleY = 1.06f, .lean = 5.0f, .armExtension = 0.2f}.pose()))},
             {{0.25f, AnimEvent::Footstep}}};
    }
    {
        // Longer than the player's swing, and it leans *back* before coming
        // forward. The lean is the tell: a player who misses the telegraph ring
        // still sees this, which is the fair-play contract the whole combat loop
        // rests on.
        auto& c = t[static_cast<std::size_t>(AnimId::EWindup)];
        c = {AnimId::EWindup, 0.35f, false,
             {key(0.00f, (Rig{}.pose())),
              key(0.25f, (Rig{.scaleX = 0.88f, .scaleY = 1.14f, .offsetX = -2.0f, .offsetY = -1.0f, .lean = -18.0f, .limbSpread = 0.4f}.pose())),
              key(0.35f, (Rig{.scaleX = 0.86f, .scaleY = 1.16f, .offsetX = -3.0f, .offsetY = -1.0f, .lean = -20.0f, .limbSpread = 0.5f, .armExtension = 0.1f}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EAttack)];
        c = {AnimId::EAttack, 0.16f, false,
             {key(0.00f, (Rig{.scaleX = 1.14f, .scaleY = 0.88f, .offsetX = 2.0f, .lean = 14.0f, .limbSwing = -0.3f, .armExtension = 0.9f}.pose())),
              key(0.10f, (Rig{.scaleX = 1.10f, .scaleY = 0.92f, .offsetX = 1.0f, .lean = 10.0f, .armExtension = 1.0f}.pose())),
              key(0.16f, (Rig{}.pose()))},
             {{0.0f, AnimEvent::AttackHit}, {0.16f, AnimEvent::AttackEnd}}};
    }
    {
        // Dims while it recovers: the tail of an enemy's attack is the window
        // the player is meant to punish, and it has to look punishable.
        auto& c = t[static_cast<std::size_t>(AnimId::ERecover)];
        c = {AnimId::ERecover, 0.40f, false,
             {key(0.00f, (Rig{.scaleX = 0.94f, .scaleY = 1.06f, .lean = -6.0f, .limbSpread = 0.2f, .alpha = 0.6f}.pose())),
              key(0.40f, (Rig{}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EHurt)];
        c = {AnimId::EHurt, 0.20f, false,
             {key(0.00f, (Rig{.scaleX = 0.86f, .scaleY = 1.16f, .offsetX = -2.0f, .lean = -16.0f, .limbSwing = -0.5f}.pose())),
              key(0.20f, (Rig{}.pose()))},
             {}};
    }
    {
        auto& c = t[static_cast<std::size_t>(AnimId::EDie)];
        c = {AnimId::EDie, 0.7f, false,
             {key(0.00f, (Rig{}.pose())),
              key(0.15f, (Rig{.scaleX = 1.16f, .scaleY = 0.84f, .offsetX = -1.0f, .lean = -12.0f, .limbSwing = -0.4f, .alpha = 0.9f}.pose())),
              key(0.70f, (Rig{.scaleX = 0.70f, .scaleY = 0.40f, .offsetY = 8.0f, .limbSpread = 0.9f, .alpha = 0.0f}.pose()))},
             {{0.65f, AnimEvent::DeathEnd}}};
    }
    return t;
}

const ClipTable& table()
{
    static const ClipTable instance = buildTable();
    return instance;
}

} // namespace

std::string_view toString(AnimId id) noexcept
{
    switch (id) {
        case AnimId::Idle:         return "Idle";
        case AnimId::Walk:         return "Walk";
        case AnimId::Run:          return "Run";
        case AnimId::JumpStart:    return "JumpStart";
        case AnimId::Jump:         return "Jump";
        case AnimId::Fall:         return "Fall";
        case AnimId::Land:         return "Land";
        case AnimId::Dash:         return "Dash";
        case AnimId::Attack:       return "Attack";
        case AnimId::Hurt:         return "Hurt";
        case AnimId::Death:        return "Death";
        case AnimId::ShiftCharge:  return "ShiftCharge";
        case AnimId::EIdle:        return "EIdle";
        case AnimId::EPatrol:      return "EPatrol";
        case AnimId::EChase:       return "EChase";
        case AnimId::EWindup:      return "EWindup";
        case AnimId::EAttack:      return "EAttack";
        case AnimId::ERecover:     return "ERecover";
        case AnimId::EHurt:        return "EHurt";
        case AnimId::EDie:         return "EDie";
        case AnimId::Count:        break;
    }
    return "?";
}

std::string_view toString(AnimEvent event) noexcept
{
    switch (event) {
        case AnimEvent::Footstep:    return "Footstep";
        case AnimEvent::AttackHit:   return "AttackHit";
        case AnimEvent::AttackEnd:   return "AttackEnd";
        case AnimEvent::JumpPush:    return "JumpPush";
        case AnimEvent::LandImpact:  return "LandImpact";
        case AnimEvent::DashTrail:   return "DashTrail";
        case AnimEvent::DeathEnd:    return "DeathEnd";
    }
    return "?";
}

Pose lerpPose(const Pose& a, const Pose& b, float t) noexcept
{
    const float k = Graphics::clampValue(t, 0.0f, 1.0f);
    Pose out;
    out.scaleX       = mix(a.scaleX, b.scaleX, k);
    out.scaleY       = mix(a.scaleY, b.scaleY, k);
    out.offsetX      = mix(a.offsetX, b.offsetX, k);
    out.offsetY      = mix(a.offsetY, b.offsetY, k);
    out.lean         = mix(a.lean, b.lean, k);
    out.limbSwing    = mix(a.limbSwing, b.limbSwing, k);
    out.limbSpread   = mix(a.limbSpread, b.limbSpread, k);
    out.armExtension = mix(a.armExtension, b.armExtension, k);
    out.alpha        = mix(a.alpha, b.alpha, k);
    return out;
}

const AnimationClip& clipFor(AnimId id) noexcept
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= table().size()) {
        return table()[0];
    }
    return table()[index];
}

float defaultBlendTime(AnimId id) noexcept
{
    // Attacks blend in fast so a swing reads as instant, death blends slowly so
    // the transition itself is visible.
    switch (id) {
        case AnimId::Attack:
        case AnimId::Hurt:
        case AnimId::JumpStart:
        case AnimId::Land:
        case AnimId::EAttack:
        case AnimId::EWindup:  return 0.04f;
        case AnimId::Death:
        case AnimId::EDie:     return 0.18f;
        default:               return 0.10f;
    }
}

AnimId playerClipFor(const Player& player, bool shiftHeld) noexcept
{
    if (!player.alive()) {
        return AnimId::Death;
    }
    // Being hit outranks everything short of dying. It is the one reaction the
    // player cannot afford to miss, so it is checked before the swing; the state
    // that owns the controller then forces the play, because `AnimationController`
    // refuses to cut a short action short and a swing in progress would otherwise
    // swallow the flinch.
    if (player.hurt()) {
        return AnimId::Hurt;
    }
    // A swing is one clip covering all three simulation phases, so it is selected
    // here and placed from `attackProgress()` by the caller.
    if (player.attacking() || player.attackPhase() != AttackPhase::Idle) {
        return AnimId::Attack;
    }
    if (player.dashing()) {
        return AnimId::Dash;
    }
    // Charging a shift. Needs both the key down and something to spend: the
    // charging pose is translucent, so putting it up for a player who is merely
    // walking around with a full chrono bar makes the player permanently
    // see-through and stops the walk cycle from ever being seen.
    if (shiftHeld && player.chrono() > 0.0f) {
        return AnimId::ShiftCharge;
    }
    if (!player.body().onGround) {
        if (player.body().velocity.y < -40.0f) {
            return AnimId::Jump;
        }
        if (player.body().velocity.y > 220.0f) {
            return AnimId::Fall;
        }
        return AnimId::JumpStart;
    }
    const float speed = std::fabs(player.body().velocity.x);
    if (speed < 12.0f) {
        return AnimId::Idle;
    }
    return speed > 190.0f ? AnimId::Run : AnimId::Walk;
}

AnimId enemyClipFor(const Enemy& enemy) noexcept
{
    // Dying outranks everything, including the hurt flash it may still be carrying:
    // a corpse that flinches is not a corpse.
    if (enemy.state() == EnemyState::Dying) {
        return AnimId::EDie;
    }
    // Being struck outranks the AI state, so a hit interrupts a windup rather than
    // queueing behind it. This check used to sit *after* the switch below, where
    // it was unreachable: the switch is exhaustive over the enum, so every path
    // had already returned and `AnimId::EHurt` - a clip with three keys, a
    // dedicated blend time and its own test - was never selected for anything. An
    // enemy that is visibly struck and visibly unchanged reads as a miss.
    if (enemy.hurt()) {
        return AnimId::EHurt;
    }
    switch (enemy.state()) {
        case EnemyState::Asleep:  return AnimId::EIdle;
        case EnemyState::Idle:    return AnimId::EIdle;
        case EnemyState::Patrol:  return AnimId::EPatrol;
        case EnemyState::Chase:   return AnimId::EChase;
        case EnemyState::Windup:  return AnimId::EWindup;
        case EnemyState::Attack:  return AnimId::EAttack;
        case EnemyState::Recover: return AnimId::ERecover;
        case EnemyState::Dying:   return AnimId::EDie;
    }
    // Unreachable while the enum and this switch agree, which is exactly the
    // property worth stating at the end of a function whose body used to be
    // unreachable code.
    return AnimId::EIdle;
}

void AnimationController::reset() noexcept
{
    m_current = AnimId::Idle;
    m_finished = false;
    m_time = 0.0f;
    m_blend = 1.0f;
    m_blendDuration = defaultBlendTime(AnimId::Idle);
    m_fromPose = Pose{};
    m_pending.clear();
    m_started = false;
    m_speed = 1.0f;
    sampleInto(m_pose, clipFor(AnimId::Idle), 0.0f);
}

bool AnimationController::play(AnimId id, bool force) noexcept
{
    if (static_cast<std::size_t>(id) >= static_cast<std::size_t>(AnimId::Count)) {
        return false;
    }
    const AnimationClip& clip = clipFor(id);
    if (!force && m_started && m_current == id) {
        return false;
    }
    if (!force && m_started && m_current != id && !clipFor(m_current).loop && !m_finished &&
        m_time < clipFor(m_current).duration) {
        // Still inside a non-looping action: refuse to cut it short. This is
        // what stops mashing attack from visually restarting the swing while the
        // input buffer still lets a second one queue.
        return false;
    }

    m_fromPose = m_pose;
    m_current = id;
    m_time = 0.0f;
    m_finished = false;
    m_blend = 0.0f;
    m_blendDuration = defaultBlendTime(id);
    m_started = true;
    sampleInto(m_pose, clip, 0.0f);
    return true;
}

bool AnimationController::playOrKeep(AnimId id) noexcept
{
    return play(id, false);
}

void AnimationController::update(float dt) noexcept
{
    if (!m_started) {
        reset();
        m_started = true;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }

    const AnimationClip& clip = clipFor(m_current);
    const float previous = m_time;
    m_time += dt * m_speed;

    if (m_time >= clip.duration) {
        if (clip.loop) {
            // Markers on a loop fire on the wrap as well, otherwise a footstep
            // on the last eighth would be silently dropped.
            const float wrapped = m_time - std::floor(m_time / clip.duration) * clip.duration;
            for (const AnimationMarker& marker : clip.markers) {
                if (marker.time >= previous || marker.time < wrapped) {
                    m_pending.push_back(marker.event);
                }
            }
            m_time = wrapped;
        } else {
            m_time = clip.duration;
            m_finished = true;
            // A finished non-looping clip still fires any marker at or before its
            // end, so a hit frame on a short clip is not lost to rounding.
            for (const AnimationMarker& marker : clip.markers) {
                if (marker.time >= previous && marker.time <= clip.duration) {
                    m_pending.push_back(marker.event);
                }
            }
        }
    } else {
        for (const AnimationMarker& marker : clip.markers) {
            if (marker.time > previous && marker.time <= m_time) {
                m_pending.push_back(marker.event);
            }
        }
    }

    if (m_blendDuration > 0.0f) {
        m_blend = std::min(1.0f, m_blend + dt / m_blendDuration);
    } else {
        m_blend = 1.0f;
    }

    Pose current;
    sampleInto(current, clip, m_time);
    m_pose = m_blend >= 1.0f ? current : lerpPose(m_fromPose, current, smooth(m_blend));
}

void AnimationController::setTime(float time) noexcept
{
    const AnimationClip& clip = clipFor(m_current);
    if (clip.duration <= 0.0f) {
        m_time = 0.0f;
        return;
    }
    m_time = clip.loop ? std::fmod(std::max(0.0f, time), clip.duration)
                       : Graphics::clampValue(time, 0.0f, clip.duration);
    m_finished = !clip.loop && m_time >= clip.duration;
    // No blend: a simulation-synced clip is being placed, not cross-faded, and a
    // blend here would only ever drag it away from the value just set.
    m_blend = 1.0f;
    m_pending.clear();
    sampleInto(m_pose, clip, m_time);
}

void AnimationController::sampleInto(Pose& out, const AnimationClip& clip, float time) const noexcept
{
    if (clip.keys.empty()) {
        out = Pose{};
        return;
    }
    if (clip.keys.size() == 1) {
        out = clip.keys.front().pose;
        return;
    }

    // Clips are written in order, so a linear scan over a handful of keys is
    // cheaper and clearer than binary search.
    const AnimationKey* upper = nullptr;
    for (std::size_t i = 1; i < clip.keys.size(); ++i) {
        if (clip.keys[i].time >= time) {
            upper = &clip.keys[i];
            break;
        }
    }
    if (upper == nullptr) {
        // Past the last key: hold the final pose rather than snapping to neutral.
        out = clip.keys.back().pose;
        return;
    }

    const AnimationKey& a = *(upper - 1);
    const float span = upper->time - a.time;
    const float t = span > 0.0f ? (time - a.time) / span : 1.0f;
    out = lerpPose(a.pose, upper->pose, smooth(t));
}

float AnimationController::progress() const noexcept
{
    const AnimationClip& clip = clipFor(m_current);
    if (clip.duration <= 0.0f) {
        return 1.0f;
    }
    return Graphics::clampValue(m_time / clip.duration, 0.0f, 1.0f);
}

std::vector<AnimEvent> AnimationController::takeEvents() noexcept
{
    std::vector<AnimEvent> out;
    out.swap(m_pending);
    return out;
}

} // namespace EraShift::Game
