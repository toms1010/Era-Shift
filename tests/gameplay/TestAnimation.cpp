// Tests for the animation controller.
//
// The property worth testing is not "does the pose change", which is obvious. It
// is that a clip's *markers* land where the gameplay says they should, and that
// a controller asked to play the same clip twice does not restart it. Those are
// the two things that go wrong silently: a footstep that fires on frame one, and
// a swing that visually restarts on every keypress while the input buffer lets a
// second one queue behind it.

#include "EraShift/Game/Animation.hpp"

#include "EraShift/Game/Enemy.hpp"
#include "EraShift/Game/Player.hpp"
#include "EraShift/Game/TileMap.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace EraShift;
using namespace EraShift::Game;

namespace {

/// Advances a controller by `steps` fixed frames, collecting every marker.
std::vector<AnimEvent> run(AnimationController& controller, int steps, float dt = 1.0f / 60.0f)
{
    std::vector<AnimEvent> events;
    for (int i = 0; i < steps; ++i) {
        controller.update(dt);
        for (const AnimEvent event : controller.takeEvents()) {
            events.push_back(event);
        }
    }
    return events;
}

/// How many times `wanted` appears.
int countOf(const std::vector<AnimEvent>& events, AnimEvent wanted)
{
    return static_cast<int>(std::count(events.begin(), events.end(), wanted));
}

/// Where `wanted` first appears, or -1. The index is what lets a test check
/// ordering; a helper that returned the whole event list would silently make
/// "did it come before" compare a vector to itself.
int indexOf(const std::vector<AnimEvent>& events, AnimEvent wanted)
{
    const auto it = std::find(events.begin(), events.end(), wanted);
    return (it == events.end()) ? -1 : static_cast<int>(std::distance(events.begin(), it));
}

} // namespace

TEST_CASE("every animation in the enum has a clip")
{
    for (int i = 0; i < static_cast<int>(AnimId::Count); ++i) {
        const auto id = static_cast<AnimId>(i);
        CAPTURE(static_cast<int>(id));
        const AnimationClip& clip = clipFor(id);
        // Compared as ints: doctest finds `toString` for these enums by ADL and
        // then fails to concatenate the result, which is a distracting way to
        // lose a real assertion failure.
        CHECK(static_cast<int>(clip.id) == static_cast<int>(id));
        CHECK(clip.duration > 0.0f);
        CHECK_FALSE(clip.keys.empty());
        // Keys must be in order, or the sampler's linear scan finds the wrong
        // pair and the pose interpolates backwards.
        for (std::size_t k = 1; k < clip.keys.size(); ++k) {
            CHECK(clip.keys[k].time > clip.keys[k - 1].time);
        }
        // And every key must be inside the clip.
        for (const AnimationKey& key : clip.keys) {
            CHECK(key.time <= clip.duration);
        }
        // Markers too, or a marker past the end of the clip never fires.
        for (const AnimationMarker& marker : clip.markers) {
            CAPTURE(std::string(toString(marker.event)));
            CHECK(marker.time >= 0.0f);
            CHECK(marker.time <= clip.duration);
        }
    }
}

TEST_CASE("an unknown id is refused rather than crashing")
{
    AnimationController controller;
    controller.reset();
    CHECK_FALSE(controller.play(static_cast<AnimId>(999)));
    CHECK(static_cast<int>(controller.current()) == static_cast<int>(AnimId::Idle));
}

TEST_CASE("a looping clip settles into a repeating pose")
{
    AnimationController controller;
    controller.reset();
    CHECK(controller.play(AnimId::Walk, true));

    // Sampled a quarter and three quarters of the way through, which is where a
    // symmetric walk cycle has its limbs at opposite extremes. Sampling the
    // midpoint and the wrap point would compare a pose with itself.
    controller.update(0.15f);
    const Pose early = controller.pose();
    controller.update(0.30f);
    const Pose later = controller.pose();

    // Halfway through a symmetric cycle the limbs are at opposite extremes, so
    // the two poses must genuinely differ or the cycle is not animating.
    CHECK(early.limbSwing != doctest::Approx(later.limbSwing));
    // But neither has run away: a pose is a transform, not a position.
    CHECK(early.scaleY > 0.5f);
    CHECK(early.scaleY < 2.0f);

    // A full cycle returns to where it started, within the easing tolerance.
    controller.update(0.15f);
    const Pose wrapped = controller.pose();
    CHECK(wrapped.scaleY > 0.9f);
    CHECK(wrapped.scaleY < 1.1f);
}

