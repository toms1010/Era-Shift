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

TEST_CASE("the lesson count reflects what was actually taught")
{
    Tutorial tutorial;
    CHECK(tutorial.lessonsSeen() == 0);
    tutorial.perform(TutorialStep::Move);
    tutorial.perform(TutorialStep::Move);   // ignored: wrong action
    CHECK(tutorial.lessonsSeen() == 1);
}