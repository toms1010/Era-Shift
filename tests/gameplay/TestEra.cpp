// Era Shift - tests for the era model.
//
// The cycle order and the names are part of the save file format, so a change
// here silently invalidates every save. That is exactly the kind of rule that
// belongs in a test rather than in a comment.

#include "EraShift/Game/Era.hpp"

#include <doctest/doctest.h>

using namespace EraShift::Game;

TEST_CASE("the three eras are distinct and stable")
{
    CHECK(static_cast<int>(Era::Past) == 0);
    CHECK(static_cast<int>(Era::Present) == 1);
    CHECK(static_cast<int>(Era::Future) == 2);
    CHECK(kEraCount == 3);
    CHECK(eraName(Era::Past) == "Past");
    CHECK(eraName(Era::Present) == "Present");
    CHECK(eraName(Era::Future) == "Future");
}

TEST_CASE("the cycle runs Past -> Present -> Future -> Past")
{
    CHECK(nextEra(Era::Past) == Era::Present);
    CHECK(nextEra(Era::Present) == Era::Future);
    CHECK(nextEra(Era::Future) == Era::Past);
}

TEST_CASE("cycling three times returns to where it started, from anywhere")
{
    for (const Era start : {Era::Past, Era::Present, Era::Future}) {
        Era era = start;
        for (int i = 0; i < 3; ++i) {
            era = nextEra(era);
        }
        CHECK(era == start);
    }
}

TEST_CASE("era names parse case-insensitively and rubbish is rejected")
{
    Era era = Era::Future;
    CHECK(parseEra("Past", era));
    CHECK(era == Era::Past);
    CHECK(parseEra("present", era));
    CHECK(era == Era::Present);
    CHECK(parseEra("FUTURE", era));
    CHECK(era == Era::Future);

    CHECK_FALSE(parseEra("Tomorrow", era));
    CHECK_FALSE(parseEra("", era));
    CHECK_FALSE(parseEra("Pastt", era));
    // A failed parse must not have modified the output.
    CHECK(era == Era::Future);
}

TEST_CASE("every era parses from its own name")
{
    for (const Era start : {Era::Past, Era::Present, Era::Future}) {
        Era parsed = Era::Past;
        CHECK(parseEra(eraName(start), parsed));
        CHECK(parsed == start);
    }
}

TEST_CASE("an era mask covers exactly the eras it is asked to")
{
    CHECK(maskIncludes(kEraMaskAll, Era::Past));
    CHECK(maskIncludes(kEraMaskAll, Era::Future));

    CHECK(maskIncludes(kEraBitPast, Era::Past));
    CHECK_FALSE(maskIncludes(kEraBitPast, Era::Present));
    CHECK_FALSE(maskIncludes(kEraBitPast, Era::Future));

    const EraMask both = kEraBitPast | kEraBitFuture;
    CHECK(maskIncludes(both, Era::Past));
    CHECK_FALSE(maskIncludes(both, Era::Present));
    CHECK(maskIncludes(both, Era::Future));

    CHECK_FALSE(maskIncludes(0, Era::Past));
}