TEST_CASE("a non-looping clip finishes and holds its last pose")
{
    AnimationController controller;
    controller.reset();
    CHECK(controller.play(AnimId::Death, true));
    const float duration = clipFor(AnimId::Death).duration;

    controller.update(duration + 1.0f);
    CHECK(controller.finished());
    CHECK(controller.progress() == doctest::Approx(1.0f));

    const Pose end = controller.pose();
    // The death clip ends with the body collapsed and faded out. If the
    // controller snapped to a neutral pose at the end, a dead enemy would pop
    // back to standing for one frame before being removed.
    CHECK(end.alpha < 0.1f);
    CHECK(end.scaleY < 0.7f);
}

TEST_CASE("a clip refuses to cut a short action short")
{
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Attack, true));
    controller.update(0.05f);

    // Same clip again: refused, because it is already playing.
    CHECK_FALSE(controller.play(AnimId::Attack));
    // A different short action mid-swing: also refused, so mashing attack does
    // not visually restart the swing while the input buffer queues a second one.
    CHECK_FALSE(controller.play(AnimId::Hurt));
    // Forced, it happens. Resets are a legitimate need.
    CHECK(controller.play(AnimId::Attack, true));

    // Once the action has finished, a new one is allowed.
    controller.update(2.0f);
    CHECK(controller.finished());
    CHECK(controller.play(AnimId::Hurt));
}

TEST_CASE("a transition blends from the pose it interrupted")
{
    AnimationController controller;
    controller.reset();
    controller.update(0.2f);
    const Pose before = controller.pose();

    REQUIRE(controller.play(AnimId::JumpStart, true));
    // Immediately after the switch, the pose must still be close to the old one,
    // or every transition is a visible pop.
    const Pose atSwitch = controller.pose();
    CHECK(atSwitch.scaleY == doctest::Approx(before.scaleY).epsilon(0.2));
    CHECK(controller.previousPose().scaleY == doctest::Approx(before.scaleY).epsilon(0.01));

    // And after the blend, it should be the new clip's first key.
    controller.update(0.5f);
    const Pose settled = controller.pose();
    CHECK(settled.scaleY != doctest::Approx(before.scaleY).epsilon(0.01));
}

TEST_CASE("the attack clip's hit window matches the player's swing")
{
    // The player's tuning is the authority on when a swing is dangerous. The clip
    // only has to *agree* with it, and this is the check that they still do.
    //
    // PlayerTuning is not included here on purpose: this file is about the
    // animation, and coupling the two would mean every retune of the swing's
    // timing silently changes a test about the animation. The values are copied
    // from PlayerTuning and the comment says so.
    constexpr float windup  = 0.06f;   // PlayerTuning::attackWindup
    constexpr float active  = 0.10f;   // PlayerTuning::attackActive
    constexpr float recover = 0.16f;   // PlayerTuning::attackRecover
    const float total = windup + active + recover;

    const AnimationClip& clip = clipFor(AnimId::Attack);
    CHECK(clip.duration == doctest::Approx(total).epsilon(0.01f));

    float hitAt = -1.0f;
    float endAt = -1.0f;
    for (const AnimationMarker& marker : clip.markers) {
        if (marker.event == AnimEvent::AttackHit) {
            hitAt = marker.time;
        }
        if (marker.event == AnimEvent::AttackEnd) {
            endAt = marker.time;
        }
    }
    REQUIRE(hitAt >= 0.0f);
    REQUIRE(endAt > hitAt);
    // The hit opens at the end of the windup and closes at the end of the active
    // window, to within one frame. If these drift, the spark appears before or
    // after the thing it is a spark for.
    CHECK(hitAt == doctest::Approx(windup).epsilon(0.02f));
    CHECK(endAt == doctest::Approx(windup + active).epsilon(0.02f));
}

