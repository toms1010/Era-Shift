// Era Shift - tests for the staged settings model.
//
// The bug this exists for: pressing LEFT on VSYNC changed the setting *and* wrote
// it, so leaving with ESC still persisted the change. ESC is the button that means
// "I changed my mind", so the screen could not be tried.
//
// `SettingsDraft` holds no store and no window — only numbers — which is what makes
// "editing writes nothing" checkable at all: a test that could write cannot prove
// that it did not.

#include "EraShift/Game/SettingsDraft.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace EraShift::Game;

namespace {

SettingSpec toggle(const char* key, int fallback = 1)
{
    SettingSpec spec;
    spec.label   = key;
    spec.section = "graphics";
    spec.key     = key;
    spec.kind    = SettingKind::Toggle;
    spec.minimum = 0;
    spec.maximum = 1;
    spec.step    = 1;
    spec.fallback = fallback;
    spec.current  = fallback;
    return spec;
}

SettingSpec scale(const char* key, int fallback = 100)
{
    SettingSpec spec;
    spec.label   = key;
    spec.section = "graphics";
    spec.key     = key;
    spec.kind    = SettingKind::Scale;
    spec.minimum = 50;
    spec.maximum = 200;
    spec.step    = 10;
    spec.fallback = fallback;
    spec.current  = fallback;
    return spec;
}

/// A draft with a toggle and a scale, both at their fallbacks.
SettingsDraft twoSettings()
{
    SettingsDraft draft;
    draft.add(toggle("vsync"));
    draft.add(scale("uiScale"));
    return draft;
}

} // namespace

TEST_CASE("a fresh draft is not dirty")
{
    const SettingsDraft draft = twoSettings();
    CHECK(draft.size() == 2);
    CHECK(draft.dirty() == false);
    CHECK(draft.changedIndices().empty());
}

TEST_CASE("a value starts at its fallback")
{
    const SettingsDraft draft = twoSettings();
    REQUIRE(draft.valueAt(0) != nullptr);
    CHECK(draft.valueAt(0)->current == 1);
    CHECK(draft.valueAt(1)->current == 100);
}

TEST_CASE("editing stages a value without changing the current one")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);

    CHECK(draft.valueAt(0)->pending == 0);
    CHECK(draft.valueAt(0)->current == 1);
    CHECK(draft.dirty());
}

TEST_CASE("editing never commits, so a discard loses the change")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(1, 1);
    REQUIRE(draft.dirty());

    draft.discard();

    CHECK(draft.dirty() == false);
    CHECK(draft.valueAt(1)->pending == 100);
    CHECK(draft.valueAt(1)->current == 100);
}

TEST_CASE("commit promotes pending and reports what moved")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(1, 1);

    const std::vector<std::size_t> committed = draft.commit();

    REQUIRE(committed.size() == 1);
    CHECK(committed.front() == 1);
    CHECK(draft.valueAt(1)->current == 110);
    CHECK(draft.valueAt(1)->pending == 110);
    CHECK(draft.dirty() == false);
}

TEST_CASE("commit with nothing staged reports nothing")
{
    // A Back press on an untouched screen must not report a save, so the caller
    // does not rewrite the file — which would bump its mtime for nothing.
    SettingsDraft draft = twoSettings();
    CHECK(draft.commit().empty());
}

TEST_CASE("editing away and back is not a change")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(0, 1);

    CHECK(draft.valueAt(0)->pending == 1);
    CHECK(draft.dirty() == false);
    CHECK(draft.commit().empty());
}

TEST_CASE("a discard after a commit cannot undo the committed value")
{
    // The order matters. Apply commits; a later Back must not roll the file back to
    // what it was before, because the window is already using the new value.
    SettingsDraft draft = twoSettings();
    draft.adjust(1, 1);
    draft.commit();
    REQUIRE(draft.valueAt(1)->current == 110);

    draft.discard();

    CHECK(draft.valueAt(1)->current == 110);
    CHECK(draft.valueAt(1)->pending == 110);
}

TEST_CASE("values are clamped at both ends")
{
    SettingsDraft draft = twoSettings();
    for (int i = 0; i < 30; ++i) {
        draft.adjust(1, -1);
    }
    CHECK(draft.valueAt(1)->pending == 50);

    for (int i = 0; i < 40; ++i) {
        draft.adjust(1, 1);
    }
    CHECK(draft.valueAt(1)->pending == 200);
}

TEST_CASE("a toggle cannot be pushed past its ends")
{
    SettingsDraft draft = twoSettings();
    for (int i = 0; i < 5; ++i) {
        draft.adjust(0, 1);
    }
    CHECK(draft.valueAt(0)->pending == 1);

    for (int i = 0; i < 5; ++i) {
        draft.adjust(0, -1);
    }
    CHECK(draft.valueAt(0)->pending == 0);
}

TEST_CASE("editing out of range is a no-op, not a crash")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(99, 1);
    draft.adjust(static_cast<std::size_t>(-1), 1);
    draft.adjust(0, 0);
    CHECK(draft.dirty() == false);
    CHECK(draft.valueAt(99) == nullptr);
    CHECK(draft.specAt(99) == nullptr);
}

