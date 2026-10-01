// Era Shift - tests for the dialogue panel.
//
// The panel is pure state plus a draw call, so the tests are about the state: what
// is considered open, what survives a replacement, and whether a stale field can
// leak from one line to the next.

#include "EraShift/Game/Dialogue.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Game;

TEST_CASE("an empty panel is closed")
{
    DialoguePanel panel;
    CHECK(panel.open() == false);
}

TEST_CASE("a panel with only a hint is still closed")
{
    // A footer with no content above it is worse than nothing, so a hint alone
    // must not count as something to draw.
    DialoguePanel panel;
    panel.hint = "E to confirm";
    CHECK(panel.open() == false);
}

TEST_CASE("a panel with a message is open")
{
    DialoguePanel panel;
    panel.show("WARDEN", "You cannot pass.");
    CHECK(panel.open());
    CHECK(panel.speaker == "WARDEN");
    CHECK(panel.message == "You cannot pass.");
}

TEST_CASE("a panel with only a speaker is open")
{
    // Narration with no body is still worth a frame.
    DialoguePanel panel;
    panel.show("THE GATE", "");
    CHECK(panel.open());
}

TEST_CASE("show replaces the contents rather than merging")
{
    // The bug this guards: a title or hint left over from the previous line, so
    // the second speaker appears under the first one's heading.
    DialoguePanel panel;
    panel.title = "THE ANCIENT GATE";
    panel.hint  = "E to confirm";
    panel.show("WARDEN", "Not yet.");

    CHECK(panel.title.empty());
    CHECK(panel.hint.empty());
    CHECK(panel.speaker == "WARDEN");
}

TEST_CASE("show does not clear a title the caller just set")
{
    // `show` clears, so a title set *before* it is lost; a title set after is
    // kept. Asserting the order makes that contract visible rather than accidental.
    DialoguePanel panel;
    panel.show("WARDEN", "Not yet.");
    panel.title = "THE ANCIENT GATE";
    CHECK(panel.title == "THE ANCIENT GATE");
    CHECK(panel.open());
}

TEST_CASE("clear empties every field")
{
    DialoguePanel panel;
    panel.title  = "T";
    panel.hint   = "H";
    panel.show("S", "M", 3.0f);
    panel.clear();

    CHECK(panel.open() == false);
    CHECK(panel.speaker.empty());
    CHECK(panel.message.empty());
    CHECK(panel.title.empty());
    CHECK(panel.hint.empty());
    CHECK(panel.duration == doctest::Approx(0.0f));
}

TEST_CASE("a cleared panel can be reused without residue")
{
    DialoguePanel panel;
    panel.show("FIRST", "one", 1.0f);
    panel.clear();
    panel.show("SECOND", "two", 2.0f);

    CHECK(panel.speaker == "SECOND");
    CHECK(panel.message == "two");
    CHECK(panel.duration == doctest::Approx(2.0f));
}

TEST_CASE("a zero duration means the panel does not expire")
{
    // A line that must be read rather than glanced at.
    DialoguePanel panel;
    panel.show("WARDEN", "Look behind you.", 0.0f);
    CHECK(panel.duration == doctest::Approx(0.0f));

    DialoguePanel timed;
    timed.show("WARDEN", "Look behind you.", 2.5f);
    CHECK(timed.duration == doctest::Approx(2.5f));
}

TEST_CASE("a long message is stored intact even though it will be clipped on screen")
{
    // The panel does not truncate. Clipping is a drawing concern with the frame's
    // dimensions available; truncating here would lose text the caller may want to
    // show differently elsewhere.
    const std::string long_message(4000, 'x');
    DialoguePanel panel;
    panel.show("WARDEN", long_message);
    CHECK(panel.message.size() == 4000);
    CHECK(panel.open());
}

TEST_CASE("an empty message with a speaker still opens")
{
    DialoguePanel panel;
    panel.show("", "");
    CHECK(panel.open() == false);

    panel.show("WARDEN", "");
    CHECK(panel.open());
}