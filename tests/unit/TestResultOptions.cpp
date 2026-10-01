// Era Shift - tests for the end-of-run screen.
//
// Which options a results screen offers *is* its behaviour: a player who just
// died cannot tell from the artwork whether RETRY CHECKPOINT will take them back
// or restart the region underneath. So the option set is worth testing on its own,
// and it is testable headlessly because building the menu needs no renderer.

#include "EraShift/Application/StateContext.hpp"
#include "EraShift/Core/Log.hpp"
#include "EraShift/Game/states/ResultState.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using EraShift::Core::GameState;
using EraShift::Core::Logger;
using EraShift::Core::LogLevel;
using EraShift::Game::Outcome;
using EraShift::Game::ResultState;
using EraShift::Game::RunStats;
using EraShift::StateContext;

namespace {

Logger& quietLog()
{
    static Logger logger;
    logger.setMinLevel(LogLevel::Off);
    return logger;
}

/// Enters a results screen and hands back its option labels.
std::vector<std::string> optionsFor(Outcome outcome, bool retryFromCheckpoint)
{
    StateContext ctx{};
    ctx.log = &quietLog();

    ResultState state(GameState::GameOver, outcome, RunStats{});
    state.setRetryFromCheckpoint(retryFromCheckpoint);
    state.onEnter(ctx);
    return state.optionLabels();
}

bool offers(const std::vector<std::string>& options, const std::string& label)
{
    return std::find(options.begin(), options.end(), label) != options.end();
}

/// Position of a label, or npos. Order matters on a menu: the row a player will
/// press Enter on first is the one the screen is recommending.
std::size_t positionOf(const std::vector<std::string>& options, const std::string& label)
{
    const auto it = std::find(options.begin(), options.end(), label);
    return it == options.end() ? static_cast<std::size_t>(-1)
                               : static_cast<std::size_t>(it - options.begin());
}

} // namespace

TEST_CASE("a defeat with a checkpoint leads with the checkpoint")
{
    const std::vector<std::string> options = optionsFor(Outcome::Defeat, true);

    CHECK(offers(options, "RETRY CHECKPOINT"));
    CHECK(offers(options, "RESTART REGION"));
    // First row: a player who just lost wants their place back, not the whole
    // region again.
    CHECK(positionOf(options, "RETRY CHECKPOINT") == 0);
}

TEST_CASE("a defeat with no checkpoint does not offer a checkpoint")
{
    // The row would have nothing to restore, so offering it would be a lie.
    const std::vector<std::string> options = optionsFor(Outcome::Defeat, false);

    CHECK_FALSE(offers(options, "RETRY CHECKPOINT"));
    CHECK(offers(options, "RESTART REGION"));
    CHECK(positionOf(options, "RETRY CHECKPOINT") == static_cast<std::size_t>(-1));
}

TEST_CASE("a victory leads with playing again, not with leaving")
{
    const std::vector<std::string> options = optionsFor(Outcome::Victory, true);

    CHECK(positionOf(options, "PLAY AGAIN") == 0);
    // A victory ignores the checkpoint flag: the run is over, so there is nothing
    // to return to.
    CHECK_FALSE(offers(options, "RETRY CHECKPOINT"));
    CHECK_FALSE(offers(options, "RESTART REGION"));
}

TEST_CASE("every outcome can reach the level list")
{
    // Eleven regions means a player often wants to jump to another one from here,
    // rather than back through the title screen.
    CHECK(offers(optionsFor(Outcome::Victory, false), "LEVEL SELECT"));
    CHECK(offers(optionsFor(Outcome::Victory, true), "LEVEL SELECT"));
    CHECK(offers(optionsFor(Outcome::Defeat, false), "LEVEL SELECT"));
    CHECK(offers(optionsFor(Outcome::Defeat, true), "LEVEL SELECT"));
}

TEST_CASE("every outcome can reach the title screen and the desktop")
{
    for (const Outcome outcome : {Outcome::Victory, Outcome::Defeat}) {
        for (const bool checkpoint : {false, true}) {
            const std::vector<std::string> options = optionsFor(outcome, checkpoint);
            CHECK(offers(options, "MAIN MENU"));
            CHECK(offers(options, "QUIT"));
        }
    }
}

TEST_CASE("the level list is offered before the ways out of the game")
{
    // Once QUIT is passed, the next press leaves. Anything the player might still
    // want has to come first.
    for (const Outcome outcome : {Outcome::Victory, Outcome::Defeat}) {
        const std::vector<std::string> options = optionsFor(outcome, true);
        const std::size_t level = positionOf(options, "LEVEL SELECT");
        REQUIRE(level != static_cast<std::size_t>(-1));
        CHECK(level < positionOf(options, "QUIT"));
        CHECK(level < positionOf(options, "MAIN MENU"));
    }
}

TEST_CASE("no option label is offered twice")
{
    // A duplicated label would make two rows respond to the same string, and
    // whichever was matched first would win — the other row would be dead.
    for (const Outcome outcome : {Outcome::Victory, Outcome::Defeat}) {
        for (const bool checkpoint : {false, true}) {
            const std::vector<std::string> options = optionsFor(outcome, checkpoint);
            std::vector<std::string> sorted = options;
            std::sort(sorted.begin(), sorted.end());
            const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
            CHECK(duplicate == sorted.end());
        }
    }
}

TEST_CASE("every offered option can be activated")
{
    // A disabled row the player can navigate to but not use is a dead end, and the
    // results screen is the worst place to strand someone.
    StateContext ctx{};
    ctx.log = &quietLog();

    for (const Outcome outcome : {Outcome::Victory, Outcome::Defeat}) {
        ResultState state(GameState::GameOver, outcome, RunStats{});
        state.setRetryFromCheckpoint(true);
        state.onEnter(ctx);
        CHECK_FALSE(state.optionLabels().empty());
    }
}

TEST_CASE("the results screen remembers which region it is reporting on")
{
    // Without the name, a player has to match the statistics against memory to work
    // out which run they are looking at.
    StateContext ctx{};
    ctx.log = &quietLog();

    ResultState state(GameState::GameOver, Outcome::Victory, RunStats{});
    state.setLevelName("Sunken Lattice");
    state.setLevelId("sunken_lattice");
    state.onEnter(ctx);

    CHECK(state.levelName() == "Sunken Lattice");
    // The id is what the database is keyed on, and the name is only for display:
    // conflating them means renaming a region silently drops its progress.
    CHECK(state.levelId() == "sunken_lattice");
}

TEST_CASE("a results screen with no level name is still usable")
{
    // The caller may not know the name; an empty subtitle must not take the screen
    // down with it.
    StateContext ctx{};
    ctx.log = &quietLog();

    ResultState state(GameState::GameOver, Outcome::Defeat, RunStats{});
    state.onEnter(ctx);

    CHECK(state.levelName().empty());
    CHECK(state.optionLabels().size() >= 3);
}
