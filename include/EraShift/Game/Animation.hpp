// Era Shift - procedural animation.
//
// There are no spritesheets. Everything on screen is a rectangle, and a
// rectangle animated well needs very few numbers: a squash, a lean, a bob, and
// how far the limbs have swung. So "animation" here is a clip of key poses that
// a controller samples and blends, and the renderer applies the result to the
// same rectangles it was already drawing.
//
// The important part is that clips can carry *markers* at times along their
// timeline, and those markers are gameplay events. The attack's hitbox opens
// because the attack clip says so, at 0.08s in, not because a timer in a
// physics step happens to line up. That keeps the thing you see and the thing
// that hurts an enemy in agreement even if the frame rate moves.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace EraShift::Game {

class Enemy;
class Player;

/// Every animation in the game. An enum rather than a string so a typo is a
/// compile error, and so the clip table can be a plain array.
enum class AnimId : std::uint8_t {
    // Player.
    Idle,
    Walk,
    Run,
    JumpStart,
    Jump,
    Fall,
    Land,
    Dash,
    Attack,
    Hurt,
    Death,
    ShiftCharge,
    // Enemy.
    EIdle,
    EPatrol,
    EChase,
    EWindup,
    EAttack,
    ERecover,
    EHurt,
    EDie,
    Count
};

/// Something that happens *because* a frame of an animation was reached.
enum class AnimEvent : std::uint8_t {
    Footstep,
    /// Open the attack hitbox. Only this marker makes the player dangerous.
    AttackHit,
    /// Close it again, so a held button cannot chain damage forever.
    AttackEnd,
    JumpPush,
    LandImpact,
    DashTrail,
    DeathEnd,
};

[[nodiscard]] std::string_view toString(AnimId id) noexcept;
[[nodiscard]] std::string_view toString(AnimEvent event) noexcept;

/// A rig pose. Every field has a neutral default, so a clip only states what it
/// changes and a missing key means "unchanged", not "broken".
struct Pose {
    float scaleX = 1.0f;      ///< <1 squashes, >1 stretches.
    float scaleY = 1.0f;
    float offsetX = 0.0f;     ///< Bob, in pixels, in facing space.
    float offsetY = 0.0f;     ///< <0 is up.
    float lean = 0.0f;        ///< Degrees, positive tips forward.
    float limbSwing = 0.0f;   ///< -1..1, drives the legs and the trailing arm.
    float limbSpread = 0.0f;  ///< -1..1, narrows or widens the silhouette.
    float armExtension = 0.0f;///< 0..1, how far the leading arm is out.
    float alpha = 1.0f;
};

[[nodiscard]] Pose lerpPose(const Pose& a, const Pose& b, float t) noexcept;

/// One key on a timeline.
struct AnimationKey {
    float time = 0.0f;
    Pose  pose{};
};

/// A gameplay event pinned to a time on a timeline.
struct AnimationMarker {
    float    time = 0.0f;
    AnimEvent event = AnimEvent::Footstep;
};

struct AnimationClip {
    AnimId    id = AnimId::Idle;
    float     duration = 0.3f;
    bool      loop = false;
    std::vector<AnimationKey> keys;
    std::vector<AnimationMarker> markers;
};

/// The clip table. Built once; clips are constant for the life of the process.
[[nodiscard]] const AnimationClip& clipFor(AnimId id) noexcept;
[[nodiscard]] float defaultBlendTime(AnimId id) noexcept;

// ---------------------------------------------------------------------------

/// Which clip the player should be playing, from the simulation's state.
///
/// The switch is on simulation state and never on input, which is the whole point:
/// the animation cannot get ahead of the physics or fall behind it, because it has
/// no clock of its own to get ahead with.
///
/// `shiftHeld` is whether the era-shift key is *down*, not whether the player can
/// afford a shift. That distinction is the entire reason this is a parameter
/// rather than something read off the player: a player holding the shift key with
/// no chrono to spend is not charging anything, and a player with a full chrono
/// bar who is not touching the key is not charging either. Deriving it from
/// anything else puts the semi-transparent charging pose on screen permanently.
///
/// It lives here, next to the clip table, rather than in the state that draws the
/// player because it is pure logic over the simulation and the gameplay suite is
/// the thing that can assert on it. A selection function no test can reach is a
/// selection function that gets the argument wrong.
[[nodiscard]] AnimId playerClipFor(const Player& player, bool shiftHeld) noexcept;

/// The same for an enemy, from its AI state.
///
/// Being struck outranks the AI state, for the same reason it does on the player:
/// the reaction is the only part of the hit the enemy cannot fail to communicate.
[[nodiscard]] AnimId enemyClipFor(const Enemy& enemy) noexcept;

// ---------------------------------------------------------------------------

/// Samples clips, blends between them, and reports the markers it crossed.
///
/// Deterministic and allocation-free after the first `update`, which is why it
/// can be unit tested rather than only felt.
class AnimationController {
public:
    /// Starts (or restarts) a clip. Returns false for an unknown id, and for a
    /// non-looping clip that is already playing unless `force` is set.
    bool play(AnimId id, bool force = false) noexcept;

    /// `playOrKeep` is `play` that will not cut a short non-looping action short.
    /// The player uses it so mashing attack does not visually restart the swing
    /// on every press, even though the input is buffered.
    bool playOrKeep(AnimId id) noexcept;

    void update(float dt) noexcept;

    [[nodiscard]] const Pose& pose() const noexcept { return m_pose; }
    [[nodiscard]] AnimId current() const noexcept { return m_current; }
    [[nodiscard]] bool isPlaying(AnimId id) const noexcept { return m_current == id; }
    /// 0..1 through the current clip; 1 for a finished non-looping clip.
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] bool finished() const noexcept { return m_finished; }
    /// The pose as it was before the most recent transition started.
    [[nodiscard]] const Pose& previousPose() const noexcept { return m_fromPose; }

    /// Markers crossed since the last call, in order. Drained on read.
    [[nodiscard]] std::vector<AnimEvent> takeEvents() noexcept;

    void reset() noexcept;

    /// Jumps the clip to `time` without firing any markers, and finishes any
    /// blend immediately.
    ///
    /// For clips that cover a simulation action, where the controller's own clock
    /// must not be the thing deciding when it happens: the simulation says how
    /// far through the swing it is, and the picture is told. Without this the two
    /// would be running independent timers over the same 0.32 seconds, and a
    /// dropped frame would show a swing that has already recovered.
    void setTime(float time) noexcept;

    void setSpeed(float speed) noexcept { m_speed = speed; }
    [[nodiscard]] float speed() const noexcept { return m_speed; }

private:
    void sampleInto(Pose& out, const AnimationClip& clip, float time) const noexcept;

    AnimId m_current = AnimId::Idle;
    bool   m_finished = false;
    float  m_time = 0.0f;
    float  m_speed = 1.0f;
    float  m_blend = 1.0f;         ///< 0 = fully previous, 1 = fully current.
    float  m_blendDuration = 0.1f;
    Pose   m_fromPose{};
    Pose   m_pose{};
    std::vector<AnimEvent> m_pending;
    bool   m_started = false;
};

} // namespace EraShift::Game