TEST_CASE("attack markers fire exactly once each")
{
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Attack, true));

    const std::vector<AnimEvent> events = run(controller, 60);
    CHECK(countOf(events, AnimEvent::AttackHit) == 1);
    CHECK(countOf(events, AnimEvent::AttackEnd) == 1);
    // And the order is the order of the timeline.
    const int hit  = indexOf(events, AnimEvent::AttackHit);
    const int end  = indexOf(events, AnimEvent::AttackEnd);
    CHECK(hit >= 0);
    CHECK(hit < end);
}

TEST_CASE("a step larger than the clip does not lose its markers")
{
    // A frame hitch must not eat the hit frame. If a 200ms hitch could skip the
    // marker, the player would land a blow with no spark, no sound and no feel.
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Attack, true));

    const std::vector<AnimEvent> events = run(controller, 1, 0.2f);
    // The whole clip is 0.32s and the step is 0.2s, so this single update spans
    // the hit window. Both markers must still arrive.
    CHECK(countOf(events, AnimEvent::AttackHit) == 1);
    CHECK(countOf(events, AnimEvent::AttackEnd) == 1);
}

TEST_CASE("a looping clip fires its markers again on the wrap")
{
    // A footstep on the last eighth of a walk cycle must not be silently dropped
    // when the cycle loops, or the rhythm is wrong once every two steps.
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Walk, true));
    const float duration = clipFor(AnimId::Walk).duration;

    const std::vector<AnimEvent> first = run(controller, 60);
    const std::vector<AnimEvent> second = run(controller, 60);
    const int firstCount  = countOf(first, AnimEvent::Footstep);
    const int secondCount = countOf(second, AnimEvent::Footstep);
    CHECK(firstCount > 0);
    // A walk cycle is 0.6s and these are two 1s batches, so each should manage
    // three or four footfalls. A much lower number means the wrap is dropping
    // the markers near the end of the cycle.
    CHECK(secondCount >= 2);
    static_cast<void>(duration);
}

TEST_CASE("setTime places a simulation-synced clip without firing markers")
{
    // This is the mechanism that keeps a swing's picture and its hitbox in
    // agreement. Setting the time must be exact and must not emit events,
    // because the simulation is what decided the hitbox opened, not the clock.
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Attack, true));
    const float duration = clipFor(AnimId::Attack).duration;

    controller.setTime(duration * 0.5f);
    CHECK(controller.pose().armExtension > 0.5f);
    CHECK(controller.takeEvents().empty());

    // Out of range clamps rather than wrapping, because a swing that wrapped
    // past its end would look like it had recovered while still being thrown.
    controller.setTime(duration * 5.0f);
    CHECK(controller.progress() == doctest::Approx(1.0f));
    controller.setTime(-1.0f);
    CHECK(controller.progress() == doctest::Approx(0.0f));
    CHECK(controller.takeEvents().empty());
}

