// Era Shift - tests for the first-run tutorial.
//
// The tutorial is pure bookkeeping over what the player just did, so every rule
// is asserted directly rather than through a playthrough.

#include "EraShift/Game/Tutorial.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace EraShift::Game;

TEST_CASE("a fresh tutorial starts on the first lesson")
{
    Tutorial tutorial;
    CHECK(tutorial.enabled());
    CHECK(tutorial.step() == TutorialStep::Move);
    CHECK(tutorial.active());
    CHECK_FALSE(tutorial.complete());
}

TEST_CASE("a disabled tutorial has no lesson and cannot be advanced")
{
    Tutorial tutorial(false);
    CHECK_FALSE(tutorial.enabled());
    CHECK_FALSE(tutorial.active());
    CHECK(tutorial.step() == TutorialStep::None);
    CHECK_FALSE(tutorial.perform(TutorialStep::Move));
    CHECK(tutorial.step() == TutorialStep::None);
}

TEST_CASE("every lesson has a title and a body")
{
    const TutorialStep steps[] = {
        TutorialStep::Move,    TutorialStep::Jump,     TutorialStep::Shift,
        TutorialStep::Enemy,   TutorialStep::Attack,   TutorialStep::Dash,
        TutorialStep::Interact, TutorialStep::Seal,    TutorialStep::Gate,
    };
    for (const TutorialStep step : steps) {
        const TutorialLesson lesson = lessonFor(step);
        CAPTURE(static_cast<int>(step));
        CHECK(lesson.step == step);
        CHECK(lesson.title != nullptr);
        CHECK(lesson.body != nullptr);
        if (lesson.title != nullptr) {
            CHECK(lesson.title[0] != '\0');
        }
        if (lesson.body != nullptr) {
            CHECK(lesson.body[0] != '\0');
        }
    }
}

TEST_CASE("the shift lesson explains the mechanic rather than the key")
{
    // This is the one lesson a player cannot guess. The others can be inferred
    // from the verb; "press Q" on its own teaches nothing about what Q does.
    const TutorialLesson lesson = lessonFor(TutorialStep::Shift);
    CHECK(std::string(lesson.body).find("era") != std::string::npos);
}

TEST_CASE("performing the taught action advances exactly one step")
{
    Tutorial tutorial;
    REQUIRE(tutorial.perform(TutorialStep::Move));
    CHECK(tutorial.step() == TutorialStep::Jump);

    // The same action twice advances twice, because the second press *is* the
    // answer to the lesson that is now showing. What must not happen is a single
    // press advancing two lessons.
    REQUIRE(tutorial.perform(TutorialStep::Jump));
    CHECK(tutorial.step() == TutorialStep::Shift);

    // And the lesson before the current one no longer counts.
    CHECK_FALSE(tutorial.perform(TutorialStep::Move));
    CHECK(tutorial.step() == TutorialStep::Shift);
}

TEST_CASE("an action that is not the lesson being taught does not advance it")
{
    // The property that stops a player mashing space from skipping the whole
    // tutorial: only the matching action counts.
    Tutorial tutorial;
    CHECK_FALSE(tutorial.perform(TutorialStep::Jump));
    CHECK_FALSE(tutorial.perform(TutorialStep::Attack));
    CHECK_FALSE(tutorial.perform(TutorialStep::Complete));
    CHECK(tutorial.step() == TutorialStep::Move);
}

TEST_CASE("lessons run in order and cannot be skipped forward")
{
    Tutorial tutorial;
    std::vector<TutorialStep> visited;
    while (tutorial.active()) {
        visited.push_back(tutorial.step());
        REQUIRE(tutorial.perform(tutorial.step()));
    }

    const std::vector<TutorialStep> expected = {
        TutorialStep::Move,     TutorialStep::Jump,      TutorialStep::Shift,
        TutorialStep::Enemy,    TutorialStep::Attack,    TutorialStep::Dash,
        TutorialStep::Interact, TutorialStep::Seal,      TutorialStep::Gate,
    };
    CHECK(visited == expected);
    CHECK(tutorial.complete());
}

TEST_CASE("finishing is idempotent")
{
    Tutorial tutorial;
    tutorial.finish();
    CHECK(tutorial.complete());
    const TutorialStep after = tutorial.step();
    tutorial.finish();
    tutorial.skip();
    CHECK(tutorial.step() == after);
    CHECK(tutorial.complete());
}

TEST_CASE("a completed tutorial cannot be restarted by a stray action")
{
    Tutorial tutorial;
    tutorial.finish();
    CHECK_FALSE(tutorial.perform(TutorialStep::Move));
    CHECK(tutorial.complete());
}