TEST_CASE("stage sets a specific value, clamped")
{
    SettingsDraft draft = twoSettings();
    draft.stage(1, 175);
    CHECK(draft.valueAt(1)->pending == 175);

    // Clamped rather than rejected: a row driven from live state can report a
    // value outside the range, and refusing it would leave the screen disagreeing
    // with whatever it read.
    draft.stage(1, 9999);
    CHECK(draft.valueAt(1)->pending == 200);

    // An out-of-range index is a separate thing and is ignored.
    SettingsDraft clean = twoSettings();
    clean.stage(99, 100);
    CHECK(clean.dirty() == false);
}

TEST_CASE("a toggle reads as ON and OFF")
{
    const SettingSpec spec = toggle("vsync");
    CHECK(spec.display(1) == "ON");
    CHECK(spec.display(0) == "OFF");
    // A digit in this row means nothing to a player scanning a settings screen.
    CHECK(spec.display(2) == "ON");
}

TEST_CASE("a scale reads as a percentage")
{
    const SettingSpec spec = scale("uiScale");
    CHECK(spec.display(100) == "100%");
    CHECK(spec.display(50) == "50%");
}

TEST_CASE("a toggle steps by one and a scale by ten")
{
    const SettingSpec toggleSpec = toggle("vsync");
    CHECK(toggleSpec.stepped(0, 1) == 1);
    CHECK(toggleSpec.stepped(1, -1) == 0);

    const SettingSpec scaleSpec = scale("uiScale");
    CHECK(scaleSpec.stepped(100, 1) == 110);
    CHECK(scaleSpec.stepped(100, -1) == 90);
    // Zero is not a direction, and a setting that ignores it is one where a
    // stray input moved the value.
    CHECK(scaleSpec.stepped(100, 0) == 100);
}

TEST_CASE("several settings commit independently")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(1, 1);

    const std::vector<std::size_t> committed = draft.commit();
    CHECK(committed.size() == 2);
    CHECK(draft.valueAt(0)->current == 0);
    CHECK(draft.valueAt(1)->current == 110);
    CHECK(draft.dirty() == false);
}

TEST_CASE("changedIndices agrees with dirty")
{
    SettingsDraft draft = twoSettings();
    CHECK(draft.changedIndices().empty());

    draft.adjust(1, 1);
    const std::vector<std::size_t> changed = draft.changedIndices();
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == 1);
    CHECK(draft.dirty());
}

// ---------------------------------------------------------------------------
// What a row displays
//
// The settings screen reads the pending value, not the committed one, so a row
// changes the moment LEFT or RIGHT is pressed and changes back on BACK. Reading
// the committed value instead is a plausible-looking bug that only shows up when
// someone tries a setting and presses ESC.
// ---------------------------------------------------------------------------

TEST_CASE("a row shows the staged value before it is applied")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);

    const SettingValue* vsync = draft.valueAt(0);
    REQUIRE(vsync != nullptr);
    CHECK(vsync->pending == 0);
    // Still not what the game is running with: nothing is written until APPLY.
    CHECK(vsync->current == 1);
    CHECK(vsync->changed());
}

TEST_CASE("a row shows the committed value again after a discard")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(1, 1);
    REQUIRE(draft.dirty());

    draft.discard();

    for (std::size_t i = 0; i < draft.size(); ++i) {
        const SettingValue* value = draft.valueAt(i);
        REQUIRE(value != nullptr);
        CHECK(value->pending == value->current);
        CHECK_FALSE(value->changed());
    }
    CHECK_FALSE(draft.dirty());
}

TEST_CASE("a row returns to its original value when edited away and back")
{
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(0, 1);

    // Nothing moved, so the screen should not offer to save.
    CHECK_FALSE(draft.dirty());
    CHECK(draft.changedIndices().empty());
}

TEST_CASE("pressing past the end of a toggle holds it there rather than wrapping")
{
    // A toggle that wrapped would make two LEFT presses look like no change at all,
    // which is the one thing a settings row must never do.
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(0, -1);
    draft.adjust(0, -1);

    CHECK(draft.valueAt(0)->pending == 0);
    CHECK(draft.valueAt(0)->changed());
}

TEST_CASE("committing one setting leaves the others staged")
{
    // The settings screen commits everything at once, but the model has to survive
    // being asked for one key: a screen that saves per row would otherwise leave a
    // half-applied draft behind.
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.adjust(1, 1);

    const std::vector<std::size_t> committed = draft.commit();

    REQUIRE(committed.size() == 2);
    CHECK(draft.valueAt(0)->current == draft.valueAt(0)->pending);
    CHECK(draft.valueAt(1)->current == draft.valueAt(1)->pending);
    CHECK_FALSE(draft.dirty());
}

TEST_CASE("a discard after a commit cannot be undone by discarding again")
{
    // BACK after APPLY must not roll the saved value back, or the screen would
    // quietly undo a save the player asked for.
    SettingsDraft draft = twoSettings();
    draft.adjust(0, -1);
    draft.commit();
    draft.discard();

    CHECK(draft.valueAt(0)->current == 0);
    CHECK(draft.valueAt(0)->pending == 0);
}