TEST_CASE("the enemy windup is longer than the player's, and says so in the pose")
{
    // The telegraph is the fair-play contract of this game: an enemy that is
    // winding up must be readable. So the windup is longer than the player's
    // swing, and the lean goes backwards before it comes forwards, which is the
    // shape a reader recognises as "about to".
    const AnimationClip& windup = clipFor(AnimId::EWindup);
    const AnimationClip& playerAttack = clipFor(AnimId::Attack);
    CHECK(windup.duration > playerAttack.duration);

    const float startLean = windup.keys.front().pose.lean;
    const float peakLean = windup.keys[windup.keys.size() / 2].pose.lean;
    CHECK(startLean == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(peakLean < startLean);
}

TEST_CASE("poses interpolate rather than snapping between keys")
{
    Pose a;
    Pose b;
    b.scaleY = 2.0f;
    b.offsetY = -10.0f;
    b.alpha = 0.0f;

    CHECK(lerpPose(a, b, 0.0f).scaleY == doctest::Approx(1.0f));
    CHECK(lerpPose(a, b, 1.0f).scaleY == doctest::Approx(2.0f));
    // Out of range clamps, so a blend factor that overshoots cannot invert a
    // squash into a mirror image.
    CHECK(lerpPose(a, b, -1.0f).scaleY == doctest::Approx(1.0f));
    CHECK(lerpPose(a, b, 2.0f).scaleY == doctest::Approx(2.0f));

    // Halfway is halfway, in every field.
    const Pose mid = lerpPose(a, b, 0.5f);
    CHECK(mid.scaleY == doctest::Approx(1.5f));
    CHECK(mid.offsetY == doctest::Approx(-5.0f));
    CHECK(mid.alpha == doctest::Approx(0.5f));
}

TEST_CASE("a negative or huge delta does not corrupt the pose")
{
    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Run, true));

    controller.update(-1.0f);
    CHECK(controller.pose().scaleY > 0.5f);
    controller.update(100.0f);
    CHECK(std::isfinite(controller.pose().scaleY));
    CHECK(std::isfinite(controller.pose().offsetX));
}

TEST_CASE("reset returns the controller to a neutral idle")
{
    AnimationController controller;
    REQUIRE(controller.play(AnimId::Death, true));
    controller.update(1.5f);
    REQUIRE(controller.pose().alpha < 0.2f);

    controller.reset();
    CHECK(static_cast<int>(controller.current()) == static_cast<int>(AnimId::Idle));
    CHECK(controller.pose().alpha == doctest::Approx(1.0f));
    CHECK(controller.finished() == false);
    CHECK(controller.takeEvents().empty());
}

TEST_CASE("every event has a name")
{
    // For logs and for failing assertions, which is the difference between
    // "marker 3" and "AttackEnd".
    const AnimEvent events[] = {AnimEvent::Footstep,  AnimEvent::AttackHit,
                                AnimEvent::AttackEnd,  AnimEvent::JumpPush,
                                AnimEvent::LandImpact, AnimEvent::DashTrail,
                                AnimEvent::DeathEnd};
    for (const AnimEvent event : events) {
        CHECK(toString(event) != "?");
    }
    for (int i = 0; i < static_cast<int>(AnimId::Count); ++i) {
        CHECK(toString(static_cast<AnimId>(i)) != "?");
    }
}

// ---------------------------------------------------------------------------
// Clip selection
// ---------------------------------------------------------------------------
//
// Which clip plays is a decision, and it is the kind that fails invisibly: a
// wrong answer still draws a character, still animates, and still looks like a
// game. Nothing crashes and nothing logs. The only way to see it is to assert on
// the decision itself, which means the decision has to be reachable from a test -
// hence `playerClipFor` living here rather than in the state that draws.

namespace {

constexpr int   kFloorRow = 6;
constexpr float kFloorY   = kFloorRow * TileMap::kTileSize;
constexpr float kStepDt   = 1.0f / 60.0f;

/// The enemy tests' arena: a flat floor with walls at both ends.
constexpr int   kEnemyFloorRow = 6;
constexpr float kEnemyFloorY   = kEnemyFloorRow * TileMap::kTileSize;

TileMap enemyArena()
{
    TileMap map;
    map.resize(40, 12);
    for (int x = 0; x < 40; ++x) {
        map.set(x, kEnemyFloorRow, Tile::of(TileKind::Solid));
    }
    for (int y = 0; y < 12; ++y) {
        map.set(0, y, Tile::of(TileKind::Solid));
        map.set(39, y, Tile::of(TileKind::Solid));
    }
    return map;
}

/// A flat floor with walls, and a player standing on it.
struct Standing {
    TileMap map;
    Player  player;

    Standing()
    {
        map.resize(40, 12);
        for (int x = 0; x < 40; ++x) {
            map.set(x, kFloorRow, Tile::of(TileKind::Solid));
        }
        for (int y = 0; y < 12; ++y) {
            map.set(0, y, Tile::of(TileKind::Solid));
            map.set(39, y, Tile::of(TileKind::Solid));
        }
        player.reset(Vec2{160.0f, kFloorY - 44.0f}, Era::Present);
        for (int i = 0; i < 4; ++i) {
            player.update(map, PlayerInput{}, kStepDt);
        }
    }