TEST_CASE("progress restores from a previous session")
{
    Tutorial seen(true);
    seen.perform(TutorialStep::Move);
    seen.perform(TutorialStep::Jump);

    Tutorial restored;
    // `restore` only carries the enabled/complete pair, not the position, so a
    // returning player restarts the lessons rather than resuming mid-sequence.
    restored.restore(true, false);
    CHECK(restored.enabled());
    CHECK(restored.step() == TutorialStep::Move);

    Tutorial done;
    done.restore(true, true);
    CHECK(done.complete());
    CHECK_FALSE(done.active());

    Tutorial off;
    off.restore(false, false);
    CHECK_FALSE(off.active());
}

TEST_CASE("a cleared lesson draws nothing")
{
    // A state clears a lesson rather than tracking whether it drew one, so clearing
    // an unset lesson has to be safe.
    TutorialLesson lesson = lessonFor(TutorialStep::Move);
    REQUIRE(lesson.title[0] != '\0');

    lesson.clear();
    CHECK(lesson.title[0] == '\0');
    CHECK(lesson.body[0] == '\0');
    CHECK(lesson.key[0] == '\0');
    CHECK(lesson.step == TutorialStep::None);

    // Idempotent, because it is called on entry and again on completion.
    lesson.clear();
    CHECK(lesson.step == TutorialStep::None);
}

TEST_CASE("a lesson that was never set can still be cleared")
{
    TutorialLesson lesson;
    lesson.clear();
    CHECK(lesson.title[0] == '\0');
    CHECK(lesson.step == TutorialStep::None);
}

TEST_CASE("clearing a lesson copy does not disturb the lesson table")
{
    TutorialLesson lesson = lessonFor(TutorialStep::Shift);
    const std::string original = lesson.body;
    lesson.clear();
    CHECK(std::string(lessonFor(TutorialStep::Shift).body) == original);
}

TEST_CASE("the lesson count reflects what was actually taught")
{
    Tutorial tutorial;
    CHECK(tutorial.lessonsSeen() == 0);
    tutorial.perform(TutorialStep::Move);
    tutorial.perform(TutorialStep::Move);   // ignored: wrong action
    CHECK(tutorial.lessonsSeen() == 1);
}

// ---------------------------------------------------------------------------
// The lesson prompt's lifecycle
//
// The prompt enters, holds, and leaves on a clock rather than a set of booleans,
// so the failures here are off-by-a-frame ones: a prompt that is judged finished
// the instant it begins leaving just vanishes, and a screenshot taken at the wrong
// moment shows nothing wrong at all.
// ---------------------------------------------------------------------------

TEST_CASE("a lesson that has not arrived yet is invisible")
{
    CHECK(lessonOpacity(true, false, 0.0f) == 0.0f);
}

TEST_CASE("a lesson fades in and then stays put")
{
    const float half = kLessonEnter * 0.5f;
    CHECK(lessonOpacity(true, false, half) > 0.0f);
    CHECK(lessonOpacity(true, false, half) < 1.0f);

    // Fully arrived, and still fully arrived a good while later: a prompt that kept
    // fading would never be readable.
    CHECK(lessonOpacity(true, false, kLessonEnter) == doctest::Approx(1.0f));
    CHECK(lessonOpacity(true, false, kLessonEnter * 100.0f) == doctest::Approx(1.0f));
}

TEST_CASE("the arrival is monotonic, so the prompt never flickers")
{
    float previous = 0.0f;
    for (int i = 0; i <= 40; ++i) {
        const float age = kLessonEnter * static_cast<float>(i) / 40.0f;
        const float now = lessonOpacity(true, false, age);
        CHECK(now >= previous);
        previous = now;
    }
}

TEST_CASE("a satisfied lesson is fully shown when its leave starts")
{
    // The clock restarts when the leave begins, so age 0 of a leave must be the
    // same as the end of the arrival. Anything else and the prompt jumps.
    CHECK(lessonOpacity(true, true, 0.0f) == doctest::Approx(1.0f));
    CHECK(lessonOpacity(true, false, kLessonEnter) ==
          doctest::Approx(lessonOpacity(true, true, 0.0f)));
}

TEST_CASE("a satisfied lesson fades out and is gone")
{
    CHECK(lessonOpacity(true, true, kLessonEnter + kLessonLeave * 0.5f) > 0.0f);
    CHECK(lessonOpacity(true, true, kLessonEnter + kLessonLeave * 0.5f) < 1.0f);
    CHECK(lessonOpacity(true, true, kLessonEnter + kLessonLeave) ==
          doctest::Approx(0.0f));
    // Stays gone, however long the state keeps ticking.
    CHECK(lessonOpacity(true, true, 1000.0f) == doctest::Approx(0.0f));
}

TEST_CASE("the leave only decreases")
{
    float previous = 1.0f;
    for (int i = 0; i <= 40; ++i) {
        const float age =
            kLessonEnter + kLessonLeave * static_cast<float>(i) / 40.0f;
        const float now = lessonOpacity(true, true, age);
        CHECK(now <= previous);
        previous = now;
    }
}

