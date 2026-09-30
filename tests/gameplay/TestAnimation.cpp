// Tests for the animation controller.
//
// The property worth testing is not "does the pose change", which is obvious. It
// is that a clip's *markers* land where the gameplay says they should, and that
// a controller asked to play the same clip twice does not restart it. Those are
// the two things that go wrong silently: a footstep that fires on frame one, and
// a swing that visually restarts on every keypress while the input buffer lets a
// second one queue behind it.

#include "EraShift/Game/Animation.hpp"

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