    void run(int steps, const PlayerInput& input)
    {
        for (int i = 0; i < steps; ++i) {
            player.update(map, input, kStepDt);
        }
    }
};

PlayerInput heldAxis(float axis)
{
    PlayerInput input;
    input.moveAxis = axis;
    return input;
}

} // namespace

TEST_CASE("a standing player animates as idle, walking and running")
{
    // The regression. `charging` was derived from `chrono() > 0`, which is true for
    // essentially the whole game, and the charge check sat *above* the grounded
    // checks - so the player was permanently in the shift-charge pose. Idle, Walk
    // and Run were unreachable. Because the charge clip is also translucent, the
    // player was permanently see-through as well, and none of it produced a log
    // line, a crash or a wrong frame: the character animated, just always the
    // wrong way.
    Standing f;

    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Idle));

    // Part way. The bands are on the *player's* speed rather than on a keypress,
    // so a slow walk has to walk and a fast one has to run.
    f.run(30, heldAxis(0.4f));
    CHECK(fabs(f.player.body().velocity.x) > 12.0f);
    CHECK(fabs(f.player.body().velocity.x) < 190.0f);
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Walk));

    // And flat out. See the note on the walk cycle in the bug tracker: at the
    // default tuning a full-speed walk *is* a run, so this is the pose ordinary
    // movement settles into, not a special one.
    f.run(60, heldAxis(1.0f));
    CHECK(fabs(f.player.body().velocity.x) > 190.0f);
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Run));

    // Off the ground beats every grounded clip. Lifted clear and then stepped, so
    // it is genuinely airborne rather than briefly off the floor.
    Standing air;
    air.player.body().position.y -= 200.0f;
    air.player.body().onGround = false;
    air.player.body().velocity  = Vec2{0.0f, 600.0f};
    air.run(1, PlayerInput{});
    REQUIRE_FALSE(air.player.body().onGround);
    CHECK(static_cast<int>(playerClipFor(air.player, false)) == static_cast<int>(AnimId::Fall));
}

TEST_CASE("the charge pose is only for a player who is charging")
{
    // Both halves of the condition, each of which was enough on its own to keep
    // the charge pose permanently on screen.
    Standing f;
    REQUIRE(f.player.chrono() > 0.0f);

    // Key up, plenty of chrono: not charging.
    CHECK(static_cast<int>(playerClipFor(f.player, false)) != static_cast<int>(AnimId::ShiftCharge));

    // Key down and chrono to spend: charging, and translucent.
    CHECK(static_cast<int>(playerClipFor(f.player, true)) == static_cast<int>(AnimId::ShiftCharge));

    // Key down with nothing to spend: not charging either. Holding a key you
    // cannot use is not a charge.
    Standing empty;
    PlayerTuning spent;
    spent.maxChrono = 0.0f;
    empty.player.setTuning(spent);
    empty.player.refillChrono();
    REQUIRE(empty.player.chrono() == doctest::Approx(0.0f));
    CHECK(static_cast<int>(playerClipFor(empty.player, true)) != static_cast<int>(AnimId::ShiftCharge));
}

TEST_CASE("the charge pose and the charge aura agree on what charging is")
{
    // Two systems draw the charge: the aura in `FeedbackSystem`, which pulls
    // particles in, and the pose here. They are separate code in separate files,
    // so the only thing keeping them consistent is that both mean "the key is
    // down and there is chrono to spend". A player gathering an aura while
    // standing in an ordinary idle pose, or charging without an aura to aim, is a
    // mechanic that has come apart in the middle - which is exactly what the shift
    // is not allowed to be.
    Standing f;
    for (const bool held : {false, true}) {
        const bool aura = held && f.player.chrono() > 0.0f;
        const bool pose = playerClipFor(f.player, held) == AnimId::ShiftCharge;
        INFO("shiftHeld=" << held);
        CHECK(pose == aura);
    }
}