TEST_CASE("the last lesson keeps drawing after the tutorial reports complete")
{
    // `Tutorial::active()` goes false the moment the final lesson is performed, which
    // is *before* the prompt has finished leaving. If visibility followed active()
    // alone, the one prompt the player had earned a satisfying end to would be the
    // one that snapped away.
    CHECK_FALSE(lessonVisible(false, false));
    CHECK(lessonVisible(false, true));
    CHECK(lessonVisible(true, false));
    CHECK(lessonVisible(true, true));
}

TEST_CASE("a completed tutorial with nothing leaving draws nothing")
{
    CHECK(lessonOpacity(false, false, 0.0f) == 0.0f);
    CHECK(lessonOpacity(false, false, kLessonEnter * 5.0f) == 0.0f);
}

TEST_CASE("opacity stays inside 0..1 for every combination")
{
    for (const bool active : {false, true}) {
        for (const bool leaving : {false, true}) {
            for (int i = -5; i <= 60; ++i) {
                const float age = static_cast<float>(i) * 0.05f;
                const float value = lessonOpacity(active, leaving, age);
                CHECK(value >= 0.0f);
                CHECK(value <= 1.0f);
            }
        }
    }
}

TEST_CASE("the enter and leave windows are short enough not to be in the way")
{
    // A prompt that took a second to arrive would still be fading when the player
    // had already done the thing.
    CHECK(kLessonEnter < 0.5f);
    CHECK(kLessonLeave < 0.5f);
}


// ---------------------------------------------------------------------------
// Lessons taught by arriving
//
// The Enemy, Seal and Gate lessons have no key to wait for: the player cannot
// swing at an enemy they have not found and cannot walk into a gate they have not
// found. They advance on proximity instead.
//
// These existed and never advanced. Nothing called `skip` for them, so the
// tutorial stopped dead on "IT LIVES IN AN ERA" and no later lesson was ever
// shown - a player who had already learned to move, jump and shift still sat
// looking at an enemy lesson that no input could dismiss.
// ---------------------------------------------------------------------------

TEST_CASE("the enemy lesson advances when the player arrives at an enemy")
{
    Tutorial tutorial;
    REQUIRE(tutorial.step() == TutorialStep::Move);

    // Straight to the lesson that is about arriving.
    tutorial.skip();   // Move
    tutorial.skip();   // Jump
    tutorial.skip();   // Shift
    REQUIRE(tutorial.step() == TutorialStep::Enemy);

    CHECK_FALSE(tutorial.reachedSituation(false));

    CHECK(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Attack);
}

TEST_CASE("arriving does not advance a lesson that needs an action")
{
    // The dangerous half: if proximity advanced *every* lesson, walking up to an
    // enemy would skip the attack lesson and the player would meet a Warden having
    // never been told what a swing is.
    Tutorial tutorial;
    REQUIRE(tutorial.step() == TutorialStep::Move);

    CHECK_FALSE(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Move);
    CHECK_FALSE(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Move);
}

TEST_CASE("the seal and gate lessons advance on arrival too")
{
    Tutorial tutorial;
    while (tutorial.step() != TutorialStep::Seal) {
        tutorial.skip();
    }
    REQUIRE(tutorial.step() == TutorialStep::Seal);
    CHECK_FALSE(tutorial.reachedSituation(false));
    CHECK(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Gate);
    CHECK(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Complete);
}

TEST_CASE("the whole tutorial can be finished by arriving and acting")
{
    // The end-to-end property that the stall broke: a player who follows the
    // prompts all the way through ends up at Complete rather than stuck.
    Tutorial tutorial;
    int guard = 0;
    while (!tutorial.complete() && guard < 32) {
        ++guard;
        if (!tutorial.reachedSituation(true)) {
            tutorial.perform(tutorial.step());
        }
    }
    CHECK(tutorial.complete());
    CHECK(tutorial.step() == TutorialStep::Complete);
}

TEST_CASE("arriving counts as a lesson seen")
{
    Tutorial tutorial;
    while (tutorial.step() != TutorialStep::Enemy) {
        tutorial.skip();
    }
    const int before = tutorial.lessonsSeen();
    REQUIRE(tutorial.reachedSituation(true));
    // So the "how much of the tutorial did this player actually see" figure counts
    // an arrival the same as a keypress.
    CHECK(tutorial.lessonsSeen() == before + 1);
}

TEST_CASE("arriving at nothing after the tutorial is finished does nothing")
{
    Tutorial tutorial;
    tutorial.finish();
    CHECK_FALSE(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::Complete);
}

TEST_CASE("a disabled tutorial cannot be advanced by arriving")
{
    Tutorial tutorial(false);
    CHECK_FALSE(tutorial.active());
    CHECK_FALSE(tutorial.reachedSituation(true));
    CHECK(tutorial.step() == TutorialStep::None);
}