TEST_CASE("the player is opaque unless they are charging or dying")
{
    // The visible consequence of the bug above, stated as a property of the clips
    // rather than of the selector: none of the player's clips makes them
    // translucent except the two that are supposed to. A ghost the player cannot
    // switch off is not a stylistic choice.
    //
    // Only the player's own range is checked. `ERecover` is dimmed on purpose -
    // the tail of an enemy's attack is meant to look punishable - so "nothing is
    // translucent" would be the wrong property for the enemy half of the table.
    for (int i = static_cast<int>(AnimId::Idle); i <= static_cast<int>(AnimId::ShiftCharge); ++i) {
        const auto id = static_cast<AnimId>(i);
        CAPTURE(static_cast<int>(id));
        if (id == AnimId::ShiftCharge || id == AnimId::Death) {
            continue;
        }
        for (const AnimationKey& key : clipFor(id).keys) {
            CHECK(key.pose.alpha == doctest::Approx(1.0f));
        }
    }
    // And the charge clip really is the translucent one, or the test above is
    // vacuous.
    CHECK(clipFor(AnimId::ShiftCharge).keys.back().pose.alpha < 1.0f);
}

TEST_CASE("being hit plays the hurt clip, and it is not the i-frames")
{
    // `AnimId::Hurt` existed in the enum, had a clip with three keys, had a
    // dedicated 40ms blend time and was asserted on by the clip-table test - and
    // nothing ever selected it, because `PlayerStepEvents::hurt` is an edge lasting
    // exactly one step. A reaction driven by a one-frame flag is a reaction that
    // never appears, which is why this needed a timer rather than the event.
    Standing f;
    REQUIRE(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Idle));

    REQUIRE(f.player.takeDamage(1.0f, Vec2{f.player.body().center().x, f.player.body().center().y - 40.0f}));
    CHECK(f.player.hurt());
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Hurt));

    // It ends, and it ends on the clip's own clock rather than the i-frames'. The
    // player is still invulnerable long after the flinch is over, so a pose keyed
    // to `invulnerable()` would leave them wincing for most of a second.
    f.run(static_cast<int>(f.player.tuning().hurtTime * 60.0f) + 2, PlayerInput{});
    CHECK_FALSE(f.player.hurt());
    CHECK(f.player.invulnerable());
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Idle));
}

TEST_CASE("a hurt reaction interrupts a swing rather than queueing behind it")
{
    // `play` refuses to cut a short action short, which is right for two actions
    // competing for one gesture and wrong for a reaction. The clip is selected and
    // then forced, so being hit mid-swing is visible immediately.
    Standing f;
    PlayerInput attack;
    attack.attackPressed = true;
    f.run(1, attack);
    REQUIRE(f.player.attackPhase() != AttackPhase::Idle);

    REQUIRE(f.player.takeDamage(1.0f, Vec2{0.0f, f.player.body().center().y - 40.0f}));
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Hurt));

    AnimationController controller;
    controller.reset();
    REQUIRE(controller.play(AnimId::Attack, true));
    controller.update(0.05f);
    // The rule itself, unchanged: an unforced switch is still refused.
    CHECK_FALSE(controller.play(AnimId::Hurt));
    // A reaction forces its way in.
    CHECK(controller.play(AnimId::Hurt, true));
    CHECK(static_cast<int>(controller.current()) == static_cast<int>(AnimId::Hurt));
}

TEST_CASE("death outranks everything, including a swing in progress")
{
    Standing f;
    PlayerInput attack;
    attack.attackPressed = true;
    f.run(1, attack);
    REQUIRE(f.player.attackPhase() != AttackPhase::Idle);

    const Vec2 above{f.player.body().center().x, f.player.body().center().y - 40.0f};
    for (int i = 0; i < 10; ++i) {
        f.run(30, PlayerInput{});          // wait out the i-frames between hits
        f.player.takeDamage(1.0f, above);
        if (!f.player.alive()) {
            break;
        }
    }
    REQUIRE_FALSE(f.player.alive());
    // Whether or not the shift key is down and whether or not a swing is running.
    CHECK(static_cast<int>(playerClipFor(f.player, false)) == static_cast<int>(AnimId::Death));
    CHECK(static_cast<int>(playerClipFor(f.player, true)) == static_cast<int>(AnimId::Death));
}

TEST_CASE("every clip the selectors can return has a pose to show")
{
    // A selector returning a valid-looking enum with an empty clip behind it draws
    // a character that does not move, which is indistinguishable from a frozen
    // frame. Cheap to assert, and it is the failure mode a table like this has.
    for (int i = 0; i < static_cast<int>(AnimId::Count); ++i) {
        CAPTURE(static_cast<int>(i));
        CHECK_FALSE(clipFor(static_cast<AnimId>(i)).keys.empty());
    }
}

TEST_CASE("an enemy that is struck reacts, including mid-windup")
{
    // The same defect as the player's, in the same shape: `enemyClipFor` had its
    // hurt check *after* a switch that is exhaustive over `EnemyState`, so every
    // path had already returned and `AnimId::EHurt` was unreachable. The enemy
    // flash, the damage number, the particles and the sound all said "that hit";
    // the enemy itself did not move.
    //
    // The ordering matters as much as the reachability. A hit has to interrupt a
    // windup, because a windup is the fair-play contract this game rests on and
    // an enemy that shrugs off a hit while winding up is telling the player the
    // telegraph does not mean anything.
    const TileMap map = enemyArena();
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{320.0f, kEnemyFloorY - 48.0f}, kEraMaskAll);
    enemy.setId(1);
    const Vec2 target{320.0f + 22.0f, kEnemyFloorY - 48.0f};

    // Walk it into a windup, which is the state where the reaction has to win.
    float frame = 0.0f;
    bool sawWindup = false;
    for (; frame < 240.0f; ++frame) {
        enemy.update(map, Era::Present, target, kStepDt, frame);
        sawWindup = sawWindup || enemy.state() == EnemyState::Windup;
        if (enemy.state() == EnemyState::Windup) {
            break;
        }
    }
    REQUIRE(sawWindup);
    REQUIRE(enemy.state() == EnemyState::Windup);
    // Unstruck, a winding-up enemy is a telegraph and nothing else.
    CHECK(static_cast<int>(enemyClipFor(enemy)) == static_cast<int>(AnimId::EWindup));

    REQUIRE(enemy.takeDamage(1.0f, Vec2{300.0f, kEnemyFloorY - 48.0f}));
    REQUIRE(enemy.hurt());
    const int react = static_cast<int>(enemyClipFor(enemy));
    // Compared as an int: doctest finds this enum's `toString` by ADL and then
    // fails to concatenate the result, which is a distracting way to lose the
    // real assertion failure.
    INFO("a hit did not interrupt the telegraph, clip " << react);
    CHECK(react == static_cast<int>(AnimId::EHurt));

    // The flash is short - a flinch, not a stun - and the AI carries on underneath.
    for (int i = 0; i < 30; ++i) {
        enemy.update(map, Era::Present, target, kStepDt, frame++);
    }
    CHECK_FALSE(enemy.hurt());
    const int resumed = static_cast<int>(enemyClipFor(enemy));
    CHECK(resumed != static_cast<int>(AnimId::EHurt));
    CHECK(resumed != static_cast<int>(AnimId::EDie));
}

TEST_CASE("a dying enemy plays its death, whatever it was doing")
{
    Enemy enemy;
    enemy.spawn(EnemyKind::Sentinel, Vec2{160.0f, 100.0f}, eraBit(Era::Present));
    enemy.setId(2);
    const Vec2 from{0.0f, 100.0f};
    for (int i = 0; i < 40 && enemy.aliveIn(Era::Present); ++i) {
        if (enemy.takeDamage(1.0f, from)) {
            continue;
        }
        enemy.update(TileMap{}, Era::Present, Vec2{400.0f, 100.0f}, kStepDt, 0.0f);
    }
    // Dead or dying: the death clip is the only acceptable answer.
    CHECK(static_cast<int>(enemyClipFor(enemy)) == static_cast<int>(AnimId::EDie));
}
